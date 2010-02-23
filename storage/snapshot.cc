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

        int return_code = -1;
        try {
            return_code = worker(sockets[0]);
        }
        catch (const std::exception & exc) {
            cerr << "child exiting with exception " << exc.what() << endl;
        }
        catch (...) {
            cerr << "child exiting with unknown exception " << endl;
        }

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

void
Snapshot::
send_message(const char * data, size_t sz)
{
    int res = write(itl->control_fd, data, sz);
    if (res != sz)
        throw Exception("write: " + string(strerror(errno)));
}

std::string
Snapshot::
recv_message()
{
    char buffer[65536];
    int found = read(itl->control_fd, buffer, 65536);
    if (found == -1)
        throw Exception("read: " + string(strerror(errno)));
    if (found == 65536)
        throw Exception("read: message was too long");

    string result(buffer, buffer + found);
    return result;
}

void
Snapshot::
recv_message(char * data, size_t sz, bool all_at_once)
{
    //cerr << getpid() << " recv_message of " << sz << " bytes" << endl;
    int found = read(itl->control_fd, data, sz);
    if (found == -1)
        throw Exception("read: " + string(strerror(errno)));
    if (found != sz) {
        cerr << "wanted: " << sz << endl;
        cerr << "found:  " << found << endl;
        throw Exception("read: message was not long enough");
    }
}

void
Snapshot::
send(const std::string & str)
{
    if (str.length() > 65536)
        throw Exception("string too long to send");
    send<int>(str.length());
    send_message(str.c_str(), str.length());
}

void
Snapshot::
recv(std::string & str)
{
    int sz = recv<int>();
    if (sz < 0 || sz > 65536)
        throw Exception("string too long to receive");
    str.resize(sz);
    recv_message(&str[0], sz);
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
    if (file_offset % page_size != 0)
        throw Exception("file offset not on a page boundary");

    size_t mem = (size_t)mem_start;
    if (mem % page_size != 0)
        throw Exception("mem_start not on a page boundary");

    if (mem_size % page_size != 0)
        throw Exception("mem_size not a multiple of page_size");

    off_t res = lseek(fd, file_offset, SEEK_SET);

    if (res != file_offset)
        throw Exception("lseek failed: " + string(strerror(errno)));

    send('d');

    send(fd);
    send(file_offset);
    send(mem_start);
    send(mem_size);

    ssize_t result = recv<size_t>();
    
    if (result == -1) {
        string error = recv_message();
        
        throw Exception("dump_memory(): snapshot process returned error: "
                        + error);
    }
        
    return result;
}

void
Snapshot::
client_dump_memory()
{
    try {
        int          fd          = recv<int>();
        size_t       file_offset = recv<size_t>();
        const char * mem_start   = recv<char *>();
        size_t       mem_size    = recv<size_t>();
        
        // Now go and do it
        size_t result = 0;
        
        size_t npages = mem_size / page_size;
        
        off_t res = lseek(fd, file_offset, SEEK_SET);
        
        if (res != file_offset)
            throw Exception("lseek failed: " + string(strerror(errno)));
        
        for (unsigned i = 0;  i < npages;  ++i, mem_start += page_size) {
            int res = write(fd, mem_start, page_size);
            if (res != page_size)
                throw Exception("write != page_size");
            result += res;
        }

        send(result);
    }
    catch (const std::exception & exc) {
        ssize_t result = -1;
        send(result);
        std::string message = exc.what();
        send(message);
    }
    catch (...) {
        ssize_t result = -1;
        send(result);
        std::string message = "unknown exception caught";
        send(message);
    }
}

size_t
Snapshot::
sync_to_disk(int fd,
             size_t file_offset,
             void * mem_start,
             size_t mem_size)
{
    // TODO: copy and pasted

    if (file_offset % page_size != 0)
        throw Exception("file offset not on a page boundary");

    size_t mem = (size_t)mem_start;
    if (mem % page_size != 0)
        throw Exception("mem_start not on a page boundary");

    if (mem_size % page_size != 0)
        throw Exception("mem_size not a multiple of page_size");

    off_t res = lseek(fd, file_offset, SEEK_SET);

    if (res != file_offset)
        throw Exception("lseek failed: " + string(strerror(errno)));

    send('s');

    send(fd);
    send(file_offset);
    send(mem_start);
    send(mem_size);

    ssize_t result = recv<size_t>();
    
    if (result == -1) {
        string error = recv_message();
        
        throw Exception("sync_to_disk(): snapshot process returned error: "
                        + error);
    }
        
    return result;
}

void
Snapshot::
client_sync_to_disk()
{
    try {
        int          fd          = recv<int>();
        size_t       file_offset = recv<size_t>();
        const char * mem_start   = recv<char *>();
        size_t       mem_size    = recv<size_t>();
        
        // Now go and do it
        size_t result = 0;
        
        size_t npages = mem_size / page_size;
        
        off_t res = lseek(fd, file_offset, SEEK_SET);
        
        if (res != file_offset)
            throw Exception("lseek failed: " + string(strerror(errno)));
        
        for (unsigned i = 0;  i < npages;  ++i, mem_start += page_size) {
            int res = write(fd, mem_start, page_size);
            if (res != page_size)
                throw Exception("write != page_size");
            result += res;
        }

        send(result);
    }
    catch (const std::exception & exc) {
        ssize_t result = -1;
        send(result);
        std::string message = exc.what();
        send(message);
    }
    catch (...) {
        ssize_t result = -1;
        send(result);
        std::string message = "unknown exception caught";
        send(message);
    }
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

    //cerr << "Snapshot::terminate(): res = " << res
    //     << " status = " << status << endl;

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
    itl->control_fd = control_fd;

    while (true) {
        char c;
        int res = read(control_fd, &c, 1);
        if (res == 0)
            return 0;

        if (res != 1) {
            cerr << "Snapshot: child read returned " << strerror(errno)
                 << endl;
            return -1;
        }

        if (c == 'd')
            client_dump_memory();
        else if (c == 's')
            client_sync_to_disk();
        else {
            cerr << "Snapshot: child got unknown command "
                 << c << endl;
            return -1;
        }
    }
}


} // namespace RS
