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
#include <vector>
#include "memory_manager.h"
#include "jml/arch/exception.h"
#include "serialization.h"

#include <boost/shared_ptr.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>

#include "jml/arch/backtrace.h"


namespace JMVCC {


/*****************************************************************************/
/* PVO_ENTRY                                                                 */
/*****************************************************************************/

struct PVOEntry {

    static const uint64_t NO_OFFSET = (uint64_t)-1;

#if 0
    static const uint64_t NEW_OBJECT = (uint64_t)-2;
    static const uint64_t DELETED_OBJECT = (uint64_t)-3;
#endif

    PVOEntry()
        : offset(NO_OFFSET)
    {
    }

    struct PVODestroyer {
        void operator () (PVO * x) const
        {
            using namespace std;
            cerr << "deleting " << x << endl;
            delete x;
        }
    };


    PVOEntry(const boost::shared_ptr<PVO> & local)
        : offset(NO_OFFSET), local(local)
    {
    }
    
    uint64_t offset;
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

    template<typename TargetPVO, typename Arg1>
    boost::shared_ptr<TargetPVO>
    construct(const Arg1 & arg1, PVOManager * owner)
    {
        ObjectId id = size();

        boost::shared_ptr<TargetPVO> result
            (new TargetPVO(id, owner, arg1), PVOEntry::PVODestroyer());

        push_back(PVOEntry(result));
        ++object_count_;

        return result;
    }

    template<typename TargetPVO>
    boost::shared_ptr<TargetPVO>
    get(ObjectId obj, PVOManager * owner) const
    {
        PVOEntry & entry = const_cast<PVOEntry &>(at(obj));

        if (entry.local) {
            boost::shared_ptr<TargetPVO> result
                = boost::dynamic_pointer_cast<TargetPVO>(entry.local);
            if (!result)
                throw ML::Exception("local object of wrong type");
            return result;
        }

        if (entry.offset == PVOEntry::NO_OFFSET)
            throw Exception("getting local object with no offset");

        boost::shared_ptr<TargetPVO> result
            (TargetPVO::reconstituted(obj, entry.offset, owner),
             PVOEntry::PVODestroyer());

        entry.local = result;
        return result;
    }

    size_t object_count_;

    size_t object_count() const
    {
        return object_count_;
    }
    
    static void * serialize(const PVOManagerVersion & obj,
                            MemoryManager & mm);

    static void reserialize(const PVOManagerVersion & obj,
                            void * where,
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

struct PVOManager : public TypedPVO<PVOManagerVersion> {

    typedef TypedPVO<PVOManagerVersion> Underlying;

    PVOManager(ObjectId id, PVOManager * owner);

    // Construct a given object type
    template<typename TargetPVO, typename Arg1>
    typename boost::enable_if<boost::is_base_of<PVO, TargetPVO>,
                              boost::shared_ptr<TargetPVO> >::type
    construct(const Arg1 & arg1)
    {
        return mutate().construct<TargetPVO>(arg1, this);
    }
    
    // Construct a typed PVO
    template<typename T, typename Arg1>
    typename boost::disable_if<boost::is_base_of<PVO, T>,
                               boost::shared_ptr<TypedPVO<T> > >::type
    construct(const Arg1 & arg1)
    {
        return construct<TypedPVO<T> >(arg1);
    }
    
    template<typename TargetPVO>
    typename boost::enable_if<boost::is_base_of<PVO, TargetPVO>,
                              boost::shared_ptr<TargetPVO> >::type
    lookup(ObjectId obj) const
    {
        if (obj >= read().size())
            throw ML::Exception("unknown object");
        
        return read().get<TargetPVO>(obj, const_cast<PVOManager *>(this));
    }

    template<typename T>
    typename boost::disable_if<boost::is_base_of<PVO, T>,
                               boost::shared_ptr<TypedPVO<T> > >::type
    lookup(ObjectId obj) const
    {
        return lookup<TypedPVO<T> >(obj);
    }


    size_t object_count() const;

#if 0
    /** Add the given PVO to the current sandbox's version of this table. */
    ObjectId add(PVO * pvo)
    {
        return mutate().add(pvo);
    }
#endif

    /** Set a new persistent version for an object.  This will record that
        there is a new persistent version on disk for the given object, so
        the pointer should be swapped and the current one cleaned up. */
    virtual void set_persistent_version(ObjectId object, void * new_version);

    /* Override these to deal with created or deleted objects. */
    virtual bool check(Epoch old_epoch, Epoch new_epoch,
                       void * new_value) const;
    virtual void * setup(Epoch old_epoch, Epoch new_epoch, void * new_value);
    virtual void commit(Epoch new_epoch, void * setup_data) throw ();
    virtual void rollback(Epoch new_epoch, void * local_data,
                          void * setup_data) throw ();
    

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

#include "typed_pvo_impl.h"
