/* pvo_store.h                                                     -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__pvo_store_h__
#define __jmvcc__pvo_store_h__

#include "pvo_manager.h"
#include "memory_manager.h"
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/creation_tags.hpp>


namespace JMVCC {


/*****************************************************************************/
/* PVO_STORE                                                                 */
/*****************************************************************************/

/** This is the basic class used for persistent storage.  It provides the
    following functionality:

    1.  Maintains the file-backed memory-mapped region(s) where objects are
        eventually serialized;
    2.  Maintains the housekeeping data structures on the memory mapped
        regions
    3.  Keeps track of the allocated and free memory in the region
*/

struct PVOStore : public PVOManager, public MemoryManager, boost::noncopyable {
    
public:
    // Create a new persistent object store
    PVOStore(const boost::interprocess::create_only_t & creation,
             const std::string & filename,
             size_t size);

    // Open an existing one
    PVOStore(const boost::interprocess::open_only_t & creation,
             const std::string & filename);

    virtual ~PVOStore();

    virtual size_t to_offset(void * pointer) const;

    virtual void * allocate_aligned(size_t nbytes, size_t alignment);

    virtual void deallocate(size_t offset, size_t bytes);

private:
    class Itl;
    boost::scoped_ptr<Itl> itl;
};


} // namespace JMVCC

#endif /* __jmvcc__pvo_store_h__ */

