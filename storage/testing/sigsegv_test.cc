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

struct Pagemap_Reader {
    // Set up and read the pagemap file for the given memory region.  If
    // entries is passed in, that will be used as the temporary buffer.
    Pagemap_Reader(int fd, const char * mem, size_t npages,
                   Pagemap_Entry * entries = 0);

    // Re-read the entries for the given address range in case they have
    // changed.  Returns the number that have changed.
    size_t update(const char * addr, size_t npages)
    {
    }

    // Return the entry for the given address
    const Pagemap_Entry & operator [] (const char * mem) const
    {
        size_t page_num = (mem - this->mem) / page_size;
    }

    // Return the entry given an index from zero to npages
    const Pagemap_Entry & operator [] (size_t page_index) const
    {
        if (page_index > npages)
            throw Exception("Pagemap_Reader::operator [](): bad page index");
        return entries[page_index];
    }

private:
    int fd;
    const char * mem;
    size_t npages;
    Pagemap_Entry * entries;
    bool delete_entries;
};

void check_and_reback(char * mem, int start, int end)
{
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

    uint64_t page_num = (size_t)memory / page_size;

    int res = lseek(old_pagemap_file, page_size * page_num, SEEK_SET);
    if (res == -1)
        throw Exception(errno, "reback_range_after_write()",
                        "lseek on old_pagemap_file");
    
    res = lseek(current_pagemap_file, page_size * page_num, SEEK_SET);
    if (res == -1)
        throw Exception(errno, "reback_range_after_write()",
                        "lseek on current_pagemap_file");

    char * mem = (char *)memory;

    for (unsigned i = 0;  i < npages;  i += CHUNK, mem += CHUNK * page_size) {
        int todo = std::min(npages - i, CHUNK);
        
        // Get the pagemap info for the range in the old process
        res = read(old_pagemap_file, old_pagemap, todo * sizeof(Pagemap_Entry));
        if (res == -1)
            throw Exception(errno, "reback_range_after_write()",
                            "read on old_pagemap_file");
        
        // Get the pagemap info for the range in the new process
        res = read(current_pagemap_file, current_pagemap,
                   todo * sizeof(Pagemap_Entry));
        if (res == -1)
            throw Exception(errno, "reback_range_after_write()",
                            "read on old_pagemap_file");
        
        // The value of j at which we start backing pages
        int backing_start = -1;
        
        // Now go through and back those pages needed
        for (unsigned j = 0;  j <= todo;  ++j) {  /* <= for last page */
            bool need_backing
                =  j < todo
                && current_pagemap[j].present
                && old_pagemap[j].present
                && current_pagemap.swapped == old_pagemap.swapped
                && current_pagemap[j].pfn == old_pagemap[j].pfn;

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

                int backing_end = j - 1;

                int npages = j - 1;
                char * start = mem + backing_start * page_size;
                size_t len   = npages * page_size;

                

                // 1.  Add this read-only region to the SIGSEGV handler's list
                // of active regions

                register_segv_region(start, len);

                // 2.  Call mprotect() to switch to read-only
                int res = mprotect(start, len, PROT_READ);
                if (res == -1)
                    throw Exception(errno, "reback_range_after_write()",
                                    "mprotect() read-only before switch");

                uint64_t page_num = (size_t)start / page_size;

                // 3.  Update the pagemap entries for the current process
                res = lseek(current_pagemap_file, page_num * sizeof(Pagemap_Entry), SEEK_SET);
                if (res == -1)
                    throw Exception(errno, "reback_range_after_write()",
                                    "lseek on current_pagemap_file after read");
                

                // TODO: what about exceptions from here onwards?
                
                // 3.  Scan again through our pages, since some of them might
                //     have changed since we last scanned.
                
                int backing_start2 = -1;
                for (unsigned k = backing_start;  k <= backing_end;  ++k) {
                    bool need_backing2
                        =  j < backing_end
                        && current_pagemap[k].present
                        && old_pagemap[k].present
                        && current_pagemap.swapped == old_pagemap.swapped
                        && current_pagemap[k].pfn == old_pagemap[k].pfn;
                    
            }
        }
    }

    
}
