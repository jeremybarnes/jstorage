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
/* SANDBOX::LOCAL_VALUES                                                     */
/*****************************************************************************/

std::pair<Sandbox::Entry *, bool>
Sandbox::Local_Values::
insert(Versioned_Object * obj)
{
    iterator it = find(obj);
    if (it != end()) {
        if (it->first != obj)
            throw Exception("object pointed to wasn't correct object");
        return std::make_pair(&it->second, false);
    }

    Versioned_Object * parent = obj->parent();
    Entry * next_entry = 0;
    if (parent) next_entry = insert(parent).first;

    size_t size_before = size();

    Versioned_Object * prev_obj = 0;
    if (next_entry) {
        prev_obj = next_entry->prev;
        next_entry->prev = obj;
    }
    else {
        prev_obj = tail;
        tail = obj;
    }

    Entry * prev_entry = 0;
    if (prev_obj) prev_entry = &operator [] (prev_obj);

    if (prev_entry)
        prev_entry->next = obj;
    else head = obj;

    Entry * entry = &operator [] (obj);

    entry->next = parent;
    entry->prev = prev_obj;

    if (size() < size_before + 1)
        throw Exception("logic error in insert: size didn't change");

    return std::make_pair(entry, true);
}

template<class It, typename DoWhat, typename Self>
Versioned_Object *
Sandbox::Local_Values::
do_in_order(Self * self,
            DoWhat dowhat,
            Versioned_Object * start,
            Versioned_Object * finish)
{
    if (start != 0)
        throw ML::Exception("not starting at start");

    if (start == 0) start = self->head;

#if 0
    It it = self->begin();
    for (It e = self->end();
         it != e && it->first != finish;  ++it) {
        bool keep_going = dowhat(it->first, it->second);
        if (!keep_going) return it->first;
    }

    return 0;
#endif

    for (Versioned_Object * current = start;
         current && current != finish;
         /* no inc */) {
        It it = self->find(current);
        if (it == self->end())
            throw ML::Exception("invalid iteration chain");
        bool keep_going = dowhat(current, it->second);
        if (!keep_going) return current;
                
        current = it->second.next;
    }

    return 0;
}

template<typename DoWhat>
Versioned_Object *
Sandbox::Local_Values::
do_in_order(DoWhat dowhat,
            Versioned_Object * start,
            Versioned_Object * finish)
{
    return do_in_order<iterator>(this, dowhat, start, finish);
}

template<typename DoWhat>
Versioned_Object *
Sandbox::Local_Values::
do_in_order(DoWhat dowhat,
            Versioned_Object * start,
            Versioned_Object * finish) const
{
    return do_in_order<const_iterator>(this, dowhat, start, finish);
}


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
    Setup_Commit(Epoch old_epoch, Epoch new_epoch,
                 vector<void *> & commit_data)
        : old_epoch(old_epoch), new_epoch(new_epoch), commit_data(commit_data)
    {
    }

    Epoch old_epoch, new_epoch;
    vector<void *> & commit_data;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        void * result = obj->setup(old_epoch, new_epoch, entry.val);
        if (result) commit_data.push_back(result);
        return result;
    }
};

struct Sandbox::Commit {
    Commit(Epoch new_epoch, vector<void *> & commit_data)
        : new_epoch(new_epoch), commit_data(commit_data), index(0)
    {
    }

    Epoch new_epoch;
    vector<void *> & commit_data;
    int index;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        if (index >= commit_data.size())
            throw Exception("Sandbox::Commit: indexes out of range");
        obj->commit(new_epoch, commit_data[index++]);
        return true;
    }
};

struct Sandbox::Rollback {
    Rollback(Epoch new_epoch, vector<void *> & commit_data)
        : new_epoch(new_epoch), commit_data(commit_data), index(0)
    {
    }
    
    Epoch new_epoch;
    vector<void *> & commit_data;
    int index;

    bool operator () (Versioned_Object * obj, Entry & entry)
    {
        if (entry.automatic) return true;
        if (index >= commit_data.size())
            throw Exception("Sandbox::Commit: indexes out of range");
        obj->rollback(new_epoch, entry.val, commit_data[index++]);
        return true;
    }
};

Epoch
Sandbox::
commit(Epoch old_epoch)
{
    Epoch new_epoch = get_current_epoch() + 1;

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

    vector<void *> commit_data;
    commit_data.reserve(local_values.size());

    Setup_Commit setup_commit(old_epoch, new_epoch, commit_data);
    
    failed_object = local_values.do_in_order(setup_commit);

    bool commit_succeeded = !failed_object;

    if (commit_succeeded) {
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
        Commit commit(new_epoch, commit_data);
        local_values.do_in_order(commit);
    }
    else {
        // The setup failed.  We need to rollback everything that was setup.
        Rollback rollback(new_epoch, commit_data);
        local_values.do_in_order(rollback, 0, failed_object);
    }
    
    guard.release();
    
    // TODO: for failed transactions, we'd do better to keep the
    // structure to avoid reallocations
    // TODO: clear as we go to better use cache
    clear();
    
    return (commit_succeeded ? new_epoch : 0);
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
        stream << s << "  " << i << " at " << obj << " "
               << type_name(*obj) << " entry@ " << &entry
               << " prev " << entry.prev << " next " << entry.next
               << " parent " << obj->parent();
        if (entry.automatic)
            stream << " [AUTO]" << endl;
        else stream << " value " << entry.val
                    << " " << obj->print_local_value(entry.val)
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

size_t
Sandbox::
num_automatic_local_values() const
{
    size_t result = 0;

    for (Local_Values::const_iterator
             it = local_values.begin(),
             end = local_values.end();
         it != end;  ++it) {
        if (it->second.automatic)
            ++result;
    }

    return result;
}

std::ostream &
operator << (std::ostream & stream,
             const Sandbox::Entry & entry)
{
    return stream << entry.print();
}

} // namespace JMVCC
