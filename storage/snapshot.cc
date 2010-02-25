/* snapshot.cc
   Jeremy Barnes, 22 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#include "snapshot.h"
#include "sigsegv.h"
#include <boost/bind.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "jml/arch/exception.h"
#include "jml/utils/string_functions.h"
#include <iostream>
#include "jml/arch/vm.h"
#include "jml/utils/guard.h"


using namespace std;
using namespace ML;


namespace RS {


/*****************************************************************************/
/* SNAPSHOT                                                                  */
/*****************************************************************************/

struct Snapshot::Itl {
    Itl()
        : pid(-1), control_fd(-1), snapshot_pm_fd(-1)
    {
    }

    pid_t pid;
    int control_fd;
    int snapshot_pm_fd;
};

Snapshot::
Snapshot(Worker worker)
    : itl(new Itl())
{
    if (!worker)
        worker = boost::bind(&Snapshot::run_child, this, _1);
    
    // Create the socket pair to communicate

    int sockets[2];
    int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    if (res == -1) {
    }

    pid_t pid = fork();

    if (pid == -1)
        throw Exception("error in fork");

    else if (pid == 0) {
        /* We are the child.  Do cleaning up here */
        // ...

        close(sockets[1]);

        int return_code = -1;
        try {
            return_code = worker(sockets[0]);
        }
        catch (const std::exception & exc) {
            cerr << "child exiting with exception " << exc.what() << endl;
        }
        catch (...) {
            cerr << "child exiting with unknown exception " << endl;
        }

        // Now die.  We do it this way to avoid any destructors running, etc
        // which might do things that we don't want them to and interfere with
        // the parent process.
        _exit(return_code);
    }

    close(sockets[0]);

    itl->pid = pid;
    itl->control_fd = sockets[1];

    int pm_fd = open(format("/proc/%d/pagemap", pid).c_str(), O_RDONLY);
    if (pm_fd == -1)
        throw Exception("open pagemap; " + string(strerror(errno)));

    itl->snapshot_pm_fd = pm_fd;
}

Snapshot::
~Snapshot()
{
    if (!itl->pid)
        return;

    int return_code = terminate();

    if (return_code != 0)
        cerr << "warning: snapshot termination returned " << return_code
             << endl;
}

int
Snapshot::
pagemap_fd() const
{
    return itl->snapshot_pm_fd;
}

int
Snapshot::
control_fd() const
{
    return itl->control_fd;
}

void
Snapshot::
send_message(const char * data, size_t sz)
{
    int res = write(itl->control_fd, data, sz);
    if (res != sz)
        throw Exception("write: " + string(strerror(errno)));
}

std::string
Snapshot::
recv_message()
{
    char buffer[65536];
    int found = read(itl->control_fd, buffer, 65536);
    if (found == -1)
        throw Exception("read: " + string(strerror(errno)));
    if (found == 65536)
        throw Exception("read: message was too long");

    string result(buffer, buffer + found);
    return result;
}

void
Snapshot::
recv_message(char * data, size_t sz, bool all_at_once)
{
    //cerr << getpid() << " recv_message of " << sz << " bytes" << endl;
    int found = read(itl->control_fd, data, sz);
    if (found == -1)
        throw Exception("read: " + string(strerror(errno)));
    if (found != sz) {
        cerr << "wanted: " << sz << endl;
        cerr << "found:  " << found << endl;
        throw Exception("read: message was not long enough");
    }
}

void
Snapshot::
send(const std::string & str)
{
    if (str.length() > 65536)
        throw Exception("string too long to send");
    send<int>(str.length());
    send_message(str.c_str(), str.length());
}

void
Snapshot::
recv(std::string & str)
{
    int sz = recv<int>();
    if (sz < 0 || sz > 65536)
        throw Exception("string too long to receive");
    str.resize(sz);
    recv_message(&str[0], sz);
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

size_t reback_range_after_write(void * memory, size_t length,
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

    size_t result = 0;

    for (unsigned i = 0;  i < npages;  i += CHUNK, mem += CHUNK * page_size) {
        int todo = std::min(npages - i, CHUNK);

        Pagemap_Reader pm_old(mem, todo * page_size, old_pagemap,
                              old_pagemap_file);
        Pagemap_Reader pm_current(mem, todo * page_size, current_pagemap,
                                  current_pagemap_file);

        cerr << "pm_old = " << endl << pm_old << endl;
        cerr << "pm_current = " << endl << pm_current << endl;
        
        // The value of j at which we start backing pages
        int backing_start = -1;
        
        // Now go through and back those pages needed
        for (unsigned j = 0;  j <= todo;  ++j) {  /* <= for last page */
            bool need_backing
                =  j < todo && needs_backing(current_pagemap[j], old_pagemap[j]);
            
            cerr << "j = " << j << " need_backing = " << need_backing
                 << endl;

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

                cerr << "need to re-back " << npages << " pages from "
                     << backing_start << " to " << backing_end << endl;

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
                    
                    result += need_backing2;

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

    return result;
}

size_t
Snapshot::
sync_to_disk(int fd,
             size_t file_offset,
             void * mem_start,
             size_t mem_size,
             Sync_Op op)
{
    if (op != RECLAIM_ONLY
        && op != SYNC_ONLY
        && op != SYNC_AND_RECLAIM
        && op != DUMP)
        throw Exception("sync_to_disk(): invalid op");
    // TODO: copy and pasted

    if (file_offset % page_size != 0)
        throw Exception("file offset not on a page boundary");

    size_t mem = (size_t)mem_start;
    if (mem % page_size != 0)
        throw Exception("mem_start not on a page boundary");

    if (mem_size % page_size != 0)
        throw Exception("mem_size not a multiple of page_size");

    off_t res = lseek(fd, file_offset, SEEK_SET);

    if (res != file_offset)
        throw Exception("lseek failed: " + string(strerror(errno)));

    send('s');

    send(fd);
    send(file_offset);
    send(mem_start);
    send(mem_size);
    send(op);

    ssize_t result = recv<size_t>();
    
    if (result == -1) {
        string error = recv_message();
        
        throw Exception("sync_to_disk(): snapshot process returned error: "
                        + error);
    }
        
    return result;
}

void
Snapshot::
client_sync_to_disk()
{
    try {
        int          fd          = recv<int>();
        size_t       file_offset = recv<size_t>();
        const char * mem_start   = recv<char *>();
        size_t       mem_size    = recv<size_t>();
        Sync_Op      op          = recv<Sync_Op>();

        cerr << "memory:" << endl;
        dump_page_info(mem_start, mem_size);

        // Now go and do it
        size_t result = 0;
        
        size_t npages = mem_size / page_size;
        
        vector<unsigned char> flags;
        if (op != DUMP)
            flags = page_flags(mem_start, npages);

        // TODO: chunk at a time to avoid too many TLB invalidations

        off_t wanted_ofs = file_offset;
        for (unsigned i = 0;  i < npages;
             ++i, mem_start += page_size, wanted_ofs += page_size) {
            // Look at what the VM subsystem says:
            // - If the page isn't present or in swapped, then it has never
            //   been touched and what's already on the disk is the only
            //   possibility;
            
            // Can we skip this page?
            if (op != DUMP && !flags[i]) continue;

            // Do we need to write it to disk?
            int res = 0;
            if (op == SYNC_ONLY || op == SYNC_AND_RECLAIM || op == DUMP) {
                res = pwrite(fd, mem_start, page_size, wanted_ofs);
                
                if (res != page_size)
                    throw Exception("write != page_size");
            }

            // Do we need to re-map the disk page on our process?
            if (op == RECLAIM_ONLY || op == SYNC_AND_RECLAIM) {
                void * addr = mmap((void *)mem_start, page_size,
                                   PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_FIXED, fd,
                                   wanted_ofs);

                if (addr != mem_start)
                    throw Exception("mmap failed: "  + string(strerror(errno)));

                if (op == RECLAIM_ONLY) res = page_size;
            }
            
            result += res;
        }

        cerr << "memory after:" << endl;
        dump_page_info(mem_start - npages * page_size, mem_size);


        send(result);
    }
    catch (const std::exception & exc) {
        ssize_t result = -1;
        send(result);
        std::string message = exc.what();
        send(message);
    }
    catch (...) {
        ssize_t result = -1;
        send(result);
        std::string message = "unknown exception caught";
        send(message);
    }
}

int
Snapshot::
terminate()
{
    if (!itl->pid)
        throw Exception("Snapshot::terminate(): already terminated");

    int res = close(itl->control_fd);
    if (res == -1)
        cerr << "warning: Snapshot::terminate(): close returned "
             << strerror(errno) << endl;

    res = close(itl->snapshot_pm_fd);
    if (res == -1)
        cerr << "warning: Snapshot::terminate(): close returned "
             << strerror(errno) << endl;

    int status = -1;
    res = waitpid(itl->pid, &status, 0);

    //cerr << "Snapshot::terminate(): res = " << res
    //     << " status = " << status << endl;

    if (res != itl->pid) {
        cerr << "warning: Snapshot::terminate(): waitpid returned pid "
             << res << " status " << status << endl;
    }
    
    itl->pid = 0;

    return status;
}

void
Snapshot::
disassociate()
{
    throw Exception("not done");
}

int
Snapshot::
run_child(int control_fd)
{
    itl->control_fd = control_fd;

    while (true) {
        char c;
        int res = read(control_fd, &c, 1);
        if (res == 0)
            return 0;

        if (res != 1) {
            cerr << "Snapshot: child read returned " << strerror(errno)
                 << endl;
            return -1;
        }

        if (c == 's')
            client_sync_to_disk();
        else {
            cerr << "Snapshot: child got unknown command "
                 << c << endl;
            return -1;
        }
    }
}


} // namespace RS
