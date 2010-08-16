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

    PayloadT operator [] (int index)
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

    const char * resolve(size_t offset)
    {
        return reinterpret_cast<const char *>(offset);
    }

    size_t encode(void * ptr)
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

    void * allocate(Bits bits, size_t length)
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

template<typename T> struct Serializer;

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

    Bits bits() const { return Bits(32); }

    unsigned extract(const void * mem, int n) const
    {
        const unsigned * p = reinterpret_cast<const unsigned *>(mem);
        return p[n];
    }

    template<typename Iterator>
    void serialize(MemoryManager * mm, void * mem,
                   Iterator first, Iterator last)
    {
        unsigned * p = reinterpret_cast<unsigned *>(mem);
        std::copy(first, last, p);
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
        : mm_(&mm), length_(last - first), serializer_(first, last)
    {
        void * mem = mm_->allocate(serializer_.bits(), length_);
        offset_ = mm_->encode(mem);
        serializer_.serialize(mm_, mem, first, last);
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

    const Metadata & metadata() const { return array_; }

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
    
    T operator [] (int index)
    {
        return array_.extract(index);
    }

};

template<typename T>
struct Serializer<Vector<T> > {
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

BOOST_AUTO_TEST_CASE(test_nested1)
{
    Vector<Vector<int> > v2;
}
