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
#include "jml/arch/bitops.h"
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


/*****************************************************************************/
/* MEMORY MANAGEMENT AND SERIALIZATION                                       */
/*****************************************************************************/

/** Small class to hold a count of bits so that length and number of bits
    parameters don't get confused.
*/
struct Bits {
    explicit Bits(size_t bits = 0)
        : bits_(bits)
    {
    }

    size_t & value() { return bits_; }
    size_t value() const { return bits_; }

    Bits operator * (size_t length) const
    {
        return Bits(bits_ * length);
    }

    size_t bits_;
};

template<typename Integral>
Bits operator * (Integral i, Bits b)
{
    return b * i;
}

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

    static size_t words_required(Bits bits, size_t length)
    {
        Bits nbits = bits * length;
        return words_to_cover(nbits).first;
    }

    long * allocate(size_t nwords)
    {
        return new long[nwords];
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

struct BitReader {
    
    BitReader(const long * data, Bits bit_ofs = Bits(0))
        : data(data), bit_ofs(bit_ofs.value())
    {
        if (bit_ofs.value() >= sizeof(long) * 8)
            throw Exception("invalid BitReader initialization");
    }

    long read(Bits bits)
    {
        long value = extract_bit_range(data, bit_ofs, bits.value());
        bit_ofs += bits.value();
        data += (bit_ofs / (sizeof(long) * 8));
        bit_ofs %= sizeof(long) * 8;
        return value;
    }

    const long * data;
    int bit_ofs;
};


/*****************************************************************************/
/* SERIALIZERS AND COLLECTIONSERIALIZERS                                     */
/*****************************************************************************/

template<typename T> struct Serializer;

template<typename T>
struct CollectionSerializer {
    typedef Serializer<T> Base;

    typename Base::Metadata metadata;

    Bits bits_per_entry() const { return Base::metadata_width(metadata); }

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    size_t prepare(Iterator first, Iterator last)
    {
        int length = last - first;

        for (int i = 0; first != last;  ++first, ++i)
            Base::prepare(*first, metadata, i);

        return MemoryManager::words_required(bits_per_entry(), length);
    }

    // Extract entry n out of the total
    unsigned extract(const long * mem, int n) const
    {
        BitReader reader(mem, n * metadata);
        return reader.read(metadata);
    }

    // Serialize a homogeneous collection where each of the elements is an
    // unsigned.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    void serialize_collection(BitWriter & writer,
                              Iterator first, Iterator last)
    {
        for (int i = 0; first != last;  ++first, ++i)
            Base::serialize(writer, *first, metadata, i);
    }

    size_t words_required(size_t length)
    {
        return MemoryManager::words_required(bits_per_entry(), length);
    }
};

template<typename T>
struct Serializer<CollectionSerializer<T> > {

    // The thing that we're reading
    typedef CollectionSerializer<T> Value;

    // The type of something that holds the metadata about a collection of
    // vectors.  The information that needs to be held is:
    // - A list containing the length of each element;
    // - A list containing the pointer offset of each element;
    // - A list containing the sizing information necessary for the elements
    //   in that list

    typedef CollectionSerializer<T> Metadata;

    // Prepare a single item for serialization, updating the metadata
    static void prepare(Value value, Metadata & metadata, int item_number)
    {
        metadata.value()
            = std::max<size_t>(metadata.value(), highest_bit(value, -1) + 1);
    }

    // Serialize a single vector object
    template<typename ValueT>
    static void serialize(BitWriter & writer,
                          const ValueT & value,
                          const Metadata & metadata,
                          int object_num)
    {
        throw Exception("serialize(): not done");
        // The sub-items are already done, so there's nothing left to do.
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader, Metadata metadata)
    {
        return reader.read(metadata);
    }

    // How many bits do we need to implement the metadata?
    static Bits metadata_width(Metadata metadata)
    {
        return metadata;
    }
};

template<>
struct Serializer<unsigned> {

    // The thing that we're reading
    typedef unsigned Value;

    // The metadata about a collection of unsigned is just the width in bits
    // of the widest entry.  The maximum value is therefore 32 on most
    // platforms in existence today.
    typedef Bits Metadata;

    // Serialize a single unsigned value, given metadata
    template<typename ValueT>
    static void serialize(BitWriter & writer, const ValueT & value,
                          Metadata metadata, int object_num)
    {
        unsigned uvalue = value;
        writer.write(uvalue, metadata);
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader, Metadata metadata)
    {
        return reader.read(metadata);
    }

    // How many bits do we need to implement the metadata?
    static Bits metadata_width(Metadata metadata)
    {
        return metadata;
    }

    // Scan a single item, updating the metadata
    static void prepare(Value value, Metadata & metadata, int item_number)
    {
        metadata.value()
            = std::max<size_t>(metadata.value(), highest_bit(value, -1) + 1);
    }
};


/*****************************************************************************/
/* VECTOR                                                                    */
/*****************************************************************************/

template<typename T>
struct Vector {

    size_t length_;
    CollectionSerializer<T> serializer_;
    const long * mem_;

    Vector()
        : length_(0), mem_(0)
    {
    }

    Vector(size_t length, const long * mem,
           const CollectionSerializer<T> & serializer)
        : length_(length), serializer_(serializer), mem_(mem)
    {
    }

    // Create and populate with data from a range
    template<typename T2>
    Vector(MemoryManager & mm, const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }

    template<typename Iterator>
    void init(MemoryManager & mm, Iterator first, Iterator last)
    {
        length_ = last - first;
        size_t nwords = serializer_.prepare(first, last);
        long * mem = mm.allocate(nwords);
        mem_ = mem;

        BitWriter writer(mem);
        serializer_.serialize_collection(writer, first, last);
    }

    size_t size() const
    {
        return length_;
    }
    
    T operator [] (int index) const
    {
        return serializer_.extract(mem_, index);
    }


    struct const_iterator
        : public boost::iterator_facade<const_iterator,
                                        const T,
                                        boost::random_access_traversal_tag,
                                        const T> {
    private:
        const_iterator(Vector vec, size_t element)
            : vec(vec), element(element)
        {
        }

        Vector vec;
        ssize_t element;

        friend class boost::iterator_core_access;
        friend class Vector;

        T dereference() const
        {
            if (element < 0 || element >= vec.size())
                throw Exception("invalid vector dereference attempt");
            return vec[element];
        }

        bool equal(const const_iterator & other) const
        {
            return other.element == element;
        }
        
        void increment()
        {
            element += 1;
        }

        void decrement()
        {
            element -= 1;
        }

        void advance(ssize_t n)
        {
            element += n;
        }

        ssize_t distance_to(const const_iterator & other) const
        {
            return other.element - element;
        }
    };

    const_iterator begin() const
    {
        return const_iterator(*this, 0);
    }

    const_iterator end() const
    {
        return const_iterator(*this, length_);
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
struct VectorMetadata {
    Vector<unsigned> sizes;
    Vector<unsigned> offsets;
    Vector<CollectionSerializer<T> > metadata;
};

template<typename T>
struct Serializer<Vector<T> > {

    // The thing that we're reading
    typedef Vector<T> Value;

    // The type of something that holds the metadata about a collection of
    // vectors.  The information that needs to be held is:
    // - A list containing the length of each element;
    // - A list containing the pointer offset of each element;
    // - A list containing the sizing information necessary for the elements
    //   in that list

    typedef VectorMetadata<T> Metadata;

    // Prepare a single item for serialization, updating the metadata
    static void prepare(Value value, Metadata & metadata, int item_number)
    {
        metadata.value()
            = std::max<size_t>(metadata.value(), highest_bit(value, -1) + 1);
    }

    // Serialize a single vector object
    template<typename ValueT>
    static void serialize(BitWriter & writer,
                          const ValueT & value,
                          const Metadata & metadata,
                          int object_num)
    {
        throw Exception("serialize(): not done");
        // The sub-items are already done, so there's nothing left to do.
    }

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader, Metadata metadata)
    {
        return reader.read(metadata);
    }

    // How many bits do we need to implement the metadata?
    static Bits metadata_width(Metadata metadata)
    {
        return metadata;
    }

};

template<typename T>
struct CollectionSerializer<Vector<T> > {
    typedef Serializer<Vector<T> > Base;

    typename Base::Metadata metadata;

    Bits bits_per_entry() const { return Base::metadata_width(metadata); }

    // Prepare to serialize.  We mostly work out the size of the metadata
    // here.
    template<typename Iterator>
    size_t prepare(Iterator first, Iterator last)
    {
        size_t length = last - first;
        
        // Serialize each of the sub-arrays, taking the absolute offset
        // of each one
        vector<size_t> offsets(length);
        vector<size_t> lengths(length);
        vector<CollectionSerializer<T> > serializers(length);

        size_t total_words = 0;

        for (int i = 0; first != last;  ++first, ++i) {
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            lengths[i] = val.size();
            CollectionSerializer<T> & serializer = serializers[i];

            size_t nwords = serializer.prepare(val.begin(), val.end());

            offsets[i] = total_words;
            total_words += nwords;
        }

        return total_words;
    }

    // Extract entry n out of the total
    Vector<T> extract(const long * mem, int n) const
    {
        // Get the offset and the length
        size_t offset = 0;
        size_t length = 0;
        CollectionSerializer<T> serializer;
        
        Vector<T> result(length, mem + offset, serializer);
        return result;
    }

    // Serialize a homogeneous collection where each of the elements is an
    // unsigned.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    void serialize_collection(BitWriter & writer,
                              Iterator first, Iterator last)
    {
        for (int i = 0; first != last;  ++first, ++i)
            Base::serialize(writer, *first, metadata, i);
    }
};


/*****************************************************************************/
/* TEST CASES                                                                */
/*****************************************************************************/

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

template<typename T1, typename T2>
bool operator == (const Vector<T1> & v1, const std::vector<T2> & v2)
{
    if (v1.size() != v2.size())
        return false;
    return std::equal(v1.begin(), v1.end(), v2.begin());
}

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
