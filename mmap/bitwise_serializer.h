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
        return Bits();
    }

    // Serialize a single unsigned value, given metadata
    template<typename ValueT>
    static void serialize(BitWriter & writer, const ValueT & value,
                          WorkingMetadata metadata, int object_num)
    {
        Integral uvalue = Encoder::encode(value);
        writer.write(uvalue, metadata);
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader, ImmutableMetadata metadata)
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
    static size_t words_required(WorkingMetadata metadata, size_t length)
    {
        return BitwiseMemoryManager::words_required(metadata, length);
    }

    // How many bits to store a single entry?
    static Bits bits_per_entry(WorkingMetadata metadata)
    {
        return metadata;
    }

    // Convert metadata to immutable metadata
    static ImmutableMetadata to_immutable(WorkingMetadata metadata)
    {
        return metadata;
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

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static size_t
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md)
    {
        int length = last - first;

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
        return Base::reconstitute(reader, md);
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
            Base::serialize(writer, *first, md, i);

        return Base::to_immutable(md);
    }
};


} // namespace JMVCC


#endif
