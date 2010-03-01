/* collection_test.cc                                              -*- C++ -*-
   Jeremy Barnes, 1 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of collections for jgraph.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "recoset/attr/attribute.h"
#include "recoset/attr/attribute_basic_types.h"
#include "recoset/storage/mmap_storage.h"
#include "jml/utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jml/utils/testing/live_counting_obj.h"

using namespace ML;
using namespace JGraph;
using namespace std;

BOOST_AUTO_TEST_CASE( test_collection1 )
{
    // A collection of int values to go in the storage; this gives their
    // traits.
    IntTraits traits;

    // A file-backed memory region for the objects to be stored
    MMap_Storage storage("test_collection_storage.bin");

    // Clear it (destroy everything that existed in it)
    storage.clear();

    // Create a collection in that storage that starts at the start
    Collection collection(&traits);
    
    // Create the collection.  We don't need to do it in a transaction as
    // there is no way that anything else could reference it.
    size_t start = collection.create(storage);
    
    BOOST_CHECK_EQUAL(collection.size(), 0);

    AttributeRef attr1 = traits.encode(1);
    AttributeRef attr1a = traits.encode(1);
    AttributeRef attr2 = traits.encode(2);
    AttributeRef attr3 = traits.encode(3);

    uint32_t id1  = collection.add(attr1);
    uint32_t id1a = collection.add(attr1a);
    uint32_t id2  = collection.add(attr2);
    uint32_t id3  = collection.add(attr3);
    
    BOOST_CHECK_EQUAL(collection.size(), 4);

    BOOST_CHECK(id1 != id1a);
    BOOST_CHECK(id1 != id2);
    BOOST_CHECK(id1 != id3);

    BOOST_CHECK_EQUAL(collection.count(id1), 1);
    BOOST_CHECK_EQUAL(collection.count(id1a), 1);
    BOOST_CHECK_EQUAL(collection.count(id2), 1);
    BOOST_CHECK_EQUAL(collection.count(id3), 1);
    BOOST_CHECK_EQUAL(collection.count(id3 + 1), 0);

    collection.remove(id1);
    BOOST_CHECK_EQUAL(collection.size(), 3);
    BOOST_CHECK_EQUAL(collection.count(id1), 0);
    BOOST_CHECK_EQUAL(collection.count(id1a), 1);
    BOOST_CHECK_EQUAL(collection.count(id2), 1);
    BOOST_CHECK_EQUAL(collection.count(id3), 1);
    BOOST_CHECK_EQUAL(collection.count(id3 + 1), 0);

    collection.remove(id3);
    BOOST_CHECK_EQUAL(collection.size(), 2);
    BOOST_CHECK_EQUAL(collection.count(id1), 0);
    BOOST_CHECK_EQUAL(collection.count(id1a), 1);
    BOOST_CHECK_EQUAL(collection.count(id2), 1);
    BOOST_CHECK_EQUAL(collection.count(id3), 0);
    BOOST_CHECK_EQUAL(collection.count(id3 + 1), 0);


}
