/* snapshot.h                                                      -*- C++ -*-
   Jeremy Barnes, 22 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Class to create and manipulate snapshots of a process's virtual memory
   space.

   Used to allow the following features with memory-mapped (file backed)
   data structures:
   - Hot snapshotting
   - Hot replication
   - Journaling and fault tolerance
*/

#ifndef __storage__snapshot_h__
#define __storage__snapshot_h__


#include <boost/scoped_ptr.hpp>
#include <boost/function.hpp>
#include <string>


namespace RS {


/*****************************************************************************/
/* SNAPSHOT                                                                  */
/*****************************************************************************/

/** This is a structure that controls a snapshot of the memory at a given
    time.  Underneath, it's a separate process that was forked with copy-on-
    write mode on to capture that status of the virtual memory at that point
    in time.  A pipe is maintained with the other process in order to allow
    it to be controlled
*/

struct Snapshot {

    // The function that will be run in the snapshot.  Takes as an argument
    // a single int which is the control_fd for it to work on.  Returns a
    // single int which will be returned to the parent once it exits.
    typedef boost::function<int (int)> Worker;

    // Create a snapshot of the current process
    Snapshot(Worker worker = Worker());
    
    // Terminate the snapshot of the current process
    ~Snapshot();

    int control_fd() const;

    // Return a snapshot object that represents the current process, but
    // operates in a different thread.
    static Snapshot & current();

    struct Remote_File {
        const std::string & filename;
        const Snapshot * owner;
        int fd;
    };

    // Create the given remote snapshot
    Remote_File create_file(const std::string & filename,
                            size_t size);

    // Close the remote file
    void close_file(const Remote_File & file);

    // Dump the given range of (remote) virtual memory into the given file.
    // Returns the number of pages written.
    size_t dump_memory(int fd,
                     size_t file_offset,
                     void * mem_start,
                     size_t mem_size);
    
    // Dump the pages of memory that have changed between the other snapshot
    // and this snapshot to the given file.  Used to update a snapshot file
    // by writing out only the pages that have been modified.
    //
    // NOTE: we have *3* processes involved here:
    // - The current process (not represented by any snapshot);
    // - The new snapshot (represented by the Snapshot object that we are
    //   running this call on;
    // - The old snapshot (represented by the Snapshot object old_snapshot).
    //
    // TODO: finish this... the goal is to detect the pages of the current
    // process that are identical to the disk pages just written, and to
    // make the VMA point to the disk area rather than pointing to our COW
    // copy of the page.  Not all pages will be like that as any which were
    // written to after the snapshot will be different.  If we don't do that,
    // then every page which was ever changed since the process started will
    // be stuck in memory forever.  The challenge is to do it in an atomic
    // manner.  This could be accomplished by:
    // 1.  Marking the page read-only with mprotect(), so that any new writes
    //     to it will cause a GPF
    // 2.  Doing the swap, moving it from read-only to read-write in the
    //     process.
    // 3.  Handling the GPF (they should be rare) by waiting for the page to
    //     come in and then retrying the write
    //
    // In addition to doing all of that, the function will look at the
    // CURRENT process's pages.  Any which have not changed between the new
    // snapshot and the current process are 
    void sync_memory(const Remote_File & file,
                     size_t file_offset,
                     void * mem_start,
                     void * mem_size,
                     const Snapshot & old_snapshot,
                     void * mem_start_old,
                     size_t mem_size_old);

    // Terminate the snapshot; the process will die and the connection will
    // be lost.  Asynchronous.  Returns the return code of the child function.
    int terminate();

    // Disassociate ourselves with the snapshot.  Used after a fork by the
    // child process so that the child process exiting doesn't cause problems
    // for the parent process.
    void disassociate();

private:
    // Run the default function for the child process
    int run_child(int control_fd);

    struct Itl;
    boost::scoped_ptr<Itl> itl;
};

} // namespace RS

#endif /* __storage__snapshot_h__ */
