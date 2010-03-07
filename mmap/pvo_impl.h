/* pvo_impl.h                                                      -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__pvo_impl_h__
#define __jmvcc__pvo_impl_h__

#include "pvo.h"
#include "pvo_manager.h"

namespace JMVCC {

/*****************************************************************************/
/* PVO                                                                       */
/*****************************************************************************/

#if 0
inline
PVOStore *
PVO::
store() const
{
    return owner_->store();
}
#endif

} // namespace JMVCC

#endif /* __jmvcc__pvo_impl_h__ */

