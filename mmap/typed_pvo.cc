/* typed_pvo.cc
   Jeremy Barnes, 11 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Typed Persistent Versioned Object.
*/

#include "typed_pvo.h"
#include "pvo_store.h"

namespace JMVCC {

PVOStore * to_store(PVOManager * owner)
{
    return owner->store();
}

void * to_pointer(PVOStore * store, size_t offset)
{
    return store->to_pointer(offset);
}

void mutate_owner(PVOManager * owner)
{
    owner->mutate();
}

} // namespace JMVCC
