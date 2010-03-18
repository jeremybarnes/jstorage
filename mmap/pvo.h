/* pvo.h                                                           -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__pvo_h__
#define __jmvcc__pvo_h__

#include <stdint.h>
#include <boost/shared_ptr.hpp>
#include "jmvcc/versioned_object.h"


namespace JMVCC {


typedef uint64_t ObjectId;

static const ObjectId NO_OBJECT_ID = (ObjectId)-1;
static const ObjectId ROOT_OBJECT_ID = (ObjectId)-2;

class PVOStore;
class PVOManager;
template<typename T> class TypedPVO;


/*****************************************************************************/
/* PVO                                                                       */
/*****************************************************************************/

/** This type stands for Persistent Versioned Object.  It's an object that
    a) has a persistent (on-disk) life, and b) can have multiple versions
    alive at a time.
*/

struct PVO : public JMVCC::Versioned_Object {

    /** Return the immutable identity of the object */
    ObjectId id() const
    {
        return id_;
    }

    /** Who owns this object */
    PVOManager * owner() const
    {
        return owner_;
    }

    /** What store are we in? */
    virtual PVOStore * store() const;

    /** How many versions of the object are there? */
    virtual size_t num_versions() const = 0;

    virtual PVO * parent() const;

protected:
    PVO(ObjectId id, PVOManager * owner)
        : id_(id), owner_(owner)
    {
    }

    /** The destructor is protected as only the PVOManager is allowed to
        actually delete a PVO.  In order to remove it from the current
        snapshot, call remove(). */
    ~PVO()
    {
    }

private:
    ObjectId id_;  ///< Identity in the memory mapped region
    PVOManager * owner_;  ///< Responsible for dealing with it

    friend class PVOEntry;
    friend class PVOManager;
    friend class PVOManagerVersion;
};


/*****************************************************************************/
/* PVOREF                                                                     */
/*****************************************************************************/

/** This is a lightweight, temporary object designed to allow for access to
    a value that may either
    a) exist in memory as a sandboxed value;
    b) exist in memory as a versioned object; or
    c) be serialized on disk in some format.
*/

template<typename Obj, typename TargetPVO = TypedPVO<Obj> >
struct PVORef {
    PVORef()
    {
    }

    PVORef(const boost::shared_ptr<TargetPVO> & pvo)
        : pvo(pvo)
    {
    }

    PVORef(const boost::shared_ptr<PVO> & pvo)
        : pvo(boost::dynamic_pointer_cast<TargetPVO>(pvo))
    {
    }

    boost::shared_ptr<TargetPVO> pvo;
    
    ObjectId id() const { return pvo->id(); }
    
    operator const Obj () const { return pvo->read(); }

    const Obj & read() const { return pvo->read(); }

    Obj & mutate() const { return pvo->mutate(); }

    void remove() const { pvo->remove(); }
};



} // namespace JMVCC

#include "pvo_impl.h"

#endif /* __jmvcc__pvo_h__ */
