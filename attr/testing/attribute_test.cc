/* attribute_test.cc                                               -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Test of attributes for jgraph.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "recoset/attr/attribute.h"
#include "recoset/attr/attribute_basic_types.h"
#include "jml/utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jml/utils/testing/live_counting_obj.h"

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

// Scalar attribute (integer)
BOOST_AUTO_TEST_CASE( test1 )
{
    IntTraits traits;

    AttributeRef attr = traits.encode(1);

    BOOST_CHECK_EQUAL(attr.print(), "1");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);
    BOOST_CHECK_EQUAL(traits.decode(attr), 1);
}

// Reference counted attribute (string)
BOOST_AUTO_TEST_CASE( test2 )
{
    StringTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);
    BOOST_CHECK_EQUAL(traits.decode(attr), "hello");

    AttributeRef attr2 = traits.encode("bonus");

    BOOST_CHECK_EQUAL(attr2.print(), "bonus");
    BOOST_CHECK_EQUAL(attr2, attr2);
    BOOST_CHECK_EQUAL(attr2 < attr2, false);
    BOOST_CHECK_EQUAL(attr2 != attr2, false);
    BOOST_CHECK_EQUAL(attr2.compare(attr2), 0);
    BOOST_CHECK_EQUAL(traits.decode(attr2), "bonus");

    BOOST_CHECK_EQUAL(attr == attr2, false);
    BOOST_CHECK_EQUAL(attr2 == attr, false);
    BOOST_CHECK_EQUAL(attr < attr2,  false);
    BOOST_CHECK_EQUAL(attr2 < attr,  true);
    BOOST_CHECK_EQUAL(attr.stableLess(attr2),  false);
    BOOST_CHECK_EQUAL(attr2.stableLess(attr),  true);
}

// Reference counted attribute, checking

struct ObjTraits : public RefCountedAttributeTraits<Obj> {

    virtual std::string print(const Attribute & attr) const
    {
        return format("%d", getObject(attr).operator int());
    }

    virtual size_t hash(const Attribute & a) const
    {
        return getObject(a).operator int();
    }

    virtual size_t stableHash(const Attribute & a) const
    {
        return getObject(a).operator int();
    }

    const Obj & decode(const Attribute & attr) const
    {
        return getObject(attr);
    }
};

// Reference counted attribute (string)
BOOST_AUTO_TEST_CASE( test3 )
{
    ObjTraits traits;

    AttributeRef attr2, attr3;

    {
        AttributeRef attr = traits.encode(3);
        
        BOOST_CHECK_EQUAL(attr.references(), 1);

        BOOST_CHECK_EQUAL(attr.print(), "3");
        BOOST_CHECK_EQUAL(attr, attr);
        BOOST_CHECK_EQUAL(attr < attr, false);
        BOOST_CHECK_EQUAL(attr != attr, false);
        BOOST_CHECK_EQUAL(attr.compare(attr), 0);
        BOOST_CHECK_EQUAL(traits.decode(attr), 3);

        BOOST_CHECK_EQUAL(destroyed + 1, constructed);

        cerr << "assign 1" << endl;

        AttributeRef attr4 = attr;

        BOOST_CHECK_EQUAL(attr.references(), 2);

        BOOST_CHECK_EQUAL(destroyed + 1, constructed);

        BOOST_CHECK_EQUAL(attr, attr4);

        cerr << "assign 2" << endl;

        attr2 = attr4;

        BOOST_CHECK_EQUAL(attr.references(), 3);

        BOOST_CHECK_EQUAL(destroyed + 1, constructed);

        BOOST_CHECK_EQUAL(attr2, attr4);

        cerr << "destroying 1" << endl;
    }

    BOOST_CHECK_EQUAL(attr2.references(), 1);

    cerr << "destroying 2" << endl;

    BOOST_CHECK_EQUAL(traits.decode(attr2), 3);
    
    cerr << "attr2 = " << attr2 << endl;

    BOOST_CHECK_EQUAL(destroyed + 1, constructed);

    attr3 = attr2;

    BOOST_CHECK_EQUAL(attr2.references(), 2);

    cerr << "attr2 = " << attr2 << endl;
    cerr << "attr3 = " << attr3 << endl;

    BOOST_CHECK_EQUAL(destroyed + 1, constructed);

    BOOST_CHECK_EQUAL(attr2, attr3);
    BOOST_CHECK_EQUAL(attr2.print(), attr3.print());

    attr2 = AttributeRef();

    BOOST_CHECK_EQUAL(attr2.references(), -1);
    BOOST_CHECK_EQUAL(attr3.references(), 1);

    BOOST_CHECK_EQUAL(destroyed + 1, constructed);

    attr3 = AttributeRef();

    BOOST_CHECK_EQUAL(attr3.references(), -1);

    BOOST_CHECK_EQUAL(destroyed, constructed);
}


// Dictionary attribute (atom)
BOOST_AUTO_TEST_CASE( test4 )
{
    AtomTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);

    AttributeRef attr2 = traits.encode("bonus");

    BOOST_CHECK_EQUAL(attr2.print(), "bonus");
    BOOST_CHECK_EQUAL(attr2, attr2);
    BOOST_CHECK_EQUAL(attr2 < attr2, false);
    BOOST_CHECK_EQUAL(attr2 != attr2, false);
    BOOST_CHECK_EQUAL(attr2.compare(attr2), 0);

    BOOST_CHECK_EQUAL(attr == attr2, false);
    BOOST_CHECK_EQUAL(attr2 == attr, false);
    BOOST_CHECK_EQUAL(attr < attr2,  true);
    BOOST_CHECK_EQUAL(attr2 < attr,  false);
    BOOST_CHECK_EQUAL(attr.stableLess(attr2),  false);
    BOOST_CHECK_EQUAL(attr2.stableLess(attr),  true);
}
