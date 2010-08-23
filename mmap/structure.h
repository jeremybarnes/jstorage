/* structure.h                                                     -*- C++ -*-
   Jeremy Barnes, 22 August 2010
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Serialization for structures.
*/

#ifndef __jstorage__mmap__structure_h__
#define __jstorage__mmap__structure_h__


#include "bitwise_serializer.h"
#include <boost/tuple/tuple.hpp>
#include "jml/compiler/compiler.h"


namespace JMVCC {


/*****************************************************************************/
/* STRUCTURE                                                                 */
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
                        int item_number)
    {
    }

    template<typename T>
    static JML_PURE_FN JML_ALWAYS_INLINE
    void serialize(long * mem, BitWriter & writer, const T & value,
                   WorkingMetadata, ImmutableMetadata, int)
    {
    }

    static JML_PURE_FN JML_ALWAYS_INLINE
    Nothing reconstitute(const long * base,
                         BitReader & reader,
                         ImmutableMetadata metadata)
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
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
    }
};

template<>
struct Serializer<Nothing> : public NullSerializer {
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

    typedef boost::tuple<WorkingMetadata0, WorkingMetadata1,
                         WorkingMetadata2, WorkingMetadata3>
        WorkingMetadata;

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
    static void prepare(const Value & value, WorkingMetadata & md, int index)
    {
        Serializer0::prepare(Extractor0::extract(value),
                             md.template get<0>(), index);
        Serializer1::prepare(Extractor1::extract(value),
                             md.template get<1>(), index);
        Serializer2::prepare(Extractor2::extract(value),
                             md.template get<2>(), index);
        Serializer3::prepare(Extractor3::extract(value),
                             md.template get<3>(), index);
    }

    static size_t words_for_children(WorkingMetadata & md)
    {
        return 0
            + Serializer0::words_for_children(md.template get<0>())
            + Serializer1::words_for_children(md.template get<1>())
            + Serializer2::words_for_children(md.template get<2>())
            + Serializer3::words_for_children(md.template get<3>())
            + 0;
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
    reconstitute(const long * mem,
                 BitReader & reader,
                 const ImmutableMetadata & md)
    {
        Value result;
        Extractor0::insert(result, Serializer0::
                           reconstitute(mem, reader, md.template get<0>()));
        Extractor1::insert(result, Serializer1::
                           reconstitute(mem, reader, md.template get<1>()));
        Extractor2::insert(result, Serializer2::
                           reconstitute(mem, reader, md.template get<2>()));
        Extractor3::insert(result, Serializer3::
                           reconstitute(mem, reader, md.template get<3>()));
        return result;
    }

    template<typename ValueT>
    static void
    serialize(long * mem, BitWriter & writer, const ValueT & value,
              WorkingMetadata & md, ImmutableMetadata & imd,
              int object_num)
    {
        Serializer0::serialize(mem, writer, Extractor0::extract(value),
                               md.template get<0>(), imd.template get<0>(),
                               object_num);
        Serializer1::serialize(mem, writer, Extractor1::extract(value),
                               md.template get<1>(), imd.template get<1>(),
                               object_num);
        Serializer2::serialize(mem, writer, Extractor2::extract(value),
                               md.template get<2>(), imd.template get<2>(),
                               object_num);
        Serializer3::serialize(mem, writer, Extractor3::extract(value),
                               md.template get<3>(), imd.template get<3>(),
                               object_num);
    }

    static void
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
        Serializer0::finish_collection(md.template get<0>(),
                                       imd.template get<0>());
        Serializer1::finish_collection(md.template get<1>(),
                                       imd.template get<1>());
        Serializer2::finish_collection(md.template get<2>(),
                                       imd.template get<2>());
        Serializer3::finish_collection(md.template get<3>(),
                                       imd.template get<3>());
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
