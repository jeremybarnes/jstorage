/* sandbox.h                                                       -*- C++ -*-
   Jeremy Barnes, 12 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   A sandbox for local changes.
*/

#ifndef __jmvcc__sandbox_h__
#define __jmvcc__sandbox_h__


#include "jml/utils/lightweight_hash.h"
#include "jml/utils/string_functions.h"
#include "versioned_object.h"
#include <boost/tuple/tuple.hpp>


namespace JMVCC {


/*****************************************************************************/
/* SANDBOX                                                                   */
/*****************************************************************************/

/// A sandbox provides a place where writes don't affect the underlying
/// objects.  These writes can then be committed atomically.

class Sandbox {
    /* Local Value handling

       We keep a lightweight hash table that allows us to look up the local
       (to the transaction) values by the address of the object that we
       are looking at.

       When we commit (or delete) these entries, however, we can't just do it
       in any old order.  For example, if an object needs to update where its
       parent looks to find it as part of its commit, then the parent needs
       to be committed afterwards.  Similarly, when destroying local objects
       parents may control the lifecycle of their child objects, which means
       that the children must be destroyed before the parents.

       In order to deal with this, each object implements a parent() method
       that allows it to specify ONE object that must be committed or destroyed
       AFTER the given object.  We keep a linked list that gives the order
       of traversal; when we add an object we make sure to add it before its
       parent.
    */

    struct Entry {
        Entry() : val(0), prev(0), next(0), automatic(true)
        {
        }

        Entry(void * val)
            : val(val), prev(0), next(0), automatic(false)
        {
        }

        void * val;
        Versioned_Object * prev;
        Versioned_Object * next;
        bool automatic;

        std::string print() const
        {
            return ML::format("val: %p", val);
        }
    };

    typedef ML::Lightweight_Hash<Versioned_Object *, Entry> Local_Values_Base;

    struct Local_Values : protected Local_Values_Base {
        Local_Values()
            : head(0), tail(0)
        {
        }

        Versioned_Object * head;
        Versioned_Object * tail;

        /** Insert the given object into the hash table.  The main complication
            comes from figuring out the order in which we need to traverse
            the objects.

            In order to keep the order consistent, we need to insert the
            object before its parent, or, if it has no parent, at the very
            end.  However, the parent also needs to go before its parent
            and so on.

            As a result, we recursively put things before their parents.

            We also keep a flag so that we know if this entry was just put
            there to maintain the structure, or if it was put there because
            there is a real value.
        */

        std::pair<Entry *, bool>
        insert(Versioned_Object * obj)
        {
            iterator it = find(obj);
            if (it != end())
                return std::make_pair(&it->second, false);

            Versioned_Object * parent = obj->parent();
            Entry * next_entry = 0;
            if (parent) next_entry = insert(parent).first;

            Versioned_Object * prev_obj = 0;
            if (next_entry) {
                prev_obj = next_entry->prev;
                next_entry->prev = obj;
            }
            else {
                prev_obj = tail;
                tail = obj;
            }

            Entry * prev_entry = 0;
            if (prev_obj) prev_entry = &operator [] (prev_obj);

            if (prev_entry)
                prev_entry->next = obj;
            else head = obj;

            Entry * entry = &operator [] (obj);

            entry->next = parent;
            entry->prev = prev_obj;

            return std::make_pair(entry, true);
        }

        /** Perform the action in the DoWhat object in order for all of the
            values in the local values.  The DoWhat object must be callable
            like

            bool keep_going = dowhat(Versioned_Object *, Entry &)

            The return value tells us whether to keep iterating or not.

            The function will return the object on which keep_going was
            set to false, or a null pointer if we got to the end.
        */
            
        template<class It, typename DoWhat, typename Self>
        static
        Versioned_Object *
        do_in_order(Self * self,
                    DoWhat dowhat,
                    Versioned_Object * start,
                    Versioned_Object * finish)
        {
            if (start == 0) start = self->head;

            for (Versioned_Object * current = start;
                 current && current != finish;
                 /* no inc */) {
                It it = self->find(current);
                if (it == self->end())
                    throw ML::Exception("invalid iteration chain");
                bool keep_going = dowhat(current, it->second);
                if (!keep_going) return current;
                
                current = it->second.next;
            }

            return 0;
        }

        template<typename DoWhat>
        Versioned_Object *
        do_in_order(DoWhat dowhat,
                    Versioned_Object * start = 0,
                    Versioned_Object * finish = 0)
        {
            return do_in_order<iterator>(this, dowhat, start, finish);
        }

        template<typename DoWhat>
        Versioned_Object *
        do_in_order(DoWhat dowhat,
                    Versioned_Object * start = 0,
                    Versioned_Object * finish = 0) const
        {
            return do_in_order<const_iterator>(this, dowhat, start, finish);
        }

        using Local_Values_Base::size;
        using Local_Values_Base::find;
        using Local_Values_Base::end;
        void clear()
        {
            Local_Values_Base::clear();
            head = tail = 0;
        }
    };

    Local_Values local_values;

    struct Free_Values;
    struct Check_Values;
    struct Setup_Commit;
    struct Commit;
    struct Rollback;
    struct Dump_Value;

public:
    ~Sandbox();

    void clear();

    // Return the local value for the given object.  Returns a null pointer
    // if there wasn't any.
    template<typename T>
    std::pair<T *, bool> local_value(Versioned_Object * obj)
    {
        Local_Values::const_iterator it = local_values.find(obj);
        if (it == local_values.end()) return std::make_pair((T *)0, false);
        return std::make_pair(reinterpret_cast<T *>(it->second.val), true);
    }

    // Return the local value for the given object, or create it if it
    // doesn't exist.  Returns the inserted value.
    template<typename T>
    T * local_value(Versioned_Object * obj, const T & initial_value)
    {
        bool inserted;
        Entry * entry;
        boost::tie(entry, inserted)
            = local_values.insert(obj);
        if (inserted || entry->automatic) {
            entry->val = malloc(sizeof(T));
            new (entry->val) T(initial_value);
            entry->automatic = false;
        }
        return reinterpret_cast<T *>(entry->val);
    }
    
    template<typename T>
    std::pair<const T *, bool> local_value(const Versioned_Object * obj)
    {
        return local_value<T>(const_cast<Versioned_Object *>(obj));
    }

    template<typename T>
    const T * local_value(const Versioned_Object * obj, const T & initial_value)
    {
        return local_value(const_cast<Versioned_Object *>(obj), initial_value);
    }

    template<typename T>
    void free_local_value(void * mem) const
    {
        T * typed = (T *)mem;
        typed->~T();
        free(mem);
    }

    /** Set the local value for the given object.  Returns the previous
        value and whether or not it existed.

        If size is not zero, free(val) will eventually be called, so the
        user must take care to make sure that this is the expected
        behaviour.
    */
    std::pair<void *, bool>
    set_local_value(Versioned_Object * obj, void * val);

    /** Commits the current transaction.  Returns zero if the transaction
        failed, or returns the id of the new epoch if it succeeded. */
    Epoch commit(Epoch old_epoch);

    void dump(std::ostream & stream = std::cerr, int indent = 0) const;

    size_t num_local_values() const { return local_values.size(); }

    friend std::ostream & operator << (std::ostream&, const Sandbox::Entry&);
};

std::ostream &
operator << (std::ostream & stream, const Sandbox::Entry & entry);


} // namespace JMVCC

#endif /* __jmvcc__sandbox_h__ */
