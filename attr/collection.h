/* collection.h                                                    -*- C++ -*-
   Jeremy Barnes, 1 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   A homogenous collection of like-typed objects.  The objects live in a
   memory mapped area somewhere with only offset-based access.

   They can be accessed by an invariant ID field.
*/

#ifndef __jgraph__attr__collection_h__
#define __jgraph__attr__collection_h__

namespace JGraph {

struct Collection {

    typedef uint32_t Id;

    // Get the object referenced by the given ID
    AttributeRef operator [] (Id id) const;

    // Insert an object, returning a reference to it
    

    // Update the value of the given attribute
    void update(Id id, const Attribute & value);

    // Remove the given attribute
    void erase(Id id);

    // The number of items in the collection
    size_t size() const;

private:
    // The traits instance telling us which type of object is stored in the
    // collection.
    const AttributeTraits * traits;

    // The region that we are looking in
    char * region;
    const char * cregion;

    // The next ID to use
    Id next_id;
};

} // namespace JGraph

#endif /* __jgraph__attr__collection_h__ */
