/* snapshot.cc
   Jeremy Barnes, 22 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#include "snapshot.h"
#include <boost/bind.hpp>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "jml/arch/exception.h"
#include <iostream>


using namespace std;
using namespace ML;


namespace RS {


/*****************************************************************************/
/* SNAPSHOT                                                                  */
/*****************************************************************************/

struct Snapshot::Itl {
    pid_t pid;
    int control_fd;
};

Snapshot::
Snapshot(Worker worker)
    : itl(new Itl())
{
    if (!worker)
        worker = boost::bind(&Snapshot::run_child, this, _1);
    
    // Create the socket pair to communicate

    int sockets[2];
    int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    if (res == -1) {
    }

    pid_t pid = fork();

    if (pid == -1)
        throw Exception("error in fork");

    else if (pid == 0) {
        /* We are the child.  Do cleaning up here */
        // ...

        close(sockets[1]);

        int return_code = worker(sockets[0]);

        // Now die.  We do it this way to avoid any destructors running, etc
        // which might do things that we don't want them to and interfere with
        // the parent process.
        _exit(return_code);
    }

    close(sockets[0]);

    itl->pid = pid;
    itl->control_fd = sockets[1];
}

Snapshot::
~Snapshot()
{
    if (!itl->pid)
        return;

    int return_code = terminate();

    if (return_code != 0)
        cerr << "warning: snapshot termination returned " << return_code
             << endl;
}

int
Snapshot::
control_fd() const
{
    return itl->control_fd;
}

Snapshot &
Snapshot::
current()
{
    throw Exception("not done");
}

Snapshot::Remote_File
Snapshot::
create_file(const std::string & filename,
            size_t size)
{
    throw Exception("not done");
}

void
Snapshot::
close_file(const Remote_File & file)
{
    throw Exception("not done");
}

size_t
Snapshot::
dump_memory(int fd,
            size_t file_offset,
            void * mem_start,
            size_t mem_size)
{
    // TODO: use mincore() as a first check; those pages that aren't in core
    // are surely not modified (unless they are swapped out)

    // mincore + /proc/self/pagemap should give us what we want

    throw Exception("not done");
}

void
Snapshot::
sync_memory(const Remote_File & file,
            size_t file_offset,
            void * mem_start,
            void * mem_size,
            const Snapshot & old_snapshot,
            void * mem_start_old,
            size_t mem_size_old)
{
    throw Exception("not done");
}

int
Snapshot::
terminate()
{
    if (!itl->pid)
        throw Exception("Snapshot::terminate(): already terminated");

    int res = close(itl->control_fd);
    if (res == -1)
        cerr << "warning: Snapshot::terminate(): close returned "
             << strerror(errno) << endl;

    int status = -1;
    res = waitpid(itl->pid, &status, 0);

    cerr << "Snapshot::terminate(): res = " << res
         << " status = " << status << endl;

    if (res != itl->pid) {
        cerr << "warning: Snapshot::terminate(): waitpid returned pid "
             << res << " status " << status << endl;
    }
    
    itl->pid = 0;

    return status;
}

void
Snapshot::
disassociate()
{
    throw Exception("not done");
}

int
Snapshot::
run_child(int control_fd)
{
    char buffer[65536];

    while (true) {
        int nread = read(control_fd, &buffer, 65536);
        
        if (nread == -1) {
            cerr << "Snapshot::run_child(): read error: "
                 << strerror(errno) << endl;
            return 1;
        }

        if (nread == 0)
            // closed; get out of here
            return 0;
        
        cerr << "child: got " << nread << " bytes from parent"
             << endl;
    }
}

} // namespace RS
