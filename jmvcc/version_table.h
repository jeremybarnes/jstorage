/* version_table.h                                                 -*- C++ -*-
   Jeremy Barnes, 4 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   A table that maintains multiple versions of an object.
*/

#ifndef __jmvcc__version_table_h__
#define __jmvcc__version_table_h__

#include "garbage.h"
#include "transaction.h"
#include "jml/arch/exception.h"

namespace JMVCC {

template<typename X>
struct No_Cleanup {

    No_Cleanup(const X & x)
        : x(x)
    {
    }

    const X & x;

    void operator () () const
    {
    }

    enum { useful = false };
};

template<typename T>
struct DeleteCleanup {
    DeleteCleanup(T * val)
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


/** Enum that tells us whether a particular data item has been published or
    not.  If it has been published, then any cleanup must be deferred.
    Otherwise, cleanups can happen straight away.
*/
enum Published {
    NEVER_PUBLISHED,  ///< Never has been published, cleanup straightaway
    PUBLISHED         ///< Was published; cleanup later
};


/** Enum that tells us whether we have exclusive or shared ownership of a
    pointed-to value of data element.  Exclusive means that we can delete
    the pointed-to thing (ie, we have the only reference to it).  Shared
    means that there is another reference somewhere, and we shouldn't
    delete the pointed-to thing.
*/

enum Sharing {
    EXCLUSIVE,        ///< We have the exclusive copy; delete the thing
    SHARED            ///< Something else has a reference as well
};


/*****************************************************************************/
/* VERSION_TABLE                                                             */
/*****************************************************************************/

template<typename T, typename ValCleanup = No_Cleanup<T>,
         typename Allocator = std::allocator<char> >
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

    uint32_t size() const { return itl.last; }

    ~Version_Table()
    {
        size_t sz = size();
        for (unsigned i = 0;  i < sz;  ++i)
            history[i].value.~T();
    }

    /// Return the value for the given epoch
    const T & value_at_epoch(Epoch epoch) const
    {
        for (int i = itl.last - 1;  i > 0;  --i) {
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

    struct RunValueDestructor {
        RunValueDestructor(T & obj)
            : obj(obj)
        {
        }
        
        T & obj;
        
        void operator () () const
        {
            obj.~T();
        }
    };
    
    void pop_back(Published published, Sharing sharing)
    {
        if (size() < 2)
            throw Exception("popping back last element");

        if (ValCleanup::useful && sharing == EXCLUSIVE) {
            ValCleanup vc(back().value);
            if (published == PUBLISHED)
                schedule_cleanup(vc);
            else vc();
        }

        RunValueDestructor cleanup(back().value);
        
        if (published == PUBLISHED)
            schedule_cleanup(cleanup);
        else cleanup();
        
        --itl.last;
    }

    void push_back(Epoch valid_to, const T & val)
    {
        push_back(Entry(valid_to, val));
    }

    void push_back(const Entry & entry)
    {
        if (itl.last == itl.capacity) {
            using namespace std;
            cerr << "last = " << itl.last << endl;
            cerr << "capacity = " << itl.capacity << endl;
            throw Exception("can't push back");
        }
        new (&history[itl.last].value) T(entry.value);
        history[itl.last].valid_to = entry.valid_to;
            
        memory_barrier();

        ++itl.last;
    }
        
    const Entry & back() const
    {
        return history[itl.last - 1];
    }

    Entry & back()
    {
        return history[itl.last - 1];
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

    static size_t bytes_for_capacity(size_t capacity)
    {
        return sizeof(Version_Table) + capacity * sizeof(Entry);
    }
    
    struct Deleter {
        Deleter(Version_Table * version_table, Sharing sharing)
            : version_table(version_table), sharing(sharing)
        {
        }

        void operator () ()
        {
            size_t capacity = version_table->itl.capacity;

            // Clean up the objects
            if (ValCleanup::useful && sharing == EXCLUSIVE) {
                for (unsigned i = 0;  i < version_table->itl.last;  ++i) {
                    ValCleanup vc(version_table->history[i].value);
                    vc();
                }
            }
            
            // TODO: how to avoid destroying the allocator before we use it?

            version_table->~Version_Table();
            version_table->itl.deallocate
                (reinterpret_cast<char *>(version_table),
                 bytes_for_capacity(capacity));
        }

        Version_Table * version_table;
        Sharing sharing;
    };

    static void free(Version_Table * version_table, Published published,
                     Sharing sharing)
    {
        Deleter deleter(version_table, sharing);
        if (published == NEVER_PUBLISHED)
            deleter();
        else schedule_cleanup(deleter);
    }

    static Version_Table * create(size_t capacity,
                                  Allocator allocator = Allocator())
    {
        // TODO: exception safety...
        void * d = allocator.allocate(bytes_for_capacity(capacity));
        Version_Table * d2 = new (d) Version_Table(capacity, allocator);
        return d2;
    }

    static Version_Table * create(const T & val, size_t capacity,
                                  Allocator allocator = Allocator())
    {
        // TODO: exception safety...
        void * d = allocator.allocate(bytes_for_capacity(capacity));
        Version_Table * d2 = new (d) Version_Table(capacity, allocator);
        d2->push_back(Entry(1,  val));
        return d2;
    }

    static Version_Table * create(const Version_Table & old, size_t capacity)
    {
        // TODO: exception safety...
        Allocator a(old.itl);
        void * d = a.allocate(bytes_for_capacity(capacity));
        Version_Table * d2 = new (d) Version_Table(capacity, old);
        return d2;
    }

    Version_Table * cleanup(Epoch unused_valid_from) const
    {
        Version_Table * version_table2 = create(size(), itl);
        
        // Copy them, skipping the one that matched
        
        // TODO: optimize
        Epoch valid_from = 1;
        bool found = false;
        for (unsigned i = 0, e = itl.last, j = 0; i != e;  ++i) {
            if (valid_from == unused_valid_from
                || (i == 0
                    && unused_valid_from < front().valid_to)) {
                // Remove this element, once nothing can look at it

                if (found)
                    throw Exception("two with the same valid_from value");
                found = true;
                if (j != 0)
                    version_table2->history[j - 1].valid_to = history[i].valid_to;
                // Call the cleanup function
                if (ValCleanup::useful) {
                    ValCleanup vc(history[i].value);
                    schedule_cleanup(vc);
                }
            }
            else {
                // Copy element i to element j
                new (&version_table2->history[j].value) T(history[i].value);
                version_table2->history[j].valid_to = history[i].valid_to;
                ++j;
                ++version_table2->itl.last;
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
        Version_Table * d2 = create(*this, itl.capacity);
        
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

private:
    // Use the empty base optimization for the allocator
    struct Itl : public Allocator {
        Itl(uint32_t capacity, const Allocator & allocator)
            : Allocator(allocator), capacity(capacity), last(0)
        {
        }

        uint32_t capacity;   // Number allocated
        uint32_t last;       // Index of last valid entry
    } itl;

    Entry history[0];  // real ones are allocated after

    Version_Table(size_t capacity, Allocator allocator = Allocator())
        : itl(capacity, allocator)
    {
    }

    Version_Table(size_t capacity, const Version_Table & old_version_table)
        : itl(capacity, old_version_table.itl)
    {
        for (unsigned i = 0;  i < old_version_table.size();  ++i)
            push_back(old_version_table.element(i));
    }
};


} // namespace JMVCC

#endif /* __jmvcc__version_table_h__ */

