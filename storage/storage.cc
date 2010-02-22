/* storage.cc
   Jeremy Barnes, 22 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Storage manager code.
*/

#include "storage.h"
#include "jml/utils/file_functions.h"
#include "jml/arch/exception.h"


using namespace std;
using namespace ML;

namespace RS {

enum { page_size = 4096 };


/*****************************************************************************/
/* CHUNK_MANAGER                                                             */
/*****************************************************************************/

struct Chunk_Manager::Itl {
    Itl()
        : fd(0), size(0)
    {
    }

    ~Itl()
    {
        if (fd) close(fd);
    }

    int fd;
    void * start;
    size_t size;
};


Chunk_Manager::
Chunk_Manager()
    : itl(new Itl())
{
}

Chunk_Manager::
~Chunk_Manager()
{
}

std::string serror(const std::string & message)
{
    return message + ": " + string(strerror(errno));
}

void
Chunk_Manager::
init(const std::string & filename)
{
    // Open the file read-only
    int fd = open(filename.c_str(), O_RDONLY, 0666);
    
    if (fd == -1)
        throw Exception(serror("error opening " + filename));

    itl->fd = fd;

    size_t size = get_file_size(fd);
    if (size % page_size != 0)
        throw Exception("backing file size is not a multiple of the page size");

    itl->size = size;

    // Now memory-map it
    void * addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    if (addr == 0)
        throw Exception(serror("chunk manager init: mmap"));

    itl->start = addr;
}

void
Chunk_Manager::
create(const std::string & filename, size_t initial_size)
{
    if (initial_size % page_size != 0)
        throw Exception("initial_size must be a multiple of the page size");

    // Create the backing file
    int fd = open(filename.c_str(), O_CREAT | O_RDWR, 0666);

    if (fd == -1)
        throw Exception(serror("error opening " + filename));

    itl->fd = fd;

    int res = ftruncate(fd, initial_size);
    if (res == -1)
        throw Exception(serror("error expanding " + filename));
    
    itl->size = initial_size;

    // Now memory-map it
    void * addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    if (addr == 0)
        throw Exception(serror("chunk manager init: mmap"));

    itl->start = addr;
}

void
Chunk_Manager::
grow(size_t new_size)
{
    throw Exception("Chunk_Manager::grow(): not done yet");
}

size_t
Chunk_Manager::
snapshot()
{
}

int
Chunk_Manager::
wait_for_snapshot(size_t snapshot_id)
{
}

int
Chunk_Manager::
snapshot_finished(size_t snapshot_id)
{
}

void *
Chunk_Manager::
base() const
{
}

size_t
Chunk_Manager::
size() const
{
}


} // namespace RS
