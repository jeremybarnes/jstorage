/* storage.h                                                       -*- C++ -*-
   Jeremy Barnes, 22 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Storage manager dealing with allocation of memory-mapped objects.

*/

#ifndef __platform__storage_h__
#define __platform__storage_h__

namespace RS {



/*****************************************************************************/
/* CHUNK_MANAGER                                                             */
/*****************************************************************************/

/** An object that manages a single, contiguous array of memory that can be
    expanded as required.  It keeps a backing copy in a file, and can be
    atomically snapshotted at any moment.
*/
struct Chunk_Manager {

    Chunk_Manager();
    ~Chunk_Manager();

    void init(const std::string & filename);

    void create(const std::string & filename, size_t initial_size);

    // Expand the chunk to a new size
    void grow(size_t new_size);

    // Create a snapshot of the current state of the memory.  Before we do
    // this, we need to make sure that nothing is writing, and that all
    // threads that were writing have executed a memory fence.
    //
    // Concurrent reads and writes can continue to happen as soon as this
    // call returns.
    //
    // Returns a handle that can be used to describe the snapshot.
    size_t snapshot();

    // Wait for the snapshot to finish, and return its exit code.
    int wait_for_snapshot(size_t snapshot_id);

    // Poll to see if a snapshot has finished.  Returns 0 if it is still in
    // progress, or its exit code if it has finished
    int snapshot_finished(size_t snapshot_id);

    // The base address at which the memory is mapped.
    void * base() const;

    // The size of the mapping.
    size_t size() const;

private:
    class Itl;
    boost::scoped_ptr<Itl> itl;
};


struct Storage_Manager {

    Storage_Manager(const std::string & filename);

    size_t malloc(size_t bytes);
    void free(size_t where, size_t bytes);
    size_t realloc(size_t where, size_t old_bytes, size_t new_bytes);

    
    void snapshot();
    
    
};

} // namespace RS

#endif /* __platform__storage_h__ */

