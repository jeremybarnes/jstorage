/* persistent_versioned_object_test.cc
   Jeremy Barnes, 2 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of persistent versioned objects.
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jmvcc/versioned2.h"
#include "jmvcc/snapshot.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>

#include <signal.h>

#include <boost/interprocess/managed_mapped_file.hpp>

using namespace boost::interprocess;


using namespace ML;
using namespace JMVCC;
using namespace std;


template<typename Pointer>
struct MemoryRegion {
    Pointer start;
    size_t  length;
};

typedef uint64_t ObjectID;

class PersistentObjectStore;


/*****************************************************************************/
/* AOTABLE                                                                   */
/*****************************************************************************/

/** A table of the addressable objects that are alive in a space.  Backed by
    a memory map.  In an independent file so that it can grow independently
    from the memory it uses.

    For references to external objects, 
*/



/*****************************************************************************/
/* ADDRESSABLE_OBJECT                                                        */
/*****************************************************************************/

struct AddressableObject : public JMVCC::Versioned_Object {

    /** Return the immutable identity of the object */
    ObjectID id() const;

    /** Who owns this object */
    PersistentObjectStore * owner() const;

    /** How many versions of the object are there? */
    virtual size_t num_versions() const;

private:
    ObjectID id_;  ///< Identity in the memory mapped region
    PersistentObjectStore * owner_;  ///< Responsible for dealing with it
};


/*****************************************************************************/
/* TYPED_AO                                                                  */
/*****************************************************************************/

template<typename T>
struct TypedAO : public AddressableObject {

    TypedAO();

    TypedAO(const T & val);

    // Client interface.  Just two methods to get at the current value.
    T & mutate()
    {
        if (!current_trans) no_transaction_exception(this);
        T * local = current_trans->local_value<T>(this);

        if (!local) {
            T value;
            {
                //value = get_data()->value_at_epoch(current_trans->epoch());
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
        if (!current_trans) {
            throw Exception("reading outside a transaction");
        }
        const T * val = current_trans->local_value<T>(this);
        
        if (val) return *val;

#if 0        
        const Data * d = get_data();

        T result = d->value_at_epoch(current_trans->epoch());
        return result;
#endif
    }


    // Get the commit ready and check that everything can go ahead, but
    // don't actually perform the commit
    virtual bool setup(Epoch old_epoch, Epoch new_epoch, void * data)
    {
        // 1.  Allocate persistent memory for the new value
        
        // 2.  Allocate heap memory for the new internal data structure

        // 3.  Success
        throw Exception("not implemented");
    }

    // Confirm a setup commit, making it permanent
    virtual void commit(Epoch new_epoch) throw ()
    {
        // 1.  Serialize the new value in the new memory

        // 2.  Switch the pointer to point to the new memory

        // 3.  Arrange for the old memory to be freed once everything has made
        //     a critical section transition
        throw Exception("not implemented");
    }

    // Roll back a setup commit
    virtual void rollback(Epoch new_epoch, void * data) throw ()
    {
        // 1.  Deallocate the memory from the owner in the mmap range
        throw Exception("not implemented");
    }

    // Clean up an unused version
    virtual void cleanup(Epoch unused_valid_from, Epoch trigger_epoch)
    {
        throw Exception("not implemented");
    }
    
    // Rename an epoch to a different number.  Returns the valid_from value
    // of the next epoch in the set.
    virtual Epoch rename_epoch(Epoch old_valid_from,
                               Epoch new_valid_from) throw ()
    {
        throw Exception("not implemented");
    }

    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
        throw Exception("not implemented");
    }

    virtual void dump_unlocked(std::ostream & stream = std::cerr,
                               int indent = 0) const
    {
        throw Exception("not implemented");
    }

    virtual std::string print_local_value(void * val) const
    {
        throw Exception("not implemented");
    }

private:
    const char * current;  ///< Current version as serialized
};

template<typename Obj, typename AO = TypedAO<Obj> >
struct AORef {
    AORef(AO * ao)
        : ao(ao), obj(0), rw(false)
    {
    }

    AO * ao;
    Obj * obj;
    bool rw;

    operator const Obj () const { return ao->read(); }
};


/*****************************************************************************/
/* PERSISTENT_OBJECT_STORE                                                   */
/*****************************************************************************/

/** This is the basic class used for persistent storage.  It provides the
    following functionality:

    1.  Maintains the file-backed memory-mapped region(s) where objects are
        eventually serialized;
    2.  Deals with keeping the on-disk versions of the memory-mapped regions
        synchronized and consistent (in response to a save() command)
    3.  Maintains the housekeeping data structures on the memory mapped
        regions
    4.  Maintains an index of named objects on the disk that can be looked
        up in order to find top-level objects
    5.  Keeps track of the allocated and free memory in the region
    6.  Keeps track of objects that have multiple versions live in memory
        and reclaims them when they disappear.
*/

struct PersistentObjectStore
    : public Versioned_Object {

    // Create a new persistent object store
    template<typename Creation>
    PersistentObjectStore(const Creation & creation,
                          const std::string & filename,
                          size_t size)
        : backing(creation, filename.c_str(), size)
    {
    }

    /*************************************************************************/
    /* ADDRESSABLE OBJECTS                                                   */
    /*************************************************************************/

    /** Set of all addressable objects that are currently instantiated into
        memory.

        An addressable object will be instantiated in memory if it has more
        than one version in the active snapshots.

        A new object that is created in a snapshot (but is not yet committed)
        will be instantiated in the snapshot's local change list.

        There is no tracking of deletions.  An ID of a deleted object is
        invalid as soon as the last snapshot that it was alive in has
        disappeared.
    */

    struct AOEntry {
        AOEntry()
            : local(0)
        {
        }

        AOEntry(AddressableObject * local)
            : local(local)
        {
        }
        
        union {
            struct {
                uint64_t offset;
            };
            uint64_t bits;
        };
        
        AddressableObject * local;
    };
    
    // TODO: should be able to live on the heap (for snapshots) or in a file
    struct AOTable : public std::vector<AOEntry> {
        AOTable()
            : object_count_(0)
        {
        }

        ~AOTable()
        {
            for (const_iterator it = begin(), e = end(); it != e;  ++it)
                delete it->local;
        }

        size_t object_count_;

        size_t object_count() const
        {
            return object_count_;
        }
    };

    AOTable objs;

    struct Snapshot_Data : public AOTable {
        template<typename AO>
        AO * construct()
        {
            auto_ptr<AO> result(new AO());
            push_back(result.get());
            return result.release();
        }

        template<typename AO, typename Arg1>
        AO * construct(const Arg1 & arg1)
        {
            auto_ptr<AO> result(new AO(arg1));
            push_back(result.get());
            return result.release();
        }
    };

    const Snapshot_Data * ss_data() const
    {
        if (!current_trans) no_transaction_exception(this);

        PersistentObjectStore * ncthis
            = const_cast<PersistentObjectStore *>(this);
        Snapshot_Data * result
            = current_trans->local_value<Snapshot_Data>(ncthis);
        if (!result)
            result
                = current_trans
                ->local_value<Snapshot_Data>(ncthis, Snapshot_Data());
        return result;
    }

    Snapshot_Data * ss_data()
    {
        if (!current_trans) no_transaction_exception(this);
        Snapshot_Data * result
            = current_trans->local_value<Snapshot_Data>(this);
        if (!result)
            result
                = current_trans
                ->local_value<Snapshot_Data>(this, Snapshot_Data());
        return result;
    }

    template<typename Underlying>
    AORef<Underlying>
    construct()
    {
        Snapshot_Data * sd = ss_data();
        return sd->construct<TypedAO<Underlying> >();
    }

    template<typename Underlying, typename Arg1>
    AORef<Underlying>
    construct(const Arg1 & arg1)
    {
        Snapshot_Data * sd = ss_data();
        return sd->construct<TypedAO<Underlying> >(arg1);
    }

    size_t object_count() const
    {
        const Snapshot_Data * sd = ss_data();
        return objs.object_count() + sd->object_count();
    }
    

    /*************************************************************************/
    /* ALLOCATION                                                            */
    /*************************************************************************/



    /*************************************************************************/
    /* SNAPSHOTTING                                                          */
    /*************************************************************************/

    // Sync the snapshot with the on-disk version.  Returns the epoch at which
    // the snapshot was made.
    Epoch sync_snapshot() const;

    enum Journal_Mode {
        JOURNAL_NONE,    ///< No journal; commits lost since last snapshot
        JOURNAL_TIMED_SYNC,  ///< Journal is kept and synced every n seconds
        JOURNAL_STRICT       ///< Journal is kept and synced at every trans
    };

    // Change the journal mode
    void set_journal_mode(Journal_Mode mode, float time = 0.0f);


    /*************************************************************************/
    /* VERSIONED_OBJECT INTERFACE                                            */
    /*************************************************************************/

    // Get the commit ready and check that everything can go ahead, but
    // don't actually perform the commit
    virtual bool setup(Epoch old_epoch, Epoch new_epoch, void * data)
    {
        // 1.  Allocate persistent memory for the new value
        
        // 2.  Allocate heap memory for the new internal data structure

        // 3.  Success
        throw Exception("not implemented");
    }

    // Confirm a setup commit, making it permanent
    virtual void commit(Epoch new_epoch) throw ()
    {
        // 1.  Serialize the new value in the new memory

        // 2.  Switch the pointer to point to the new memory

        // 3.  Arrange for the old memory to be freed once everything has made
        //     a critical section transition
        throw Exception("not implemented");
    }

    // Roll back a setup commit
    virtual void rollback(Epoch new_epoch, void * data) throw ()
    {
        // 1.  Deallocate the memory from the owner in the mmap range
        throw Exception("not implemented");
    }

    // Clean up an unused version
    virtual void cleanup(Epoch unused_valid_from, Epoch trigger_epoch)
    {
        throw Exception("not implemented");
    }
    
    // Rename an epoch to a different number.  Returns the valid_from value
    // of the next epoch in the set.
    virtual Epoch rename_epoch(Epoch old_valid_from,
                               Epoch new_valid_from) throw ()
    {
        throw Exception("not implemented");
    }

private:
    managed_mapped_file backing;

    struct Sandbox_State {

        ~Sandbox_State()
        {
        }
    };
};


BOOST_AUTO_TEST_CASE( test1 )
{
    // The region for persistent objects, as anonymous mapped memory
    PersistentObjectStore store(create_only, "backing1", 65536);

    {
        Local_Transaction trans;
        // Two persistent versioned objects
        AORef<int> obj1 = store.construct<int>(0);
        AORef<int> obj2 = store.construct<int>(1);
        
        BOOST_CHECK_EQUAL(obj1, 0);
        BOOST_CHECK_EQUAL(obj2, 1);

        BOOST_CHECK_EQUAL(store.object_count(), 2);

        // Don't commit the transaction
    }

    {
        Local_Transaction trans;

        // Make sure the objects didn't get committed
        BOOST_CHECK_EQUAL(store.object_count(), 0);
    }
}

#if 0
{
    BOOST_CHECK_EQUAL(store.object_count(), 0);
        
        // Put the current versions of both in the shared memory
        addr1 = obj1.save(current_epoch(), mm);
        addr2 = obj2.save(current_epoch(), mm);
        
        BOOST_CHECK_EQUAL(*(int *)addr1, 0);
        BOOST_CHECK_EQUAL(*(int *)addr2, 1);
        
        // Make sure that they were put at the right place
        BOOST_CHECK(mm.contains_address(addr1));
        BOOST_CHECK(mm.contains_address(addr2));
        
        {
            Local_Transaction trans;
            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);
            
            obj1.write(2);
            obj2.write(3);
            
            BOOST_CHECK_EQUAL(obj1.read(), 2);
            BOOST_CHECK_EQUAL(obj2.read(), 3);
            
            BOOST_CHECK(trans.commit());
        }
        
        BOOST_CHECK_EQUAL(obj1.history_size(), 2);
        BOOST_CHECK_EQUAL(obj2.history_size(), 2);
        
        // Make sure the objects were deleted and their bits set to all 1s
        BOOST_CHECK_EQUAL(*(int *)addr1, -1);
        BOOST_CHECK_EQUAL(*(int *)addr2, -1);
        
        {
            Local_Transaction trans;
            BOOST_CHECK_EQUAL(obj1.read(), 2);
            BOOST_CHECK_EQUAL(obj2.read(), 3);
        }

        mm.save(current_epoch());
        
        BOOST_CHECK_EQUAL(mm.epoch(), current_epoch());
    }

}
#endif
