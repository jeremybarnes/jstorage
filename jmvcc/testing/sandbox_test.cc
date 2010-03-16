/* sandbox_test.cc
   Jeremy Barnes, 3 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test for the sandbox functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/vector_utils.h"
#include "jml/utils/pair_utils.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <boost/thread.hpp>
#include "jmvcc/sandbox.h"
#include "jmvcc/versioned.h"
#include "jmvcc/versioned2.h"
#include "jml/utils/testing/live_counting_obj.h"

using namespace ML;
using namespace JMVCC;
using namespace std;

using boost::unit_test::test_suite;

// Make sure that the sandbox calls the destructors on the objects when it
// dies
BOOST_AUTO_TEST_CASE( test_sandbox_calls_destructors )
{
    constructed = destroyed = 0;

    
    {
        Versioned<Obj> ver(0);
        
        Sandbox sandbox;
        
        BOOST_CHECK_EQUAL(constructed, destroyed + 1);
        
        sandbox.local_value<Obj>(&ver, Obj());
        
        BOOST_CHECK_EQUAL(constructed, destroyed + 2);
    }

    BOOST_CHECK_EQUAL(constructed, destroyed);
}


size_t counter = 1;

struct With_Parent : public Versioned2<Obj> {
    With_Parent(With_Parent * parent, int index)
        : parent_(parent), index(index), destroy_order(0)
    {
    }

    With_Parent * parent_;
    int index;
    mutable size_t destroy_order;

    virtual With_Parent * parent() const { return parent_; }

    virtual void destroy_local_value(void * val) const
    {
        destroy_order = counter++;
        Versioned2<Obj>::destroy_local_value(val);
    }
};

BOOST_AUTO_TEST_CASE( test_sandbox_destructor_order )
{
    With_Parent obj1(0, 0);
    With_Parent obj2(&obj1, 1);
    
    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj1, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 1);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
        sandbox.local_value<Obj>(&obj2, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 2);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
    }
    
    BOOST_CHECK_EQUAL(obj1.destroy_order, 2);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 1);

    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj2, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 2);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 1);
        sandbox.local_value<Obj>(&obj1, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 2);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
    }
    
    BOOST_CHECK_EQUAL(obj1.destroy_order, 4);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 3);

    With_Parent obj3(&obj2, 3);

    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj1, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 1);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
        sandbox.local_value<Obj>(&obj2, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 2);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
        sandbox.local_value<Obj>(&obj3, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 3);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
    }
    
    BOOST_CHECK_EQUAL(obj1.destroy_order, 7);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 6);
    BOOST_CHECK_EQUAL(obj3.destroy_order, 5);

    counter = 1;
    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj3, Obj());
        sandbox.local_value<Obj>(&obj1, Obj());
        sandbox.local_value<Obj>(&obj2, Obj());
    }

    BOOST_CHECK_EQUAL(obj1.destroy_order, 3);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 2);
    BOOST_CHECK_EQUAL(obj3.destroy_order, 1);

    counter = 1;
    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj1, Obj());
        sandbox.local_value<Obj>(&obj3, Obj());
        sandbox.local_value<Obj>(&obj2, Obj());
    }

    BOOST_CHECK_EQUAL(obj1.destroy_order, 3);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 2);
    BOOST_CHECK_EQUAL(obj3.destroy_order, 1);

    counter = 1;
    {
        Sandbox sandbox;
        sandbox.local_value<Obj>(&obj3, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 3);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 2);
        sandbox.local_value<Obj>(&obj2, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 3);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 1);
        sandbox.local_value<Obj>(&obj1, Obj());
        BOOST_CHECK_EQUAL(sandbox.num_local_values(), 3);
        BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
    }

    BOOST_CHECK_EQUAL(obj1.destroy_order, 3);
    BOOST_CHECK_EQUAL(obj2.destroy_order, 2);
    BOOST_CHECK_EQUAL(obj3.destroy_order, 1);

}

// Make sure that the destructors are called in the right order
BOOST_AUTO_TEST_CASE( stress_test_sandbox_destructor_order )
{
    int ntests = 10;
    int nobj  = 200;
    int nnoparent = 3;
    //int niter = 10;

    for (unsigned i = 0;  i < ntests;  ++i) {

        vector<boost::shared_ptr<With_Parent> > objects;
        
        for (unsigned j = 0;  j < nobj;  ++j) {
            With_Parent * parent = 0;
            if (j > nnoparent)
                parent = objects[random() % objects.size()].get();

            objects.push_back(boost::shared_ptr<With_Parent>
                              (new With_Parent(parent, j)));
        }

        counter = 1;

        // Create local values for each
        {
            Sandbox sandbox;

            for (unsigned j = 0;  j < nobj;  ++j) {
                sandbox.local_value<Obj>(objects[j].get(), Obj(j));
                BOOST_CHECK_EQUAL(sandbox.num_local_values(), j + 1);
            }

            BOOST_CHECK_EQUAL(sandbox.num_local_values(), nobj);
            BOOST_CHECK_EQUAL(sandbox.num_automatic_local_values(), 0);
        }

        // Check that they were destroyed in the right order
        for (unsigned j = 0;  j < nobj;  ++j) {
#if 0
            cerr << "object " << j << " parent ";
            if (objects[j]->parent())
                cerr << objects[j]->parent()->index
                     << " destroyed " << objects[j]->parent()->destroy_order;
            else cerr << "(none)";
            cerr << " destroy_order " << objects[j]->destroy_order << endl;
#endif

            BOOST_CHECK(objects[j]->destroy_order != 0);

            if (objects[j]->parent())
                BOOST_CHECK(objects[j]->parent()->destroy_order
                                    > objects[j]->destroy_order);
            //else BOOST_CHECK(objects[j]->destroy_order >= nobj - nnoparent);

        }

        for (unsigned j = 0;  j < nobj;  ++j)
            objects[j]->destroy_order = 0;

#if 0
        // Create a few local values
        for (unsigned k = 0;  k < niter;  ++k) {
            set<int> done;

            {
                Sandbox sandbox;
                
                for (unsigned j = 0;  j < k * 10;  ++j)
                    sandbox.local_value<Obj>(objects[random() % 100].get(),
                                             Obj());
            }
            
        }
#endif

    }
}
