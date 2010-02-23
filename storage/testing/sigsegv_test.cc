/* sigsegv_test.cc
   Jeremy Barnes, 23 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of the segmentation fault handler functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/file_functions.h"
#include "jml/utils/info.h"
#include "jml/arch/exception.h"
#include "jml/arch/vm.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "recoset/storage/snapshot.h"
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>


using namespace ML;
using namespace RS;
using namespace std;

void * mmap_addr = 0;

volatile int num_handled = 0;

volatile siginfo_t handled_info;
volatile ucontext_t handled_context;

void test1_segv_handler(int signum, siginfo_t * info, void * context)
{
    cerr << "handled" << endl;

    ++num_handled;
    ucontext_t * ucontext = (ucontext_t *)context;

    (ucontext_t &)handled_context = *ucontext;
    (siginfo_t &)handled_info = *info;

    // Make the memory writeable
    int res = mprotect(mmap_addr, page_size, PROT_READ | PROT_WRITE);
    if (res == -1)
        cerr << "error in mprotect: " << strerror(errno) << endl;

    // Return to the trapping statement, which will perform the call
}

BOOST_AUTO_TEST_CASE ( test1_segv_restart )
{
    // Create a memory mapped page, read only
    mmap_addr = mmap(0, page_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);

    BOOST_REQUIRE(mmap_addr != 0);

    // Install a segv handler
    struct sigaction action;
    action.sa_sigaction = test1_segv_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;

    int res = sigaction(SIGSEGV, &action, 0);
    
    BOOST_REQUIRE_EQUAL(res, 0);

    char * mem = (char *)mmap_addr;
    BOOST_CHECK_EQUAL(*mem, 0);

    cerr << "before handler" << endl;
    cerr << "addr = " << mmap_addr << endl;

    // write to the memory address; this will cause a SEGV
 dowrite:
    *mem = 'x';

    cerr << "after handler" << endl;

    void * x = &&dowrite;
    cerr << "x = " << x << endl;

    cerr << "signal info:" << endl;
    cerr << "  errno:   " << strerror(handled_info.si_errno) << endl;
    cerr << "  code:    " << handled_info.si_code << endl;
    cerr << "  si_addr: " << handled_info.si_addr << endl;
    cerr << "  status:  " << strerror(handled_info.si_status) << endl;
    cerr << "  RIP:     " << format("%012p", handled_context.uc_mcontext.gregs[16]) << endl;

    // Check that it was handled properly
    BOOST_CHECK_EQUAL(*mem, 'x');
    BOOST_CHECK_EQUAL(num_handled, 1);
}
