/* sandbox.cc
   Jeremy Barnes, 12 December 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Implementation of the sandbox.
*/

#include "sandbox.h"
#include "transaction.h"
#include "jml/arch/atomic_ops.h"
#include "jml/arch/demangle.h"

using namespace std;
using namespace ML;

namespace JMVCC {


/*****************************************************************************/
/* SANDBOX                                                                   */
/*****************************************************************************/

Sandbox::
~Sandbox()
{
    clear();
}

struct Sandbox::Free_Values {
    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (!entry.automatic)
            obj->destroy_local_value(entry.val);
        return true;
    }
};

void
Sandbox::
clear()
{
    local_values.do_in_order(Free_Values());
    local_values.clear();
}

struct Sandbox::Check_Values {
    Check_Values(Epoch old_epoch, Epoch new_epoch)
        : old_epoch(old_epoch), new_epoch(new_epoch)
    {
    }

    Epoch old_epoch, new_epoch;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;

        return obj->check(old_epoch, new_epoch, entry.val);
    }
};

struct Sandbox::Setup_Commit {
    Setup_Commit(Epoch old_epoch, Epoch new_epoch)
        : old_epoch(old_epoch), new_epoch(new_epoch)
    {
    }

    Epoch old_epoch, new_epoch;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        return obj->setup(old_epoch, new_epoch, entry.val);
    }
};

struct Sandbox::Commit {
    Commit(Epoch new_epoch)
        : new_epoch(new_epoch)
    {
    }

    Epoch new_epoch;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        obj->commit(new_epoch);
        return true;
    }
};

struct Sandbox::Rollback {
    Rollback(Epoch new_epoch)
        : new_epoch(new_epoch)
    {
    }
    
    Epoch new_epoch;
    
    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        obj->commit(new_epoch);
        return true;
    }
};

Epoch
Sandbox::
commit(Epoch old_epoch)
{
    Epoch new_epoch = get_current_epoch() + 1;

    bool result = true;

    // Check everything, before the lock is obtained
    Versioned_Object * failed_object
        = local_values.do_in_order(Check_Values(old_epoch, new_epoch));
    if (failed_object) {
        clear();
        return 0;
    }

    ACE_Guard<ACE_Mutex> guard(commit_lock);

    new_epoch = get_current_epoch() + 1;

    // Commit everything
    failed_object
        = local_values.do_in_order(Setup_Commit(old_epoch, new_epoch));

    if (!failed_object) {
        // The setup succeeded.  This means that the commit is guaranteed to
        // succeed.

        // First we update the epoch.  This ensures that any new snapshot
        // created will see the correct epoch value, and won't look at
        // old values which might not have a list.
        //
        // IT IS REALLY IMPORTANT THAT THIS BE DONE IN THE GIVEN ORDER.
        // If we were to update the epoch afterwards, then new transactions
        // could be created with the old epoch.  These transactions might
        // need the values being cleaned up, racing with the creation
        // process.
        set_current_epoch(new_epoch);

        // Make sure these writes are seen before we clean up
        memory_barrier();

        // Success: we are in a new epoch
        local_values.do_in_order(Commit(new_epoch));
    }
    else {
        // The setup failed.  We need to rollback everything that was setup.
        local_values.do_in_order(Rollback(new_epoch), 0, failed_object);
    }

    guard.release();

    // TODO: for failed transactions, we'd do better to keep the
    // structure to avoid reallocations
    // TODO: clear as we go to better use cache
    clear();
        
    return (result ? new_epoch : 0);
}

struct Sandbox::Dump_Value {
    Dump_Value(std::ostream & stream, const std::string & s)
        : stream(stream), s(s), i(0)
    {
    }

    std::ostream & stream;
    const std::string & s;
    int i;

    bool operator () (const Versioned_Object * obj, const Entry & entry)
    {
        if (entry.automatic) return true;

        stream << s << "  " << i << " at " << obj << ": value "
               << obj->print_local_value(entry.val)
               << endl;
        
        ++i;
        return true;
    }
};

void
Sandbox::
dump(std::ostream & stream, int indent) const
{
    string s(indent, ' ');
    stream << "sandbox: " << local_values.size() << " local values"
           << endl;
    local_values.do_in_order(Dump_Value(stream, s));
}

std::pair<void *, bool>
Sandbox::
set_local_value(Versioned_Object * obj, void * val)
{
    std::pair<Entry *, bool> inserted
        = local_values.insert(obj);
        
    void * old_value = 0;
        
    if (!inserted.second) {
        old_value = inserted.first->val;
        inserted.first->val = val;
    }
        
    return std::make_pair(old_value, !inserted.second);
}

std::ostream &
operator << (std::ostream & stream,
             const Sandbox::Entry & entry)
{
    return stream << entry.print();
}

} // namespace JMVCC
