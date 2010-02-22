/* storage_test.cc
   Jeremy Barnes, 18 February 2010
   Copyright (c) 2010 Recoset.  All rights reserved.

   Test of the storage functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>

#include <signal.h>


using namespace ML;
using namespace std;

using boost::unit_test::test_suite;

void dump_maps()
{
    std::ifstream stream("/proc/self/maps");

    cerr << string(60, '=') << endl;
    cerr << "maps" << endl;

    while (stream) {
        string s;
        std::getline(stream, s);
        cerr << s << endl;
    }

    cerr << string(60, '=') << endl;
    cerr << endl << endl;
}

struct Page_Info {
    Page_Info()
        : mapping(0), count(0), flags(0)
    {
    }

    union {
        struct {
            uint64_t pfn:55;
            uint64_t shift:6;
            uint64_t reserved:1;
            uint64_t swapped:1;
            uint64_t present:1;
        };
        struct {
            uint64_t swap_type:5;
            uint64_t swap_offset:50;
            uint64_t shift_:6;
            uint64_t reserved_:1;
            uint64_t swapped_:1;
            uint64_t present_:1;
        };
        uint64_t mapping;
    };

    std::string print_mapping() const
    {
        if (present)
            return format("P%c %09x s%5d", (swapped ? 'S' : '.'),
                          pfn, (int)shift);
        else if (swapped)
            return format(".S %02d/%06x s%5d", (int)swap_type, (int)swap_offset);
        else return "..                 ";
    }

    uint64_t count;

    union {
        struct {
            uint64_t locked:1;
            uint64_t error:1;
            uint64_t referenced:1;
            uint64_t uptodate:1;
            uint64_t dirty:1;
            uint64_t lru:1;
            uint64_t active:1;
            uint64_t slab:1;
            uint64_t writeback:1;
            uint64_t reclaim:1;
            uint64_t buddy:1;
            uint64_t mmap:1;
            uint64_t anon:1;
            uint64_t swapbacked:1;
            uint64_t compound_head:1;
            uint64_t compound_tail:1;
            uint64_t huge:1;
            uint64_t unevictable:1;
            uint64_t hwpoison:1;
            uint64_t nopage:1;
            uint64_t ksm:1;
            uint64_t unused:42;
        };
        uint64_t flags;
    };

    std::string print_flags() const
    {
        const char * letters = "lerudLaswRbmAShtHUPnk";
        string result;
        uint64_t val = 1;
        for (unsigned i = 0;  i < 22;  ++i) {
            bool f = flags & val;
            val <<= 1;
            if (f) result += letters[i];
            else result += '.';
        }

        result += format(" %06x", flags);

        return result;
    }

    std::string print() const
    {
        string result = print_mapping();
        if (pfn != 0)
            result = result + format("  c:%6d ", count)
                + print_flags();
        return result;
    }
};

std::vector<Page_Info> page_info(const void * addr, int npages)
{
    int pm_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pm_fd == -1)
        throw Exception("open pagemap; " + string(strerror(errno)));

    int pf_fd = open("/proc/kpageflags", O_RDONLY);
    if (pf_fd == -1)
        throw Exception("open pageflags: " + string(strerror(errno)));

    int pc_fd = open("/proc/kpagecount", O_RDONLY);
    if (pc_fd == -1)
        throw Exception("open pagecount: " + string(strerror(errno)));

    size_t page_num = (size_t)addr / 4096;

    int res = lseek(pm_fd, page_num * 8, SEEK_SET);
    if (res == -1)
        throw Exception("lseek");

    vector<Page_Info> result;
    for (unsigned i = 0;  i < npages;  ++i) {
        Page_Info info;

        int res = read(pm_fd, &info.mapping, 8);
        if (res != 8)
            throw Exception("read pm_fd");
     
        if (info.present) {
            int res = lseek(pf_fd, info.pfn * 8, SEEK_SET);
            if (res == -1)
                throw Exception("lseek pf_fd");
            res = read(pf_fd, &info.flags, 8);
            if (res != 8)
                throw Exception("read flags");

            res = lseek(pc_fd, info.pfn * 8, SEEK_SET);
            if (res == -1)
                throw Exception("lseek pc_fd");
            res = read(pc_fd, &info.count, 8);
            if (res != 8)
                throw Exception("read count");
        }

        result.push_back(info);
    }

    return result;
}

// Here, we make sure that a file that is mapped into the middle of another
// mapping really works.
BOOST_AUTO_TEST_CASE( test1 )
{
    // 1.  Create a file with data to test

    const char * fn1 = "test1.bin";

    int fd1 = open("test1.bin", O_CREAT | O_TRUNC | O_RDWR, 0666);

    if (fd1 == -1)
        throw Exception("error opening test.bin");

    Call_Guard close_fd1(boost::bind(close, fd1));
    Call_Guard unlink_fn1(boost::bind(unlink, fn1));

    int npages = 3;
    int page_size = 4096;

    const char * s1 = "0123456789abcdefghijklmnopqrstuv";
    const char * s2 = "abcdefghijklmnopqrstuv0123456789";

    for (unsigned i = 0;  i < npages;  ++i) {
        for (unsigned j = 0;  j < (page_size / 32);  ++j) {
            int res = write(fd1, s1, 32);
            if (res != 32) {
                throw Exception(string("res != 32: ") + strerror(errno));
            }
        }            
    }

    const char * fn2 = "test2.bin";
    int fd2 = open(fn2, O_CREAT | O_TRUNC | O_RDWR, 0666);

    if (fd2 == -1)
        throw Exception("error opening test.bin");

    Call_Guard close_fd2(boost::bind(close, fd2));
    Call_Guard unlink_fn2(boost::bind(unlink, fn2));

    for (unsigned i = 0;  i < npages;  ++i) {
        for (unsigned j = 0;  j < (page_size / 32);  ++j) {
            int res = write(fd2, s2, 32);
            if (res != 32) {
                throw Exception(string("res != 32: ") + strerror(errno));
            }
        }            
    }

    // 2.  Memory map the file, but don't page it in

    void * addr = mmap(0, npages * page_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE,
                       fd1, 0);

    //dump_maps();

    if (addr == 0)
        throw Exception("mmap failed");

    cerr << endl << " after mmap" << endl;
    vector<Page_Info> info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    Call_Guard munmap_fd1(boost::bind(munmap, addr, npages * page_size));

    // Check that we get a good result
    char buf[33];
    buf[32] = 0;

    char * p1 = (char *)addr;
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    char * p2 = p1 + page_size;
    std::copy(p2, p2 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);
    
    char * p3 = p2 + page_size;
    std::copy(p3, p3 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    cerr << endl << " after reading values" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    void * addr2 = mmap((char *)p2, 1 * page_size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED,
                        fd2, 0);

    if (addr2 == 0)
        throw Exception("mmap addr2 failed");

    //dump_maps();

    cerr << endl << " after map 2" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    BOOST_CHECK_EQUAL(addr2, (void *)p2);

    Call_Guard munmap_fd2(boost::bind(munmap, addr2, page_size));

    // Check that the new mapping was put in place in the middle of the old
    std::copy(p2, p2 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s2);
    
    // Check that the old mapping didn't disappear
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    std::copy(p3, p3 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    cerr << endl << " after reading values 2" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    // Now we modify the first page of file 1
    int res = lseek(fd1, 0, SEEK_SET);

    if (res == -1)
        throw Exception("error in seek: " + string(strerror(errno)));

    const char * s4 = "ABCDefghijklmnopqrstuv0123456789";

    for (unsigned i = 0;  i < 1;  ++i) {
        for (unsigned j = 0;  j < (page_size / 32);  ++j) {
            int res = write(fd1, s4, 32);
            if (res != 32) {
                throw Exception(string("res != 32: ") + strerror(errno));
            }
        }            
    }

    // Check that the changes are visible
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s4);

    cerr << endl << " after write to file" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    signal(SIGCHLD, SIG_DFL);

    // Now change it in a child fork
    int pid = fork();

    const char * s5 = "ABCDEFGHijklmnopqrstuv0123456789";

    cerr << "pid = " << pid << endl;

    if (pid == -1)
        throw Exception("fork failed");

    if (pid == 0) {
        // child process
        cerr << endl << " in child process" << endl;
        info = page_info(addr, 3);
        for (unsigned i = 0;  i < 3;  ++i) {
            cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
                 << info[i].print() << endl;
        }

        int res = lseek(fd1, 0, SEEK_SET);
        
        if (res == -1) {
            cerr << "error in seek: " << strerror(errno) << endl;
            kill(getpid(), SIGKILL);
        }
        
        for (unsigned i = 0;  i < 1;  ++i) {
            for (unsigned j = 0;  j < (page_size / 32);  ++j) {
                int res = write(fd1, s5, 32);
                if (res != 32) {
                    cerr << "error in write: " << strerror(errno) << endl;
                    kill(getpid(), SIGKILL);
                }
            }
        }

        close(fd1);
        
        cerr << "child done" << endl;

        // Kill so that the Boost testing script doesn't continue
        kill(getpid(), SIGKILL);
    }
    else {
        // parent process
        // We wait
        int status;
        cerr << "parent waiting; pid = " << pid << endl;

        pid_t res = waitpid(pid, &status, 0);

        cerr << "res = " << res << " status = " << status << endl;
    }
    
    // Check that the changes are visible (mapping still reflects the file)
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s5);

    // Write to the page to remove the underlying mapping

    res = lseek(fd1, 0, SEEK_SET);
    
    if (res == -1) {
        cerr << "error in seek: " << strerror(errno) << endl;
        kill(getpid(), SIGKILL);
    }
    
    for (unsigned i = 0;  i < 1;  ++i)
        for (unsigned j = 0;  j < (page_size / 32);  ++j)
            std::copy(s1, s1 + 32, p1 + i * page_size + j * 32);

    cerr << endl << " after COW" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    // Check that the changes are visible
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);
    
    // Now change it again in a child fork
    pid = fork();
    if (pid == -1)
        throw Exception("fork failed");

    if (pid == 0) {
        // child process
        int res = lseek(fd1, 0, SEEK_SET);
        
        if (res == -1) {
            cerr << "error in seek: " << strerror(errno) << endl;
            kill(getpid(), SIGKILL);
        }

        cerr << endl << " child fork 2" << endl;
        info = page_info(addr, 3);
        for (unsigned i = 0;  i < 3;  ++i) {
            cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
                 << info[i].print() << endl;
        }
        
        for (unsigned i = 0;  i < 1;  ++i) {
            for (unsigned j = 0;  j < (page_size / 32);  ++j) {
                int res = write(fd1, s4, 32);
                if (res != 32) {
                    cerr << "error in write: " << strerror(errno) << endl;
                    kill(getpid(), SIGKILL);
                }
            }
        }
        
        close(fd1);
        
        cerr << "child done" << endl;

        // Kill so that the Boost testing script doesn't continue
        kill(getpid(), SIGKILL);
    }
    else {
        // parent process
        // We wait
        int status;
        cerr << "parent waiting; pid = " << pid << endl;

        pid_t res = waitpid(pid, &status, 0);

        cerr << "res = " << res << " status = " << status << endl;
    }

    // Check that the changes are visible
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    cerr << endl << " after fork finished" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    // Finally, re-map the page
    void * addr3 = mmap((char *)p1, 1 * page_size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED,
                        fd1, 0);

    cerr << endl << " after remap" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

    if (addr3 == 0)
        throw Exception("mmap addr2 failed");

    BOOST_CHECK_EQUAL(addr3, (void *)p1);
    
    // Check that the changes are visible
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s4);

    cerr << endl << " after remap acc" << endl;
    info = page_info(addr, 3);
    for (unsigned i = 0;  i < 3;  ++i) {
        cerr << format("%d %12x ", i, (const char *)addr + i * page_size)
             << info[i].print() << endl;
    }

}
