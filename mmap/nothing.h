/* nothing.h                                                       -*- C++ -*-
   Jeremy Barnes, 23 August 2010
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Structure that does... nothing.  Used instead of void as it is possible
   to instantiate this one.
*/

#ifndef __jstorage__mmap__nothing_h__
#define __jstorage__mmap__nothing_h__

#include "bitwise_serializer.h"

namespace JMVCC {

/*****************************************************************************/
/* NULL SERIALIZER                                                           */
/*****************************************************************************/

/** Helper class that contains... nothing.  Used instead of void as void
    can't be passed around as a value. */
struct Nothing {
};

struct NullSerializer {
    typedef Nothing WorkingMetadata;
    typedef Nothing ImmutableMetadata;

    static JML_PURE_FN JML_ALWAYS_INLINE
    Nothing new_metadata(unsigned length)
    {
        return WorkingMetadata();
    }

    static JML_PURE_FN JML_ALWAYS_INLINE
    size_t words_for_children(WorkingMetadata)
    {
        return 0;
    }

    template<typename Value>
    static JML_PURE_FN JML_ALWAYS_INLINE
    void prepare(Value value, WorkingMetadata & metadata,
                 int item_number, size_t length)
    {
    }

    static void finish_prepare(WorkingMetadata & md, size_t length)
    {
    }

    template<typename T>
    static JML_PURE_FN JML_ALWAYS_INLINE
    void serialize(BitWriter & writer, long * child_mem, const T & value,
                   WorkingMetadata, ImmutableMetadata, int, size_t)
    {
    }

    static JML_PURE_FN JML_ALWAYS_INLINE
    Nothing reconstitute(BitReader & reader,
                         const long * base,
                         ImmutableMetadata metadata,
                         size_t length)
    {
        return Nothing();
    }

    static JML_PURE_FN JML_ALWAYS_INLINE
    size_t bits_per_entry(Nothing)
    {
        return 0;
    }

    static void
    JML_PURE_FN JML_ALWAYS_INLINE
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & md, ImmutableMetadata & imd,
                      size_t length)
    {
    }
};

template<>
struct Serializer<Nothing> : public NullSerializer {
};

} // namespace JMVCC

#endif /* __jstorage__mmap__nothing_h__ */
