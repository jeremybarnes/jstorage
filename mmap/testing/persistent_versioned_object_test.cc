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
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>
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


typedef uint64_t ObjectId;

static const ObjectId NO_OBJECT_ID = (ObjectId)-1;

class PersistentObjectStore;


/*****************************************************************************/
/* ADDRESSABLE_OBJECT                                                        */
/*****************************************************************************/

struct AddressableObject : public JMVCC::Versioned_Object {

    AddressableObject(ObjectId id, PersistentObjectStore * owner)
        : id_(id), owner_(owner)
    {
    }

    /** Return the immutable identity of the object */
    ObjectId id() const
    {
        return id_;
    }

    /** Who owns this object */
    PersistentObjectStore * owner() const
    {
        return owner_;
    }

    /** How many versions of the object are there? */
    virtual size_t num_versions() const = 0;

private:
    ObjectId id_;  ///< Identity in the memory mapped region
    PersistentObjectStore * owner_;  ///< Responsible for dealing with it

    friend class AOTableVersion;
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

    TypedAO(ObjectId id, PersistentObjectStore * owner, const T & val = T())
        : AddressableObject(id, owner)
    {
        version_table = VT::create(new T(val), 1);
    }
    
    ~TypedAO()
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
        reinterpret_cast<T *>(val)->~T();
    }
};


/*****************************************************************************/
/* AOTABLE                                                                   */
/*****************************************************************************/

/** A table of addressable objects.  Performs the mapping between an object
    ID and an offset in the file.  This is itself a versioned object.

    Set of all addressable objects that are currently instantiated into
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
    }

    AOTableVersion(const AOTableVersion & other)
        : Underlying(other), object_count_(other.object_count_)
    {
    }

    ~AOTableVersion()
    {
        clear();
    }

    void add(AddressableObject * local)
    {
        local->id_ = size();
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
    typedef TypedAO<AOTableVersion> Underlying;

    AOTable(ObjectId id, PersistentObjectStore * owner)
        : Underlying(id, owner)
    {
    }

    template<typename AO>
    AO * construct()
    {
        auto_ptr<AO> result(new AO(NO_OBJECT_ID, owner()));
        mutate().add(result.get());
        return result.release();
    }
    
    // Construct a given object type
    template<typename AO, typename Arg1>
    typename boost::enable_if<boost::is_base_of<AddressableObject, AO>, AO *>::type
    construct(const Arg1 & arg1)
    {
        auto_ptr<AO> result(new AO(NO_OBJECT_ID, owner(), arg1));
        mutate().add(result.get());
        return result.release();
    }
    
    // Construct a typed AO
    template<typename T, typename Arg1>
    typename boost::disable_if<boost::is_base_of<AddressableObject, T>,
                               TypedAO<T> *>::type
    construct(const Arg1 & arg1)
    {
        return construct<TypedAO<T> >(arg1);
    }
    
    AddressableObject *
    lookup(ObjectId obj) const
    {
        if (obj >= read().size())
            throw Exception("unknown object");

        return read().operator [] (obj).local.get();
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
    AORef(AO * ao = 0)
        : ao(ao), obj(0), rw(false)
    {
    }

    AORef(AddressableObject * ao)
        : ao(dynamic_cast<AO *>(ao)), obj(0), rw(false)
    {
    }

    AO * ao;
    Obj * obj;
    bool rw;

    ObjectId id() const { return ao->id(); }

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

struct PersistentObjectStore : public AOTable {

    // Create a new persistent object store
    template<typename Creation>
    PersistentObjectStore(const Creation & creation,
                          const std::string & filename,
                          size_t size)
        : AOTable(0, this),
          backing(creation, filename.c_str(), size)
    {
    }

private:
    managed_mapped_file backing;
};




/*****************************************************************************/

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
        TypedAO<Obj> tao(0, 0, 1);
        BOOST_CHECK_EQUAL(constructed, destroyed + 1);

    }    

    BOOST_CHECK_EQUAL(constructed, destroyed);
}

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

            AORef<Obj> obj1 = store.construct<Obj>(0);

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

BOOST_AUTO_TEST_CASE( test_commit_objects_committed )
{
    const char * fname = "pvot_backing3";
    remove_file_on_destroy destroyer1(fname);
    unlink(fname);

    constructed = destroyed = 0;

    {
        // The region for persistent objects, as anonymous mapped memory
        PersistentObjectStore store(create_only, fname, 65536);

        ObjectId oid1, oid2;
        
        AORef<Obj> obj1, obj2;

        {
            Local_Transaction trans;
            // Two persistent versioned objects
            
            obj1 = store.construct<Obj>(0);

            BOOST_CHECK_EQUAL(constructed, destroyed + 1);

            oid1 = obj1.id();
            BOOST_CHECK_EQUAL(oid1, 0);
            
            obj2 = store.construct<Obj>(1);

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
            
            oid2 = obj2.id();

            BOOST_CHECK_EQUAL(oid2, 1);

            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);
            
            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            BOOST_REQUIRE(trans.commit());
        }

        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
        
        {
            Local_Transaction trans;
            
            //AORef<Obj> obj1 = store.find<Obj>(oid1);
            //AORef<Obj> obj2 = store.find<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);

            // Make sure the objects didn't get committed
            BOOST_CHECK_EQUAL(store.object_count(), 2);
        }

        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
        
        {
            Local_Transaction trans;
            
            AORef<Obj> obj1 = store.lookup(oid1);
            AORef<Obj> obj2 = store.lookup(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);

            // Make sure the objects didn't get committed
            BOOST_CHECK_EQUAL(store.object_count(), 2);
        }
    }
    
    BOOST_CHECK_EQUAL(constructed, destroyed);
}

BOOST_AUTO_TEST_CASE( test_persistence )
{
    const char * fname = "pvot_backing4";
    remove_file_on_destroy destroyer1(fname);
    unlink(fname);

    constructed = destroyed = 0;

    {
        // The region for persistent objects, as anonymous mapped memory
        PersistentObjectStore store(create_only, fname, 65536);

        //ObjectId oid1, oid2;
        
        AORef<Obj> obj1, obj2;

        {
            Local_Transaction trans;
            // Two persistent versioned objects
            
            obj1 = store.construct<Obj>(0);
            //iod1 = obj1.id();

            BOOST_CHECK_EQUAL(constructed, destroyed + 1);
            
            obj2 = store.construct<Obj>(1);
            //oid2 = obj2.id();

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
            
            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);
            
            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            BOOST_REQUIRE(trans.commit());
        }

        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
    }

    BOOST_CHECK_EQUAL(constructed, destroyed);
    {
        // The region for persistent objects, as anonymous mapped memory
        PersistentObjectStore store(open_only, fname, 65536);

        {
            Local_Transaction trans;
            
            AORef<Obj> obj1 = store.lookup<Obj>(oid1);
            AORef<Obj> obj2 = store.lookup<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);

            // Make sure the objects didn't get committed
            BOOST_CHECK_EQUAL(store.object_count(), 2);
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
