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

template<typename T> struct Serializer;

template<>
struct Serializer<unsigned> {
    template<typename Iterator>
    Serializer(Iterator first, Iterator last)
    {
        
    }

    Serializer()
    {
    }

    void scan(unsigned value)
    {
    }
    
    unsigned bits() const { return 32; }

    unsigned extract(const void * mem, int n) const
    {
        const unsigned * p = reinterpret_cast<const unsigned *>(mem);
        return p[n];
    }

    
    
};

struct MemoryManager {

    const char * resolve(size_t offset)
    {
        return reinterpret_cast<const char *>(offset);
    }

};

template<typename T, typename MM = MemoryManager>
struct Array {
    size_t length_;
    size_t offset_;
    MM * mm;
    Serializer<T> serializer_;

    T extract(int element) const
    {
        return serializer_.extract(mm->resolve(offset_), element);
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
