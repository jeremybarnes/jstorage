/* persistent_versioned_object_test.cc
   Jeremy Barnes, 2 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of persistent versioned objects.
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jml/arch/demangle.h"
#include "jmvcc/versioned2.h"
#include "jmvcc/snapshot.h"
#include <boost/shared_ptr.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>
#include "jml/utils/testing/live_counting_obj.h"
#include "jml/utils/hash_map.h"

#include <signal.h>

#include <boost/interprocess/managed_mapped_file.hpp>

using namespace boost::interprocess;


using namespace ML;
using namespace JMVCC;
using namespace std;


typedef uint64_t ObjectID;

class PersistentObjectStore;

class AddressableObject;

struct AOInfo {
    AOInfo(bool live = true)
        : live(live)
    {
    }

    bool live;
};

typedef hash_map<AddressableObject *, AOInfo> AOS;
AOS aos;

void print_aos_info()
{
    size_t live = 0;

    for (AOS::const_iterator it = aos.begin(), end = aos.end();
         it != end;  ++it) {
        cerr << format("%012p %c %s\n", it->first, it->second.live ? 'L' : '.',
                       (it->second.live ? type_name(*it->first).c_str() : ""));
        
        live += it->second.live;
        
    }

    cerr << "total " << live << " live objects" << endl;
}


/*****************************************************************************/
/* ADDRESSABLE_OBJECT                                                        */
/*****************************************************************************/

struct AddressableObject : public JMVCC::Versioned_Object {

#if 0
    AddressableObject()
    {
        aos[this].live = true;
    }

    virtual ~AddressableObject()
    {
        cerr << "destroying AO " << type_name(*this) << " at " << this
             << endl;
        aos[this].live = false;
    }
#endif

    /** Return the immutable identity of the object */
    ObjectID id() const;

    /** Who owns this object */
    PersistentObjectStore * owner() const;

    /** How many versions of the object are there? */
    virtual size_t num_versions() const = 0;

private:
    ObjectID id_;  ///< Identity in the memory mapped region
    PersistentObjectStore * owner_;  ///< Responsible for dealing with it
};


/*****************************************************************************/
/* TYPED_AO                                                                  */
/*****************************************************************************/

/** Note that an addressable object potentially has multiple versions:
    - Zero or one read-write version local to each sandbox;
    - One or zero versions in the permanent storage;
    - One or zero versions for each snapshot
*/

template<typename T>
struct TypedAO
    : public AddressableObject {

    TypedAO(const T & val = T())
    {
        cerr << "creating TypedAO " << type_name<T>()
             << " at " << this << endl;
        version_table = VT::create(new T(val), 1);
        dump();
        cerr << endl;
    }
    
    ~TypedAO()
    {
        cerr << "destroying TypedAO " << type_name<T>() << " at " <<
            this << endl;
        dump();
        cerr << endl;
        
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
            cerr << "valcleanup running for " << val << endl;
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
            
            static Lock lock;
            Guard guard2(lock);
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
        cerr << "destroy_local_value for " << type_name<T>() << " at "
             << val << endl;
        reinterpret_cast<T *>(val)->~T();
    }
};


/*****************************************************************************/
/* AOTABLE                                                                   */
/*****************************************************************************/

/** A table of addressable objects.  Performs the mapping between an object
    ID and an offset in the file.  This is itself a versioned object.
*/

struct AOEntry {
    AOEntry()
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
        
    boost::shared_ptr<AddressableObject> local;
};

std::ostream & operator << (std::ostream & stream,
                            const AOEntry & entry)
{
    return stream << entry.local << " ref " << entry.local.use_count() << " "
                  << type_name(*entry.local) << endl;
}

/** The addressable objects table for a single snapshot (ie no version
    control). */
struct AOTableVersion : public std::vector<AOEntry> {
    typedef std::vector<AOEntry> Underlying;

    AOTableVersion()
        : object_count_(0)
    {
        cerr << "default construct AOTableVersion " << this << endl;
    }

    AOTableVersion(const AOTableVersion & other)
        : Underlying(other), object_count_(other.object_count_)
    {
        cerr << "copied AOTableVersion " << &other << " to "
             << this << endl;
    }

    ~AOTableVersion()
    {
        cerr << "killed AOTableVersion " << this << endl;
        for (unsigned i = 0;  i < size();  ++i)
            cerr << "entry " << i << ": " << operator [] (i) << endl;
        clear();
    }

    void add(AddressableObject * local)
    {
        push_back(AOEntry(local));
        ++object_count_;
    }

    size_t object_count_;

    size_t object_count() const
    {
        return object_count_;
    }
};

inline std::ostream &
operator << (std::ostream & stream, const AOTableVersion & ver)
{
    return stream;
}

struct AOTable : public TypedAO<AOTableVersion> {
    template<typename AO>
    AO * construct()
    {
        auto_ptr<AO> result(new AO());
        mutate().add(result.get());
        return result.release();
    }
    
    template<typename AO, typename Arg1>
    AO * construct(const Arg1 & arg1)
    {
        auto_ptr<AO> result(new AO(arg1));
        mutate().add(result.get());
        return result.release();
    }

    size_t object_count() const
    {
        return read().object_count();
    }
};


/*****************************************************************************/
/* AOREF                                                                     */
/*****************************************************************************/

/** This is a lightweight, temporary object designed to allow for access to
    a value that may either
    a) exist in memory as a sandboxed value;
    b) exist in memory as a versioned object; or
    c) be serialized on disk in some format.
*/

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

    const Obj & read() const { return ao->read(); }
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

    ~PersistentObjectStore()
    {
        cerr << "destroying PersistentObjecctStore at " << this << endl;
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

    AOTable objs;

    template<typename Underlying>
    AORef<Underlying>
    construct()
    {
        return objs.construct<TypedAO<Underlying> >();
    }

    template<typename Underlying, typename Arg1>
    AORef<Underlying>
    construct(const Arg1 & arg1)
    {
        return objs.construct<TypedAO<Underlying> >(arg1);
    }
    
    size_t object_count() const
    {
        return objs.object_count();
    }
    

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
};

#if 0

BOOST_AUTO_TEST_CASE( test_construct_in_trans1 )
{
    const char * fname = "pvot_backing1";
    unlink(fname);
    remove_file_on_destroy destroyer1(fname);

    // The region for persistent objects, as anonymous mapped memory
    PersistentObjectStore store(create_only, "pvot_backing1", 65536);

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

BOOST_AUTO_TEST_CASE( test_typedao_destroyed )
{
    constructed = destroyed = 0;
    {
        TypedAO<Obj> tao(1);
        BOOST_CHECK_EQUAL(constructed, destroyed + 1);

    }    

    BOOST_CHECK_EQUAL(constructed, destroyed);
}

#endif

BOOST_AUTO_TEST_CASE( test_rollback_objects_destroyed )
{
    const char * fname = "pvot_backing2";
    remove_file_on_destroy destroyer1(fname);
    unlink(fname);

    constructed = destroyed = 0;

    {
        // The region for persistent objects, as anonymous mapped memory
        PersistentObjectStore store(create_only, fname, 65536);
        
        {
            Local_Transaction trans;
            // Two persistent versioned objects

            cerr << endl << endl;
            cerr << "before creating object" << endl;
            AORef<Obj> obj1 = store.construct<Obj>(0);
            cerr << "obj1.ao = " << obj1.ao << endl;
            cerr << "after creating object" <<endl;
            cerr << endl << endl;

            print_aos_info();

            BOOST_CHECK_EQUAL(constructed, destroyed + 1);
            
            AORef<Obj> obj2 = store.construct<Obj>(1);

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
            
            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);
            
            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            // Don't commit the transaction
        }

        BOOST_CHECK_EQUAL(constructed, destroyed);
        
        {
            Local_Transaction trans;
            
            // Make sure the objects didn't get committed
            BOOST_CHECK_EQUAL(store.object_count(), 0);
        }
    }

    BOOST_CHECK_EQUAL(constructed, destroyed);
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
