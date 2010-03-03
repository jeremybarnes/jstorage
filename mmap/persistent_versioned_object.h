/* persistent_versioned_object.h                                   -*- C++ -*-
   Jeremy Barnes, 2 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent version of a versioned object.
*/

#ifndef __jmvcc__persistent_versioned_object_h__
#define __jmvcc__persistent_versioned_object_h__

#include "jmvcc/versioned2.h"

namespace JMVCC {



// Defines some memory somewhere (on or off the heap)
struct Memory_Space {

};


// Basic methods performed on a persistent object
struct PersistentObject {
    virtual void init(const char * mem);
    virtual const char * serialize(const Memory_Space & space,
                                   const char * close_to) const = 0;
    
};


} // namespace JMVCC

#endif /* __jmvcc__persistent_versioned_object_h__ */
