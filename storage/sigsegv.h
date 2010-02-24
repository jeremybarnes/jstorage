/* sigsegv.h                                                       -*- C++ -*-
   Jeremy Barnes, 24 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Handler for segfaults.
*/

#ifndef __jmvcc__storage__sigsegv_h__
#define __jmvcc__storage__sigsegv_h__

#include <vector>

namespace RS {

int register_segv_region(const void * start, const void * end);
void unregister_segv_region(int region);
void install_segv_handler();
size_t get_num_segv_faults_handled();


} // namespace RS

#endif /* __jmvcc__storage__sigsegv_h__ */

   
