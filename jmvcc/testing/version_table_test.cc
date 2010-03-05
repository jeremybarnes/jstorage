/* versioned_test.cc
   Jeremy Barnes, 14 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
   
   Test of versioned objects.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/vector_utils.h"
#include "jml/utils/pair_utils.h"
#include "jml/utils/hash_map.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jmvcc/version_table.h"
#include "jml/utils/testing/live_counting_obj.h"

using namespace ML;
using namespace JMVCC;
using namespace std;

using boost::unit_test::test_suite;

struct MyAllocData {
    
    MyAllocData()
        : objects_allocated(0), bytes_allocated(0),
          objects_outstanding(0), bytes_outstanding(0)
    {
    }

    struct Alloc_Info {
        Alloc_Info(size_t size = 0, size_t index = 0)
            : size(size), index(index), freed(false)
        {
        }

        size_t size;
        size_t index;
        bool freed;
    };

    typedef hash_map<void *, Alloc_Info> Info;
    Info info;

    size_t objects_allocated;
    size_t bytes_allocated;
    size_t objects_outstanding;
    size_t bytes_outstanding;

    ~MyAllocData()
    {
        if (objects_outstanding != 0 || bytes_outstanding != 0) {
            dump();
            throw Exception("destroyed allocated with outstanding objects");
        }

        for (Info::iterator it = info.begin(), end = info.end();
             it != end;  ++it) {
            if (!it->second.freed)
                throw Exception("memory not freed");
            free(it->first);
        }
    }

    void * allocate(size_t bytes)
    {
        void * mem = malloc(bytes);
        if (!mem)
            throw Exception("couldn't allocate memory");
        if (info.count(mem))
            throw Exception("memory was allocated twice");

        info[mem] = Alloc_Info(bytes, info.size() - 1);

        ++objects_allocated;
        ++objects_outstanding;
        bytes_allocated += bytes;
        bytes_outstanding += bytes;

        memset(mem, -1, bytes);

        return mem;
    }

    void deallocate(void * ptr)
    {
        if (!info.count(ptr))
            throw Exception("free of unknown memory");

        Alloc_Info & ainfo = info[ptr];

        if (ainfo.freed)
            throw Exception("double-free");

        ainfo.freed = true;
        --objects_outstanding;
        bytes_outstanding -= ainfo.size;

        memset(ptr, -1, ainfo.size);
    }

    void dump() const
    {
        cerr << "objects: allocated " << objects_allocated
             << " outstanding: " << objects_outstanding << endl;
        cerr << "bytes: allocated " << bytes_allocated
             << " outstanding: " << bytes_outstanding << endl;

        for (Info::const_iterator it = info.begin(), end = info.end();
             it != end;  ++it) {
            cerr << format("  %012p %8d %8zd %s",
                           it->first,
                           it->second.index,
                           it->second.size,
                           (it->second.freed ? "" : "LIVE"))
                 << endl;
        }
    }
};

struct MyAlloc {
    MyAlloc(MyAllocData & data)
        : data(data)
    {
    }

    MyAllocData & data;
    
    void * allocate(size_t bytes)
    {
        return data.allocate(bytes);
    }
    
    void deallocate(void * ptr)
    {
        data.deallocate(ptr);
    }
};

BOOST_AUTO_TEST_CASE( test_version_table_memory1 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, MyAlloc> VT;

    cerr << "sizeof(Obj) = " << sizeof(Obj) << endl;
    cerr << "sizeof(VT::Entry) = " << sizeof(VT::Entry) << endl;
    cerr << "sizeof(VT) = " << sizeof(VT) << endl;

    constructed = destroyed = 0;

    MyAllocData alloc;

    VT * vt = VT::create(10, alloc);

    VT::free(vt);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory2 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, MyAlloc> VT;
    MyAllocData alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);
    VT::free(vt);
    
    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory_with_copy )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, MyAlloc> VT;
    MyAllocData alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);

    size_t old_outstanding = constructed - destroyed;
    
    VT * vt2 = VT::create(*vt, 12);
    VT::free(vt);

    size_t new_outstanding = constructed - destroyed;
    BOOST_CHECK_EQUAL(old_outstanding, new_outstanding);

    VT::free(vt2);

    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

BOOST_AUTO_TEST_CASE( test_version_table_memory_with_copy2 )
{
    typedef Version_Table<Obj, No_Cleanup<Obj>, MyAlloc> VT;
    MyAllocData alloc;

    constructed = destroyed = 0;

    VT * vt = VT::create(10, alloc);
    vt->push_back(0, 1);

    size_t old_outstanding = constructed - destroyed;
    
    VT * vt2 = VT::create(*vt, 12);
    VT::free(vt2);

    size_t new_outstanding = constructed - destroyed;
    BOOST_CHECK_EQUAL(old_outstanding, new_outstanding);

    VT::free(vt);

    BOOST_CHECK_EQUAL(constructed, destroyed);

    BOOST_CHECK_EQUAL(alloc.objects_outstanding, 0);
    BOOST_CHECK_EQUAL(alloc.bytes_outstanding, 0);
}

