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

    void send_message(const char * data, size_t sz);

    std::string recv_message();

    void recv_message(char * data, size_t sz, bool all_at_once = true);

    template<typename X>
    void send(const X & x)
    {
        send_message((const char *)&x, sizeof(X));
    }

    void send(const std::string & str);

    template<typename X>
    void recv(X & x)
    {
        recv_message((char *)&x, sizeof(X));
    }

    void recv(std::string & str);

    template<typename X>
    X recv()
    {
        X result;
        recv(result);
        return result;
    }

    // Return the FD of the /proc/pid/pagemap for the snapshot process.
    int pagemap_fd() const;

    // What operation to perform on the file?
    enum Sync_Op {
        RECLAIM_ONLY,     ///< Replace private pages with disk-backed ones
        SYNC_ONLY,        ///< Dump modified (private) pages to disk
        SYNC_AND_RECLAIM, ///< Sync then reclaim
        DUMP              ///< Dump all pages to disk
    };

    // Go through the given memory range in the snapshot's address space
    // and perform an operation on each page that is found to be out-of-sync
    // (ie, modified) from the copy on disk.
    //
    // If op is SYNC_ONLY, then the pages that are out-of-sync will be written
    // to disk.  Note that nothing is modified that will make the pages look
    // in-sync, so calling twice with SYNC_ONLY will result in the pages being
    // written twice.
    //
    // If op is RECLAIM_ONLY, then it is assumed that a SYNC_ONLY operation
    // has already run and no page has been modified since.  Those pages that
    // are out-of-sync will have the corresponding disk page re-mapped onto
    // that address.  At the end, no pages will be out of sync.  The disk will
    // not be touched.  Any pages that were modified since the SYNC_ONLY
    // completed will be reverted to their on-disk versions.  The primary
    // advantage of this call is that it turns private pages (which must be
    // evicted to swap) into backed pages (that can be cheaply evicted), and
    // reduces the memory pressure on the machine.
    //
    // If op is RECLAIM_AND_SYNC, then the sync will happen, followed by the
    // reclaim.
    //
    // If op is DUMP, then all pages will be written to the disk file.  Note
    // that in this case, no reclamation takes place.  The file must have been
    // expanded (via truncate() or similar) to be large enough to accomodate
    // the data.  This is useful when creating a file for the first time.
    //
    // This function is not tolerant to simultaneous modification of the pages
    // on the snapshot, nor to simultaneous modification of the disk file from
    // any process.
    //
    // Note that the reclaim operations don't actually read the content of the
    // pages, so it is not important that they be in memory.
    size_t sync_to_disk(int fd,
                        size_t file_offset,
                        void * mem_start,
                        size_t mem_size,
                        Sync_Op op);
    
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

    // Client process for the sync_to_disk command
    void client_sync_to_disk();

    struct Itl;
    boost::scoped_ptr<Itl> itl;
};

} // namespace RS

#endif /* __storage__snapshot_h__ */
