/* bitwise_serializer.h                                            -*- C++ -*-
   Jeremy Barnes, 19 August 2010
   Copyright (c) 2010 Recoset Inc.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#ifndef __jstorage__bitwise_serializer_h__
#define __jstorage__bitwise_serializer_h__


#include "jml/arch/bit_range_ops.h"
#include "jml/arch/bitops.h"
#include "jml/arch/format.h"
#include "jml/arch/exception.h"
#include "jml/compiler/compiler.h"

#include "bitwise_memory_manager.h"

namespace JMVCC {

template<typename T> struct Serializer;
template<typename T, typename BaseT = Serializer<T> >
 struct CollectionSerializer;


/*****************************************************************************/
/* ENCODEDUNSIGNEDINTEGRALSER                                                */
/*****************************************************************************/

template<typename Encoder>
struct EncodedUnsignedIntegralSerializer {

    // The thing that we're reading
    typedef typename Encoder::Decoded Value;

    // The thing that we decode as
    typedef typename Encoder::Encoded Integral;

    // The metadata about a collection of unsigned integer values is just the
    // width in bits of the widest entry.  The maximum value is therefore
    // 64 on most platforms in existence today.

    // Metadata type for when we're working (needs to be mutable)
    typedef Bits WorkingMetadata;

    // Metadata type for when we're accessing (not mutable)
    typedef Bits ImmutableMetadata;

    static WorkingMetadata new_metadata(size_t length)
    {
        // Maximum size so far is zero
        return Bits(0);
    }

    // Serialize a single value.  The base points to the start of the block
    // of memory for this part of the data structure
    template<typename ValueT>
    static void serialize(long * base,
                          BitWriter & writer,
                          const ValueT & value,
                          WorkingMetadata metadata,
                          ImmutableMetadata & imd,
                          int object_num)
    {
        Integral uvalue = Encoder::encode(value);
        writer.write(uvalue, metadata);
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(const long * base,
                              BitReader & reader, ImmutableMetadata metadata)
    {
        return Encoder::decode(reader.read(metadata));
    }

    // Find how many bits to advance to get to this element
    static Bits get_element_offset(int n, ImmutableMetadata metadata)
    {
        return n * metadata;
    }

    // Scan a single item, updating the metadata
    static void prepare(Value value, WorkingMetadata & metadata,
                        int item_number)
    {
        Integral uvalue = Encoder::encode(value);
        metadata.value()
            = std::max<size_t>(metadata.value(), highest_bit(uvalue, -1) + 1);
    }

    // How many words to allocate to store this in?
    static size_t words_for_base(WorkingMetadata metadata, size_t length)
    {
        return BitwiseMemoryManager::words_required(metadata, length);
    }

    // How many words to allocate in an extra buffer for the child data?
    // Since there are no pointers in this structure (it's all fixed
    // width), we don't need any
    static size_t words_for_children(WorkingMetadata metadata)
    {
        return 0;
    }

    // How many bits to store a single entry?
    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & metadata)
    {
        return metadata;
    }

    static void
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
        imd = md;
    }
};


/*****************************************************************************/
/* SERIALIZERS FOR UNSIGNED INTEGRAL TYPES                                   */
/*****************************************************************************/

template<typename T>
struct IdentityEncoder {
    static JML_PURE_FN T encode(T value)
    {
        return value;
    }

    static JML_PURE_FN T decode(T value)
    {
        return value;
    }

    typedef T Encoded;
    typedef T Decoded;
};

template<typename T>
struct UnsignedIntegralSerializer
    : public EncodedUnsignedIntegralSerializer<IdentityEncoder<T> > {
};

template<>
struct Serializer<unsigned char>
    : public UnsignedIntegralSerializer<unsigned char> {
};

template<>
struct Serializer<char>
    : public UnsignedIntegralSerializer<unsigned char> {
};

template<>
struct Serializer<unsigned short>
    : public UnsignedIntegralSerializer<unsigned short> {
};

template<>
struct Serializer<unsigned int>
    : public UnsignedIntegralSerializer<unsigned int> {
};

template<>
struct Serializer<unsigned long int>
    : public UnsignedIntegralSerializer<unsigned long int> {
};

template<>
struct Serializer<unsigned long long int>
    : public UnsignedIntegralSerializer<unsigned long long int> {
};


/*****************************************************************************/
/* SERIALIZER<BITS>                                                          */
/*****************************************************************************/

struct BitsEncoder {

    static JML_PURE_FN unsigned encode(Bits value)
    {
        return value.value();
    }

    template<typename T>
    static JML_PURE_FN Bits decode(T value)
    {
        return Bits(value);
    }

    typedef Bits Decoded;
    typedef unsigned Encoded;
};

template<>
struct Serializer<Bits>
    : public EncodedUnsignedIntegralSerializer<BitsEncoder> {
};


/*****************************************************************************/
/* COLLECTIONSERIALIZER                                                      */
/*****************************************************************************/

/** Base template for serialization of a collection.  Builds on top of the
    Serializer class for that type.
*/
template<typename T, typename BaseT>
struct CollectionSerializer : public BaseT {

    typedef BaseT Base;
    typedef typename BaseT::WorkingMetadata WorkingMetadata;
    typedef typename BaseT::ImmutableMetadata ImmutableMetadata;

    using Base::prepare;
    using Base::bits_per_entry;
    using Base::serialize;
    using Base::reconstitute;
    using Base::finish_collection;

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static void
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md)
    {
        for (int i = 0; first != last;  ++first, ++i)
            prepare(*first, md, i);
    }

    static size_t words_for_base(WorkingMetadata & md, size_t length)
    {
        return BitwiseMemoryManager::words_required(bits_per_entry(md),
                                                    length);
    }

    static Bits get_element_offset(int element, const ImmutableMetadata & md)
    {
        return element * bits_per_entry(md);
    }

    // Extract entry n out of the total
    static T
    extract_from_collection(const long * mem, int n,
                            ImmutableMetadata md)
    {
        Bits bit_offset = get_element_offset(n, md);
        BitReader reader(mem, bit_offset);
        return reconstitute(mem, reader, md);
    }

    // Serialize a homogeneous collection where each of the elements is of
    // the same type.  We don't serialize any details of the collection itself,
    // only its elements.
    //
    // Returns an immutable metadata object that can be later used to access
    // the elements using the extract_from_collection() function.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last,
                         WorkingMetadata & md)
    {
        BitWriter writer(mem);

        ImmutableMetadata imd;

        for (int i = 0; first != last;  ++first, ++i)
            serialize(mem, writer, *first, md, imd, i);

        finish_collection(md, imd);

        return imd;
    }

};

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

/*****************************************************************************/
/* BASEARRAYENTRY                                                            */
/*****************************************************************************/

template<typename BaseData, typename ChildMetadata>
struct BaseArrayEntry {
    BaseArrayEntry()
        : data(BaseData()), offset(0), length(0), metadata(ChildMetadata())
    {
    }

    BaseData data;
    unsigned offset;
    unsigned length;
    ChildMetadata metadata;
};

template<typename ChildMetadata>
struct BaseArrayEntry<void, ChildMetadata> {
    BaseArrayEntry()
        : data(0), offset(0), length(0), metadata(ChildMetadata())
    {
    }

    int data;
    unsigned offset;
    unsigned length;
    ChildMetadata metadata;
};

template<typename BaseData>
struct BaseArrayEntry<BaseData, void> {
    BaseArrayEntry()
        : data(BaseData()), offset(0), length(0), metadata(0)
    {
    }

    BaseData data;
    unsigned offset;
    unsigned length;
    int metadata;
};

template<>
struct BaseArrayEntry<void, void> {
    BaseArrayEntry()
        : data(0), offset(0), length(0), metadata(0)
    {
    }

    int data;
    unsigned offset;
    unsigned length;
    int metadata;
};

#if 0
/*****************************************************************************/
/* BASEANDCOLLECTIONSERIALIZER                                               */
/*****************************************************************************/

/** Combine two serializers:
    - a base serializer, that serializes a fixed-width array;
    - a collection serializer, that serializes some variable length data in some
      memory that's allocated further on.
*/

template<typename BaseT,
         typename BaseSerializerT,
         typename ElementT,
         typename ElementSerializerT>
struct BaseAndCollectionSerializer {

    typedef typename ElementSerializerT::WorkingMetadata
        ChildWorkingMetadata;
    typedef typename ElementerializerT::ImmutableMetadata
        ChildImmutableMetadata;

    // Our base array contains:
    // - The base data;
    // - Offset
    // - Length
    // - 

    typedef BaseArrayEntry<BaseT, ChildImmutableMetadata>
        ImmutableMetadataEntry;
    typedef BaseArrayEntry<BaseT, ChildWorkingMetadata>
        WorkingMetadataEntry;

    // Type of the serializer to deal with the base data
    typedef BaseSerializerT BaseSerializer;

    // Type of the immutable metadata to go with this entry
    typedef ArrayAndData<ImmutableMetadataEntry, unsigned>
        ImmutableMetadata;
    
    typedef CollectionSerializer<ImmutableMetadataEntry>
        EntrySerializer;

    struct WorkingMetadata : public std::vector<WorkingMetadataEntry> {
        WorkingMetadata(size_t length)
            : std::vector<WorkingMetadataEntry>(length),
              base_md(length)
        {
        }

        // how many words in the various entries start
        size_t data_offset;

        // The total number of words that we need to store
        size_t total_words;

        // Metadata about how the working entries are serialized
        typename EntrySerializer::WorkingMetadata entries_md;

        // Metadata about how the base entries are serialized
        typename BaseSerializer::WorkingMetadata base_md;
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result;
        return result;
    }

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static size_t
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md)
    {
        int length = last - first;

        typename WorkingMetadata::Entry & entry = metadata.entries[item_number];
        entry.length = val.size();
        entry.offset = metadata.total_words;
        entry.metadata = ChildSerializer::new_metadata(val.size());
        size_t nwords = ChildSerializer::
            prepare_collection(val.begin(), val.end(), entry.metadata);
        metadata.total_words += nwords;


        for (int i = 0; first != last;  ++first, ++i)
            Base::prepare(*first, md, i);

        return Base::words_required(md, length);
    }

    // Extract entry n out of the total
    static T
    extract_from_collection(const long * mem, int n,
                            ImmutableMetadata md)
    {
        Bits bit_offset = Base::get_element_offset(n, md);
        BitReader reader(mem, bit_offset);
        return Base::reconstitute(mem, reader, md);
    }

    // Serialize a homogeneous collection where each of the elements is of
    // the same type.  We don't serialize any details of the collection itself,
    // only its elements.
    //
    // Returns an immutable metadata object that can be later used to access
    // the elements using the extract_from_collection() function.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last,
                         const WorkingMetadata & md)
    {
        BitWriter writer(mem);
        for (int i = 0; first != last;  ++first, ++i)
            Base::serialize(mem, writer, *first, md, i);

        return Base::to_immutable(md);
    }
};

// If there's no child, then we don't need to do all this junk...
template<typename BaseSerializer>
struct BaseAndCollectionSerializer<BaseSerializer, void>
    : public CollectionSerializer<typename BaseSerializer::Value, BaseSerializer> {
};
#endif

} // namespace JMVCC


#endif
