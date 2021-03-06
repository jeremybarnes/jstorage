/* storage_test.cc
   Jeremy Barnes, 18 February 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of the storage functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/utils/file_functions.h"
#include "jml/utils/info.h"
#include "jml/arch/exception.h"
#include "jml/arch/vm.h"
#include "jml/utils/guard.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include "jgraph/storage/snapshot.h"
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <set>


using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

// TODO: test that this only runs from the parent.  For the moment, we need
// to look at the logfile and make sure that the line occurs only once with
// a single PID.
struct With_Global_Destructor {
    ~With_Global_Destructor()
    {
        cerr << "global destructor called from pid " << getpid() << endl;
    }

} global;

int test_snapshot_child(int & var, int control_fd)
{
    try {

        BOOST_CHECK_EQUAL(var, 0);

        // wait for the 'x' to say 'start'
        char buf = '0';
        int res = read(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);
        BOOST_REQUIRE_EQUAL(buf, 'x');
        
        BOOST_CHECK_EQUAL(var, 0);

        var = 1;

        buf = 'a';
        res = write(control_fd, &buf, 1);

        BOOST_REQUIRE_EQUAL(res, 1);

        res = read(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);
        BOOST_REQUIRE_EQUAL(buf, 'y');

        BOOST_CHECK_EQUAL(var, 1);

        buf = 'b';
        res = write(control_fd, &buf, 1);
        BOOST_REQUIRE_EQUAL(res, 1);

        return 0;
    } catch (const std::exception & exc) {
        cerr << "child: error " << exc.what() << endl;
        return 1;
    } catch (...) {
        cerr << "child: error" << endl;
        return 1;
    }
}

BOOST_AUTO_TEST_CASE( test_snapshot )
{
    // Stop boost from thinking that a child exiting is an error
    signal(SIGCHLD, SIG_DFL);

    int var = 0;

    Snapshot::Worker w
        = boost::bind(test_snapshot_child, boost::ref(var), _1);
    Snapshot s(w);

    BOOST_CHECK_EQUAL(var, 0);

    // write an "x" to say "start"
    char buf = 'x';
    int res = write(s.control_fd(), &buf, 1);
    
    BOOST_REQUIRE_EQUAL(res, 1);

    res = read(s.control_fd(), &buf, 1);
    BOOST_REQUIRE_EQUAL(res, 1);
    BOOST_REQUIRE_EQUAL(buf, 'a');

    var = 2;

    buf = 'y';
    res = write(s.control_fd(), &buf, 1);
    
    BOOST_REQUIRE_EQUAL(res, 1);
    
    res = read(s.control_fd(), &buf, 1);
    BOOST_REQUIRE_EQUAL(res, 1);
    BOOST_REQUIRE_EQUAL(buf, 'b');

    BOOST_CHECK_EQUAL(s.terminate(), 0);
}

struct Backed_Region {
    Backed_Region(const std::string & filename, size_t size, bool wipe = true)
    {
        fd = open(filename.c_str(), (wipe ? (O_CREAT | O_TRUNC) : 0) | O_RDWR,
                  0666);
        if (fd == -1)
            throw Exception("Backed_Region(): open + " + filename + ": "
                            + string(strerror(errno)));

        size_t sz = get_file_size(fd);

        if (!wipe && sz != size)
            throw Exception("backing file was wrong size");

        if (sz != size) {
            int res = ftruncate(fd, size);
            if (res == -1)
                throw Exception("truncate didn't work: " + string(strerror(errno)));
        }            

        void * addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

        if (addr == 0)
            throw Exception("mmap failed: " + string(strerror(errno)));

        data = (char *)addr;
        this->size = size;
    }

    void close()
    {
        if (data) munmap(data, size);
        data = 0;
        if (fd != -1) ::close(fd);
        fd = -1;
        size = 0;
    }
    
    ~Backed_Region()
    {
        close();
    }

    int fd;
    char * data;
    size_t size;
};

void set_page(char * data, int page_offset, const std::string & str)
{
    data += page_size * page_offset;

    for (unsigned i = 0;  i < page_size;  ++i) {
        int j = i % str.length();
        data[i] = str[j];
    }

    data[page_size - 1] = 0;
}

// This test case tests our ability to create a snapshot and to write an
// area of memory from that snapshot to disk.
BOOST_AUTO_TEST_CASE( test_backing_file )
{
    signal(SIGCHLD, SIG_DFL);

    int npages = 5;

    Call_Guard guard(boost::bind(unlink, "region1"));

    int files_open_before = num_open_files();

    // 1.  Create a backed regions
    Backed_Region region1("region1", npages * page_size);

    // 2.  Write to the first one
    const char * cs1 = "1abcdef\0";
    string s1(cs1, cs1 + 8);
    set_page(region1.data, 0, s1);
    set_page(region1.data, 1, s1);
    set_page(region1.data, 2, s1);
    set_page(region1.data, 3, s1);
    set_page(region1.data, 4, s1);
    
    // 3.  Create a snapshot
    Snapshot snapshot1;

    // 4.  Write the snapshot to region1
    size_t written
        = snapshot1.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                 Snapshot::DUMP);
    
    BOOST_CHECK_EQUAL(written, npages * page_size);

    // 5.  Re-map it and check that it gave the correct data
    Backed_Region region1a("region1", npages * page_size, false);

    BOOST_CHECK_EQUAL_COLLECTIONS(region1.data, region1.data + region1.size,
                                  region1a.data, region1a.data + region1a.size);

    BOOST_CHECK_EQUAL(snapshot1.terminate(), 0);

    region1.close();
    region1a.close();

    // Make sure that everything was properly closed
    BOOST_CHECK_EQUAL(files_open_before, num_open_files());
}

namespace RS {

} // namespace RS

// This test case makes sure that pages that weren't modified in the snapshot
// from the originally mapped file are not written to disk needlessly
// TODO: make sure that the number of mappings doesn't explode...

BOOST_AUTO_TEST_CASE( test_backing_file_efficiency )
{
    // Don't make boost::test think that processes exiting is a problem
    signal(SIGCHLD, SIG_DFL);

    int npages = 5;

    // So we can make sure that all file descriptors were returned
    int files_open_before = num_open_files();

    Call_Guard unlink_guard(boost::bind(unlink, "region1"));

    // 1.  Create a backed regions
    Backed_Region region1("region1", npages * page_size);

    // 2.  Write to 3 of the 5 pages
    const char * cs1 = "1abcdef\0";
    string s1(cs1, cs1 + 8);
    set_page(region1.data, 0, s1);
    set_page(region1.data, 2, s1);
    set_page(region1.data, 4, s1);

    dump_page_info(region1.data, region1.data + region1.size);

    cerr << "<=========== shapshot1" << endl;
    
    // 3.  Create a snapshot
    Snapshot snapshot1;

    // 4.  Sync changed pages to region1
    size_t written
        = snapshot1.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                 Snapshot::SYNC_ONLY);

    BOOST_CHECK_EQUAL(written, 3 * page_size);

    int pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (pagemap_fd == -1)
        throw Exception("opening /proc/self/pagemap: %s", strerror(errno));
    Call_Guard guard(boost::bind(close, pagemap_fd));

    cerr << "before reback:" << endl;
    dump_page_info(region1.data, region1.data + region1.size);

    size_t n_rebacked
        = Snapshot::
        reback_range_after_write(region1.fd, 0, region1.data, region1.size,
                                 snapshot1.pagemap_fd(), pagemap_fd);
    
    cerr << "after reback:" << endl;
    dump_page_info(region1.data, region1.data + region1.size);

    close(pagemap_fd);
    guard.clear();

    BOOST_CHECK_EQUAL(n_rebacked, 3 * page_size);

    written = snapshot1.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                     Snapshot::RECLAIM_ONLY);

    // 5.  Check that nothing is synced a second time
    written = snapshot1.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                     Snapshot::SYNC_ONLY);
    
    BOOST_CHECK_EQUAL(written, 0);

    BOOST_CHECK_EQUAL(snapshot1.terminate(), 0);

    set_page(region1.data, 1, s1);

    Snapshot snapshot2;

    written = snapshot2.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                     Snapshot::SYNC_AND_RECLAIM);
    
    BOOST_CHECK_EQUAL(written, (int)page_size);

    written = snapshot2.sync_to_disk(region1.fd, 0, region1.data, region1.size,
                                     Snapshot::SYNC_AND_RECLAIM);

    BOOST_CHECK_EQUAL(written, 0);

    // 5.  Re-map it and check that it gave the correct data
    Backed_Region region1a("region1", npages * page_size, false);

    BOOST_CHECK_EQUAL_COLLECTIONS(region1.data, region1.data + region1.size,
                                  region1a.data, region1a.data + region1a.size);

    BOOST_CHECK_EQUAL(snapshot2.terminate(), 0);

    region1.close();
    region1a.close();

    // Make sure that everything was properly closed
    BOOST_CHECK_EQUAL(files_open_before, num_open_files());
}

// This test case makes sure that pages that weren't modified in the snapshot
// from the originally mapped file are not written to disk needlessly
// TODO: make sure that the number of mappings doesn't explode...

BOOST_AUTO_TEST_CASE( test_backing_file_efficiency2 )
{
    // Don't make boost::test think that processes exiting is a problem
    signal(SIGCHLD, SIG_DFL);

    int npages = 5;

    Call_Guard guard(boost::bind(unlink, "region1"));

    // So we can make sure that all file descriptors were returned
    int files_open_before = num_open_files();

    // 1.  Create a backed region
    Backed_Region region1("region1", npages * page_size);

    // 2.  Write to some of the pages
    const char * cs1 = "1abcdef\0";
    string s1(cs1, cs1 + 8);

    set<int> pages_changed;

    for (unsigned i = 0;  i < npages;  ++i) {
        int page = random() % npages;
        set_page(region1.data, page, s1);
        pages_changed.insert(page);
    }

    cerr << "wrote to " << pages_changed.size() << " of " << npages
         << " pages" << endl;

    // 3.  Create a snapshot
    Snapshot snapshot1;

    cerr << endl << "before first sync" << endl;
    dump_page_info(region1.data, region1.data + region1.size);
    cerr << endl;

    // 4.  Sync changed pages to snapshot
    size_t written, rebacked, reclaimed;
    boost::tie(written, rebacked, reclaimed)
        = snapshot1.sync_and_reback(region1.fd, 0, region1.data, region1.size,
                                    Snapshot::RECLAIM);

    BOOST_CHECK_EQUAL(written,   pages_changed.size() * page_size);
    BOOST_CHECK_EQUAL(rebacked,  pages_changed.size() * page_size);
    BOOST_CHECK_EQUAL(reclaimed, pages_changed.size() * page_size);
    
    cerr << endl << "after first sync" << endl;
    dump_page_info(region1.data, region1.data + region1.size);
    cerr << endl;


    // Re-map it and check that it gave the correct data
    {
        Backed_Region region1a("region1", npages * page_size, false);
        
        BOOST_CHECK_EQUAL_COLLECTIONS(region1.data, region1.data + region1.size,
                                      region1a.data,
                                      region1a.data + region1a.size);
    }

    cerr << endl << "first sync and mapped" << endl;
    dump_page_info(region1.data, region1.data + region1.size);
    cerr << endl;

    // 5.  Check that nothing is synced a second time
    boost::tie(written, rebacked, reclaimed)
        = snapshot1.sync_and_reback(region1.fd, 0, region1.data, region1.size,
                                    Snapshot::RECLAIM);

    BOOST_CHECK_EQUAL(written,   0);
    BOOST_CHECK_EQUAL(rebacked,  0);
    BOOST_CHECK_EQUAL(reclaimed, 0);

    // Re-map it and check that it gave the correct data
    {
        Backed_Region region1a("region1", npages * page_size, false);
        
        BOOST_CHECK_EQUAL_COLLECTIONS(region1.data, region1.data + region1.size,
                                      region1a.data,
                                      region1a.data + region1a.size);
    }

    cerr << endl << "before changing pages again" << endl;
    dump_page_info(region1.data, region1.data + region1.size);
    cerr << endl;

    set<int> pages_changed2;
    
    string s2 = "wxywxywx";
    for (unsigned i = 0;  i < npages / 5;  ++i) {
        int page = random() % npages;
        set_page(region1.data, page, s2);
        pages_changed2.insert(page);
    }

    cerr << "wrote to " << pages_changed2.size() << " of " << npages
         << " pages" << endl;

    cerr << endl << "after changing pages again" << endl;
    dump_page_info(region1.data, region1.data + region1.size);
    cerr << endl;

    // Check that only the changed pages were written
    boost::tie(written, rebacked, reclaimed)
        = snapshot1.sync_and_reback(region1.fd, 0, region1.data, region1.size,
                                    Snapshot::RECLAIM);
    
    BOOST_CHECK_EQUAL(written,   pages_changed2.size() * page_size);
    BOOST_CHECK_EQUAL(rebacked,  pages_changed2.size() * page_size);
    BOOST_CHECK_EQUAL(reclaimed, pages_changed2.size() * page_size);


    BOOST_CHECK_EQUAL(snapshot1.terminate(), 0);


    // Re-map it and check that it gave the correct data
    {
        Backed_Region region1a("region1", npages * page_size, false);
        
        BOOST_CHECK(std::equal(region1.data, region1.data + region1.size,
                               region1a.data));
    }

    region1.close();

    // Make sure that everything was properly closed
    BOOST_CHECK_EQUAL(files_open_before, num_open_files());
}

// TODO: add a test where we simultaneously write all over the memory and
// make sure that the writing doesn't affect the snapshotting and vice-versa
