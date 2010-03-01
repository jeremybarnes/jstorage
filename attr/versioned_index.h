/* versioned_index.h                                               -*- C++ -*-
   Jeremy Barnes, 1 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   A versioned index class.
*/

#ifndef __jgraph__attr__versioned_index_h__
#define __jgraph__attr__versioned_index_h__

namespace JGraph {

/*****************************************************************************/
/* VERSIONED_INDEX                                                           */
/*****************************************************************************/

/** This is a versioned index class.  They key and value are always integers.
    It's a sorted index, that allows multiple versions to be stored via
    the VersionedObject interface.  Thread safe with no read locks; all
    commits need to be done in transactions.

    The keys and values are 64 bit integers, but will be compressed as much
    as possible.

    The base data structure is a list of (key, value) arrays, sorted by
    key.  This is supplemented by a list of patch records, each one
    associated to a different version of the data structure.  Each patch
    replaces zero or more records in the main list with zero or more
    records from somewhere else in memory.  If the list of patches gets too
    complicated, then they are re-written.
*/

struct Versioned_Index {
};

} // namespace JGraph

#endif /* __jgraph__attr__versioned_index_h__ */
