/* versioned2.h                                                    -*- C++ -*-
   Jeremy Barnes, 12 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Turns a normal object into a versioned one.  This is a lock-free version.
*/

#ifndef __jmvcc__versioned2_h__
#define __jmvcc__versioned2_h__

#include "versioned_object.h"
#include "transaction.h"
#include "jml/arch/cmp_xchg.h"
#include "jml/arch/atomic_ops.h"
#include "jml/arch/exception.h"
#include "jml/arch/threads.h"
#include "garbage.h"


namespace JMVCC {


/*****************************************************************************/
/* VERSION_TABLE                                                             */
/*****************************************************************************/

template<typename T>
struct Version_Table {

    // This structure provides a list of values.  Each one is tagged with the
    // earliest epoch in which it is valid.  The latest epoch in which it is
    // valid + 1 is that of the next entry in the list; that in current has no
    // latest epoch.

    struct Entry {
        explicit Entry(Epoch valid_to = 1, const T & value = T())
            : valid_to(valid_to), value(value)
        {
        }
        
        Epoch valid_to;
        T value;
    };

    Version_Table(size_t capacity)
        : capacity(capacity), last(0)
    {
    }

    Version_Table(size_t capacity, const Version_Table & old_version_table)
        : capacity(capacity), last(0)
    {
        for (unsigned i = 0;  i < old_version_table.size();  ++i)
            push_back(old_version_table.element(i));
    }

    uint32_t capacity;   // Number allocated
    uint32_t last;       // Index of last valid entry
    Entry history[1];  // real ones are allocated after

    uint32_t size() const { return last; }

    ~Version_Table()
    {
        size_t sz = size();
        for (unsigned i = 0;  i < sz;  ++i)
            history[i].value.~T();
    }

    /// Return the value for the given epoch
    const T & value_at_epoch(Epoch epoch) const
    {
        for (int i = last - 1;  i > 0;  --i) {
            Epoch valid_from = history[i - 1].valid_to;
            if (epoch >= valid_from)
                return history[i].value;
        }
            
        return history[0].value;
    }
        
    Version_Table * copy(size_t new_capacity) const
    {
        if (new_capacity < size())
            throw Exception("new capacity is wrong");

        return create(*this, new_capacity);
    }

    Entry & front()
    {
        return history[0];
    }

    const Entry & front() const
    {
        return history[0];
    }

    void pop_back()
    {
        if (size() < 2)
            throw Exception("popping back last element");
        --last;
        // Need to: make sure that garbage collection runs its destructor
    }

    void push_back(Epoch valid_to, const T & val)
    {
        push_back(Entry(valid_to, val));
    }

    void push_back(const Entry & entry)
    {
        if (last == capacity) {
            using namespace std;
            cerr << "last = " << last << endl;
            cerr << "capacity = " << capacity << endl;
            throw Exception("can't push back");
        }
        new (&history[last].value) T(entry.value);
        history[last].valid_to = entry.valid_to;
            
        memory_barrier();

        ++last;
    }
        
    const Entry & back() const
    {
        return history[last - 1];
    }

    Entry & back()
    {
        return history[last - 1];
    }

    Entry & element(int index)
    {
        if (index < 0 || index >= size())
            throw Exception("invalid element");
        return history[index];
    }

    const Entry & element(int index) const
    {
        if (index < 0 || index >= size())
            throw Exception("invalid element");
        return history[index];
    }

    struct Deleter {
        Deleter(Version_Table * version_table)
            : version_table(version_table)
        {
        }

        void operator () ()
        {
            version_table->~Version_Table();
            ::free(version_table);
        }

        Version_Table * version_table;
    };

    static void free(Version_Table * version_table)
    {
        schedule_cleanup(Deleter(version_table));
    }

    static void free_now(Version_Table * version_table)
    {
        Deleter do_it(version_table);
        do_it();
    }

    static Version_Table * create(size_t capacity)
    {
        // TODO: exception safety...
        void * d = malloc(sizeof(Version_Table) + capacity * sizeof(Entry));
        Version_Table * d2 = new (d) Version_Table(capacity);
        return d2;
    }

    static Version_Table * create(const T & val, size_t capacity)
    {
        // TODO: exception safety...
        void * d = malloc(sizeof(Version_Table) + capacity * sizeof(Entry));
        Version_Table * d2 = new (d) Version_Table(capacity);
        d2->push_back(Entry(1,  val));
        return d2;
    }

    static Version_Table * create(const Version_Table & old, size_t capacity)
    {
        // TODO: exception safety...
        void * d = malloc(sizeof(Version_Table) + capacity * sizeof(Entry));
        Version_Table * d2 = new (d) Version_Table(capacity, old);
        return d2;
    }

    Version_Table * cleanup(Epoch unused_valid_from) const
    {
        Version_Table * version_table2 = create(size());
        
        // Copy them, skipping the one that matched
        
        // TODO: optimize
        Epoch valid_from = 1;
        bool found = false;
        for (unsigned i = 0, e = last, j = 0; i != e;  ++i) {
            //cerr << "i = " << i << " e = " << e << " j = " << j
            //     << " element = " << history[i].value << " valid to "
            //     << history[i].valid_to << " found = "
            //     << found << " valid_from = " << valid_from << endl;
            //cerr << "version_table2->size() = " << version_table2->size() << endl;
            
            if (valid_from == unused_valid_from
                || (i == 0
                    && unused_valid_from < front().valid_to)) {
                //cerr << "  removing" << endl;
                if (found)
                    throw Exception("two with the same valid_from value");
                found = true;
                if (j != 0)
                    version_table2->history[j - 1].valid_to = history[i].valid_to;
            }
            else {
                // Copy element i to element j
                new (&version_table2->history[j].value) T(history[i].value);
                version_table2->history[j].valid_to = history[i].valid_to;
                ++j;
                ++version_table2->last;
            }
            
            valid_from = history[i].valid_to;
        }
        
        if (!found) {
            free_now(version_table2);
            return 0;
        }

        if (size() != version_table2->size() + 1)
            throw Exception("sizes were wrong");

        return version_table2;
    }

    std::pair<Version_Table *, Epoch>
    rename_epoch(Epoch old_valid_from, Epoch new_valid_from) const
    {
        int s = size();
        
        if (s == 0)
            throw Exception("renaming with no values");
        
        if (old_valid_from < history[0].valid_to) {
            // The last one doesn't have a valid_from, so we assume that
            // it's ok and leave it.
            Epoch e = (s == 2 ? history[1].valid_to : 0);
            return std::make_pair(const_cast<Version_Table *>(this), e);
        }
        
        // This is subtle.  Since we have valid_to values stored and not
        // valid_from values, we need to find the particular one and change
        // it.
        
        // TODO: maybe we could modify in place???
        Version_Table * d2 = create(*this, capacity);
        
        // TODO: optimize
        Epoch result = 0;
        bool found = false;
        for (unsigned i = 0;  i != s;  ++i) {
            if (d2->history[i].valid_to != old_valid_from) continue;
            d2->history[i].valid_to = new_valid_from;
            found = true;
            if (i == s - 3)
                result = d2->history[s - 2].valid_to;
            break;
        }

        if (!found) {
            free_now(d2);
            d2 = 0;
        }

        return std::make_pair(d2, result);
    }

};


/*****************************************************************************/
/* VERSIONED2                                                                */
/*****************************************************************************/

/** This template takes an underlying type and turns it into a versioned
    object.  It's used for simple objects where a new copy of the object
    can be stored for each version.

    For more complicated cases (for example, where a lot of the state
    can be shared between an old and a new version), the object should
    derive directly from Versioned_Object instead.
*/

template<typename T>
struct Versioned2 : public Versioned_Object {

    explicit Versioned2(const T & val = T())
    {
        //static Info info;
        version_table = VT::create(val, 1);
    }

    ~Versioned2()
    {
        VT::free(const_cast<VT *>(vt()));
    }

    // Client interface.  Just two methods to get at the current value.
    T & mutate()
    {
        if (!current_trans) no_transaction_exception(this);
        T * local = current_trans->local_value<T>(this);

        if (!local) {
            T value;
            {
                value = vt()
                    ->value_at_epoch(current_trans->epoch());
            }
            local = current_trans->local_value<T>(this, value);
            
            if (!local)
                throw Exception("mutate(): no local was created");
        }
        
        return *local;
    }

    void write(const T & val)
    {
        mutate() = val;
    }
    
    const T read() const
    {
        if (!current_trans)
            throw Exception("reading outside a transaction");

        const T * val = current_trans->local_value<T>(this);
        
        if (val) return *val;
        
        const VT * d = vt();

        T result = d->value_at_epoch(current_trans->epoch());
        return result;
    }

    size_t history_size() const
    {
        size_t result = vt()->size() - 1;
        return result;
    }

private:
    // Internal version_table object allocated for when we have more than one
    // version
    typedef Version_Table<T> VT;

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

        if (!result) VT::free_now(new_version_table);
        else VT::free(const_cast<VT *>(old_version_table));

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
                                      *reinterpret_cast<T *>(new_value));
            
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
            d2->pop_back();
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
            
            static Lock lock;
            Guard guard2(lock);
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
            stream << " addr " << &entry.value;
            stream << " value " << entry.value;
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


#endif /* __jmvcc__versioned2_h__ */
