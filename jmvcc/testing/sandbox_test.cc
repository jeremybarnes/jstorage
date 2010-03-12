/* sandbox_test.cc
   Jeremy Barnes, 3 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test for the sandbox functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/vector_utils.h"
#include "jml/utils/pair_utils.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/thread.hpp>
#include "jmvcc/sandbox.h"
#include "jmvcc/versioned.h"
#include "jml/utils/testing/live_counting_obj.h"

using namespace ML;
using namespace JMVCC;
using namespace std;

using boost::unit_test::test_suite;

// Make sure that the sandbox calls the destructors on the objects when it
// dies
BOOST_AUTO_TEST_CASE( test_sandbox_calls_destructors )
{
    constructed = destroyed = 0;

    
    {
        Versioned<Obj> ver(0);
        
        Sandbox sandbox;
        
        BOOST_CHECK_EQUAL(constructed, destroyed + 1);
        
        sandbox.local_value<Obj>(&ver, Obj());
        
        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
    }

    BOOST_CHECK_EQUAL(constructed, destroyed);
}

// Make sure that the destructors are called in the right order
BOOST_AUTO_TEST_CASE( test_sandbox_destructor_order )
{
}
