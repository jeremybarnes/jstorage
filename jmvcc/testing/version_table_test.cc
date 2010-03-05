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

using namespace ML;
using namespace JMVCC;
using namespace std;

using boost::unit_test::test_suite;

struct MyAlloc {

    struct Alloc_Info {
        size_t size;
        bool freed;
    };

    hash_map<void *, Alloc_Info> info;

    void * allocate(size_t bytes);
    void deallocate(void * ptr);
};

BOOST_AUTO_TEST_CASE( test_version_table_memory )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, MyAlloc> VT;

    MyAlloc alloc;

    VT * vt = VT::create(10, alloc);

    constructed = destroyed = 0;

    VT::free(vt);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);
}
