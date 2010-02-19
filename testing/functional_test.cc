/* functional_test.cc
   Jeremy Barnes, 18 February 2010
   Copyright (c) 2010 Recoset.  All rights reserved.

   Functional testing for the platform.
*/


#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>

using namespace ML;
using namespace std;

using boost::unit_test::test_suite;

// Scalar attribute (integer)
BOOST_AUTO_TEST_CASE( storage_functional_test1 )
{
    // GOAL: test our ability to create a table and add a document to it
    Storage storage;
    
    TableStorage table_storage = storage.create_table("MyTable");
    table_storage.clear();

    BOOST_CHECK_EQUAL(table.record_count(), 0);
    
    TableView view;
    
}
