/* typed_pvo.h                                                     -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__typed_pvo_h__
#define __jmvcc__typed_pvo_h__

#include "pvo.h"
#include "jmvcc/version_table.h"

namespace JMVCC {

/*****************************************************************************/
/* TYPED_PVO                                                                  */
/*****************************************************************************/

/** Note that an addressable object potentially has multiple versions:
    - Zero or one read-write version local to each sandbox;
    - One or zero versions in the permanent storage;
    - One or zero versions for each snapshot
*/

template<typename T>
struct TypedPVO
    : public PVO {

    TypedPVO(ObjectId id, PVOManager * owner, const T & val = T())
        : PVO(id, owner)
    {
        version_table = VT::create(new T(val), 1);
    }
    
    ~TypedPVO()
    {
        VT * d = const_cast< VT * > (vt());
        VT::free(d, PUBLISHED, EXCLUSIVE);
    }
    
    // Client interface.  Just two methods to get at the current value.
    T & mutate()
    {
        if (!current_trans) no_transaction_exception(this);
        T * local = current_trans->local_value<T>(this);

        if (!local) {
            const T * value = value_at_epoch(current_trans->epoch());
            local = current_trans->local_value<T>(this, *value);
            
            if (!local)
                throw Exception("mutate(): no local was created");
        }
        
        return *local;
    }

    void write(const T & val)
    {
        mutate() = val;
    }
    
    const T & read() const
    {
        if (!current_trans) no_transaction_exception(this);

        const T * val = current_trans->local_value<T>(this);
        
        if (val) return *val;
        
        const VT * d = vt();

        const T * result = d->value_at_epoch(current_trans->epoch());
        return *result;
    }

    size_t history_size() const
    {
        size_t result = vt()->size() - 1;
        return result;
    }

    virtual size_t num_versions() const
    {
        return history_size();
    }

private:
    const T * value_at_epoch(Epoch epoch) const
    {
        return vt()->value_at_epoch(epoch);
    }

    struct ValCleanup {
        ValCleanup(T * val)
            : val(val)
        {
        }

        T * val;

        void operator () () const
        {
            delete val;
        }

        enum { useful = true };
    };

    // Internal version_table object allocated for when we have more than one
    // version
    typedef Version_Table<T *, ValCleanup> VT;

    // The single internal version_table member.  Updated atomically.
    mutable VT * version_table;

    const VT * vt() const
    {
        return reinterpret_cast<const VT *>(version_table);
    }

    bool set_version_table(const VT * & old_version_table,
                           VT * new_version_table)
    {
        memory_barrier();

        bool result = cmp_xchg(reinterpret_cast<VT * &>(version_table),
                               const_cast<VT * &>(old_version_table),
                               new_version_table);

        if (!result) VT::free(new_version_table, NEVER_PUBLISHED, SHARED);
        else VT::free(const_cast<VT *>(old_version_table), PUBLISHED, SHARED);

        return result;
    }
        
public:
    // Implement object interface

    virtual bool setup(Epoch old_epoch, Epoch new_epoch, void * new_value)
    {
        for (;;) {
            const VT * d = vt();

            if (new_epoch != get_current_epoch() + 1)
                throw Exception("epochs out of order");
            
            Epoch valid_from = 1;
            if (d->size() > 1)
                valid_from = d->element(d->size() - 2).valid_to;
            
            if (valid_from > old_epoch)
                return false;  // something updated before us
            
            VT * new_version_table = d->copy(d->size() + 1);
            new_version_table->back().valid_to = new_epoch;
            new_version_table->push_back(1 /* valid_to */,
                                         new T(*reinterpret_cast<T *>(new_value)));
            
            if (set_version_table(d, new_version_table)) return true;
        }
    }

    virtual void commit(Epoch new_epoch) throw ()
    {
        const VT * d = vt();

        // Now that it's definitive, we have an older entry to clean up
        Epoch valid_from = 1;
        if (d->size() > 2)
            valid_from = d->element(d->size() - 3).valid_to;

        snapshot_info.register_cleanup(this, valid_from);
    }

    virtual void rollback(Epoch new_epoch, void * local_version_table) throw ()
    {
        const VT * d = vt();

        for (;;) {
            VT * d2 = d->copy(d->size());
            d2->pop_back(NEVER_PUBLISHED, EXCLUSIVE);
            if (set_version_table(d, d2)) return;
        }
    }

    virtual void cleanup(Epoch unused_valid_from, Epoch trigger_epoch)
    {
        const VT * d = vt();

        for (;;) {

            if (d->size() < 2) {
                using namespace std;
                cerr << "cleaning up: unused_valid_from = " << unused_valid_from
                     << " trigger_epoch = " << trigger_epoch << endl;
                cerr << "current_epoch = " << get_current_epoch() << endl;
                throw Exception("cleaning up with no values to clean up");
            }

            
            VT * result = d->cleanup(unused_valid_from);
            if (result) {
                if (set_version_table(d, result)) return;
                continue;
            }
            
            using namespace std;
            cerr << "----------- cleaning up didn't exist ---------" << endl;
            dump_unlocked();
            cerr << "unused_valid_from = " << unused_valid_from << endl;
            cerr << "trigger_epoch = " << trigger_epoch << endl;
            snapshot_info.dump();
            cerr << "----------- end cleaning up didn't exist ---------" << endl;
            
            throw Exception("attempt to clean up something that didn't exist");
        }
    }
    
    virtual Epoch rename_epoch(Epoch old_valid_from, Epoch new_valid_from)
        throw ()
    {
        const VT * d = vt();

        for (;;) {
            std::pair<VT *, Epoch> result
                = d->rename_epoch(old_valid_from, new_valid_from);

            if (!result.first)
                throw Exception("not found");

            if (set_version_table(d, result.first)) return result.second;
        }
    }

    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
        dump_itl(stream, indent);
    }

    virtual void dump_unlocked(std::ostream & stream = std::cerr,
                               int indent = 0) const
    {
        dump_itl(stream, indent);
    }

    void dump_itl(std::ostream & stream, int indent = 0) const
    {
        const VT * d = vt();

        using namespace std;
        std::string s(indent, ' ');
        stream << s << "object at " << this << std::endl;
        stream << s << "history with " << d->size()
               << " values" << endl;
        for (unsigned i = 0;  i < d->size();  ++i) {
            const typename VT::Entry & entry = d->element(i);
            stream << s << "  " << i << ": valid to "
                   << entry.valid_to;
            stream << " addr " <<  entry.value;
            stream << " value " << *entry.value;
            stream << endl;
        }
    }

    virtual std::string print_local_value(void * val) const
    {
        return ostream_format(*reinterpret_cast<T *>(val));
    }

    virtual void destroy_local_value(void * val) const
    {
        reinterpret_cast<T *>(val)->~T();
    }
};


} // namespace JMVCC

#endif /* __jmvcc__typed_pvo_h__ */
