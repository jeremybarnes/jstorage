/* md_and_array_test.cc
   Jeremy Barnes, 14 August 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.
   Copyright (c) 2010 Recoset Inc.  All rights reserved.

*/
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jml/arch/exception_handler.h"
#include "jml/arch/vm.h"
#include "jml/utils/unnamed_bool.h"
#include "jml/utils/testing/testing_allocator.h"
#include "jml/utils/hash_map.h"
#include "jml/arch/demangle.h"
#include "jml/utils/exc_assert.h"
#include "jml/utils/vector_utils.h"
#include "jml/arch/bit_range_ops.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/assign/list_of.hpp>
#include <iostream>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>
#include <bitset>
#include <map>
#include <set>


using namespace std;
using namespace ML;


#if 0
// Metadata
// has the following capabilities:
// - length: return the number of elements
// - data:   returns the amount of data

template<typename MetadataT, typename PayloadT>
struct MDAndArray : public MetadataT {
    using MetadataT::data;
    using MetadataT::length;

    MDAndArray(const MetadataT & md)
        : md(md)
    {
    }

    PayloadT operator [] (int index) const
    {
        return Extractor<Payload>::extract(data(), index);
    }
};
#endif

/** Small class to hold a count of bits so that length and number of bits
    parameters don't get confused */
struct Bits {
    explicit Bits(size_t bits)
        : bits_(bits)
    {
    }

    size_t value() const { return bits_; }

    Bits operator * (size_t length) const
    {
        return Bits(bits_ * length);
    }

    size_t bits_;
};

struct MemoryManager {

    const long * resolve(size_t offset)
    {
        return reinterpret_cast<const long *>(offset);
    }

    size_t encode(long * ptr)
    {
        return reinterpret_cast<size_t>(ptr);
    }

    /** How many words of memory do we need to cover the given number of
        bits?  Returns the result in the first entry and the number of
        wasted bits in the second.
    */
    static std::pair<size_t, size_t> words_to_cover(Bits bits)
    {
        size_t nbits = bits.value();
        size_t factor = 8 * sizeof(long);
        size_t result = nbits / factor;
        size_t wasted = result * factor - nbits;
        result += (wasted > 0);
        return std::make_pair(result, wasted);
    }

    long * allocate(Bits bits, size_t length)
    {
        Bits nbits = bits * length;
        size_t nwords, wasted;
        boost::tie(nwords, wasted) = words_to_cover(nbits);

        long * result = new long[nwords];

        // Initialize the wasted part of memory to all zeros to avoid
        // repeatability errors caused by non-initialization.
        if (wasted) result[nwords - 1] = 0;
        return result;
    }
};

struct BitWriter {
    
    BitWriter(long * data, Bits bit_ofs = Bits(0))
        : data(data), bit_ofs(bit_ofs.value())
    {
        if (bit_ofs.value() >= sizeof(long) * 8)
            throw Exception("invalid bitwriter initialization");
    }

    void write(long val, Bits bits)
    {
        set_bit_range(data, val, bit_ofs, bits.value());
        bit_ofs += bits.value();
        data += (bit_ofs / (sizeof(long) * 8));
        bit_ofs %= sizeof(long) * 8;
    }

    long * data;
    int bit_ofs;
};


template<typename T> struct Serializer;

template<typename T>
struct CollectionSerializer {
    
    typedef Serializer<T> Base;

    typename Serializer<T>::Width width;
    size_t width;

    Bits bits() const { return Base::width_to_bits(width); }

    // Extract entry n out of the total
    unsigned extract(const long * mem, int n) const
    {
        const unsigned * p = reinterpret_cast<const unsigned *>(mem);
        return p[n];
    }

    // Serialize a homogeneous collection where each of the elements is an
    // unsigned.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    void serialize_collection(BitWriter & writer,
                              Iterator first, Iterator last)
    {
        for (; first != last;  ++first) {
            unsigned val = *first;
            writer.write(val, bits());
        }
    }
};

template<> struct Serializer<Bits>;


template<>
struct Serializer<unsigned> {
    template<typename Iterator>
    Serializer(Iterator first, Iterator last)
    {
        // No scanning to do
    }

    Serializer()
    {
    }

    // The thing that we're reading
    typedef unsigned Value;

    // The type of something that holds the width.  This is going to have a
    // maximum value of 32.
    typedef unsigned Width;

    // Object to serialize a width
    typedef Serializer<Bits> WidthSerializer;

    // Object to serialize a collection of widths
    typedef CollectionSerializer<Bits> WidthCollectionSerializer;

    // Serialize a single object, given a width
    void serialize(BitWriter & writer, Value value, Width width) const
    {
        writer.write(value, width);
    }

    // Reconstitute a single object, given a width
    Value reconstitute(BitReader & reader, Width width) const
    {
        return reader.read(value, width);
    }

    // How many bits do we need to implement the width?
    Bits width_to_bits(Width width) const
    {
        return width;
    }
};

template<typename T, typename MM = MemoryManager>
struct Array {
    Array()
        : mm_(0), length_(0), offset_(0)
    {
    }

    template<typename Iterator>
    Array(MM & mm, Iterator first, Iterator last)
        : mm_(&mm),
          length_(last - first),
          serializer_(first, last)
    {
        long * mem = mm_->allocate(serializer_.bits(), length_);
        offset_ = mm_->encode(mem);
        BitWriter writer(mem);
        serializer_.serialize(writer, first, last);
    }

    MM * mm_;
    size_t length_;
    Serializer<T> serializer_;
    size_t offset_;

    T extract(int element) const
    {
        return serializer_.extract(mm_->resolve(offset_), element);
    }
};

template<typename T>
struct Vector {
    typedef Array<T> Metadata;

    Metadata array_;

    Vector()
    {
    }

    template<typename T2>
    Vector(MemoryManager & mm, const std::vector<T2> & vec)
        : array_(mm, vec.begin(), vec.end())
    {
    }

    size_t size() const
    {
        return array_.length_;
    }
    
    T operator [] (int index) const
    {
        return array_.extract(index);
    }

};

template<typename T>
std::ostream & operator << (std::ostream & stream, const Vector<T> & vec)
{
    stream << "[ ";
    for (unsigned i = 0;  i < vec.size();  ++i) {
        stream << vec[i] << " ";
    }
    return stream << "]";
}

template<typename T>
struct Serializer<Vector<T> > {

    template<typename Iterator>
    Serializer(Iterator first, Iterator last)
    {
        // No scanning to do
    }

    Serializer()
    {
    }

    size_t length;
    size_t offset;

    Bits bits() const { return Bits(0); }

    Vector<T> extract(const long * mem, int n) const
    {
        const Metadata * p = reinterpret_cast<const Metadata *>(mem);
        return p[n];
    }

    // Serialize a homogeneous collection, each element of which is a
    // Vector<T>.
    template<typename Iterator>
    void serialize_collection(BitWriter & writer,
                              Iterator first, Iterator last)
    {
        // We're serializing a vector of vectors; each element of the
        // iterator is a Vector<T>.
        // 1.  Size each of the sub-collections individually
        // 2.  Serialize the sub-collections to get the offsets and
        //     lengths.
        // 3.  Serialize the objects themselves

        size_t n = last - first;
        vector<Serializer<T> > serializers;

        Serializer<T> serializer;
        for (; first != last;  ++first) {
            T to_write = *first;
            serializer.serialize(T);
        }
    }
};

// Two cases:
// 1.  Root case: the metadata object is actually present
// 2.  Contained case: the metadata object is passed

BOOST_AUTO_TEST_CASE( test_non_nested )
{
    MemoryManager mm;

    vector<unsigned> values = boost::assign::list_of<int>(1)(2)(3)(4);
    Vector<unsigned> v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

#if 0

template<typename T1, typename T2>
bool operator == (const Vector<T1> & v1, const std::vector<T2> & v2);

template<typename T1, typename T2>
bool operator == (const std::vector<T1> & v1, const Vector<T2> & v2)
{
    return v2 == v1;
}

BOOST_AUTO_TEST_CASE(test_nested1)
{
    MemoryManager mm;

    vector<unsigned> values1 = boost::assign::list_of<int>(1)(2)(3)(4);
    vector<unsigned> values2 = boost::assign::list_of<int>(5)(6);
    vector<unsigned> values3;
    vector<unsigned> values4 = boost::assign::list_of<int>(7)(8)(9)(10)(11);

    vector<vector<unsigned> > values;
    values.push_back(values1);
    values.push_back(values2);
    values.push_back(values3);
    values.push_back(values4);

    Vector<Vector<unsigned> > v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

#endif
