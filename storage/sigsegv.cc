/* sigsegv.cc
   Jeremy Barnes, 24 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Segmentation fault handlers.
*/

#include "sigsegv.h"
#include <ace/Synch.h>
#include "jml/arch/spinlock.h"
#include <signal.h>
#include "jml/arch/atomic_ops.h"
#include "jml/arch/exception.h"


using namespace ML;
using namespace std;

namespace RS {

struct Segv_Descriptor {
    Segv_Descriptor()
        : active(false), ref(0), start(0), end(0)
    {
    }

    bool matches(const void * addr) const
    {
        if (!active || ref == 0)
            return false;
        const char * addr2 = (const char *)addr;

        return addr2 >= start && addr2 < end;
    }

    volatile bool active;
    volatile int ref;
    const char * start;
    const char * end;
};


enum { NUM_SEGV_DESCRIPTORS = 64 };

Segv_Descriptor SEGV_DESCRIPTORS[NUM_SEGV_DESCRIPTORS];

Spinlock segv_lock;

size_t num_faults_handled = 0;

size_t get_num_segv_faults_handled()
{
    return num_faults_handled;
}

int register_segv_region(const void * start, const void * end)
{
    ACE_Guard<Spinlock> guard(segv_lock);
    
    // Busy wait until we get one
    int idx = -1;
    while (idx == -1) {
        for (unsigned i = 0;  i < NUM_SEGV_DESCRIPTORS && idx == -1;  ++i)
            if (SEGV_DESCRIPTORS[i].ref == 0) idx = i;
        
        if (idx == -1) sched_yield();
    }

    Segv_Descriptor & descriptor = SEGV_DESCRIPTORS[idx];
    descriptor.start  = (const char *)start;
    descriptor.end    = (const char *)end;
    descriptor.active = true;

    memory_barrier();

    descriptor.ref = 1;

    return idx;
}

void unregister_segv_region(int region)
{
    ACE_Guard<Spinlock> guard(segv_lock);

    if (region < 0 || region >= NUM_SEGV_DESCRIPTORS)
        throw Exception("unregister_segv_region(): invalid region");

    Segv_Descriptor & descriptor = SEGV_DESCRIPTORS[region];
    
    if (descriptor.ref == 0 || !descriptor.active)
        throw Exception("segv region is not active");
    
    descriptor.active = false;
    descriptor.start = 0;
    descriptor.end = 0;

    memory_barrier();

    atomic_add(descriptor.ref, -1);
}

void segv_handler(int signum, siginfo_t * info, void * context)
{
    if (signum != SIGSEGV) raise(signum);

    // We could do various things here to filter out the signal

    //ucontext_t * ucontext = (ucontext_t *)context;

    const char * addr = (const char *)info->si_addr;

    int region = -1;
    {
        ACE_Guard<Spinlock> guard(segv_lock);

        for (unsigned i = 0;  i < NUM_SEGV_DESCRIPTORS && region == -1;  ++i) {
            if (SEGV_DESCRIPTORS[i].matches(addr)) {
                region = i;
                atomic_add(SEGV_DESCRIPTORS[i].ref, 1);
            }
        }
    }

    if (region == -1) raise(signum);

    Segv_Descriptor & descriptor = SEGV_DESCRIPTORS[region];

    // busy wait for it to become inactive
    //timespec zero_point_one_ms = {0, 100000};

    // TODO: should we call nanosleep in a signal handler?
    // NOTE: doesn't do the right thing in linux 2.4; small nanosleeps are busy
    // waits

    while (descriptor.active) {
        //nanosleep(&zero_point_one_ms, 0);
    }

    atomic_add(descriptor.ref, -1);
    atomic_add(num_faults_handled, 1);
}

void install_segv_handler()
{
    // Install a segv handler
    struct sigaction action;

    action.sa_sigaction = segv_handler;
    action.sa_flags = SA_SIGINFO;

    int res = sigaction(SIGSEGV, &action, 0);

    if (res == -1)
        throw Exception(errno, "install_segv_handler()", "sigaction");
}


} // namespace RS
