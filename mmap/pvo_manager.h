/* pvo_manager.h                                                   -*- C++ -*-
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object class, the base of objects.
*/

#ifndef __jmvcc__pvo_manager_h__
#define __jmvcc__pvo_manager_h__

#include "pvo.h"
#include <memory>
#include "typed_pvo.h"

#include <boost/shared_ptr.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>


namespace JMVCC {


/*****************************************************************************/
/* PVO_ENTRY                                                                 */
/*****************************************************************************/

struct PVOEntry {

    PVOEntry()
    {
    }

    PVOEntry(PVO * local)
        : local(local)
    {
    }
        
    union {
        struct {
            uint64_t offset;
        };
        uint64_t bits;
    };
        
    boost::shared_ptr<PVO> local;
};

std::ostream & operator << (std::ostream & stream,
                            const PVOEntry & entry);


/*****************************************************************************/
/* PVO_MANAGER_VERSION                                                       */
/*****************************************************************************/

/** The addressable objects table for a single snapshot (ie no version
    control). */
struct PVOManagerVersion : public std::vector<PVOEntry> {
    typedef std::vector<PVOEntry> Underlying;

    PVOManagerVersion();

    PVOManagerVersion(const PVOManagerVersion & other);

    ~PVOManagerVersion();

    void add(PVO * local);

    size_t object_count_;

    size_t object_count() const
    {
        return object_count_;
    }
    
    static void * serialize(const PVOManagerVersion & obj,
                            MemoryManager & mm);
    static void deallocate(void * mem, MemoryManager & mm);

    static void reconstitute(PVOManagerVersion & obj,
                             const void * mem,
                             MemoryManager & mm);
};

template<>
struct Serializer<PVOManagerVersion> : public PVOManagerVersion {
};

std::ostream &
operator << (std::ostream & stream, const PVOManagerVersion & ver);


/*****************************************************************************/
/* PVO_MANAGER                                                               */
/*****************************************************************************/

/** A table of addressable objects.  Performs the mapping between an object
    ID and an offset in the file.  This is itself a versioned object.

    Set of all addressable objects that are currently instantiated into
    memory.
    
    An addressable object will be instantiated in memory if it has more
    than one version in the active snapshots.
    
    A new object that is created in a snapshot (but is not yet committed)
    will be instantiated in the snapshot's local change list.
    
    There is no tracking of deletions.  An ID of a deleted object is
    invalid as soon as the last snapshot that it was alive in has
    disappeared.
*/

struct PVOManager : protected TypedPVO<PVOManagerVersion> {

    typedef TypedPVO<PVOManagerVersion> Underlying;

    PVOManager(ObjectId id, PVOManager * owner);

    template<typename TargetPVO>
    TargetPVO * construct()
    {
        std::auto_ptr<TargetPVO> result(new TargetPVO(NO_OBJECT_ID, owner()));
        mutate().add(result.get());
        return result.release();
    }
    
    // Construct a given object type
    template<typename TargetPVO, typename Arg1>
    typename boost::enable_if<boost::is_base_of<PVO, TargetPVO>,
                              TargetPVO *>::type
    construct(const Arg1 & arg1)
    {
        std::auto_ptr<TargetPVO> result
            (new TargetPVO(NO_OBJECT_ID, owner(), arg1));
        mutate().add(result.get());
        return result.release();
    }
    
    // Construct a typed PVO
    template<typename T, typename Arg1>
    typename boost::disable_if<boost::is_base_of<PVO, T>,
                               TypedPVO<T> *>::type
    construct(const Arg1 & arg1)
    {
        return construct<TypedPVO<T> >(arg1);
    }
    
    PVO *
    lookup(ObjectId obj) const;

    size_t object_count() const;

    /** Set a new persistent version for an object.  This will record that
        there is a new persistent version on disk for the given object, so
        the pointer should be swapped and the current one cleaned up. */
    virtual void set_persistent_version(ObjectId object, void * new_version);

private:
    PVOManager();
    PVOManager(ObjectId id, PVOManager * owner,
               const PVOManagerVersion & version);

    void swap(PVOManager & other)
    {
        Underlying::swap(other);
    }
};


} // namespace JMVCC

#endif /* __jmvcc__pvo_manager_h__ */
