/* storage_test.cc
   Jeremy Barnes, 18 February 2010
   Copyright (c) 2010 Recoset.  All rights reserved.

   Test of the storage functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "recoset/storage/snapshot.h"
#include <signal.h>


using namespace ML;
using namespace RS;
using namespace std;

using boost::unit_test::test_suite;

// TODO: test that this only runs from the parent.  For the moment, we need
// to look at the logfile and make sure that the line occurs only once with
// a single PID.
struct With_Global_Destructor {
    ~With_Global_Destructor()
    {
        cerr << "global destructor called from pid " << getpid() << endl;
    }

} global;

int test_snapshot_child(int & var, int control_fd)
{
    try {

        BOOST_CHECK_EQUAL(var, 0);

        // wait for the 'x' to say 'start'
        char buf = '0';
        int res = read(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);
        BOOST_REQUIRE_EQUAL(buf, 'x');
        
        BOOST_CHECK_EQUAL(var, 0);

        var = 1;

        buf = 'a';
        res = write(control_fd, &buf, 1);

        BOOST_REQUIRE_EQUAL(res, 1);

        res = read(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);
        BOOST_REQUIRE_EQUAL(buf, 'y');

        BOOST_CHECK_EQUAL(var, 1);

        buf = 'b';
        res = write(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);

        return 0;
    } catch (const std::exception & exc) {
        cerr << "child: error " << exc.what() << endl;
        return 1;
    } catch (...) {
        cerr << "child: error" << endl;
        return 1;
    }
}

BOOST_AUTO_TEST_CASE( test_snapshot )
{
    // Stop boost from thinking that a child exiting is an error
    signal(SIGCHLD, SIG_DFL);

    int var = 0;

    Snapshot::Worker w
        = boost::bind(test_snapshot_child, boost::ref(var), _1);
    Snapshot s(w);

    BOOST_CHECK_EQUAL(var, 0);

    // write an "x" to say "start"
    char buf = 'x';
    int res = write(s.control_fd(), &buf, 1);
    
    BOOST_REQUIRE_EQUAL(res, 1);

    res = read(s.control_fd(), &buf, 1);
    BOOST_REQUIRE_EQUAL(res, 1);
    BOOST_REQUIRE_EQUAL(buf, 'a');

    var = 2;

    buf = 'y';
    res = write(s.control_fd(), &buf, 1);
    
    BOOST_REQUIRE_EQUAL(res, 1);
    
    res = read(s.control_fd(), &buf, 1);
    BOOST_REQUIRE_EQUAL(res, 1);
    BOOST_REQUIRE_EQUAL(buf, 'b');
}
