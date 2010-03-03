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

typedef uint32_t ObjectID;

class PersistentObjectStore;


/*****************************************************************************/
/* AOTABLE                                                                   */
/*****************************************************************************/

/** A table of the addressable objects that are alive in a space.  Backed by
    a memory map.  In an independent file so that it can grow independently
    from the memory it uses.

    For references to external objects, 
*/

struct AOTable {
    // We 
#if 0
    struct Entry {
        union {
            struct {
                uint64_t area:22;          ///< For if we have multiple areas
                uint64_t offset:40;        ///< 1TB; should be enough
                uint64_t alloc:1;          ///< Allocated or free?
                uint64_t locked1:1;        ///< For 32 bit machines
            };
            uint64_t bits1;
        };
        union {
            struct {
                uint64_t generation:24;    ///< For external to detect deletion
                uint64_t type:24;          ///< Type of object
                uint64_t ver:3;            ///< Version of this data structure
                uint64_t locked2:1;        ///< For 32 bit machines
            };
            uint64_t bits2;
        };
    };
#endif
    struct Entry {
        union {
            struct {
                uint64_t offset:48;
                uint64_t unused:14;
                uint64_t alloc:1;
                uint64_t locked:1;    ///< For machines without CAS64
            };
            uint64_t bits;
        };
    };

    std::vector<Entry> entries;
};


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
/* ADDRESSABLE_OBJECT_TEMPLATE                                              */
/*****************************************************************************/

template<typename T, bool Inline = sizeof(T) <= sizeof(void *)>
struct AOT : public AddressableObject {

    // Get the commit ready and check that everything can go ahead, but
    // don't actually perform the commit
    virtual bool setup(Epoch old_epoch, Epoch new_epoch, void * data)
    {
        // 1.  Allocate persistent memory for the new value
        
        // 2.  Allocate heap memory for the new internal data structure

        // 3.  Success
    }

    // Confirm a setup commit, making it permanent
    virtual void commit(Epoch new_epoch) throw ()
    {
        // 1.  Serialize the new value in the new memory

        // 2.  Switch the pointer to point to the new memory

        // 3.  Arrange for the old memory to be freed once everything has made
        //     a critical section transition
    }

    // Roll back a setup commit
    virtual void rollback(Epoch new_epoch, void * data) throw ()
    {
        // 1.  Deallocate the memory from the owner in the mmap range
    }

    // Clean up an unused version
    virtual void cleanup(Epoch unused_valid_from, Epoch trigger_epoch)
    {
    }
    
    // Rename an epoch to a different number.  Returns the valid_from value
    // of the next epoch in the set.
    virtual Epoch rename_epoch(Epoch old_valid_from,
                               Epoch new_valid_from) throw ()
    {
    }

    virtual void dump(std::ostream & stream = std::cerr, int indent = 0) const
    {
    }

    virtual void dump_unlocked(std::ostream & stream = std::cerr,
                               int indent = 0) const
    {
    }

    virtual std::string print_local_value(void * val) const
    {
    }

private:
    const char * current;  ///< Current version as serialized
};



/*****************************************************************************/
/* SNAPSHOT_MANAGER                                                          */
/*****************************************************************************/

struct Snapshot_Manager {
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
        : backing(creation, filename, size)
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
    std::hash_map<ObjectID, AddressableObject *> addressable;

    AddressableObject * get_object();


    /*************************************************************************/
    /* NAMED OBJECTS                                                         */
    /*************************************************************************/

    struct NamedObjectRef {
    };

    template<typename Underlying>
    struct PVConstructor {
        PVConstructor(void * mem, size_t size, PersistentObjectStore * store)
            : mem(mem), size(size), store(store), used(false), called(false)
        {
        }

        ~ObjectConstructor()
        {
            if (!called)
                throw Exception("ObjectConstructor was not called");
            if (!used)
                store->deallocate(mem, size);
        }
        
        void * mem;
        size_t size;
        PersistentObjectStore * store;
        bool used;
        bool called;
        
        PV<Underlying> operator () () const
        {
            called = true;
            PV<Underlying> result(new (mem) Object());
            used = true;
            return result;
        }

        template<typename Arg1>
        PV<Underlying> operator () (const Arg1 & arg1) const
        {
            called = true;
            PV<Underlying> result(new (mem) Object(arg1));
            used = true;
            return result;
        }

        template<typename Arg1, typename Arg2>
        PV<Underlying>
        operator () (const Arg1 & arg1, const Arg2 & arg2) const
        {
            called = true;
            PV<Underlying> result(new (mem) Object(arg1, arg2));
            used = true;
            return result;
        }
    };

    template<typename Underlying>
    ObjectConstructor<PV<Underlying> >
    construct(const std::string & name)
    {
        size_t size = sizeof(Underlying);
        void * mem = backing.construct<char>[size]();
        return ObjectConstructor(mem, size, this);
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
        PV<int> obj1 = store.construct<int>("obj1")(0);
        PV<int> obj2 = store.construct<int>("obj2")(1);
        
        BOOST_CHECK_EQUAL(obj1.read(), 0);
        BOOST_CHECK_EQUAL(obj2.read(), 1);

        BOOST_CHECK_EQUAL(store.object_count(), 2);

        // Don't commit the transaction
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
