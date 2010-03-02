/* mmap_storage.h                                                  -*- C++ -*-
   Jeremy Barnes, 1 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Storage manager for memory mapped objects.
*/

#ifndef __storage__mmap_storage_h__
#define __storage__mmap_storage_h__

#include <string>

namespace JGraph {

struct MMap_Storage {
    MMap_Storage(const std::string & backing_file)
    {
    }

    // Clear all elements
    void clear()
    {
    }
};

} // namespace JGraph

#endif /* __storage__mmap_storage_h__ */
