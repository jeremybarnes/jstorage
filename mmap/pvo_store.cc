/* pvo_store.cc
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object manager class.
*/

#include "pvo_store.h"
#include "pvo_manager.h"
#include <boost/interprocess/managed_mapped_file.hpp>


using namespace std;


namespace JMVCC {

/*****************************************************************************/
/* PVO_STORE                                                                 */
/*****************************************************************************/

struct PVOStore::Itl {
    Itl(const boost::interprocess::create_only_t & creation,
        const std::string & filename,
        size_t size,
        PVOStore & owner)
        : owner(owner), mmap(creation, filename.c_str(), size)
    {
        root_offset = mmap.construct<uint64_t>("Root")(0);
    }

    void bootstrap_create()
    {
        void * ptr = PVOManagerVersion::serialize(owner.exclusive(),
                                                  *owner.store());
        size_t offset = (const char *)ptr - (const char *)mmap.get_address();
        *root_offset = offset;
    }

    Itl(const boost::interprocess::open_only_t & creation,
        const std::string & filename,
        PVOStore & owner)
        : owner(owner), mmap(creation, filename.c_str())
    {
        size_t num_objects;
        boost::tie(root_offset, num_objects)
            = mmap.find<uint64_t>("Root");
        
        if (root_offset == 0 || num_objects != 1)
            throw Exception("couldn't find root offset");

        if (*root_offset == 0)
            throw Exception("root_offset wasn't properly set");
    }

    void bootstrap_open()
    {
        const void * mem = (const char *)mmap.get_address() + *root_offset;

        // Bootstrap the initial version into existence
        PVOManagerVersion::reconstitute(owner.exclusive(), mem, *owner.store());
    }

    PVOStore & owner;
    boost::interprocess::managed_mapped_file mmap;
    uint64_t * root_offset;
};


PVOStore::
PVOStore(const boost::interprocess::create_only_t & creation,
         const std::string & filename,
         size_t size)
    : PVOManager(ROOT_OBJECT_ID, this),
      itl(new Itl(creation, filename, size, *this))
{
    itl->bootstrap_create();
}

PVOStore::
PVOStore(const boost::interprocess::open_only_t & creation,
         const std::string & filename)
    : PVOManager(ROOT_OBJECT_ID, this),
      itl(new Itl(creation, filename, *this))
{
    itl->bootstrap_open();
}

PVOStore::
~PVOStore()
{
}

PVOStore *
PVOStore::
store() const
{
    return const_cast<PVOStore *>(this);
}

void *
PVOStore::
allocate_aligned(size_t nbytes, size_t alignment)
{
    size_t free_before = itl->mmap.get_free_memory();
    void * result = itl->mmap.allocate_aligned(nbytes, alignment);
#if 1
    cerr << "allocated " << nbytes << " bytes (really "
         << free_before - itl->mmap.get_free_memory()
         << ") at alignment " << alignment << " returned "
         << result << " offset " << to_offset(result) << endl;
#endif
    return result;
}

size_t
PVOStore::
to_offset(void * pointer) const
{
    return (const char *)pointer - (const char *)itl->mmap.get_address();
}

void *
PVOStore::
to_pointer(size_t offset) const
{
    return (char *)itl->mmap.get_address() + offset;
}

void
PVOStore::
deallocate(void * ptr, size_t bytes)
{
    cerr << "deallocated " << bytes << " bytes at " << ptr << endl;
    return itl->mmap.deallocate(ptr);
}

uint64_t
PVOStore::
get_free_memory() const
{
    return itl->mmap.get_free_memory();
}

void *
PVOStore::
set_persistent_version(ObjectId object, void * new_version)
{
    if (object == ROOT_OBJECT_ID) {
        size_t old_offset = *itl->root_offset;
        void * result = to_pointer(old_offset);
        size_t new_offset
            = (const char *)new_version - (const char *)itl->mmap.get_address();
        cerr << "ROOT object offset goes from " << old_offset << " to "
             << new_offset << endl;
        //cerr << "old_offset = " << *itl->root_offset << endl;
        //cerr << "new_offset = " << new_offset << endl;
        *itl->root_offset = new_offset;
        return result;
    }

    return PVOManager::set_persistent_version(object, new_version);
}

PVO *
PVOStore::
parent() const
{
    return 0;
}

} // namespace JMVCC
