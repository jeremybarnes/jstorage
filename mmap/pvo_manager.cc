/* pvo_manager.cc
   Jeremy Barnes, 6 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Persistent Versioned Object manager class.
*/

#include "pvo.h"
#include "pvo_manager.h"
#include "pvo_store.h"
#include "jml/arch/demangle.h"


using namespace std;
using namespace ML;


namespace JMVCC {

/*****************************************************************************/
/* PVO_ENTRY                                                                 */
/*****************************************************************************/

std::ostream & operator << (std::ostream & stream,
                            const PVOEntry & entry)
{
    using namespace std;
    return stream << entry.local << " ref " << entry.local.use_count() << " "
                  << type_name(*entry.local) << endl;
}


/*****************************************************************************/
/* PVO_MANAGER_VERSION                                                       */
/*****************************************************************************/

PVOManagerVersion::
PVOManagerVersion()
    : object_count_(0)
{
}

PVOManagerVersion::
PVOManagerVersion(const PVOManagerVersion & other)
    : Underlying(other), object_count_(other.object_count_)
{
}

PVOManagerVersion::
~PVOManagerVersion()
{
    clear();
}

void
PVOManagerVersion::
add(PVO * local)
{
    local->id_ = size();
    push_back(PVOEntry(local));
    ++object_count_;
}

std::pair<void *, size_t>
PVOManagerVersion::
serialize(PVOStore & store) const
{
    size_t mem_needed = (size() + 2) * 8;  // just a pointer each
    uint64_t * mem
        = (uint64_t *)store.allocate_aligned(mem_needed, 8);
    mem[0] = 0;  ++mem;  // version
    mem[0] = size();  ++mem;  // size
    for (unsigned i = 0;  i < size();  ++i)
        mem[i] = operator [] (i).offset;
    return std::make_pair(mem, mem_needed);
}

std::ostream &
operator << (std::ostream & stream, const PVOManagerVersion & ver)
{
    return stream;
}

std::pair<void *, size_t>
serialize(const PVOManagerVersion & version, PVOStore & store)
{
    return version.serialize(store);
}


/*****************************************************************************/
/* PVO_MANAGER                                                               */
/*****************************************************************************/

PVOManager::
PVOManager(ObjectId id, PVOManager * owner)
    : Underlying(id, owner)
{
}

PVO *
PVOManager::
lookup(ObjectId obj) const
{
    if (obj >= read().size())
        throw Exception("unknown object");
    
    return read().operator [] (obj).local.get();
}

size_t
PVOManager::
object_count() const
{
    return read().object_count();
}

void *
PVOManager::
allocate_aligned(size_t nbytes, size_t alignment)
{
    return store()->allocate_aligned(nbytes, alignment);
}

} // namespace JMVCC

