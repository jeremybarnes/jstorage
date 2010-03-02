/* graphmap_test.cc
   Jeremy Barnes, 18 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of the graphmap functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jml/arch/vm.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>

#include <signal.h>


using namespace ML;
//using namespace JGraph;
using namespace std;

BOOST_AUTO_TEST_CASE( test1 )
{
}
