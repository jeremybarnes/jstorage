/* md_and_array_test.cc
   Jeremy Barnes, 14 August 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.
   Copyright (c) 2010 Recoset Inc.  All rights reserved.

*/
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jstorage/mmap/bitwise_memory_manager.h"
#include "jstorage/mmap/bitwise_serializer.h"

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
using namespace JMVCC;


#if 0
/*****************************************************************************/
/* ARRAYANDMETADATA                                                          */
/*****************************************************************************/

template<typename ElementT, typename MetadataT,
         typename ArraySerializerT = CollectionSerializer<ElementT>,
         typename MetadataSerializerT = Serializer<MetadataT> >
class ArrayAndMetadata {
    typedef MetadataT Metadata;
    typedef ElementT Element;

    typedef ArraySerializerT ArraySerializer;
    typedef MetadataSerializerT MetadataSerializer;
    typedef typename MetadataSerializer::ImmutableMetadata ElementMetadata;

    size_t length_;
    size_t offset_;
    Metadata metadata_;
    ElementMetadata element_metadata_;

public:

    size_t size() const { return length_; }

    ElementT extract(int element, const long * mem) const
    {
    }

    ArrayAndMetadata(size_t length, size_t offset,
                     Metadata metadata,
                     ElementMetadata element_metadata)
    {
    }

#if 0
    {
        init(length, mem, metadata);
    }
#endif

#if 0
    // Create and populate with data from a range
    template<typename T2>
    Vector(BitwiseMemoryManager & mm, const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }
#endif

    void init(size_t length, const long * mem,
              MetadataT metadata,
              ChildMetadata child_m)
    {
        length_ = length;
        mem_ = mem;
        metadata_ = metadata;
    }

    template<typename Iterator>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last)
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

    T operator [] (int index) const
    {
        return Serializer::extract(mem_, index, child_metadata_);
    }

};
#endif


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
    {
        init(length, mem, metadata);
    }

    // Create and populate with data from a range
    template<typename T2>
    Vector(BitwiseMemoryManager & mm, const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }

    void init(size_t length, const long * mem,
              const Metadata & metadata)
    {
        length_ = length;
        mem_ = mem;
        metadata_ = metadata;
    }

    template<typename Iterator>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last)
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



/*****************************************************************************/
/* TEST CASES                                                                */
/*****************************************************************************/

// Two cases:
// 1.  Root case: the metadata object is actually present
// 2.  Contained case: the metadata object is passed

BOOST_AUTO_TEST_CASE( test_non_nested )
{
    BitwiseMemoryManager mm;

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
    BitwiseMemoryManager mm;

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

    cerr << "v1[3] = " << v1[3] << endl;
}

#endif

#if 0
BOOST_AUTO_TEST_CASE(test_nested2)
{
    BitwiseMemoryManager mm;

    vector<unsigned> values1 = boost::assign::list_of<int>(1)(2)(3)(4);
    vector<unsigned> values2 = boost::assign::list_of<int>(5)(6);
    vector<unsigned> values3;
    vector<unsigned> values4 = boost::assign::list_of<int>(7)(8)(9)(10)(11);
    vector<unsigned> values5 = boost::assign::list_of<int>(0)(0)(0)(0)(0);

    vector<vector<unsigned> > vvalues1;
    vvalues1.push_back(values1);
    vvalues1.push_back(values2);
    vvalues1.push_back(values3);
    vvalues1.push_back(values4);
    vvalues1.push_back(values5);

    vector<vector<unsigned> > vvalues2;
    vvalues2.push_back(values5);

    vector<vector<unsigned> > vvalues3;

    vector<vector<unsigned> > vvalues4;
    vvalues4.push_back(values5);
    vvalues4.push_back(values1);
    vvalues4.push_back(values2);
    vvalues4.push_back(values3);

    vector<vector<vector<unsigned> > > vvvalues;
    vvvalues.push_back(vvalues1);
    vvvalues.push_back(vvalues2);
    vvvalues.push_back(vvalues3);
    vvvalues.push_back(vvalues4);

    Vector<Vector<Vector<unsigned> > > v1(mm, vvvalues);

    BOOST_CHECK_EQUAL(v1.size(), vvvalues.size());
    BOOST_CHECK_EQUAL(v1[0], vvvalues[0]);
    BOOST_CHECK_EQUAL(v1[1], vvvalues[1]);
    BOOST_CHECK_EQUAL(v1[2], vvvalues[2]);
    BOOST_CHECK_EQUAL(v1[3], vvvalues[3]);

    cerr << "v1[3] = " << v1[3] << endl;
}
#endif
