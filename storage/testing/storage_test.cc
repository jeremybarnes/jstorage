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
#include <fcntl.h>
#include <errno.h>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>


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

    void * addr = mmap(0, npages * page_size, PROT_READ, MAP_PRIVATE,
                       fd1, 0);

    dump_maps();

    if (addr == 0)
        throw Exception("mmap failed");

    Call_Guard munmap_fd1(boost::bind(munmap, addr, npages * page_size));

    // Check that we get a good result
    char buf[33];
    buf[32] = 0;

    const char * p1 = (const char *)addr;
    std::copy(p1, p1 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    const char * p2 = p1 + page_size;
    std::copy(p2, p2 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);
    
    const char * p3 = p2 + page_size;
    std::copy(p3, p3 + 32, buf);
    BOOST_CHECK_EQUAL(string(buf), s1);

    void * addr2 = mmap((char *)p2, 1 * page_size,
                        PROT_READ, MAP_PRIVATE | MAP_FIXED,
                        fd2, 0);

    if (addr2 == 0)
        throw Exception("mmap addr2 failed");

    dump_maps();

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
}
