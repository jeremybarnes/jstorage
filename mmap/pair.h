/* pair.h                                                          -*- C++ -*-
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Serialization for a pair.
*/

#ifndef __jstorage__jmvcc__pair_h__
#define __jstorage__jmvcc__pair_h__

#include "bitwise_serializer.h"
#include <utility>

namespace JMVCC {


/*****************************************************************************/
/* PAIRSERIALIZER                                                            */
/*****************************************************************************/

template<typename T1, typename T2,
         class Serializer1T = CollectionSerializer<T1>,
         class Serializer2T = CollectionSerializer<T2> >
struct PairSerializer {

    typedef Serializer1T Serializer1;
    typedef Serializer2T Serializer2;

    typedef typename Serializer1T::WorkingMetadata WorkingMetadata1;
    typedef typename Serializer2T::WorkingMetadata WorkingMetadata2;
    typedef typename Serializer1T::ImmutableMetadata ImmutableMetadata1;
    typedef typename Serializer2T::ImmutableMetadata ImmutableMetadata2;

    typedef std::pair<WorkingMetadata1, WorkingMetadata2>
    WorkingMetadata;
    typedef std::pair<ImmutableMetadata1, ImmutableMetadata2>
    ImmutableMetadata;

    static WorkingMetadata new_metadata(size_t length)
    {
        return WorkingMetadata(Serializer1::new_metadata(length),
                               Serializer2::new_metadata(length));
    }

    template<typename Value>
    static void prepare(const Value & value, WorkingMetadata & md, int index)
    {
        Serializer1::prepare(value.first, md.first, index);
        Serializer2::prepare(value.second, md.second, index);
    }

    static size_t words_for_children(WorkingMetadata & md)
    {
        size_t words1 = Serializer1::words_for_children(md.first);
        size_t words2 = Serializer2::words_for_children(md.second);
        return words1 + words2;
    }

    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & md)
    {
        return md.first + md.second;
    }
    
    static std::pair<T1, T2>
    reconstitute(const long * mem,
                 BitReader & reader,
                 const ImmutableMetadata & md)
    {
        T1 res1 = Serializer1::reconstitute(mem, reader, md.first);
        T2 res2 = Serializer1::reconstitute(mem, reader, md.first);
        return std::make_pair(res1, res2);
    }

    template<typename ValueT>
    static void
    serialize(long * mem, BitWriter & writer, const ValueT & value,
              WorkingMetadata & md, ImmutableMetadata & imd,
              int object_num)
    {
        Serializer1::serialize(mem, writer, value.first, md.first, imd.first,
                               object_num);
        Serializer2::serialize(mem, writer, value.second, md.second,
                               imd.second, object_num);
    }

    static void
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
        Serializer1::finish_collection(md.first, imd.first);
        Serializer2::finish_collection(md.second, imd.second);
    }
    
};

/** Default serializer for pairs is PairSerializer */
template<typename T1, typename T2>
struct Serializer<std::pair<T1, T2> >
    : public PairSerializer<T1, T2> {
};

} // namespace JMVCC


#endif /* __jstorage__jmvcc__pair_h__ */

