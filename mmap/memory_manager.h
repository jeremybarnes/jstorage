/* memory_manager.h                                                -*- C++ -*-
   Jeremy Barnes, 9 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Interface for a memory manager that can allocate over memory mapped
   regions.
*/

#ifndef __jgraph__memory_mananger_h__
#define __jgraph__memory_mananger_h__


namespace JMVCC {


/*****************************************************************************/
/* MEMORY_MANAGER                                                            */
/*****************************************************************************/

class MemoryManager {
public:
    virtual ~MemoryManager()
    {
    }

    virtual size_t to_offset(void * pointer) const = 0;

    virtual void * to_pointer(size_t offset) const = 0;

    virtual void * allocate_aligned(size_t nbytes, size_t alignment) = 0;

    virtual void deallocate(void * pointer, size_t bytes) = 0;
};


} // namespace JMVCC


#endif /* __jgraph__memory_mananger_h__ */
