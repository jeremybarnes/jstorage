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
    stream << entry.local;
    if (entry.local)
        stream << " " << type_name(*entry.local);
    stream << " ref " << entry.local.use_count() << " "
           << " offset ";
    if (entry.offset != PVOEntry::NO_OFFSET)
        stream << entry.offset;
    else stream << "<NONE>";
    if (entry.removed)
        stream << " REM";
    if (entry.removed_explicitly)
        stream << " EXP";
    stream << endl;
    return stream;
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
compact()
{
    for (int i = size() - 1;  i >= 0;  --i) {
        if (operator [] (i).removed)
            pop_back();
        else break;
    }
}

void *
PVOManagerVersion::
serialize(const PVOManagerVersion & obj,
          MemoryManager & mm)
{
    // TODO: exception safety

    size_t mem_needed = (obj.size() + 3) * 8;  // just a pointer each
    void * mem = mm.allocate_aligned(mem_needed, 8);

    try {
        reserialize(obj, mem, mm);
    } catch (...) {
        try {
            mm.deallocate(mem, mem_needed);
        }
        catch (...) {
        }

        throw;
    }

    return mem;
}

void
PVOManagerVersion::
reserialize(const PVOManagerVersion & obj,
            void * where,
            MemoryManager & mm)
{
    uint64_t * mem = (uint64_t *)where;

    if (obj.object_count() > obj.size())
        throw Exception("inconsistent version table");

    mem[0] = 0;  // version
    mem[1] = obj.size();  // size
    mem[2] = obj.object_count();
    
    mem += 3;

    //cerr << "reserialize at " << mem << endl;

    for (unsigned i = 0;  i < obj.size();  ++i) {
        //cerr << "object " << i << " offset " << obj[i].offset
        //     << " removed " << obj[i].removed << endl;

        if (obj[i].removed)
            mem[i] = PVOEntry::NO_OFFSET;
        else mem[i] = obj[i].offset;
    }
}

void
PVOManagerVersion::
reconstitute(PVOManagerVersion & obj,
             const void * mem,
             MemoryManager & mm)
{
    const uint64_t * md = (const uint64_t *)mem;
    uint64_t ver = md[0];

    if (ver != 0)
        throw Exception("how do we deallocate unknown version");


    uint64_t size = md[1];

    uint64_t object_count = md[2];

    if (!obj.empty())
        throw Exception("reconstitution over non-empty version table");

    obj.resize(size);
    obj.object_count_ = object_count;
    
    const uint64_t * data = md + 3;

    for (unsigned i = 0;  i < size;  ++i) {
        obj[i].offset = data[i];
    }
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

    
    size_t mem_needed = (size + 3) * 8;
    
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
    : Underlying(id, owner, current_trans != 0/* add_local */,
                 PVOManagerVersion())
{
}

size_t
PVOManager::
object_count() const
{
    return read().object_count();
}

void *
PVOManager::
set_persistent_version(ObjectId object, void * new_version)
{
    PVOManagerVersion & ver = mutate();
    if (object > ver.size())
        throw Exception("invalid object id");

    size_t old_offset = ver[object].offset;
    ver[object].offset = store()->to_offset(new_version);

    void * result = (old_offset == PVOEntry::NO_OFFSET
                     ? 0: store()->to_pointer(old_offset));

    //cerr << "set_persistent_version: object " << object << " goes from "
    //     << result << " to " << new_version << endl;
    return result;
}

bool
PVOManager::
check(Epoch old_epoch, Epoch new_epoch,
      void * new_value) const
{
    return Underlying::check(old_epoch, new_epoch, new_value);
}

void *
PVOManager::
setup(Epoch old_epoch, Epoch new_epoch, void * new_value)
{
    //cerr << "PVOManager setup: read() = " << &read()
    //     << " new_epoch = " << new_epoch << endl;
    return Underlying::setup(old_epoch, new_epoch, new_value);
}

void
PVOManager::
commit(Epoch new_epoch, void * setup_data) throw ()
{
    //cerr << "PVOManager commit: read() = " << &read() << " setup_data = "
    //     << setup_data << " new_epoch = " << new_epoch << endl;

    //dump(cerr);

    //cerr << "1.  compact" << endl;
    mutate().compact();

    //cerr << "2.  reserialize" << endl;
    // Setup_Data points to where our new data is
    // We need to record the actual values on this table
    PVOManagerVersion::reserialize(read(), setup_data, *store());

    PVOManagerVersion * new_table
        = (PVOManagerVersion *)current_trans->set_local_value(this, 0).first;

    PVOManagerVersion * old_table = set_last_value(new_table);
    delete old_table; // TODO: should be deleted later

    //cerr << "3.  Underlying" << endl;
    // Write the new table
    Underlying::commit(new_epoch, setup_data);

    //cerr << "at end of commit: " << endl;
    //dump(cerr);
}

void
PVOManager::
rollback(Epoch new_epoch, void * local_data, void * setup_data) throw ()
{
    // First, free all of the objects that had their value changed
    //rollback_new_values();

    // Now rollback the previous ones

    return Underlying::rollback(new_epoch, local_data, setup_data);
}

} // namespace JMVCC

