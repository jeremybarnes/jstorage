/* struct.h                                                        -*- C++ -*-
   Jeremy Barnes, 1 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Data structure attributes.
*/

#ifndef __jgraph__attr__struct_h__
#define __jgraph__attr__struct_h__


namespace JGraph {




/*****************************************************************************/
/* REFCOUNTEDATTRIBUTETRAITS                                                 */
/*****************************************************************************/

/** Base class for reference counted attributes.  These attributes have another
    object stored somewhere that is pointed to by the Attribute object.  This
    other object has a reference count so that the attribute can be destroyed
    when it is no longer referenced.
*/

template<class Underlying>
struct RefCountedAttributeTraits : public AttributeTraits {
    RefCountedAttributeTraits();
    virtual ~RefCountedAttributeTraits();

    AttributeRef encode(const Underlying & val) const;
    const Underlying & decode(const Attribute & val) const
    {
        return getObject(val);
    }

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;

    virtual void deleteObject(const Attribute & a) const;

    const Underlying & getObject(const Attribute & attr) const;
};


} // namespace JGraph


#endif /* __jgraph__attr__struct_h__ */
