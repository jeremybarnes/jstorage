/* persistent_versioned_object_test.cc
   Jeremy Barnes, 2 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of persistent versioned objects.
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "mmap/pvo.h"
#include "mmap/pvo_manager.h"
#include "mmap/pvo_store.h"

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
#include <boost/interprocess/file_mapping.hpp>
#include <signal.h>

using namespace boost::interprocess;


using namespace ML;
using namespace JMVCC;
using namespace std;

namespace JMVCC {

template<>
struct Serializer<Obj> {

    static void * serialize(const Obj & obj, MemoryManager & mm)
    {
        void * mem = mm.allocate_aligned(4, 4);
        int32_t * v = reinterpret_cast<int32_t *>(mem);
        *v = obj.val;

        cerr << "serializing Obj to " << mem << " wrote " << obj.val << endl;
        
        return mem;
    }

    static void deallocate(void * mem, MemoryManager & mm)
    {
        mm.deallocate(mem, 4);
    }

    static void reconstitute(Obj & obj,
                             const void * mem,
                             MemoryManager & mm)
    {
        cerr << "&obj = " << &obj << endl;
        cerr << "mem = " << mem << endl;

        const int32_t * p = (const int32_t *)mem;
        obj.val = *p;

        cerr << "reconstituing Obj from " << mem << " got " << *p << endl;
    }
};

} // namespace JMVCC

/*****************************************************************************/

BOOST_AUTO_TEST_CASE( test_construct_in_trans1 )
{
    const char * fname = "pvot_backing1";
    unlink(fname);
    remove_file_on_destroy destroyer1(fname);

    // The region for persistent objects, as anonymous mapped memory
    PVOStore store(create_only, "pvot_backing1", 65536);

    {
        Local_Transaction trans;
        // Two persistent versioned objects
        PVORef<int> obj1 = store.construct<int>(0);
        PVORef<int> obj2 = store.construct<int>(1);
        
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

BOOST_AUTO_TEST_CASE( test_typedpvo_destroyed )
{
    const char * fname = "pvot_backing1a";
    remove_file_on_destroy destroyer1(fname);
    unlink(fname);

    constructed = destroyed = 0;
    {
        PVOStore store(create_only, fname, 65536);

        {
            Local_Transaction trans;
            BOOST_CHECK_EQUAL(constructed, destroyed);
            PVORef<Obj> tpvo = store.construct<Obj>(1);
            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
        }

        BOOST_CHECK_EQUAL(constructed, destroyed);

        {
            Local_Transaction trans;
            PVORef<Obj> tpvo2 = store.construct<Obj>(2);
            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
            trans.commit();
            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
        }
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
        PVOStore store(create_only, fname, 65536);
        
        {
            Local_Transaction trans;
            // Two persistent versioned objects

            PVORef<Obj> obj1 = store.construct<Obj>(0);

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);
            
            PVORef<Obj> obj2 = store.construct<Obj>(1);

            BOOST_CHECK_EQUAL(constructed, destroyed + 4);
            
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
        PVOStore store(create_only, fname, 65536);

        ObjectId oid1, oid2;
        
        PVORef<Obj> obj1, obj2;

        {
            Local_Transaction trans;
            // Two persistent versioned objects
            
            obj1 = store.construct<Obj>(0);

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);

            oid1 = obj1.id();
            BOOST_CHECK_EQUAL(oid1, 0);
            
            obj2 = store.construct<Obj>(1);

            BOOST_CHECK_EQUAL(constructed, destroyed + 4);
            
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
            
            //PVORef<Obj> obj1 = store.find<Obj>(oid1);
            //PVORef<Obj> obj2 = store.find<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 0);
            BOOST_CHECK_EQUAL(obj2.read(), 1);

            // Make sure the objects didn't get committed
            BOOST_CHECK_EQUAL(store.object_count(), 2);
        }

        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
        
        {
            Local_Transaction trans;
            
            PVORef<Obj> obj1 = store.lookup<Obj>(oid1);
            PVORef<Obj> obj2 = store.lookup<Obj>(oid2);

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
    //remove_file_on_destroy destroyer1(fname);
    unlink(fname);

    constructed = destroyed = 0;

    ObjectId oid1, oid2;

    size_t free_memory_before = 0, free_memory_after = 0;

    {
        // The region for persistent objects, as anonymous mapped memory
        PVOStore store(create_only, fname, 65536);

        free_memory_before = store.get_free_memory();

        PVORef<Obj> obj1, obj2;

        {
            Local_Transaction trans;
            // Two persistent versioned objects
            
            obj1 = store.construct<Obj>(14);

            BOOST_CHECK_EQUAL(constructed, destroyed + 2);

            oid1 = obj1.id();
            BOOST_CHECK_EQUAL(oid1, 0);
            
            obj2 = store.construct<Obj>(31);

            BOOST_CHECK_EQUAL(constructed, destroyed + 4);
            
            oid2 = obj2.id();

            BOOST_CHECK_EQUAL(oid2, 1);

            BOOST_CHECK_EQUAL(obj1.read(), 14);
            BOOST_CHECK_EQUAL(obj2.read(), 31);
            
            BOOST_CHECK_EQUAL(store.object_count(), 2);

            BOOST_CHECK_EQUAL(free_memory_before, store.get_free_memory());
            
            BOOST_REQUIRE(trans.commit());

            BOOST_CHECK(free_memory_before > store.get_free_memory());

            free_memory_after = store.get_free_memory();
        }
        
        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
    }

    //for (;;) ;
    
    BOOST_CHECK_EQUAL(constructed, destroyed);
    {
        // The region for persistent objects, as anonymous mapped memory
        PVOStore store(open_only, fname);

        BOOST_CHECK_EQUAL(free_memory_after, store.get_free_memory());

        {
            Local_Transaction trans;

            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            PVORef<Obj> obj1 = store.lookup<Obj>(oid1);
            PVORef<Obj> obj2 = store.lookup<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 14);
            BOOST_CHECK_EQUAL(obj2.read(), 31);

            BOOST_CHECK_EQUAL(store.object_count(), 2);
        }

        BOOST_CHECK_EQUAL(free_memory_after, store.get_free_memory());
    }

    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    {
        // The region for persistent objects, as anonymous mapped memory
        PVOStore store(open_only, fname);

        {
            Local_Transaction trans;

            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            trans.Sandbox::dump(cerr);

            PVORef<Obj> obj1 = store.lookup<Obj>(oid1);
            PVORef<Obj> obj2 = store.lookup<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 14);
            BOOST_CHECK_EQUAL(obj2.read(), 31);

            obj1.mutate() = 23;
            obj2.mutate() = 45;

            BOOST_CHECK_EQUAL(store.object_count(), 2);

            BOOST_CHECK_EQUAL(free_memory_after, store.get_free_memory());

            trans.commit();

            // Give the memory time to be reclaimed
        }

        BOOST_CHECK_EQUAL(free_memory_after - store.get_free_memory(), 0);
    }
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    {
        // The region for persistent objects, as anonymous mapped memory
        PVOStore store(open_only, fname);

        {
            Local_Transaction trans;

            BOOST_CHECK_EQUAL(store.object_count(), 2);
            
            trans.Sandbox::dump(cerr);

            PVORef<Obj> obj1 = store.lookup<Obj>(oid1);
            PVORef<Obj> obj2 = store.lookup<Obj>(oid2);

            BOOST_CHECK_EQUAL(obj1.read(), 23);
            BOOST_CHECK_EQUAL(obj2.read(), 45);

            BOOST_CHECK_EQUAL(store.object_count(), 0);

            obj1.remove();
            obj2.remove();

            BOOST_CHECK_EQUAL(store.object_count(), 0);
            
            BOOST_CHECK_THROW(obj1.read(), ML::Exception);
            BOOST_CHECK_THROW(obj2.read(), ML::Exception);

            BOOST_CHECK_EQUAL(free_memory_after, store.get_free_memory());

            trans.commit();

            // Give the memory time to be reclaimed
        }

        BOOST_CHECK_EQUAL(free_memory_before, store.get_free_memory());
    }
    
    BOOST_CHECK_EQUAL(constructed, destroyed);
}

size_t counter = 1;

struct With_Parent : public Versioned2<Obj> {
    With_Parent(With_Parent * parent, int index)
        : parent_(parent), index(index), destroy_order(0)
    {
    }

    With_Parent * parent_;
    int index;
    mutable size_t destroy_order;

    virtual With_Parent * parent() const { return parent_; }

    virtual void destroy_local_value(void * val) const
    {
        destroy_order = counter++;
        Versioned2<Obj>::destroy_local_value(val);
    }
};



#if 0
template<class Var>
struct Object_Test_Thread2 {
    typedef typename Var::value_type Val;

    Var * vars;
    int nvars;
    int iter;
    boost::barrier & barrier;
    size_t & failures;

    Object_Test_Thread2(Var * vars,
                        int nvars,
                        int iter, boost::barrier & barrier,
                        size_t & failures)
        : vars(vars), nvars(nvars), iter(iter), barrier(barrier),
          failures(failures)
    {
    }
    
    void operator () ()
    {
        // Wait for all threads to start up before we continue
        barrier.wait();
        
        int errors = 0;
        int local_failures = 0;
        
        for (unsigned i = 0;  i < iter;  ++i) {
            // Keep going until we succeed
            int var1 = random() % nvars, var2 = random() % nvars;
            
            bool succeeded = false;

            while (!succeeded) {
                Local_Transaction trans;
                
                // Now that we're inside, the total should be zero
                ssize_t total = 0;

                for (unsigned i = 0;  i < nvars;  ++i)
                    total += vars[i].read();

                if (total != 0) {
                    ACE_Guard<ACE_Mutex> guard(commit_lock);
                    cerr << "--------------- total not zero" << endl;
                    snapshot_info.dump();
                    cerr << "total is " << total << endl;
                    cerr << "trans.epoch() = " << trans.epoch() << endl;
                    ++errors;
                    for (unsigned i = 0;  i < nvars;  ++i)
                        vars[i].dump();
                    cerr << "--------------- end total not zero" << endl;
                }

                Val & val1 = vars[var1].mutate();
                Val & val2 = vars[var2].mutate();
                    
                val1 -= 1;
                val2 += 1;
                
                succeeded = trans.commit();
                local_failures += !succeeded;
            }
        }

        static Lock lock;
        Guard guard(lock);
        
        BOOST_CHECK_EQUAL(errors, 0);
        
        failures += local_failures;
    }
};

template<class Var>
void run_object_test2(int nthreads, int niter, int nvals)
{
    cerr << endl << "testing 2 with " << nthreads << " threads and "
         << niter << " iter"
         << " class " << demangle(typeid(Var).name()) << endl;

    constructed = destroyed = 0;

    {
        Var vals[nvals];
        boost::barrier barrier(nthreads);
        boost::thread_group tg;
        
        size_t failures = 0;
        
        Timer timer;
        for (unsigned i = 0;  i < nthreads;  ++i)
            tg.create_thread(Object_Test_Thread2<Var>(vals, nvals, niter,
                                                      barrier, failures));
        
        tg.join_all();
        
        cerr << "elapsed: " << timer.elapsed() << endl;
        
        ssize_t total = 0;
        {
            Local_Transaction trans;
            for (unsigned i = 0;  i < nvals;  ++i)
                total += vals[i].read();
        }
        
        BOOST_CHECK_EQUAL(snapshot_info.entry_count(), 0);
        
        BOOST_CHECK_EQUAL(total, 0);
        for (unsigned i = 0;  i < nvals;  ++i) {
            //cerr << "current_epoch = " << get_current_epoch() << endl;
            if (vals[i].history_size() != 0)
                vals[i].dump();
            BOOST_CHECK_EQUAL(vals[i].history_size(), 0);
        }
    }

    BOOST_CHECK_EQUAL(constructed, destroyed);
}
#endif


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
