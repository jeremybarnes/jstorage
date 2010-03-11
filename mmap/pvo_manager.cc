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

void *
PVOManagerVersion::
serialize(const PVOManagerVersion & obj,
          MemoryManager & mm)
{
    size_t mem_needed = (obj.size() + 3) * 8;  // just a pointer each
    uint64_t * mem
        = (uint64_t *)mm.allocate_aligned(mem_needed, 8);

    mem[0] = 0;  // version
    mem[1] = obj.size();  // size
    mem[2] = obj.object_count();  // size

    cerr << "serialize" << endl;
    cerr << "mem = " << mem << endl;
    cerr << "obj.size() = " << obj.size() << " obj.object_count() = "
         << obj.object_count() << endl;

    mem += 3;

    for (unsigned i = 0;  i < obj.size();  ++i)
        mem[i] = obj[i].offset;

    return mem - 3;
}

void
PVOManagerVersion::
reconstitute(PVOManagerVersion & obj,
             const void * mem,
             MemoryManager & mm)
{
    const uint64_t * md = (const uint64_t *)mem;
    uint64_t ver = md[0];

    cerr << "reconstitute" << endl;

    cerr << "mem = " << mem << endl;

    cerr << "ver = " << ver << endl;

    if (ver != 0)
        throw Exception("how do we deallocate unknown version");


    uint64_t size = md[1];

    cerr << "size = " << size << endl;

    uint64_t object_count = md[2];

    cerr << "object_count = " << object_count << endl;


    if (!obj.empty())
        throw Exception("reconstitution over non-empty version table");

    obj.resize(size);
    obj.object_count_ = object_count;
    
    const uint64_t * data = md + 3;

    for (unsigned i = 0;  i < size;  ++i)
        obj[i].offset = data[i];
}

void
PVOManagerVersion::
deallocate(void * mem, MemoryManager & mm)
{
    const uint64_t * md = (const uint64_t *)mem;
    uint64_t ver = md[0];
    uint64_t size = md[1];

    if (ver != 0)
        throw Exception("how do we deallocate unknown version");

    
    size_t mem_needed = (size + 2) * 8;
    
    mm.deallocate(mem, mem_needed);
}

std::ostream &
operator << (std::ostream & stream, const PVOManagerVersion & ver)
{
    return stream;
}


/*****************************************************************************/
/* PVO_MANAGER                                                               */
/*****************************************************************************/

PVOManager::
PVOManager(ObjectId id, PVOManager * owner)
    : Underlying(id, owner)
{
    cerr << "creating PVOManager " << this << " with ID " << id
         << " and owner " << owner << endl;
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

void
PVOManager::
set_persistent_version(ObjectId object, void * new_version)
{
    cerr << "set_persistent_version for " << object << endl;

    PVOManagerVersion & ver = mutate();
    if (object > ver.size())
        throw Exception("invalid object id");
    ver[object].offset = store()->to_offset(new_version);
}

} // namespace JMVCC

