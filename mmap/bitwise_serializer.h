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
    // of memory for the child of the data structure
    template<typename ValueT>
    static void serialize(BitWriter & writer,
                          long * child_mem,
                          const ValueT & value,
                          WorkingMetadata metadata,
                          ImmutableMetadata & imd,
                          int object_num,
                          size_t length)
    {
        Integral uvalue = Encoder::encode(value);
        writer.write(uvalue, metadata);
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader,
                              const long * child_mem,
                              ImmutableMetadata metadata,
                              size_t length)
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
    static Bits bits_per_entry(const Bits & metadata)
    {
        return metadata;
    }

    static void
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & md, ImmutableMetadata & imd,
                      size_t length)
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

    static JML_PURE_FN size_t
    words_for_base(const WorkingMetadata & md, size_t length)
    {
        return BitwiseMemoryManager::words_required(bits_per_entry(md),
                                                    length);
    }

    static JML_PURE_FN Bits
    get_element_offset(int element, const ImmutableMetadata & md)
    {
        return element * bits_per_entry(md);
    }

    // Extract entry n out of the total
    static T
    extract_from_collection(const long * mem, int n,
                            ImmutableMetadata md,
                            size_t length)
    {
        Bits bit_offset = get_element_offset(n, md);
        BitReader reader(mem, bit_offset);
        const long * child_mem = mem + words_for_base(md, length);
        return reconstitute(reader, child_mem, md, length);
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

        size_t length = last - first;

        long * child_mem = mem + words_for_base(md, length);

        for (int i = 0; first != last;  ++first, ++i)
            serialize(writer, child_mem, *first, md, imd, i, length);

        finish_collection(mem, child_mem, md, imd, length);

        return imd;
    }

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

} // namespace JMVCC


#endif
