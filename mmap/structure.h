/* structure.h                                                     -*- C++ -*-
   Jeremy Barnes, 22 August 2010
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Serialization for structures.
*/

#ifndef __jstorage__mmap__structure_h__
#define __jstorage__mmap__structure_h__


#include "bitwise_serializer.h"
#include "nothing.h"
#include <boost/tuple/tuple.hpp>
#include "jml/compiler/compiler.h"


namespace JMVCC {


/*****************************************************************************/
/* EXTRACTOR                                                                 */
/*****************************************************************************/

/** Helper class that builds an extractor to extract a given field from a
    given structure. */
template<typename StructureT, typename Type, Type StructureT::* Field,
         class SerializerT = CollectionSerializer<Type> >
struct Extractor {
    typedef SerializerT Serializer;
    typedef StructureT Structure;

    static const Type & extract(const StructureT & structure)
    {
        return structure .* Field;
    }

    template<typename T>
    static void insert(Structure & structure, const T & value)
    {
        (structure .* Field) = value;
    }
};

/** Extractor that does nothing for slots that aren't used */
struct NoExtractor {
    typedef CollectionSerializer<void, NullSerializer> Serializer;

    template<typename StructureT, typename T>
    static JML_PURE_FN JML_ALWAYS_INLINE void
    insert(StructureT & structure, const T & value)
    {
    }

    template<typename StructureT>
    static JML_PURE_FN JML_ALWAYS_INLINE int
    extract(const StructureT & structure)
    {
        return 0;
    }
};


/*****************************************************************************/
/* STRUCURE SERIALIZER                                                       */
/*****************************************************************************/

/** A template to build a serializer for a structure with up to 4 members.
    Each member is specified with an exractor, which gives the serialization
    type for each and extracts the entry from the structure.
*/
template<typename StructureT,
         class Extractor0,
         class Extractor1 = NoExtractor,
         class Extractor2 = NoExtractor,
         class Extractor3 = NoExtractor>
struct StructureSerializer {

    typedef StructureT Value;

    typedef typename Extractor0::Serializer Serializer0;
    typedef typename Extractor1::Serializer Serializer1;
    typedef typename Extractor2::Serializer Serializer2;
    typedef typename Extractor3::Serializer Serializer3;

    typedef typename Serializer0::WorkingMetadata WorkingMetadata0;
    typedef typename Serializer1::WorkingMetadata WorkingMetadata1;
    typedef typename Serializer2::WorkingMetadata WorkingMetadata2;
    typedef typename Serializer3::WorkingMetadata WorkingMetadata3;
    typedef typename Serializer0::ImmutableMetadata ImmutableMetadata0;
    typedef typename Serializer1::ImmutableMetadata ImmutableMetadata1;
    typedef typename Serializer2::ImmutableMetadata ImmutableMetadata2;
    typedef typename Serializer3::ImmutableMetadata ImmutableMetadata3;

    struct WorkingMetadata
        : public boost::tuple<WorkingMetadata0, WorkingMetadata1,
                              WorkingMetadata2, WorkingMetadata3> {
        unsigned chofs[4];
    };

    typedef boost::tuple<ImmutableMetadata0, ImmutableMetadata1,
                         ImmutableMetadata2, ImmutableMetadata3>
        ImmutableMetadata;

    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result;
        result.template get<0>() = Serializer0::new_metadata(length);
        result.template get<1>() = Serializer1::new_metadata(length);
        result.template get<2>() = Serializer2::new_metadata(length);
        result.template get<3>() = Serializer3::new_metadata(length);
        return result;
    }

    template<typename Value>
    static void prepare(const Value & value, WorkingMetadata & md, int index,
                        size_t length)
    {
        Serializer0::prepare(Extractor0::extract(value),
                             md.template get<0>(), index, length);
        md.chofs[0] = 0;
        Serializer1::prepare(Extractor1::extract(value),
                             md.template get<1>(), index, length);
        md.chofs[1]
            = Serializer0::words_for_children(md.template get<0>());
        Serializer2::prepare(Extractor2::extract(value),
                             md.template get<2>(), index, length);
        md.chofs[2]
            = md.chofs[1]
            + Serializer1::words_for_children(md.template get<1>());
        Serializer3::prepare(Extractor3::extract(value),
                             md.template get<3>(), index, length);
        md.chofs[3]
            = md.chofs[2]
            + Serializer2::words_for_children(md.template get<2>());
    }

    static size_t words_for_children(WorkingMetadata & md)
    {
        return md.chofs[3]
            + Serializer3::words_for_children(md.template get<3>());
    }

    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & md)
    {
        return 0
            + Serializer0::bits_per_entry(md.template get<0>())
            + Serializer1::bits_per_entry(md.template get<1>())
            + Serializer2::bits_per_entry(md.template get<2>())
            + Serializer3::bits_per_entry(md.template get<3>())
            + 0;
    }
    
    static Value
    reconstitute(BitReader & reader,
                 const long * & child_mem,
                 const ImmutableMetadata & md,
                 size_t length)
    {
        Value result;

        unsigned chofs0 = 0;
        ImmutableMetadata0 md0 = md.template get<0>();
        Extractor0::insert(result, Serializer0::
                           reconstitute(reader, child_mem + chofs0, md0,
                                        length));
        unsigned chofs1
            = chofs0 + Serializer0::words_for_children(md0);

        ImmutableMetadata1 md1 = md.template get<1>();
        Extractor1::insert(result, Serializer1::
                           reconstitute(reader, child_mem + chofs1, md1,
                                        length));
        unsigned chofs2
            = chofs1 + Serializer1::words_for_children(md1);

        ImmutableMetadata2 md2 = md.template get<2>();
        Extractor2::insert(result, Serializer2::
                           reconstitute(reader, child_mem + chofs2, md2,
                                        length));
        unsigned chofs3
            = chofs2 + Serializer2::words_for_children(md2);

        ImmutableMetadata3 md3 = md.template get<3>();
        Extractor3::insert(result, Serializer3::
                           reconstitute(reader, child_mem + chofs3, md3,
                                        length));

        return result;
    }

    template<typename ValueT>
    static void
    serialize(BitWriter & writer, long * child_mem, const ValueT & value,
              WorkingMetadata & md, ImmutableMetadata & imd,
              int object_num, size_t length)
    {
        Serializer0::serialize(writer, child_mem + md.chofs[0],
                               Extractor0::extract(value),
                               md.template get<0>(),
                               imd.template get<0>(),
                               object_num, length);
        Serializer1::serialize(writer, child_mem + md.chofs[1],
                               Extractor1::extract(value),
                               md.template get<1>(),
                               imd.template get<1>(),
                               object_num, length);
        Serializer2::serialize(writer, child_mem + md.chofs[2],
                               Extractor2::extract(value),
                               md.template get<2>(),
                               imd.template get<2>(),
                               object_num, length);
        Serializer3::serialize(writer, child_mem + md.chofs[3],
                               Extractor3::extract(value),
                               md.template get<3>(),
                               imd.template get<3>(),
                               object_num, length);
    }

    static void
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & md, ImmutableMetadata & imd,
                      size_t length)
    {
        Serializer0::finish_collection(mem, child_mem + md.chofs[0],
                                       md.template get<0>(),
                                       imd.template get<0>(),
                                       length);
        Serializer1::finish_collection(mem, child_mem + md.chofs[1],
                                       md.template get<1>(),
                                       imd.template get<1>(),
                                       length);
        Serializer2::finish_collection(mem, child_mem + md.chofs[2],
                                       md.template get<2>(),
                                       imd.template get<2>(),
                                       length);
        Serializer3::finish_collection(mem, child_mem + md.chofs[3],
                                       md.template get<3>(),
                                       imd.template get<3>(),
                                       length);
    }
};


/*****************************************************************************/
/* TUPLES                                                                    */
/*****************************************************************************/

template<typename Type, unsigned N,
         typename SerializerT = CollectionSerializer<Type> > 
struct TupleExtractor {

    typedef SerializerT Serializer;

    template<typename Tuple>
    static JML_ALWAYS_INLINE const Type & extract(const Tuple & tuple)
    {
        return tuple.template get<N>();
    }

    template<typename T, typename Tuple>
    static JML_ALWAYS_INLINE void insert(Tuple & tuple, const T & value)
    {
        tuple.template get<N>() = value;
    }
};

template<unsigned N,
         typename SerializerT> 
struct TupleExtractor<boost::tuples::null_type, N, SerializerT>
    : public NoExtractor {
};

template<typename T0, typename T1, typename T2, typename T3>
struct Serializer<boost::tuple<T0, T1, T2, T3> >
    : public StructureSerializer<boost::tuple<T0, T1, T2, T3>,
                                 TupleExtractor<T0, 0>,
                                 TupleExtractor<T1, 1>,
                                 TupleExtractor<T2, 2>,
                                 TupleExtractor<T3, 3> > {
};

} // namespace JMVCC

#endif /* __jstorage__mmap__structure_h__ */
