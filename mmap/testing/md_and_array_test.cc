/* md_and_array_test.cc
   Jeremy Barnes, 14 August 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.
   Copyright (c) 2010 Recoset Inc.  All rights reserved.

*/
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jstorage/mmap/bitwise_memory_manager.h"
#include "jstorage/mmap/bitwise_serializer.h"
#include "jstorage/mmap/array.h"
#include "jstorage/mmap/pair.h"
#include "jstorage/mmap/structure.h"
#include "jstorage/mmap/string.h"

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
#include "jml/utils/pair_utils.h"
#include "jml/arch/bit_range_ops.h"
#include "jml/arch/bitops.h"
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
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


/*****************************************************************************/
/* TEST CASES                                                                */
/*****************************************************************************/

#if 0

// Two cases:
// 1.  Root case: the metadata object is actually present
// 2.  Contained case: the metadata object is passed

BOOST_AUTO_TEST_CASE( test_non_nested )
{
    BitwiseMemoryManager mm;

    vector<unsigned> values = boost::assign::list_of<int>(1)(2)(3)(4);
    Array<unsigned> v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

BOOST_AUTO_TEST_CASE(test_pair_terminal)
{
    BitwiseMemoryManager mm;

    vector<pair<unsigned, unsigned> > values = boost::assign::list_of<pair<unsigned, unsigned> >(make_pair(1, 2))(make_pair(2, 3))(make_pair(3, 4))(make_pair(4, 5));
    Array<pair<unsigned, unsigned> > v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

BOOST_AUTO_TEST_CASE(test_pair_of_pairs)
{
    BitwiseMemoryManager mm;

    vector<pair<unsigned, pair<unsigned, unsigned> > > values
        = boost::assign::list_of<pair<unsigned, pair<unsigned, unsigned> > >
        (make_pair(1, make_pair(2, 3)))(make_pair(2, make_pair(3, 4)))
        (make_pair(3, make_pair(4, 5)))(make_pair(4, make_pair(5, 6)));
    Array<pair<unsigned, pair<unsigned, unsigned> > > v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

struct Pair {
    Pair(unsigned first = 0, unsigned second = 0)
        : first(first), second(second)
    {
    }

    unsigned first;
    unsigned second;

    bool operator == (const Pair & other) const
    {
        return first == other.first && second == other.second;
    }
};

std::ostream & operator << (std::ostream & stream, const Pair & p)
{
    return stream << std::make_pair(p.first, p.second);
}

Pair make_Pair(unsigned f, unsigned s)
{
    return Pair(f, s);
}

struct PairStructSerializer:
    public StructureSerializer<Pair,
                               Extractor<Pair, unsigned, &Pair::first>,
                               Extractor<Pair, unsigned, &Pair::second> > {
};

namespace JMVCC {

template<>
struct Serializer<Pair> : public PairStructSerializer {
};

} // namespace JMVCC

BOOST_AUTO_TEST_CASE(test_structure_terminal)
{
    BitwiseMemoryManager mm;

    vector<Pair> values = boost::assign::list_of<Pair>(make_Pair(1, 2))(make_Pair(2, 3))(make_Pair(3, 4))(make_Pair(4, 5));
    Array<Pair> v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

BOOST_AUTO_TEST_CASE(test_tuple_terminal)
{
    BitwiseMemoryManager mm;

    typedef boost::tuple<unsigned, unsigned, unsigned> Tuple;

    vector<Tuple> values = boost::assign::list_of<Tuple>
        (boost::make_tuple(1, 2, 3))
        (boost::make_tuple(2, 3, 4))
        (boost::make_tuple(3, 4, 5))
        (boost::make_tuple(4, 5, 6));
    Array<Tuple> v1(mm, values);
    
    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

BOOST_AUTO_TEST_CASE( test_string )
{
    BitwiseMemoryManager mm;

    vector<string> values
        = boost::assign::list_of<string>("hello")("how")("are")("you");

    Array<string> v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
}

BOOST_AUTO_TEST_CASE(test_tuple_with_string)
{
    BitwiseMemoryManager mm;

    typedef boost::tuple<unsigned, std::string, unsigned> Tuple;

    vector<Tuple> values = boost::assign::list_of<Tuple>
        (boost::make_tuple(1, "hello!", 3))
        (boost::make_tuple(2, "how@", 4))
        (boost::make_tuple(3, "are#", 5))
        (boost::make_tuple(4, "you$", 6))
        (boost::make_tuple(5, "today?", 7));

    Array<Tuple> v1(mm, values);
    
    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
    BOOST_CHECK_EQUAL(v1[4], values[4]);
}

#endif

namespace JMVCC {

template<typename T1, typename T2>
bool operator == (const Array<T1> & v1, const std::vector<T2> & v2)
{
    if (v1.size() != v2.size())
        return false;
    return std::equal(v1.begin(), v1.end(), v2.begin());
}

template<typename T1, typename T2>
bool operator == (const std::vector<T1> & v1, const Array<T2> & v2)
{
    return v2 == v1;
}

} // namespace JMVCC

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

    Array<Array<unsigned> > v1(mm, values);

    BOOST_CHECK_EQUAL(v1.size(), values.size());
    BOOST_CHECK_EQUAL(v1[0], values[0]);
    BOOST_CHECK_EQUAL(v1[1], values[1]);
    BOOST_CHECK_EQUAL(v1[2], values[2]);
    BOOST_CHECK_EQUAL(v1[3], values[3]);
    BOOST_CHECK_EQUAL(v1[4], values[4]);

    cerr << "v1[3] = " << v1[3] << endl;
}

#if 1
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

    Array<Array<Array<unsigned> > > v1(mm, vvvalues);

    BOOST_CHECK_EQUAL(v1.size(), vvvalues.size());
    BOOST_CHECK_EQUAL(v1[0], vvvalues[0]);
    BOOST_CHECK_EQUAL(v1[1], vvvalues[1]);
    BOOST_CHECK_EQUAL(v1[2], vvvalues[2]);
    BOOST_CHECK_EQUAL(v1[3], vvvalues[3]);

    cerr << "v1[3] = " << v1[3] << endl;
}
#endif
