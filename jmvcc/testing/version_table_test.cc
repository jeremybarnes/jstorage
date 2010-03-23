/* versioned_test.cc
   Jeremy Barnes, 14 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
   
   Test of versioned objects.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/vector_utils.h"
#include "jml/utils/pair_utils.h"
#include "jml/utils/hash_map.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jmvcc/version_table.h"
#include "jml/utils/testing/live_counting_obj.h"
#include "jml/utils/testing/testing_allocator.h"

using namespace ML;
using namespace JMVCC;
using namespace std;

using boost::unit_test::test_suite;
BOOST_AUTO_TEST_CASE( test_version_table_memory1 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;

    cerr << "sizeof(Obj) = " << sizeof(Obj) << endl;
    cerr << "sizeof(VT::Entry) = " << sizeof(VT::Entry) << endl;
    cerr << "sizeof(VT) = " << sizeof(VT) << endl;

    constructed = destroyed = 0;

    Testing_Allocator_Data alloc;

    VT * vt = VT::create(10, alloc);

    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory2 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;
    Testing_Allocator_Data alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);
    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);
        
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory3 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;
    Testing_Allocator_Data alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);
    vt->push_back(1, 2);
    vt->pop_back(NEVER_PUBLISHED, EXCLUSIVE);

    BOOST_CHECK_EQUAL(constructed, destroyed + 1);

    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory_with_copy )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;
    Testing_Allocator_Data alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);

    size_t old_outstanding = constructed - destroyed;
    
    VT * vt2 = VT::create(*vt, 12);
    VT::free(vt, NEVER_PUBLISHED, SHARED);

    size_t new_outstanding = constructed - destroyed;
    BOOST_CHECK_EQUAL(old_outstanding, new_outstanding);

    VT::free(vt2, NEVER_PUBLISHED, EXCLUSIVE);

    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory_with_copy2 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;
    Testing_Allocator_Data alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);

    size_t old_outstanding = constructed - destroyed;
    
    VT * vt2 = VT::create(*vt, 12);
    VT::free(vt2, NEVER_PUBLISHED, SHARED);

    size_t new_outstanding = constructed - destroyed;
    BOOST_CHECK_EQUAL(old_outstanding, new_outstanding);

    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);

    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory_with_copy3 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, Testing_Allocator> VT;
    Testing_Allocator_Data alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);

    VT * vt2 = VT::create(*vt, 12);

    size_t old_outstanding = constructed - destroyed;

    vt->push_back(1, 2);
    vt2->push_back(1, 2);

    size_t new_outstanding = constructed - destroyed;
    BOOST_CHECK_EQUAL(old_outstanding + 2, new_outstanding);

    VT::free(vt2, NEVER_PUBLISHED, EXCLUSIVE);
    VT::free(vt, NEVER_PUBLISHED, SHARED);

    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_pointer1 )
{
    typedef Version_Table<Obj *, DeleteCleanup<Obj>, Testing_Allocator> VT;

    constructed = destroyed = 0;

    Testing_Allocator_Data alloc;

    VT * vt = VT::create(10, alloc);

    VT::free(vt, NEVER_PUBLISHED, SHARED);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_pointer2 )
{
    typedef Version_Table<Obj *, DeleteCleanup<Obj>, Testing_Allocator> VT;

    constructed = destroyed = 0;

    Testing_Allocator_Data alloc;

    VT * vt = VT::create(new Obj(1), 10, alloc);

    BOOST_CHECK_EQUAL(constructed, destroyed + 1);

    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_pointer3 )
{
    typedef Version_Table<Obj *, DeleteCleanup<Obj>, Testing_Allocator> VT;

    constructed = destroyed = 0;

    Testing_Allocator_Data alloc;

    VT * vt = VT::create(new Obj(1), 10, alloc);

    vt->push_back(0, new Obj(1));
    vt->push_back(1, new Obj(2));

    BOOST_CHECK_EQUAL(constructed, destroyed + 3);

    vt->pop_back(NEVER_PUBLISHED, EXCLUSIVE);

    BOOST_CHECK_EQUAL(constructed, destroyed + 2);

    VT::free(vt, NEVER_PUBLISHED, EXCLUSIVE);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

