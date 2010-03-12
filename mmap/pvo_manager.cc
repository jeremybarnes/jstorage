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

bool
PVOManager::
check(Epoch old_epoch, Epoch new_epoch,
      void * new_value) const
{
    return Underlying::check(old_epoch, new_epoch, new_value);
}

bool
PVOManager::
setup(Epoch old_epoch, Epoch new_epoch, void * new_value)
{
    return Underlying::setup(old_epoch, new_epoch, new_value);
#if 0
    // Get the new version table to set it up
    PVOManagerVersion & new_version
        = *reinterpret_cast<PVOManagerVersion *>(new_value);
    
    // Get the old version table to compare against
    const PVOManagerVersion & old_version
        = read();

    // Get the old and the new table so that we can tell which objects
    // have new values and need to be serialized

    for (unsigned i = 0;  i < new_version.size();  ++i) {
        if (i >= old_version.size()) {
            // new version; serialize it
            bool serialized = ...;
        }
    }

    // First, serialize all of the objects that are either new or have
    // changed value
    const PVOManagerVersion & new_version
        = 

    Call_Guard guard(boost::bind(&PVOManager::rollback_new_values, this));

    // Serialize the new version table
    bool result = Underlying::setup(old_epoch, new_epoch, new_value);

    // If it succeeded, we don't need to roll back any of them
    if (result) guard.reset();

    return result;
#endif
}

void
PVOManager::
commit(Epoch new_epoch) throw ()
{
    // Firstly, arrange for all of the old versions to be cleaned up
    

    // Secondly, write the new table
    Underlying::commit(new_epoch);

}

void
PVOManager::
rollback(Epoch new_epoch, void * local_data) throw ()
{
    // First, free all of the objects that had their value changed
    //rollback_new_values();

    // Now rollback the previous ones

    return Underlying::rollback(new_epoch, local_data);
}

} // namespace JMVCC

