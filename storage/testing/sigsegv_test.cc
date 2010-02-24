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
#include "jml/arch/atomic_ops.h"
#include "jml/arch/vm.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "recoset/storage/sigsegv.h"
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

#include <boost/thread.hpp>
#include <boost/thread/barrier.hpp>


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

    BOOST_REQUIRE(mmap_addr != MAP_FAILED);

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

void test2_segv_handler_thread(char * addr)
{
    *addr = 'x';
}

BOOST_AUTO_TEST_CASE ( test2_segv_handler )
{
    // Create a memory mapped page, read only
    void * vaddr = mmap(0, page_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    
    char * addr = (char *)vaddr;

    BOOST_REQUIRE(addr != MAP_FAILED);

    install_segv_handler();

    int region = register_segv_region(addr, addr + page_size);

    int nthreads = 1;

    boost::thread_group tg;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(boost::bind(&test2_segv_handler_thread, addr));

    sleep(1);

    int res = mprotect(vaddr, page_size, PROT_READ | PROT_WRITE);

    BOOST_CHECK_EQUAL(res, 0);

    unregister_segv_region(region);

    tg.join_all();

    BOOST_CHECK_EQUAL(*addr, 'x');

    BOOST_CHECK_EQUAL(get_num_segv_faults_handled(), 1);
}

// Thread to continually modify the memory
void test2_segv_handler_stress_thread1(int * addr,
                                       int npages,
                                       boost::barrier & barrier,
                                       volatile bool & finished)
{
    barrier.wait();

    cerr << "m";

    int * end = addr + npages * page_size / sizeof(int);

    while (!finished) {
        for (int * p = addr;  p != end;  ++p)
            atomic_add(*p, 1);
        memory_barrier();
    }

    cerr << "M";
}

// Thread to continually unmap and remap the memory
void test2_segv_handler_stress_thread2(int * addr,
                                       boost::barrier & barrier,
                                       volatile bool & finished)
{
    barrier.wait();

    cerr << "p";

    while (!finished) {
        int region = register_segv_region(addr, addr + page_size);
        mprotect(addr, page_size, PROT_READ);
        mprotect(addr, page_size, PROT_READ | PROT_WRITE);
        unregister_segv_region(region);
    }

    cerr << "P";
}

BOOST_AUTO_TEST_CASE ( test2_segv_handler_stress )
{
    int npages = 8;

    // Create a memory mapped page, read only
    void * vaddr = mmap(0, npages * page_size, PROT_WRITE | PROT_READ,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    
    int * addr = (int *)vaddr;

    BOOST_REQUIRE(addr != MAP_FAILED);

    install_segv_handler();

    // 8 threads simultaneously causing faults, with 8 threads writing to
    // pages

    int nthreads = 8;

    volatile bool finished = false;

    boost::barrier barrier(nthreads + npages);

    boost::thread_group tg;
    for (unsigned i = 0;  i < nthreads;  ++i)
        tg.create_thread(boost::bind(&test2_segv_handler_stress_thread1,
                                     addr, npages, boost::ref(barrier),
                                     boost::ref(finished)));

    for (unsigned i = 0;  i < npages;  ++i)
        tg.create_thread(boost::bind(&test2_segv_handler_stress_thread2,
                                     addr + i * page_size,
                                     boost::ref(barrier),
                                     boost::ref(finished)));

    sleep(2);

    finished = true;

    tg.join_all();

    cerr << endl;

    // All values in all of the pages should be the same value
    int val = *addr;

    cerr << "val = " << val << endl;
    
    for (unsigned i = 0;  i < npages;  ++i)
        for (unsigned j = 0;  j < page_size / sizeof(int);  ++j)
            BOOST_CHECK_EQUAL(addr[i * page_size / sizeof(int) + j], val);

    cerr << get_num_segv_faults_handled() << " segv faults handled" << endl;

    BOOST_CHECK(get_num_segv_faults_handled() > 1);
}


bool needs_backing(const Pagemap_Entry & current_pagemap,
                   const Pagemap_Entry & old_pagemap)
{
    return current_pagemap.present
        && old_pagemap.present
        && current_pagemap.swapped == old_pagemap.swapped
        && current_pagemap.pfn == old_pagemap.pfn;
}

/** Make the VM subsystem know that modified pages in a MAP_PRIVATE mmap
    segment have now been written to disk and so the private copy can be
    returned to the system.  The function operates transparently to all
    other threads, which can read and write memory as they wish (using
    appropriate memory barriers) transparently.

    In order to detect which pages are now written to disk, it is necessary
    to have forked another process that has exactly those pages that were
    written to disk in memory.  It can't have remapped the written pages
    onto its address space yet.  A file descriptor to that process's open
    /proc/pid/pagemaps file needs to be passed in, as well as a file
    descriptor to the *current* process's /proc/self/pagemaps file.

    In order to determine if a page can be updated, we perform the following
    process:
      - If the page is not present in the current process, then it must still
        be backed by the file and so it is skipped;
      - If the page is present in the current process but not present in the
        previous process, then the current process must have written to it
        and so it is skipped;
      - If the page is present in both processes but the physical page is
        different, then it must have been modified in the current process
        since the snapshot and so it is skipped;
      - Otherwise, we can memory map the portion of the file in the current
        process as it must be identical to the portion of the file.

    In order to do this atomically, the following procedure is used:
    1.  We remap the memory range read-only so that writes are no longer
        possible.
    2.  We re-perform the check to make sure that the page wasn't written
        to between when we checked and when we made it read-only.
    3.  We memory map the underlying page as a copy on write mapping.

    If between 1 and 3 there was an attempted write to the page being
    remapped, the thread that was doing it will get a segmentation fault.
    This fault is handled by:
    1.  Checking that the fault address was within the memory range.  If not,
        a normal segfault is generated.
    2.  Busy-waiting in the signal handler until the page is re-mapped
        and can therefore be written to.
    3.  Returning from the signal handler to allow the write to happen to the
        newly mapped page.

    Of course, to do this we need to make sure that all threads that start up
    handle sigsegv.
*/

void reback_range_after_write(void * memory, size_t length,
                              int backing_file_fd,
                              size_t backing_file_offset,
                              int old_pagemap_file,
                              int current_pagemap_file)
{
    if (!is_page_aligned(memory))
        throw Exception("reback_range_after_write(): memory not page aligned");
    if (length % page_size != 0)
        throw Exception("reback_range_after_write(): not an integral number of"
                        "pages");

    size_t npages = length / page_size;

    size_t CHUNK = 1024;  // do 1024 pages = 4MB at once

    // To store the page map entries in
    Pagemap_Entry current_pagemap[CHUNK];
    Pagemap_Entry old_pagemap[CHUNK];

    char * mem = (char *)memory;

    for (unsigned i = 0;  i < npages;  i += CHUNK, mem += CHUNK * page_size) {
        int todo = std::min(npages - i, CHUNK);

        Pagemap_Reader pm_old(mem, todo * page_size, old_pagemap,
                              old_pagemap_file);
        Pagemap_Reader pm_current(mem, todo * page_size, current_pagemap,
                                  current_pagemap_file);
        
        // The value of j at which we start backing pages
        int backing_start = -1;
        
        // Now go through and back those pages needed
        for (unsigned j = 0;  j <= todo;  ++j) {  /* <= for last page */
            bool need_backing
                =  j < todo && needs_backing(current_pagemap[j], old_pagemap[j]);

            if (backing_start != -1 && !need_backing) {
                // We need to re-map the pages from backing_start to
                // backing_end.  Note that remapping pages that are not
                // present doesn't cause any problems; we could potentially
                // make our chunks larger (and reduce the cost of TLB
                // invalidations from mprotect calls) by including these
                // pages in the ranges (the blank ones were never paged in):
                //
                // +-------+-------+-------+-------+-------+-------+-------+
                // | dirty |       | clean |       | clean |       | clean |
                // +-------+-------+-------+-------+-------+-------+-------+
                //
                // no coalesce:    <-- 1 -->       <-- 2 -->       <-- 3 -->
                // coalesce:       <------------------ 1 ------------------>
                //
                // By coalescing, we could reduce our 3 mprotect calls and
                // 3 mmap calls into one of each.

                int backing_end = j;

                int npages = backing_end - backing_start;
                char * start = mem + backing_start * page_size;
                size_t len   = npages * page_size;

                // 1.  Add this read-only region to the SIGSEGV handler's list
                // of active regions

                int region = register_segv_region(start, start + len);

                // Make sure that the segv region will be removed once we exit
                // this scope
                Call_Guard segv_unregister_guard
                    (boost::bind(unregister_segv_region, region));

                // 2.  Call mprotect() to switch to read-only
                int res = mprotect(start, len, PROT_READ);
                if (res == -1)
                    throw Exception(errno, "reback_range_after_write()",
                                    "mprotect() read-only before switch");

                // Make sure that an exception will cause the memory to be
                // un-protected.
                Call_Guard mprotect_guard(boost::bind(mprotect, start, len,
                                                      PROT_READ | PROT_WRITE));
                
                // 3.  Re-scan the page map as entries may have changed
                size_t num_changed = pm_current.update();

                // TODO: what about exceptions from here onwards?
                
                // 4.  Scan again and break into chunks
                
                int backing_start2 = -1;
                for (unsigned k = backing_start;  k <= backing_end;  ++k) {
                    bool need_backing2
                        =  k < backing_end
                        && needs_backing(current_pagemap[k], old_pagemap[k]);
                    
                    if (backing_start2 != -1 && !need_backing2) {
                        // We need to re-map the file behind backing_start2
                        // to k
                        int backing_end2 = k;
                        
                        int npages = backing_end2 - backing_start2;
                        char * start = mem + backing_start2 * page_size;
                        size_t len   = npages * page_size;

                        off_t foffset
                            = backing_file_offset + backing_start2 * page_size;
                        
                        void * addr = mmap(start, len,
                                           PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_FIXED,
                                           backing_file_fd, foffset);

                        if (addr != start)
                            throw Exception(errno, "reback_range_after_write()",
                                            "mmap backing file");

                        backing_start2 = -1;
                    }

                    if (need_backing2 && backing_start2 == -1)
                        backing_start2 = k;
                }
                
                // If no entries were changed when we updated the pagemaps
                // then we just unprotected all of the memory so we can
                // clear the cleanup
                if (num_changed == 0)
                    mprotect_guard.clear();

                backing_start = -1;
            }

            if (need_backing && backing_start == -1)
                backing_start = j;
        }
    }
}
