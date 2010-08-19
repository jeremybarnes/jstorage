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

std::ostream & operator << (std::ostream & stream, Bits bits)
{
    return stream << format("Bits(%d)", bits.value());
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

template<typename T> struct CollectionSerializer;

#if 0
template<typename T>
struct CollectionSerializer {
    typedef Serializer<T> Base;

    typedef Base::WorkingMetadata WorkingMetadata;
    typedef Base::ImmutableMetadata ImmutableMetadata;

    static Bits bits_per_entry() const { return Base::metadata_width(metadata); }

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
    static void serialize_collection(long * mem,
                                     Iterator first, Iterator last)
    {
        BitWriter writer(mem);
        for (int i = 0; first != last;  ++first, ++i)
            Base::serialize(writer, *first, metadata, i);
    }

    size_t words_required(size_t length)
    {
        return MemoryManager::words_required(bits_per_entry(), length);
    }
};
#endif

#if 0

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

#endif

#if 0

template<typename T> struct Serializer;

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

#endif

template<>
struct CollectionSerializer<unsigned> {
    // Metadata type for when we're working (needs to be mutable)
    typedef Bits WorkingMetadata;

    // Metadata type for when we're accessing (not mutable)
    typedef Bits ImmutableMetadata;

    static WorkingMetadata new_metadata(size_t length)
    {
        return Bits();
    }

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static size_t prepare(Iterator first, Iterator last, WorkingMetadata & md)
    {
        int length = last - first;

        for (int i = 0; first != last;  ++first, ++i) {
            unsigned uvalue = *first;
            md.value() = std::max<size_t>(md.value(),
                                          highest_bit(uvalue, -1) + 1);
        }

        return MemoryManager::words_required(md, length);
    }

    // Extract entry n out of the total
    static unsigned extract(const long * mem, int n,
                            ImmutableMetadata md)
    {
        BitReader reader(mem, n * md);
        return reader.read(md);
    }

    // Serialize a homogeneous collection where each of the elements is an
    // unsigned.  We don't serialize any details of the collection itself.
    // Returns an immutable metadata object that can be later used to access
    // the elements.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last,
                         const WorkingMetadata & md)
    {
        BitWriter writer(mem);
        for (int i = 0; first != last;  ++first, ++i) {
            unsigned uvalue = *first;
            writer.write(uvalue, md);
        }

        return md;
    }
};


template<>
struct CollectionSerializer<Bits> {
    // Metadata type for when we're working (needs to be mutable)
    typedef Bits WorkingMetadata;

    // Metadata type for when we're accessing (not mutable)
    typedef Bits ImmutableMetadata;

    static WorkingMetadata new_metadata(size_t length)
    {
        return Bits();
    }

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static size_t prepare(Iterator first, Iterator last, WorkingMetadata & md)
    {
        int length = last - first;

        for (int i = 0; first != last;  ++first, ++i) {
            Bits uvalue = *first;
            md.value() = std::max<size_t>(md.value(),
                                          highest_bit(uvalue.value(), -1) + 1);
        }

        return MemoryManager::words_required(md, length);
    }

    // Extract entry n out of the total
    static Bits extract(const long * mem, int n,
                        ImmutableMetadata md)
    {
        BitReader reader(mem, n * md);
        return Bits(reader.read(md));
    }

    // Serialize a homogeneous collection where each of the elements is an
    // unsigned.  We don't serialize any details of the collection itself.
    // Returns an immutable metadata object that can be later used to access
    // the elements.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last,
                         const WorkingMetadata & md)
    {
        BitWriter writer(mem);
        for (int i = 0; first != last;  ++first, ++i) {
            Bits uvalue = *first;
            writer.write(uvalue.value(), md);
        }

        return md;
    }
};


/*****************************************************************************/
/* VECTOR                                                                    */
/*****************************************************************************/

template<typename T>
struct Vector {

    size_t length_;
    typedef CollectionSerializer<T> Serializer;
    typedef typename Serializer::ImmutableMetadata Metadata;
    Metadata metadata_;
    const long * mem_;

    Vector()
        : length_(0), mem_(0)
    {
    }

    Vector(size_t length, const long * mem,
           const Metadata & metadata)
        : length_(length), metadata_(metadata), mem_(mem)
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
        typename Serializer::WorkingMetadata metadata
            = Serializer::new_metadata(length_);

        size_t nwords = Serializer::prepare(first, last, metadata);
        long * mem = mm.allocate(nwords);
        mem_ = mem;

        metadata_
            = Serializer::serialize_collection(mem, first, last, metadata);
    }

    size_t size() const
    {
        return length_;
    }
    
    T operator [] (int index) const
    {
        return Serializer::extract(mem_, index, metadata_);
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
struct CollectionSerializer<Vector<T> > {

    typedef CollectionSerializer<T> ChildSerializer;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    typedef CollectionSerializer<unsigned> LengthSerializer;
    typedef CollectionSerializer<ChildImmutableMetadata> ChildMetadataSerializer;

    struct WorkingMetadata {
        WorkingMetadata(size_t length)
            : offsets(length), lengths(length), metadata(length)
        {
        }

        vector<size_t> offsets;
        vector<size_t> lengths;
        vector<ChildWorkingMetadata> metadata;

        // how many words in the various entries start
        size_t length_offset, metadata_offset, data_offset;

        typename LengthSerializer::WorkingMetadata offsets_metadata;
        typename LengthSerializer::WorkingMetadata lengths_metadata;
        typename ChildMetadataSerializer::WorkingMetadata metadata_metadata;
    };
    
    struct ImmutableMetadata {
        Vector<unsigned> offsets;
        Vector<unsigned> lengths;
        Vector<typename ChildSerializer::ImmutableMetadata> metadata;
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result(length);
        return result;
    }

    // Prepare to serialize.  We mostly work out the size of the metadata
    // here.
    template<typename Iterator>
    static size_t prepare(Iterator first, Iterator last, WorkingMetadata & md)
    {
        size_t length = last - first;
        
        // Serialize each of the sub-arrays, taking the absolute offset
        // of each one
        size_t total_words = 0;

        for (int i = 0; first != last;  ++first, ++i) {
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            md.lengths[i] = val.size();
            typename ChildSerializer::WorkingMetadata & wmd
                = md.metadata[i];

            wmd = ChildSerializer::new_metadata(val.size());
            size_t nwords = ChildSerializer::prepare(val.begin(), val.end(),
                                                     wmd);
            
            md.offsets[i] = total_words;
            total_words += nwords;
        }

        // Add to that the words necessary to serialize the metadata arrays
        // for the children

        md.offsets_metadata = LengthSerializer::new_metadata(length);
        size_t offset_words = LengthSerializer::prepare(md.offsets.begin(),
                                                        md.offsets.end(),
                                                        md.offsets_metadata);

        md.lengths_metadata = LengthSerializer::new_metadata(length);
        size_t length_words = LengthSerializer::prepare(md.lengths.begin(),
                                                        md.lengths.end(),
                                                        md.lengths_metadata);

        md.metadata_metadata = ChildMetadataSerializer::new_metadata(length);
        size_t metadata_words
            = ChildMetadataSerializer
            ::prepare(md.metadata.begin(),
                      md.metadata.end(),
                      md.metadata_metadata);

        md.length_offset = offset_words;
        md.metadata_offset = offset_words + length_words;
        md.data_offset = md.metadata_offset + metadata_words;

        cerr << "data words " << total_words
             << " offset words " << offset_words
             << " length words " << length_words
             << " md words " << metadata_words
             << " total words " << total_words + md.data_offset
             << endl;

        total_words += md.data_offset;

        return total_words;
    }

    // Extract entry n out of the total
    static Vector<T> extract(const long * mem, int n,
                             const ImmutableMetadata & metadata)
    {
        throw Exception("extract for vector: not done");

        // Get the offset and the length
        size_t offset = 0;
        size_t length = 0;
        ChildImmutableMetadata child_metadata;
        
        Vector<T> result(length, mem + offset, child_metadata);
        return result;
    }

    // Serialize a homogeneous collection where each of the elements is a
    // vector<T>.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last, WorkingMetadata & md)
    {
        cerr << "offsets = " << md.offsets << endl;
        cerr << "lengths = " << md.lengths << endl;
        cerr << "md      = " << md.metadata << endl;
        
        // First: the three metadata arrays
        LengthSerializer::
            serialize_collection(mem, md.offsets.begin(), md.offsets.end(),
                                 md.offsets_metadata);

        LengthSerializer::
            serialize_collection(mem + md.length_offset,
                                 md.lengths.begin(), md.lengths.end(),
                                 md.lengths_metadata);

        ChildMetadataSerializer::
            serialize_collection(mem + md.metadata_offset,
                                 md.metadata.begin(),
                                 md.metadata.end(),
                                 md.metadata_metadata);

        // And now the data from each of the child arrays
        for (int i = 0; first != last;  ++first, ++i) {
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            typename ChildSerializer::WorkingMetadata & wmd
                = md.metadata[i];

            wmd = ChildSerializer::
                serialize_collection(mem + md.data_offset + md.offsets[i],
                                     val.begin(), val.end(),
                                     wmd);
        }

        ImmutableMetadata result;
        return result;
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
    vector<unsigned> values5 = boost::assign::list_of<int>(0)(0)(0)(0)(0);

    vector<vector<unsigned> > values;
    values.push_back(values1);
    values.push_back(values2);
    values.push_back(values3);
    values.push_back(values4);
    values.push_back(values5);

    Vector<Vector<unsigned> > v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
    BOOST_CHECK_EQUAL(v1[4], values[4]);
}
