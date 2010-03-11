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
        size_t size)
        : mmap(creation, filename.c_str(), size)
    {
    }

    Itl(const boost::interprocess::open_only_t & creation,
        const std::string & filename)
        : mmap(creation, filename.c_str())
    {
    }

    boost::interprocess::managed_mapped_file mmap;
};


PVOStore::
PVOStore(const boost::interprocess::create_only_t & creation,
         const std::string & filename,
         size_t size)
    : PVOManager(0, this),
      itl(new Itl(creation, filename, size))
{
}

PVOStore::
PVOStore(const boost::interprocess::open_only_t & creation,
         const std::string & filename)
    : PVOManager(0, this),
      itl(new Itl(creation, filename))
{
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
    return itl->mmap.allocate_aligned(nbytes, alignment);
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
    return itl->mmap.deallocate(ptr);
}

} // namespace JMVCC
