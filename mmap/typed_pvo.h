/* typed_pvo.h                                                     -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__typed_pvo_h__
#define __jmvcc__typed_pvo_h__

#include "pvo.h"
#include "jmvcc/version_table.h"
#include "serialization.h"
#include "jml/utils/guard.h"
#include "jml/arch/demangle.h"


namespace JMVCC {

PVOStore * to_store(PVOManager * owner);
void * to_pointer(PVOStore * store, size_t offset);
void mutate_owner(PVOManager * owner);


/*****************************************************************************/
/* TYPED_PVO                                                                 */
/*****************************************************************************/

/** Note that an addressable object potentially has multiple versions:
    - Zero or one read-write version local to each sandbox;
    - One or zero versions in the permanent storage;
    - One or zero versions for each snapshot
*/

template<typename T>
struct TypedPVO
    : public PVO, boost::noncopyable {

    /** Local values

        If ever an object's state has changed, whether that be its value,
        or it is a new object, or the object has been deleted, we put
        an entry in the sandbox.

        For an object that already existed (and so wasn't created), the
        local value pointer will simply point to the new value of the
        object in this sandbox.

        For an object that was created within the sandbox, the local value
        pointer also points to the new value of the object.  The object is
        created on commit.

        For an object that already existed but was deleted, the local
        value pointer will be a null pointer.
    */

    // Client interface.  Just two methods to get at the current value.
    T & mutate()
    {
        if (!current_trans) no_transaction_exception(this);

        bool has_local;
        T * local;
        boost::tie(local, has_local) = current_trans->local_value<T>(this);

        if (!has_local) {
            const T * value = value_at_epoch(current_trans->epoch());

            local = current_trans->local_value<T>(this, *value);
            
            if (!local)
                throw Exception("mutate(): no local was created");
        }
        else if (!local)
            throw Exception("attempt to access a removed object");

        return *local;
    }

    // ONLY for when there is external locking and NO other thread could ever
    // be in a critical section at the same time.  Modifies the underlying
    // objects directly.  Note that the object obtained is always that for
    // the current epoch.
    T & exclusive()
    {
        const T * value = value_at_epoch(get_current_epoch());
        return *const_cast<T *>(value);
    }

    void write(const T & val)
    {
        mutate() = val;
    }
    
    const T & read() const
    {
        if (!current_trans) no_transaction_exception(this);

        bool has_local;
        const T * local;

        boost::tie(local, has_local)
            = current_trans->local_value<T>(this);
        
        if (has_local) {
            if (local) return *local;
            else throw Exception("attempt to access a removed object");
        }
        
        const VT * d = vt();

        const T * result = d->value_at_epoch(current_trans->epoch());
        return *result;
    }

    virtual void remove()
    {
        if (!current_trans) no_transaction_exception(this);

        std::pair<void *, bool> values
            = current_trans->set_local_value(this, 0);

        void * old_local_value = values.first;
        bool had_old_value = values.second;

        if (had_old_value) {
            if (old_local_value == 0)
                throw Exception("double remove() operation");
            // TODO: exception safety
            destroy_local_value(old_local_value);
            current_trans->free_local_value<T>(old_local_value);
        }
        
        owner()->remove_child(id(), true /* explicitly */);
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
    
    static TypedPVO<T> *
    reconstituted(ObjectId id, size_t offset, PVOManager * owner)
    {
        TypedPVO<T> * result = new TypedPVO<T>(id, owner, false, T());
        
        try {
            PVOStore * store = to_store(owner);
            void * mem = to_pointer(store, offset);

            Serializer<T>::reconstitute(result->exclusive(), mem, *store);
            return result;
        } catch (...) {
            delete result;
            throw;
        }
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

protected:
    // Needs to be able to call the destructor
    friend class PVOManager;
    friend class PVOManagerVersion;

    /** Create it and add it to the current transaction. */
    TypedPVO(PVOManager * owner, const T & val)
        : PVO(owner)
    {
        version_table = VT::create(new T(val), 1);
        mutate();
    }
    
    /** Create it with the given ID and owner, but don't add it anywhere.  This
        constructor is mostly used to bootstrap.  If we are currently in a
        transaction, it will be added to the sandbox however. */
    TypedPVO(ObjectId id, PVOManager * owner, bool add_local,
             const T & val)
        : PVO(id, owner)
    {
        version_table = VT::create(new T(val), 1);
        if (add_local) mutate();
    }
    
    ~TypedPVO()
    {
        VT * d = const_cast< VT * > (vt());
        VT::free(d, PUBLISHED, EXCLUSIVE);
    }

    // Note that this can't be used if the object was ever published as it's
    // not atomic.
    void swap(TypedPVO & other)
    {
        std::swap(version_table, other.version_table);
    }
        
public:
    // Implement object interface

    // Is it possible to perform the commit?
    bool check_commit_possible(const VT * d, Epoch old_epoch,
                               Epoch new_epoch) const
    {
        Epoch valid_from = 1;
        if (d->size() > 1)
            valid_from = d->element(d->size() - 2).valid_to;
        
        if (valid_from > old_epoch)
            return false;  // something updated before us
        return true;
    }

    virtual bool check(Epoch old_epoch, Epoch new_epoch,
                       void * new_value) const
    {
        return check_commit_possible(vt(), old_epoch, new_epoch);
    }

    void free_setup_data(void * setup_data)
    {
        Serializer<T>::deallocate(setup_data, *store());
    }

    virtual void * setup(Epoch old_epoch, Epoch new_epoch, void * new_value)
    {
        // Perform the commit assuming that it's going to go ahead.  We
        // have to:
        // 1.  Commit the new value to permanent storage;
        // 2.  Set up the new table to point to it;
        // 3.  Swap the new table in place, pointing to both the OLD and the
        //
        // Before:
        //                          current epoch
        //                          |       new epoch
        //                          |       |
        //                          v       v             PVO Manager
        // +-------+--------+-------+-------+         +------+------+
        // |  v1   |   v2   |   v3  |  v4   |<--------| Mem  | Disk |
        // +-------+--------+-------+-------+         +------+------+
        //     |        |       |       |                        |
        //     v        v       v       v                        |
        // +------+ +------+ +-----+ +-----+                     |
        // |      | |      | |     | |     |                     |
        // +------+ +------+ +-----+ +-----+                     |
        // Memory^                                               |
        //                           +-----+                     |
        // Disk>                     |     |<--------------------+
        //                           +-----+   
        //
        // After setup:
        //                          current epoch
        //                          |       new epoch
        //                          |       |
        //                          v       v             PVO Manager
        // +-------+--------+-------+-------+------+  +------+------+
        // |  v1   |   v2   |   v3  |  v4   |  v5  |<-| Mem  | Disk |
        // +-------+--------+-------+-------+------+  +------+------+
        //     |        |       |       |       |                |
        //     v        v       v       v       v                |
        // +------+ +------+ +-----+ +-----+ +-----+             |
        // |      | |      | |     | |     | |     |             |
        // +------+ +------+ +-----+ +-----+ +-----+             |
        // Memory^                                               |
        //                           +-----+ +-----+             |
        // Disk>                     |     | |     |<- setup_data|
        //                           +-----+ +-----+             |
        //                              ^                        |
        //                              |                        |
        //                              +------------------------+
        //                             
        //
        // After commit:
        //                          current epoch
        //                          |       new epoch
        //                          |       |
        //                          v       v             PVO Manager
        // +-------+--------+-------+-------+------+  +------+------+
        // |  v1   |   v2   |   v3  |  v4   |  v5  |<-| Mem  | Disk |
        // +-------+--------+-------+-------+------+  +------+------+
        //     |        |       |       |       |                |
        //     v        v       v       v       v                |
        // +------+ +------+ +-----+ +-----+ +-----+             |
        // |      | |      | |     | |     | |     |             |
        // +------+ +------+ +-----+ +-----+ +-----+             |
        // Memory^                                               |
        //                           +.....+ +-----+             |
        // Disk>                     :freed: |     |             |
        //                           +.....+ +-----+             |
        //                                      ^                |
        //                                      |                |
        //                                      +----------------+
        //
        // Note that the free is deferred until all critical sections have
        // finished, as something could be accessing the data.
        //
        //
        // After rollback:
        //
        //                          current epoch
        //                          |       new epoch
        //                          |       |
        //                          v       v             PVO Manager
        // +-------+--------+-------+-------+         +------+------+
        // |  v1   |   v2   |   v3  |  v4   | <-------| Mem  | Disk |
        // +-------+--------+-------+-------+         +------+------+
        //     |        |       |       |                        |
        //     v        v       v       v                        |
        // +------+ +------+ +-----+ +-----+ +.....+             |
        // |      | |      | |     | |     | :freed:             |
        // +------+ +------+ +-----+ +-----+ +.....+             |
        // Memory^                                               |
        //                           +-----+ +.....+             |
        // Disk>                     |     | :freed:             |
        //                           +-----+ +.....+             |
        //                              ^                        |
        //                              |                        |
        //                              +------------------------+
        //
        // Again the free is deferred

        // NOTE: setup has another job: to make sure that if the parent ovbject
        // needs to be modified, that it will have a local value and will
        // therefore be ready to commit.

        if (new_value == 0) {
            // This object was removed.  Nothing to do.
            return (void *)1;
        }

        // TODO: don't copy; steal, since the pointer will be destroyed no
        // matter what.
        std::auto_ptr<T> nv(new T(*reinterpret_cast<T *>(new_value)));

        PVOManager * owner = this->owner();
        if (owner && (void *)owner != (void *)this) {
            // A commit of this object will require the owner to be committed
            // as well.  Here we make sure that this happens.
            mutate_owner(owner);
        }

        void * setup_data = Serializer<T>::serialize(*nv, *store());

        Call_Guard guard(boost::bind(&TypedPVO<T>::free_setup_data, this,
                                     setup_data));

        for (;;) {
            const VT * d = vt();

            if (new_epoch != get_current_epoch() + 1)
                throw Exception("epochs out of order");
            
            if (!check_commit_possible(d, old_epoch, new_epoch))
                return false;

            VT * new_version_table = d->copy(d->size() + 1);
            new_version_table->back().valid_to = new_epoch;
            new_version_table->push_back(1 /* valid_to */, nv.get());
            
            if (set_version_table(d, new_version_table)) {
                nv.release();  // no need to delete it now
                guard.clear(); // no need to deallocate now
                return setup_data;
            }
        }
    }

    virtual void commit(Epoch new_epoch, void * setup_data) throw ()
    {
        if (setup_data == (void *)1) setup_data = 0;

        const VT * d = vt();

        // Now that it's definitive, we may have an older version to clean up

        if (d->size() > 1) {
            Epoch valid_from = 1;
            if (d->size() > 2)
                valid_from = d->element(d->size() - 3).valid_to;

            snapshot_info.register_cleanup(this, valid_from);
        }
        else {
            // This object should never have been committed
            // TODO: check this condition
        }

        void * old_mem = owner()->set_persistent_version(id(), setup_data);
        
        // Deallocate the memory.  TODO: should we wait for the end of the
        // critical section?
        if (old_mem)
            Serializer<T>::deallocate(old_mem, *store());
    }

    virtual void rollback(Epoch new_epoch, void * local_data,
                          void * setup_data) throw ()
    {
        // If it was a removal there is nothing to do
        if (setup_data == (void *)1) return;

        // The commit didn't happen.  We need to:
        // 1.  Free up the memory associated with the object that we just
        //     serialized.  Since it was published, something might be
        //     accessing it.
        // 2.  Swap back an old version of the table into place

        const VT * d = vt();

        for (;;) {
            VT * d2 = d->copy(d->size());
            d2->pop_back(NEVER_PUBLISHED, EXCLUSIVE);
            if (set_version_table(d, d2)) return;
        }

        free_setup_data(setup_data);
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
        if (!val) return "REMOVED";
        return ostream_format(*reinterpret_cast<T *>(val));
    }

    virtual void destroy_local_value(void * val) const
    {
        // Check if the object was removed
        if (!val || val == (void *)1) return;
        current_trans->free_local_value<T>(val);
    }
};


} // namespace JMVCC

#endif /* __jmvcc__typed_pvo_h__ */

#include "typed_pvo_impl.h"
