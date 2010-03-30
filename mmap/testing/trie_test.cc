/* trie_test.cc
   Jeremy Barnes, 20 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of the trie functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jml/arch/exception_handler.h"
#include "jml/arch/vm.h"
#include "jml/utils/unnamed_bool.h"
#include "jml/utils/testing/testing_allocator.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <iostream>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>
#include <bitset>
#include <map>


using namespace ML;
using namespace std;

struct TrieNode;
struct TrieLeaf;
struct TrieState;
struct TriePtr;

/** Base class for an allocator.  */
struct TrieAllocator {
    virtual ~TrieAllocator() {}

    virtual void * allocate(size_t bytes) = 0;
    virtual void deallocate(void * mem, size_t bytes) = 0;
    
    template<typename T>
    T * create()
    {
        size_t bytes = sizeof(T);
        void * addr = allocate(bytes);
        try {
            return new (addr) T();
        } catch (...) {
            deallocate(addr, bytes);
            throw;
        }
    }

    template<typename T, typename Arg1>
    T * create(const Arg1 & arg1)
    {
        size_t bytes = sizeof(T);
        void * addr = allocate(bytes);
        try {
            return new (addr) T(arg1);
        } catch (...) {
            deallocate(addr, bytes);
            throw;
        }
    }

    template<typename T, typename Arg1, typename Arg2>
    T * create(const Arg1 & arg1, const Arg2 & arg2)
    {
        size_t bytes = sizeof(T);
        void * addr = allocate(bytes);
        try {
            return new (addr) T(arg1, arg2);
        } catch (...) {
            deallocate(addr, bytes);
            throw;
        }
    }

    template<typename T, typename Arg1, typename Arg2, typename Arg3>
    T * create(const Arg1 & arg1, const Arg2 & arg2, const Arg3 & arg3)
    {
        size_t bytes = sizeof(T);
        void * addr = allocate(bytes);
        try {
            return new (addr) T(arg1, arg2, arg3);
        } catch (...) {
            deallocate(addr, bytes);
            throw;
        }
    }

    template<typename T, typename Arg1, typename Arg2, typename Arg3,
             typename Arg4>
    T * create(const Arg1 & arg1, const Arg2 & arg2, const Arg3 & arg3,
               const Arg4 & arg4)
    {
        size_t bytes = sizeof(T);
        void * addr = allocate(bytes);
        try {
            return new (addr) T(arg1, arg2, arg3, arg4);
        } catch (...) {
            deallocate(addr, bytes);
            throw;
        }
    }

    template<typename T>
    void destroy(T * value)
    {
        value->~T();
        size_t bytes = sizeof(T);
        deallocate((char *)value, bytes);
    }
};

struct TrieKey {
    TrieKey(uint64_t val = 0)
        : bits_(val)
    {
    }

    TrieKey(const char * val)
    {
        for (unsigned i = 0;  i < 8;  ++i)
            chars_[i] = val[i];
    }

    void init(const TrieKey & key, int match_start, int match_width)
    {
        if (match_start < 0 || match_start >= 8)
            throw Exception("TrieKey::init(): invalid match start");
        if (match_width <= 0 || match_start + match_width > 8)
            throw Exception("TrieKey::init(): invalid match width");
        for (unsigned i = 0;  i < match_width;  ++i)
            chars_[i] = key[i + match_start];
    }

    unsigned char operator [] (unsigned index) const
    {
        if (index >= 8)
            throw Exception("TrieKey: invalid index");
        return chars_[index];
    }
 
    unsigned char & operator [] (unsigned index)
    {
        if (index >= 8)
            throw Exception("TrieKey: invalid index");
        return chars_[index];
    }

    static bool equal_ranges(const TrieKey & key1, int start1,
                             const TrieKey & key2, int start2,
                             int width)
    {
        if (width <= 0 || width > 8)
            throw Exception("TrieKey::equal_ranges(): invalid width");
        if (start1 < 0 || start1 + width > 8)
            throw Exception("TrieKey::equal_ranges(): invalid start1");
        if (start2 < 0 || start2 + width > 8)
            throw Exception("TrieKey::equal_ranges(): invalid start2");

        for (unsigned i = 0;  i < width;  ++i)
            if (key1[i + start1] != key2[i + start2]) return false;
        return true;
    }

    static bool less_ranges(const TrieKey & key1, int start1,
                            const TrieKey & key2, int start2,
                            int width)
    {
        if (width <= 0 || width > 8)
            throw Exception("TrieKey::less_ranges(): invalid width");
        if (start1 < 0 || start1 + width > 8)
            throw Exception("TrieKey::less_ranges(): invalid start1");
        if (start2 < 0 || start2 + width > 8)
            throw Exception("TrieKey::less_ranges(): invalid start2");

        for (unsigned i = 0;  i < width;  ++i) {
            if (key1[i + start1] < key2[i + start2]) return true;
            if (key1[i + start1] > key2[i + start2]) return false;
        }
        
        return false;
    }

    union {
        unsigned char chars_[8];
        uint64_t bits_;
    };

    bool operator == (const TrieKey & other) const
    {
        return bits_ == other.bits_;
    }

    bool operator != (const TrieKey & other) const
    {
        return ! operator == (other);
    }

    bool operator < (const TrieKey & other) const
    {
        return bits_ < other.bits_;
    }

    std::string print(int width = -1) const
    {
        std::string result;

        for (unsigned i = 0;  i < 8;  ++i) {
            if (i == width) result += "| ";
            result += format("%02x", chars_[i]);
            if (i != 7) result += ' ';
        }

        return result;
    }
};

std::ostream & operator << (std::ostream & stream, const TrieKey & key)
{
    return stream << key.print();
}

BOOST_AUTO_TEST_CASE( test_trie_key )
{
    {
        int i = 0;
        TrieKey key(i);
        BOOST_CHECK_EQUAL(key.print(), "00 00 00 00 00 00 00 00");
    }

    {
        const char * chars = "01234567";
        TrieKey key(chars);
        BOOST_CHECK_EQUAL(key.print(), "30 31 32 33 34 35 36 37");

        for (unsigned i = 0;  i < 8;  ++i)
            BOOST_CHECK_EQUAL(key[i], chars[i]);

        const TrieKey & key2 = key;
        for (unsigned i = 0;  i < 8;  ++i)
            BOOST_CHECK_EQUAL(key2[i], chars[i]);

        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(key[-1], Exception);
        BOOST_CHECK_THROW(key[8], Exception);
        BOOST_CHECK_THROW(key2[-1], Exception);
        BOOST_CHECK_THROW(key2[8], Exception);

        key[0] = 4;
        BOOST_CHECK_EQUAL(key.print(), "04 31 32 33 34 35 36 37");

        BOOST_CHECK_EQUAL(key, key);
        BOOST_CHECK_EQUAL(key, key2);

        BOOST_CHECK((key < key) == false);
        BOOST_CHECK((key < key2) == false);
    }

    {
        // Test equal, etc
        TrieKey key1("01230000");
        TrieKey key2("01231000");

        BOOST_CHECK(key1 != key2);
        BOOST_CHECK(key1 < key2);

        for (unsigned i = 0;  i < 7;  ++i)
            for (unsigned j = 1;  j < 8 - i;  ++j)
                BOOST_CHECK(TrieKey::equal_ranges(key1, i, key1, i, j));

        // Check that the first 4 characters are equal
        BOOST_CHECK(TrieKey::equal_ranges(key1, 0, key2, 0, 4));

        // Check that the first 5 characters are not equal
        BOOST_CHECK(!TrieKey::equal_ranges(key1, 0, key2, 0, 5));
    }
}

// Operations structure to do with the leaves.  Allows the client to control
// how the data is stored in the leaves.

struct TrieOps : public TrieAllocator {
    virtual ~TrieOps() {}

    // Create a new leaf branch that will contain the rest of the key
    // from here.
    virtual TriePtr new_branch(TrieState & state) = 0;

    // Free the given branch
    virtual void free(TriePtr ptr) = 0;

    // How many leaves are in this leaf branch?
    virtual uint64_t size(TriePtr ptr) const = 0;

    // Insert the new leaf into the given branch
    virtual TriePtr insert(TriePtr ptr, TrieState & state) = 0;

    // Expand the branch at ptr so that a new element can be inserted into it
    virtual TriePtr expand(TriePtr ptr, TrieState & state) = 0;

    // How many characters does this match?
    virtual int width(TriePtr ptr) const = 0;

    // How much memory does this leaf use?
    virtual size_t memusage(TriePtr ptr) const = 0;

    // Print a summary in a string
    virtual std::string print(TriePtr ptr) const = 0;
};

struct TriePtr {

    TriePtr()
        : bits(0)
    {
    }

    union {
        struct {
            uint64_t is_leaf:1;
            uint64_t type:2;
            uint64_t ptr:61;
        };
        uint64_t bits;
    };

    JML_IMPLEMENT_OPERATOR_BOOL(ptr);

    // Match the next part of the key, returning the pointer that matched.
    // If no pointer matched then return the null pointer.
    void match(TrieOps & ops, TrieState & state) const;
    
    // Insert the given key, updating the state as we go.  The returned
    // value is the new value that this TriePtr should take; it's used
    // (for example) when the current node gets too full and needs to be
    // reallocated.
    TriePtr insert(TrieOps & ops, TrieState & state);

    // How many characters does this pointer match?
    int width(TrieOps & ops) const;

    // How big is the tree?
    size_t size(TrieOps & ops) const;

    // Free everything associated with it
    void free(TrieOps & ops);

    // How much memory does it take up?
    size_t memusage(TrieOps & ops) const;

    // Extract the key to the given pointer
    void key(TrieOps & ops, uint16_t iterator, char * write_to) const;

    //private:
    template<typename T>
    void set_node(T * node)
    {
        is_leaf = 0;
        this->type = T::node_type;
        ptr = (uint64_t)node;
    }

    template<typename T>
    void set_leaf(T * node)
    {
        is_leaf = 1;
        this->type = T::node_type;
        ptr = (uint64_t)node;
    }

    template<typename T>
    void set_payload(T * node)
    {
        is_leaf = 1;
        type = 0;
        ptr = (uint64_t)node;
    }

    bool operator == (const TriePtr & other) const
    {
        return ptr == other.ptr;
    }

    bool operator != (const TriePtr & other) const
    {
        return ptr != other.ptr;
    }

    template<typename T>
    T * as() const
    {
        return (T *)ptr;
    }

    template<typename Node>
    void
    match_node_as(TrieOps & ops, TrieState & state) const;

    template<typename Node>
    void
    match_leaf_as(TrieOps & ops, TrieState & state) const;

    template<typename Node>
    TriePtr
    do_insert_as(TrieOps & ops, TrieState & state);

    template<typename Node>
    size_t
    do_size_as() const;

    template<typename T>
    int width_as() const;

    std::string print() const;

    std::string print(TrieOps & ops) const;
};

std::ostream & operator << (std::ostream & stream, const TriePtr & p)
{
    return stream << p.print();
}

// A path through a trie.  Gives the basis for an iterator.
struct TriePath {
    TriePath()
        : width_(0), depth_(0)
    {
    }

    TriePath(TriePtr root, int width)
        : width_(0), depth_(0)
    {
        push_back(root, width, 0);
    }

    struct Entry {
        TriePtr ptr;
        int16_t iterator;  // first element must be zero
        int16_t width;
    };

    Entry & back()
    {
        if (depth_ == 0)
            throw Exception("back() with empty TriePath");
        return entries_[depth_ - 1];
    }

    const Entry & back() const
    {
        if (depth_ == 0)
            throw Exception("back() with empty TriePath");
        return entries_[depth_ - 1];
    }

    int depth() const { return depth_; }
    int width() const { return width_; }

    // nmatched: ptr.width()
    void push_back(TriePtr ptr, int width, int iterator = 0)
    {
        if (depth_ > 8)
            throw Exception("too deep");
        if (width + width_ > 8)
            throw Exception("too wide");
        width_ += width;

        entries_[depth_].ptr = ptr;
        entries_[depth_].width = width;
        entries_[depth_].iterator = iterator;

        ++depth_;
    }

    void replace_at_depth(int depth, TriePtr ptr, int width, int iterator = 0)
    {
        cerr << "replace_at_depth: depth = " << depth << " ptr = " << ptr
             << " width = " << width << " iterator = " << iterator
             << endl;
        cerr << "entries[depth].ptr = " << entries_[depth].ptr << endl;
        cerr << "entries[depth].width = " << (int)entries_[depth].width << endl;
        cerr << "entries[depth].iterator = " << (int)entries_[depth].iterator
             << endl;

        if (depth < 0 || depth >= depth_)
            throw Exception("replace_at_depth: invalid depth");
        if (depth != depth_ - 1 && entries_[depth].width != width)
            throw Exception("replace_at_depth(): width doesn't match");
        
        width_ -= entries_[depth].width;
        width_ += width;

        entries_[depth].ptr = ptr;
        entries_[depth].width = width;
        entries_[depth].iterator = iterator;
    }

    const Entry & at_depth(int depth) const
    {
        if (depth < 0 || depth >= depth_)
            throw Exception("at_depth(): invalid depth");
        return entries_[depth];
    }

    void dump(std::ostream & stream, int indent = 0) const
    {
        string s(indent, ' ');
        stream << s << "path: " << endl;
        stream << s << "  width: " << width_ << endl;
        stream << s << "  parents (" << depth_ << "): " << endl;
        for (unsigned i = 0;  i < depth_;  ++i) {
            stream << s << "    " << i << ": "
                   << "w" << (int)entries_[i].width << " "
                   << "it" << (int)entries_[i].iterator << " "
                   << entries_[i].ptr << endl;
        }
    }

    void dump(std::ostream & stream, TrieOps & ops, int indent = 0) const
    {
        string s(indent, ' ');
        stream << s << "path: " << endl;
        stream << s << "  width: " << width_ << endl;
        stream << s << "  parents (" << depth_ << "): " << endl;
        for (unsigned i = 0;  i < depth_;  ++i) {
            stream << s << "    " << i << ": "
                   << "w" << (int)entries_[i].width << " "
                   << "it" << (int)entries_[i].iterator << " "
                   << entries_[i].ptr.print(ops) << endl;
        }
    }

private:
    int width_;             // Number of characters matched
    int depth_;             // Number of entries
    Entry entries_[8];
};

struct TrieState : public TriePath {

    TrieState(const TrieKey & key)
        : key(key)
    {
    }
    
    ~TrieState()
    {
    }

    TrieKey key;

    void dump(std::ostream & stream) const
    {
        stream << "state: " << endl;
        stream << "  key: " << key.print(width()) << endl;
        TriePath::dump(stream, 2);
    }

    void dump(std::ostream & stream, TrieOps & ops) const
    {
        stream << "state: " << endl;
        stream << "  key: " << key.print(width()) << endl;
        TriePath::dump(stream, ops, 2);
    }
};

std::ostream & operator << (std::ostream & stream, const TrieState & state)
{
    state.dump(stream);
    return stream;
}


/*****************************************************************************/
/* DENSE TRIE NODES                                                          */
/*****************************************************************************/

template<typename Payload>
struct DenseTrieBase {

    enum { node_type = 1 };

    static int width()
    {
        return 1;
    }

    typedef Payload value_type;

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    int16_t match(const TrieKey & key, int done_width) const
    {
        int index = key[done_width];
        if (!presence[index]) return -1;
        return index;
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    int16_t insert(const TrieKey & key, int done_width)
    {
        int index = key[done_width];
        if (!presence[index]) presence[index] = true;
        return index;
    }

    // Since we should always be able to insert into this one, it's an error
    // if it doesn't work.
    TriePtr expand(const TrieOps & ops, TrieState & state)
    {
        throw Exception("DenseTrieLeaf::expand(): should never be necessary");
    }

    void set_ptr(int16_t it, Payload new_ptr)
    {
        if (it < 0 || it >= 256)
            throw Exception("set_ptr with null pointer");
        if (!presence[it])
            throw Exception("iterator with no presence");
        children[it] = new_ptr;
    }

    bool not_null(int16_t it) const
    {
        if (it == -1)
            return false;
        if (it < 0 || it >= 256)
            throw Exception("set_ptr with null pointer");
        if (!presence[it])
            throw Exception("iterator with no presence");

        return true;
    }
    
    const value_type & dereference(int16_t it) const
    {
        if (it < 0 || it >= 256)
            throw Exception("set_ptr with null pointer");
        if (!presence[it])
            throw Exception("iterator with no presence");

        return children[it];
    }

    value_type & dereference(int16_t it)
    {
        if (it < 0 || it >= 256)
            throw Exception("set_ptr with null pointer");
        if (!presence[it])
            throw Exception("iterator with no presence");

        return children[it];
    }

    std::string print() const
    {
        string result = format("Dense node: population %zd", presence.count());
        return result;
    }

    std::bitset<256> presence;  // one bit per leaf; says if it's there or not
    Payload children[256];
};

struct DenseTrieNode : public DenseTrieBase<TriePtr> {

    void free_children(TrieOps & ops)
    {
        for (unsigned i = 0;  i < 256;  ++i)
            children[i].free(ops);
    }

    size_t memusage(TrieOps & ops) const
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage(ops);
        return result;
    }

    size_t size(TrieOps & ops) const
    {
        size_t result = 0;
        for (unsigned i = 0;  i < 256;  ++i)
            if (presence[i])
                result += children[i].size(ops);
        return result;
    }

};


/*****************************************************************************/
/* SINGLE TRIE NODE                                                          */
/*****************************************************************************/

template<typename Payload>
struct SingleTrieBase {

    SingleTrieBase()
        : width_(0)
    {
    }

    SingleTrieBase(const TrieKey & key, int done_width, int match_width,
                   const Payload & payload = Payload())
        : width_(match_width), payload_(payload)
    {
        for (unsigned i = 0;  i < match_width;  ++i)
            key_[i] = key[done_width + match_width];
    }

    typedef Payload value_type;

    enum { node_type = 2 };

    int width()
    {
        return width_;
    }

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    int16_t match(const TrieKey & key, int done_width)
    {
        cerr << "match() for " << print() << endl;
        cerr << "done_width " << done_width << " width_ = " << (int)width_
             << endl;
        cerr << "key = " << key.print(done_width) << endl;

        for (unsigned i = 0;  i < width_;  ++i) {
            if (key[i + done_width] != key_[i]) {
                cerr << "  DIDN'T MATCH ON " << i << endl;
                return -1;
            }
        }

        cerr << "MATCHED" << endl;

        return 0;
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    int16_t insert(const TrieKey & key, int done_width)
    {
        return match(key, done_width);
    }

    void set_ptr(int16_t it, Payload new_ptr)
    {
        if (it < -1 || it > 0)
            throw Exception("not_null(): invalid iterator");
        if (it != 0)
            throw Exception("set_ptr but pointer doesn't exist");
        payload_ = new_ptr;
    }

    bool not_null(int16_t it) const
    {
        if (it < -1 || it > 0)
            throw Exception("not_null(): invalid iterator");
        return it == 0;
    }
    
    const value_type & dereference(int16_t it) const
    {
        if (it < -1 || it > 0)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("dereferenced null ptr");
        return payload_;
    }

    value_type & dereference(int16_t it)
    {
        if (it < -1 || it > 0)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("dereferenced null ptr");
        return payload_;
    }

    std::string print() const
    {
        string result = format("Single node: width %d key ", width_)
            + key_.print();
        //result += " payload " + ostream_format(payload_);
        return result;
    }

    TriePtr expand(const TrieOps & ops, TrieState & state)
    {
        // Simply convert to a multi node, then insert it
        

        // Which character differs?
        int i = 0;
        for (;  i < width_;  ++i) {
            int idx = state.width() + i;
            if (state.key[idx] != key_[idx]) break;
        }

        cerr << "common prefix size = " << i << endl;

        if (i > 0) {
            // We have a prefix in common.  Make a new stem to match this
            // part.
            throw Exception("not finished: prefix in common");
        }

        // Create a node to split off, copying the suffix

        // shorten the chain
        

        throw Exception("single node needs to expand");

        // 1.  We create a common stem for whatever part is in common

        // 2.  We create a node to split off

        // 3.  We create two branches
    }

    uint8_t width_;
    TrieKey key_;
    Payload payload_;
};

struct SingleTrieNode : public SingleTrieBase<TriePtr> {

    void free_children(TrieOps & ops)
    {
        payload_.free(ops);
    }

    size_t memusage(TrieOps & ops) const
    {
        return sizeof(*this) + payload_.memusage(ops);
    }

    size_t size(TrieOps & ops) const
    {
        return payload_.size(ops);
    }
};


/*****************************************************************************/
/* MULTI TRIE NODE                                                           */
/*****************************************************************************/

template<typename Payload>
struct MultiTrieBase {

    MultiTrieBase(int width = 0)
        : width_(width), size_(0)
    {
    }

    typedef Payload value_type;

    enum { node_type = 3 };

    uint8_t width_;
    uint8_t size_;

    struct Entry {
        TrieKey key;
        Payload payload;
    };

    enum { NUM_ENTRIES = 15 };

    Entry entries_[NUM_ENTRIES];

    int width() const
    {
        return width_;
    }

    struct FindKey {
        FindKey(int done_width, int match_width)
            : done_width(done_width), match_width(match_width)
        {
        }

        int done_width, match_width;

        bool operator () (const Entry & entry, const TrieKey & key) const
        {
            return TrieKey::less_ranges(entry.key, 0, key,
                                        done_width, match_width);
        }
    };

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    int16_t match(const TrieKey & key, int done_width)
    {
        const Entry * entry
            = std::lower_bound(entries_, entries_ + size_,
                               key, FindKey(done_width, width_));
        if (entry == entries_ + size_
            || !TrieKey::equal_ranges(key, done_width, entry->key, 0, width_))
            return -1;
        
        return entry - entries_;
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    int16_t insert(const TrieKey & key, int done_width)
    {
        Entry * entry
            = std::lower_bound(entries_, entries_ + size_,
                               key, FindKey(done_width, width_));

        if (entry != entries_ + size_
            && TrieKey::equal_ranges(key, done_width, entry->key, 0, width_))
            return entry - entries_;

        if (size_ == NUM_ENTRIES) // full
            return -1;  // need to expand to another kind of node...

        // Move everything after forward
        for (Entry * it = entries_ + size_ - 1;  it >= entry;  --it)
            it[1] = it[0];

        // Initialize the new entry
        entry->key.init(key, done_width, width_);
        
        ++size_;

        return entry - entries_;
    }

    void set_ptr(int16_t it, Payload new_ptr)
    {
        if (it < -1 || it >= size_)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("set_ptr for null iterator");
        entries_[it].payload = new_ptr;
    }

    bool not_null(int16_t it) const
    {
        if (it < -1 || it >= size_)
            throw Exception("not_null(): invalid iterator");
        return it > -1;
    }
    
    const value_type & dereference(int16_t it) const
    {
        if (it < -1 || it >= size_)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("dereferenced null ptr");
        return entries_[it].payload;
    }

    value_type & dereference(int16_t it)
    {
        if (it < -1 || it >= size_)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("dereferenced null ptr");
        return entries_[it].payload;
    }

    TrieKey extract_key(int16_t it) const
    {
        if (it < -1 || it >= size_)
            throw Exception("not_null(): invalid iterator");
        if (it == -1)
            throw Exception("dereferenced null ptr");
        return entries_[it].key;
    }
    
    std::string print() const
    {
        string result = format("Multi node: width %d size %d", width_, size_);
        return result;
    }

    void dump(std::ostream & stream) const
    {
        stream << "Multi node: width=" << width() << " size = " << size()
               << endl;
        for (unsigned i = 0;  i < size_;  ++i)
            stream << "  " << entries_[i].key << " --> " << entries_[i].payload
                   << endl;
    }

    TriePtr expand(const TrieOps & ops, TrieState & state)
    {
        throw Exception("Multi Node Expand");
    }

    size_t size() const
    {
        return size_;
    }
};

struct MultiTrieNode : public MultiTrieBase<TriePtr> {

    void free_children(TrieOps & ops)
    {
        for (unsigned i = 0;  i < size_;  ++i)
            entries_[i].payload.free(ops);
    }

    size_t memusage(TrieOps & ops) const
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < size_;  ++i)
            result += entries_[i].payload.memusage(ops);
        return result;
    }

    size_t size(TrieOps & ops) const
    {
        size_t result = 0;
        for (unsigned i = 0;  i < size_;  ++i)
            result += entries_[i].payload.size(ops);
        return result;
    }
};

BOOST_AUTO_TEST_CASE( test_multi_trie_node )
{
    MultiTrieBase<uint64_t> node(8);  // width of 8
    BOOST_CHECK_EQUAL(node.width(), 8);
    BOOST_CHECK_EQUAL(node.size(), 0);

    int i = 0;
    TrieKey key1(i);
    int16_t place = node.match(key1, 0);
    BOOST_CHECK_EQUAL(place, -1);
    BOOST_CHECK(!node.not_null(place));

    {
        JML_TRACE_EXCEPTIONS(false);
        BOOST_CHECK_THROW(node.dereference(place), Exception);
    }

    place = node.insert(key1, 0);
    BOOST_CHECK_EQUAL(node.size_, 1);
    BOOST_CHECK_EQUAL(place, 0);
    BOOST_CHECK(node.not_null(place));
    node.dereference(place) = 10;
    BOOST_CHECK_EQUAL(node.dereference(place), 10);
    
    TrieKey key2("01234567");
    int place2 = node.match(key2, 0);
    BOOST_CHECK_EQUAL(place2, -1);
    place2 = node.insert(key2, 0);

    BOOST_CHECK_EQUAL(place2, 1);
    BOOST_CHECK_EQUAL(node.size_, 2);
    node.dereference(place2) = 20;

    BOOST_CHECK_EQUAL(node.dereference(node.match(key1, 0)), 10);
    BOOST_CHECK_EQUAL(node.dereference(node.match(key2, 0)), 20);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key1, 0)), 10);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key2, 0)), 20);

    TrieKey key3("01230000");

    BOOST_CHECK_EQUAL(node.match(key3, 0), -1);
    int place3 = node.insert(key3, 0);

    BOOST_CHECK_EQUAL(place3, 1);
    node.dereference(place3) = 15;
    BOOST_CHECK_EQUAL(node.match(key1, 0), 0);
    BOOST_CHECK_EQUAL(node.match(key2, 0), 2);
    BOOST_CHECK_EQUAL(node.match(key3, 0), 1);
    BOOST_CHECK_EQUAL(node.insert(key1, 0), 0);
    BOOST_CHECK_EQUAL(node.insert(key2, 0), 2);
    BOOST_CHECK_EQUAL(node.insert(key3, 0), 1);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key1, 0)), 10);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key3, 0)), 15);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key2, 0)), 20);
    
    TrieKey key4("01200000");

    BOOST_CHECK_EQUAL(node.match(key4, 0), -1);
    int place4 = node.insert(key4, 0);

    BOOST_CHECK_EQUAL(place4, 1);
    node.dereference(place4) = 12;
    BOOST_CHECK_EQUAL(node.match(key1, 0), 0);
    BOOST_CHECK_EQUAL(node.match(key2, 0), 3);
    BOOST_CHECK_EQUAL(node.match(key3, 0), 2);
    BOOST_CHECK_EQUAL(node.match(key4, 0), 1);
    BOOST_CHECK_EQUAL(node.insert(key1, 0), 0);
    BOOST_CHECK_EQUAL(node.insert(key2, 0), 3);
    BOOST_CHECK_EQUAL(node.insert(key3, 0), 2);
    BOOST_CHECK_EQUAL(node.insert(key4, 0), 1);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key1, 0)), 10);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key4, 0)), 12);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key3, 0)), 15);
    BOOST_CHECK_EQUAL(node.dereference(node.insert(key2, 0)), 20);

    // Stress test: different lengths; using a map to check results
    for (unsigned l = 1;  l <= 8;  ++l) {
        //cerr << "l = " << l << endl;
        
        MultiTrieBase<uint64_t> node(l);
        map<string, uint64_t> check_against;
        
        for (int i = 0;  i < MultiTrieBase<uint64_t>::NUM_ENTRIES + 5;  ++i) {
            // Create a new entry
            string key;
            for (unsigned j = 0;  j < l;  ++j)
                key.push_back(random());
            
            if (check_against.count(key)) {
                --i;
                continue;
            }
            
            if (i >= MultiTrieBase<uint64_t>::NUM_ENTRIES) {
                // insert should fail
                
                while (key.length() != 8)
                    key.push_back(random());

                TrieKey tkey(key.c_str());
                
                int place = node.insert(tkey, 0);
                BOOST_CHECK_EQUAL(place, -1);
                continue;
            }

            check_against[key] = i;

            // Add some junk to the end, which should be ignored
            while (key.length() != 8)
                key.push_back(random());

            TrieKey tkey(key.c_str());
            
            int place = node.insert(tkey, 0);
            BOOST_CHECK(node.not_null(place));
            node.dereference(place) = i;

            BOOST_CHECK_EQUAL(node.size(), i + 1);
        }

#if 0
        node.dump(cerr);
        cerr << endl;

        cerr << "map with " << check_against.size() << " entries" << endl;
        int i = 0;
        for (map<string, uint64_t>::const_iterator
                 it = check_against.begin(), end = check_against.end();
             it != end;  ++it, ++i) {
            string k = it->first;
            while (k.length() < 8)
                k.push_back(0);
            TrieKey key(k.c_str());
            cerr << "  " << i << " " << key << " --> " << it->second << endl;
        }
        // They should be sorted in the same order
#endif
        int i = 0;
        for (map<string, uint64_t>::const_iterator
                 it = check_against.begin(), end = check_against.end();
             it != end;  ++it, ++i) {
            uint64_t val = node.dereference(i);
            BOOST_CHECK_EQUAL(val, it->second);

            string s = it->first;
            while (s.length() < 8)
                s += ' ';
            TrieKey tkey(s.c_str());
            TrieKey k = node.extract_key(i);

            BOOST_CHECK(TrieKey::equal_ranges(tkey, 0, k, 0, l));
        }
    }
}


/*****************************************************************************/
/* TRIEPTR                                                                   */
/*****************************************************************************/

// Macro that will figure out what the type of the node is and coerce it into
// the correct type before calling the given method on it

#define TRIE_SWITCH_ON_NODE(action, args)                               \
    do {                                                                \
    switch (type) {                                                     \
    case DenseTrieNode::node_type: action<DenseTrieNode> args;  break;  \
    case SingleTrieNode::node_type: action<SingleTrieNode> args;  break;\
    case MultiTrieNode::node_type: action<MultiTrieNode> args;  break;  \
    default: throw Exception("unknown node type");                      \
    }                                                                   \
    } while (0)

size_t
TriePtr::
memusage(TrieOps & ops) const
{
    if (ptr == 0) return 0;
    else if (is_leaf) return ops.memusage(*this);
    else TRIE_SWITCH_ON_NODE(return as, ()->memusage(ops));
}

template<typename Node>
void
TriePtr::
match_node_as(TrieOps & ops, TrieState & state) const
{
    if (!ptr) return;

    const Node * node = as<Node>();

    typename Node::const_iterator found
        = node->match(state.key);

    typename Node::value_type result;
    if (node->not_null(found) && (result = node->dereference(found))) {
        state.push_back(result);
        result.match(state);
        return;
    }
    
    return;
}

void
TriePtr::
match(TrieOps & ops, TrieState & state) const
{
#if 0
    if (!ptr || state.depth == 8) return;

    if (is_leaf)
        TRIE_SWITCH_ON_LEAF(state.depth, match_leaf_as, (state));
    else TRIE_SWITCH_ON_NODE(state.depth, match_node_as, (state));
#endif
}

template<typename Node>
TriePtr
TriePtr::
do_insert_as(TrieOps & ops, TrieState & state)
{
    Node * node = as<Node>();

    //cerr << "do_insert_as: node = " << node << " depth = " << state.depth
    //     << endl;

    int depth = state.depth();

    if (!ptr) node = ops.create<Node>();

    int16_t found = node->insert(state.key, state.width());

    if (node->not_null(found)) {
        TriePtr child = TriePtr(node->dereference(found));
        state.push_back(child, node->width());
        
        TriePtr new_child = child.insert(ops, state);
        
        //cerr << "new_child = " << new_child << " child = " << child << endl;

        // Do we need to update our pointer?
        if (new_child != child) {
            //cerr << "new_child update" << endl;
            node->set_ptr(found, new_child);
            state.replace_at_depth(depth, new_child, node->width());
        }

        //cerr << "finished parent " << parent << " node = " << node
        //     << " this = " << *this << endl;

        TriePtr result;
        result.set_node(node);
        return result;
    }
    
    throw Exception("node insert failed; need to change to new kind of node");
}

TriePtr
TriePtr::
insert(TrieOps & ops, TrieState & state)
{
    // If there's nothing there then we ask for a new branch.  If there's a
    // node there then we ask for it to be inserted, otherwise we ask for
    // a new leaf to be inserted.

    //cerr << "insert: this = " << this << " is_leaf = " << is_leaf
    //     << " type = " << type << " ptr = " << ptr << endl;
    //cerr << "state = " << state << endl;

    if (!*this) return ops.new_branch(state);
    else if (is_leaf) return ops.insert(*this, state);
    else TRIE_SWITCH_ON_NODE(return do_insert_as, (ops, state));
}

size_t
TriePtr::
size(TrieOps & ops) const
{
    if (!ptr) return 0;
    if (is_leaf) return ops.size(*this);

    TRIE_SWITCH_ON_NODE(return as, ()->size(ops));
}

template<typename T>
void destroy_as(TriePtr & obj, TrieOps & ops)
{
    T * o = obj.as<T>();
    o->free_children(ops);
    ops.destroy(o);
    obj = TriePtr();
}

void
TriePtr::
free(TrieOps & ops)
{
    if (!ptr) return;
    if (is_leaf) ops.free(*this);
    else TRIE_SWITCH_ON_NODE(destroy_as, (*this, ops));
}

template<typename T>
int
TriePtr::
width_as() const
{
    T * o = as<T>();
    return o->width();
}

int
TriePtr::
width(TrieOps & ops) const
{
    if (!ptr) return 0;
    if (is_leaf) return ops.width(*this);
    TRIE_SWITCH_ON_NODE(return width_as, ());
}

std::string
TriePtr::
print() const
{
    string result = format("%012x %c %d", ptr, (is_leaf ? 'l' : 'n'), type);
    //if (!is_leaf)
    //    TRIE_SWITCH_ON_NODE(return result + " " + as, ()->print());
    return result;
}

std::string
TriePtr::
print(TrieOps & ops) const
{
    string result = print();
    if (!ptr) return result;
    if (is_leaf) return result + " " + ops.print(*this);
    TRIE_SWITCH_ON_NODE(return result + " " + as, ()->print());
    throw Exception("how did we get here?");
}


/*****************************************************************************/
/* TRIE                                                                      */
/*****************************************************************************/


struct DenseTrieLeaf : public DenseTrieBase<uint64_t> {

    size_t memusage()
    {
        size_t result = sizeof(*this);
        return result;
    }

    size_t size()
    {
        return presence.count();
    }
};

struct MultiTrieLeaf : public MultiTrieBase<uint64_t> {

    MultiTrieLeaf(int width = 0)
        : MultiTrieBase<uint64_t>(width)
    {
    }

    size_t memusage()
    {
        return sizeof(*this);
    }

    size_t size()
    {
        return size_;
    }
};

struct SingleTrieLeaf : public SingleTrieBase<uint64_t> {

    size_t memusage()
    {
        return sizeof(*this);
    }

    size_t size()
    {
        return 1;
    }

    TriePtr expand(TrieOps & ops, TrieState & state)
    {
        // Convert to a multi leaf
        MultiTrieLeaf * new_leaf
            = ops.create<MultiTrieLeaf>(width_);

        // Insert us
        new_leaf->insert(key_, 0);

        TriePtr result;
        result.set_leaf(new_leaf);
        return result;
    }
};

#define TRIE_SWITCH_ON_LEAF(action, type, args)                         \
    do {                                                                \
    if (!ptr)                                                           \
        throw Exception("null leaf pointer");                           \
    if (!ptr.is_leaf)                                                   \
        throw Exception("not leaf");                                    \
    switch (type) {                                                     \
    case DenseTrieLeaf::node_type: action<DenseTrieLeaf> args;  break;  \
    case SingleTrieLeaf::node_type: action<SingleTrieLeaf> args;  break;\
    case MultiTrieLeaf::node_type: action<MultiTrieLeaf> args;  break;  \
    default: throw Exception("unknown leaf type");                      \
    }                                                                   \
    } while (0)

/*****************************************************************************/
/* TRIE                                                                      */
/*****************************************************************************/

template<typename Alloc = std::allocator<char> >
struct Trie {
    // Use the empty base class trick to avoid memory allocation if
    // sizeof(Alloc) == 0
    struct Itl : public Alloc {
        Itl(const Alloc & alloc)
            : Alloc(alloc)
        {
        }

        TriePtr root;
        
        Alloc & allocator() { return *this; }
    } itl;

    Trie(const Alloc & alloc = Alloc())
        : itl(alloc)
    {
    }

    struct LeafOps : public TrieOps {
        LeafOps(const Trie * trie)
            : trie(const_cast<Trie *>(trie))
        {
        }

        Trie * trie;

        virtual ~LeafOps() {}

        virtual void * allocate(size_t bytes)
        {
            return trie->itl.allocator().allocate(bytes);
        }
        
        virtual void deallocate(void * mem, size_t bytes)
        {
            trie->itl.allocator().deallocate((char *)mem, bytes);
        }
        
        virtual TriePtr new_branch(TrieState & state)
        {
            cerr << "new_branch: state " << endl;
            state.dump(cerr, *this);

            // A single thing to insert, so we do a single leaf
            SingleTrieLeaf * new_leaf
                = create<SingleTrieLeaf>();
            
            new_leaf->width_ = 8 - state.depth();
            for (unsigned i = 0;  i < new_leaf->width_;  ++i)
                new_leaf->key_.chars_[i] = state.key[i];
            new_leaf->payload_ = 0;
            
            TriePtr result;
            result.set_leaf(new_leaf);
            state.push_back(result, new_leaf->width_);
            return result;
        }

        template<typename Leaf>
        void free_as(TriePtr ptr)
        {
            Leaf * l = ptr.as<Leaf>();
            if (!l) return;
            destroy(l);
        }

        virtual void free(TriePtr ptr)
        {
            TRIE_SWITCH_ON_LEAF(free_as, ptr.type, (ptr));
        }

        template<typename Leaf>
        size_t size_as(TriePtr ptr) const
        {
            Leaf * l = ptr.as<Leaf>();
            return l->size();
        }
        
        virtual uint64_t size(TriePtr ptr) const
        {
            if (!ptr) return 0;

            TRIE_SWITCH_ON_LEAF(return size_as, ptr.type, (ptr));
        }
        
        template<typename Leaf>
        TriePtr insert_as(TriePtr ptr, TrieState & state)
        {
            Leaf * l = ptr.as<Leaf>();
            if (!l) l = create<Leaf>();
            
            int16_t iterator = l->insert(state.key, state.width());
            if (l->not_null(iterator)) {
                TriePtr result;
                result.set_leaf(l);
                state.push_back(result, l->width(), iterator);
                return result;
            }

            // Insert failed; we need to expand the current node, and then
            // insert it back in
            TriePtr expanded = expand_as<Leaf>(ptr, state);
            return insert(expanded, state);
        }
        
        virtual TriePtr insert(TriePtr ptr, TrieState & state)
        {
            TRIE_SWITCH_ON_LEAF(return insert_as, ptr.type, (ptr, state));
        }

        template<typename Leaf>
        TriePtr expand_as(TriePtr ptr, TrieState & state)
        {
            Leaf * l = ptr.as<Leaf>();
            return l->expand(*this, state);
        }

        virtual TriePtr expand(TriePtr ptr, TrieState & state)
        {
            TRIE_SWITCH_ON_LEAF(return expand_as, ptr.type, (ptr, state));
        }
        
        template<typename Leaf>
        size_t memusage_as(TriePtr ptr) const
        {
            if (!ptr) return 0;
            return sizeof(Leaf);
        }
        
        virtual size_t memusage(TriePtr ptr) const
        {
            TRIE_SWITCH_ON_LEAF(return memusage_as, ptr.type, (ptr));
        }

        template<typename Leaf>
        int width_as(TriePtr ptr) const
        {
            Leaf * l = ptr.as<Leaf>();
            return l->width();
        }

        virtual int width(TriePtr ptr) const
        {
            TRIE_SWITCH_ON_LEAF(return width_as, ptr.type, (ptr));
        }

        template<typename Leaf>
        uint64_t & dereference_as(TriePtr ptr, uint16_t iterator)
        {
            Leaf * l = ptr.as<Leaf>();
            return l->dereference(iterator);
        }

        uint64_t & dereference(TriePtr ptr, uint16_t iterator)
        {
            TRIE_SWITCH_ON_LEAF(return dereference_as, ptr.type, (ptr, iterator));
        }

        template<typename Leaf>
        std::string print_as(TriePtr ptr) const
        {
            Leaf * l = ptr.as<Leaf>();
            return l->print();
        }

        virtual std::string print(TriePtr ptr) const
        {
            TRIE_SWITCH_ON_LEAF(return print_as, ptr.type, (ptr));
        }
    };
    
    struct TrieIterator
        : public boost::iterator_facade<TrieIterator,
                                        uint64_t,
                                        boost::bidirectional_traversal_tag> {
        
        Trie * trie;

        uint64_t key() const
        {
            LeafOps ops(trie);

            if (path_.width() != 8)
                throw Exception("key() on not fully inserted path");

            uint64_t result = 0;
            char * p = (char *)&result;

            for (unsigned i = 0;  i < path_.depth();  ++i) {
                const TriePath::Entry & entry = path_.at_depth(i);
                entry.ptr.key(ops, entry.iterator, p);
                p += entry.width;
            }

            return result;
        }
        
        uint64_t & value() const
        {
            if (path_.width() != 8)
                throw Exception("key() on not fully inserted path");

            LeafOps ops(trie);
            return ops.dereference(path_.back().ptr, path_.back().iterator);
        }
        
    private:
        TriePath path_;

        bool equal(const TrieIterator & other) const
        {
            // If their back elements are equal then the two are equal
            if (path_.depth() == 0 && other.path_.depth() == 0) return true;
            return path_.back() == other.path_.back();
        }
        
        uint64_t & dereference() const
        {
            return value();
        }

        void increment()
        {
            throw Exception("increment not done");
        }
        
        void decrement()
        {
            throw Exception("decrement not done");
        }
    };

    ~Trie()
    {
        LeafOps ops(this);
        itl.root.free(ops);
    }

    uint64_t & operator [] (uint64_t key)
    {
        LeafOps ops(this);
        TrieState state(key);

        cerr << "state init" << endl;
        state.dump(cerr, ops);

        TriePtr val = itl.root.insert(ops, state);

        if (itl.root != val) itl.root = val;

        cerr << "final state: " << endl;
        state.dump(cerr, ops);
        cerr << endl;

        return ops.dereference(state.back().ptr, state.back().iterator);
    }

    size_t size() const
    {
        LeafOps ops(this);
        return itl.root.size(ops);
    }
    
    size_t memusage() const
    {
        LeafOps ops(this);
        return itl.root.memusage(ops);
    }
};

template<typename Alloc>
size_t memusage(const Trie<Alloc> & trie)
{
    return trie.memusage();
}

#if 1

BOOST_AUTO_TEST_CASE( test_trie )
{
    Trie<> trie;

    BOOST_CHECK_EQUAL(sizeof(Trie<>), 8);

    BOOST_CHECK_EQUAL(memusage(trie), 0);
    BOOST_CHECK_EQUAL(trie.size(), 0);

    trie[0] = 10;

    BOOST_CHECK_EQUAL(trie[0], 10);
    BOOST_CHECK_EQUAL(trie.size(), 1);

    cerr << "memusage(trie) = " << memusage(trie) << endl;

    trie[1] = 20;

    BOOST_CHECK_EQUAL(trie[0], 10);
    BOOST_CHECK_EQUAL(trie[1], 20);
    BOOST_CHECK_EQUAL(trie.size(), 2);

    cerr << "memusage(trie) = " << memusage(trie) << endl;

    trie[0x1000000000000000ULL] = 30;

    BOOST_CHECK_EQUAL(trie[0], 10);
    BOOST_CHECK_EQUAL(trie[1], 20);
    BOOST_CHECK_EQUAL(trie[0x1000000000000000ULL], 30);
    BOOST_CHECK_EQUAL(trie.size(), 3);

    cerr << "memusage(trie) = " << memusage(trie) << endl;
}

BOOST_AUTO_TEST_CASE( test_all_memory_freed )
{
    Testing_Allocator_Data data;
    Testing_Allocator allocator(data);

    {
        Trie<Testing_Allocator> trie(allocator);

        BOOST_CHECK_EQUAL(data.bytes_outstanding, 0);
        BOOST_CHECK_EQUAL(data.objects_outstanding, 0);

        trie[0] = 10;
        trie[1] = 20;
        trie[0x1000000000000000ULL] = 30;

        BOOST_CHECK(data.bytes_outstanding > 0);
        BOOST_CHECK(data.objects_outstanding > 0);
    }

    BOOST_CHECK_EQUAL(data.bytes_outstanding, 0);
    BOOST_CHECK_EQUAL(data.objects_outstanding, 0);
}

#endif
