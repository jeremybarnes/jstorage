/* pvo.cc
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   PVO implementation.
*/

#include "pvo.h"
#include "pvo_manager.h"


namespace JMVCC {


/*****************************************************************************/
/* PVO                                                                       */
/*****************************************************************************/

#if 0
PVO::
PVO(PVOManager * owner)
    : id_(owner->add(this)), owner_(owner)
{
}
#endif

PVOStore *
PVO::
store() const
{
    return owner_->store();
}

PVO *
PVO::
parent() const
{
    return owner_;
}

} // namespace JMVCC
