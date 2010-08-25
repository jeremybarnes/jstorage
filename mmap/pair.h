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
    static void prepare(const Value & value, WorkingMetadata & md, int index,
                        size_t length)
    {
        Serializer1::prepare(value.first, md.first, index, length);
        Serializer2::prepare(value.second, md.second, index, length);
    }

    static void finish_prepare(WorkingMetadata & md, size_t length)
    {
        Serializer1::finish_prepare(md.first, length);
        Serializer2::finish_prepare(md.second, length);
    }

    static size_t words_for_children(const WorkingMetadata & md)
    {
        size_t words1 = Serializer1::words_for_children(md.first);
        size_t words2 = Serializer2::words_for_children(md.second);
        return words1 + words2;
    }

    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & md)
    {
        return Serializer1::bits_per_entry(md.first)
            +  Serializer2::bits_per_entry(md.second);
    }
    
    static std::pair<T1, T2>
    reconstitute(BitReader & reader,
                 const long * child_mem,
                 const ImmutableMetadata & md,
                 size_t length)
    {
        T1 res1 = Serializer1::reconstitute(reader, child_mem, md.first,
                                            length);
        
        size_t offset = Serializer1::words_for_children(md.first);

        T2 res2 = Serializer2::reconstitute(reader, child_mem + offset,
                                            md.second, length);
        return std::make_pair(res1, res2);
    }

    template<typename ValueT>
    static void
    serialize(BitWriter & writer, long * child_mem, const ValueT & value,
              WorkingMetadata & md, ImmutableMetadata & imd,
              int object_num, size_t length)
    {
        Serializer1::serialize(writer, child_mem,
                               value.first, md.first, imd.first,
                               object_num, length);
        size_t offset = Serializer1::words_for_children(md.first);
        Serializer2::serialize(writer, child_mem + offset,
                               value.second, md.second,
                               imd.second, object_num, length);
    }

    static void
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & md, ImmutableMetadata & imd,
                      size_t length)
    {
        Serializer1::finish_collection(mem, child_mem, md.first, imd.first,
                                       length);
        size_t offset = Serializer1::words_for_children(md.first);
        Serializer2::finish_collection(mem, child_mem + offset,
                                       md.second, imd.second,
                                       length);
    }
    
};

/** Default serializer for pairs is PairSerializer */
template<typename T1, typename T2>
struct Serializer<std::pair<T1, T2> >
    : public PairSerializer<T1, T2> {
};

} // namespace JMVCC


#endif /* __jstorage__jmvcc__pair_h__ */

