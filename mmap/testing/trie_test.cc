/* trie_test.cc
   Jeremy Barnes, 20 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Test of the trie functionality.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jml/utils/string_functions.h"
#include "jml/arch/exception.h"
#include "jml/arch/vm.h"
#include "jml/utils/unnamed_bool.h"
#include "jml/utils/testing/testing_allocator.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
#include <iostream>
#include "jml/utils/guard.h"
#include <boost/bind.hpp>
#include <fstream>
#include <vector>
#include <bitset>


using namespace ML;
using namespace std;

struct TrieNode;
struct TrieLeaf;
struct TrieState;

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
};

/** Template to wrap an STL-like allocator into a standard allocator.  These
    objects will be created on-the-fly, so it is important that they be
    lightweight.
*/
template<class Allocator>
struct TrieAllocatorAdaptor : public TrieAllocator {

    TrieAllocatorAdaptor(const Allocator & allocator = Allocator())
        : allocator(allocator)
    {
    }

    virtual ~TrieAllocatorAdaptor()
    {
    }

    virtual void * allocate(size_t bytes)
    {
        return allocator.allocate(bytes);
    }

    virtual void deallocate(void * mem, size_t bytes)
    {
        allocator.deallocate((char *)mem, bytes);
    }

    Allocator allocator;
};


struct TriePtr {

    TriePtr()
        : bits(0)
    {
    }

#if 0
    template<typename T>
    TriePtr(T * val)
        : is_leaf(0), type(0), ptr((uint64_t)val)
    {
        //cerr << "TriePtr initialized to " << *this << " from "
        //     << val << endl;
    }

    template<typename T>
    TriePtr & operator = (T * val)
    {
        type = 0;
        ptr = (uint64_t)val;

        //cerr << "TriePtr initialized to " << *this << " from "
        //     << val << endl;

        return *this;
    }
#endif

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
    void match(TrieState & state) const;
    
    // Insert the given key, updating the state as we go.  The returned
    // value is the new value that this TriePtr should take; it's used
    // (for example) when the current node gets too full and needs to be
    // reallocated.
    TriePtr insert(TrieState & state);

    // Get the leaf for a leaf node
    uint64_t & leaf(TrieState & state) const;

    // How big is the tree?
    size_t size(int depth) const;

    // Free everything associated with it
    void free(TrieAllocator & allocator, int depth);

    size_t memusage(int depth) const;


    //private:
    template<typename T>
    void set_node(T * node)
    {
        is_leaf = 0;
        ptr = (uint64_t)node;
    }

    template<typename T>
    void set_leaf(T * node)
    {
        is_leaf = 1;
        ptr = (uint64_t)node;
    }

    template<typename T>
    void set_payload(T * node)
    {
        is_leaf = 1;
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
    match_node_as(TrieState & state) const;

    template<typename Node>
    void
    match_leaf_as(TrieState & state) const;

    template<typename Node>
    TriePtr
    do_insert_as(TrieState & state);

    template<typename Node>
    size_t
    do_size_as(int depth) const;

    std::string print() const
    {
        return format("%012x", ptr);
    }
};

std::ostream & operator << (std::ostream & stream, const TriePtr & p)
{
    return stream << p.print();
}

struct TrieState {
    template<typename Alloc>
    TrieState(const char * key, TriePtr root, Alloc & allocator)
        : key(key), depth(0), nparents(1),
          allocator(*reinterpret_cast<TrieAllocator *>(alloc_space))
    {
        BOOST_STATIC_ASSERT(sizeof(TrieAllocatorAdaptor<Alloc>) <= 256);
        new (alloc_space) TrieAllocatorAdaptor<Alloc>(allocator);
    }
    
    ~TrieState()
    {
        allocator.~TrieAllocator();
    }

    const char * key;
    int depth;
    int nparents;
    TriePtr parents[9];
    TriePtr & back() { return parents[nparents - 1]; }

    void push_back(TriePtr parent)
    {
        parents[nparents++] = parent;
    }

    void matched(int nchars)
    {
        depth += nchars;
        key += nchars;
    }

    void dump(std::ostream & stream) const
    {
        stream << "state: " << endl;
        stream << "  depth: " << depth << endl;
        stream << "  key:   ";
        const char * old_key = key - depth;

        for (unsigned i = 0;  i < 8;  ++i) {
            if (i == depth) stream << "| ";
            stream << format("%02x ",
                             (old_key[i] < 0 ? old_key[i] + 256 : old_key[i]));
        }
        stream << endl;

        stream << "  nparents: " << nparents << endl;
        for (unsigned i = 0;  i < nparents;  ++i) {
            stream << "    " << i << ": " << parents[i] << endl;
        }
    }

    // Space for the allocator to be constructed
    char alloc_space[256];
    TrieAllocator & allocator;
};

std::ostream & operator << (std::ostream & stream, const TrieState & state)
{
    state.dump(stream);
    return stream;
}

template<typename Payload>
struct DenseTrieBase {

    static int width()
    {
        return 1;
    }

    typedef Payload value_type;
    typedef value_type * iterator;
    typedef const value_type * const_iterator;

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    const_iterator match(const char * key) const
    {
        int index = *key;
        if (index < 0) index += 256;
        if (!presence[index]) return 0;
        return &children[index];
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    iterator insert(const char * key)
    {
        int index = *key;
        if (index < 0) index += 256;
        if (!presence[index]) presence.set(index);
        return &children[index];
    }

    void set_ptr(iterator it, Payload new_ptr)
    {
        *it = new_ptr;
    }

    bool not_null(const_iterator it) const
    {
        return it;
    }
    
    value_type dereference(const_iterator it) const
    {
        return *it;
    }

    std::bitset<256> presence;  // one bit per leaf; says if it's there or not
    Payload children[256];
};

struct DenseTrieNode : public DenseTrieBase<TriePtr> {

    void free_children(TrieAllocator & allocator, int depth)
    {
        for (unsigned i = 0;  i < 256;  ++i)
            children[i].free(allocator, depth + 1);
    }

    size_t memusage(int depth) const
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage(depth + 1);
        return result;
    }

    size_t size(int depth)
    {
        size_t result = 0;
        for (unsigned i = 0;  i < 256;  ++i)
            if (presence[i])
                result += children[i].size(depth + 1);
        return result;
    }

};

struct DenseTrieLeaf : public DenseTrieBase<uint64_t> {

    void free_children(TrieAllocator & allocator, int depth)
    {
    }

    size_t memusage(int depth)
    {
        size_t result = sizeof(*this);
        return result;
    }

    size_t size(int depth)
    {
        return presence.count();
    }
};


/*****************************************************************************/
/* TRIEPTR                                                                   */
/*****************************************************************************/

#define TRIE_SWITCH_ON_NODE(depth, action, args)                        \
    do {                                                                \
        if (depth > 7)                                                  \
            throw Exception("create: invalid depth");                   \
        action<DenseTrieNode> args;                                     \
    } while (0)

#define TRIE_SWITCH_ON_LEAF(depth, action, args)                        \
    do {                                                                \
        if (depth > 7)                                                  \
            throw Exception("create: invalid depth");                   \
        action<DenseTrieLeaf> args;                                     \
    } while (0)

// Macro that will figure out what the type of the node is and coerce it into
// the correct type before calling the given method on it
#define TRIE_SWITCH_ON_TYPE(depth, action, args)                        \
    do {                                                                \
        if (is_leaf) TRIE_SWITCH_ON_LEAF(depth, action, args);          \
        else         TRIE_SWITCH_ON_NODE(depth, action, args);          \
    } while (0)

size_t
TriePtr::
memusage(int depth) const
{
    if (ptr == 0) return 0;

    TRIE_SWITCH_ON_TYPE(depth, return as, ()->memusage(depth));
}

template<typename Node>
void
TriePtr::
match_node_as(TrieState & state) const
{
    if (!ptr) return;

    const Node * node = as<Node>();

    typename Node::const_iterator found
        = node->match(state.key);

    typename Node::value_type result;
    if (node->not_null(found) && (result = node->dereference(found))) {
        state.matched(node->width());
        state.push_back(result);
        result.match(state);
        return;
    }
    
    return;
}

template<typename Node>
void
TriePtr::
match_leaf_as(TrieState & state) const
{
    if (!ptr) return;

    const Node * node = as<Node>();

    typename Node::const_iterator found
        = node->match(state.key);

    if (node->not_null(found)) {
        state.matched(node->width());
        TriePtr leaf_ptr;
        leaf_ptr.set_payload(found);
        state.push_back(leaf_ptr);
    }
}

void
TriePtr::
match(TrieState & state) const
{
    if (!ptr || state.depth == 8) return;

    if (is_leaf)
        TRIE_SWITCH_ON_LEAF(state.depth, match_leaf_as, (state));
    else TRIE_SWITCH_ON_NODE(state.depth, match_node_as, (state));
}

template<typename Node>
TriePtr
TriePtr::
do_insert_as(TrieState & state)
{
    Node * node = as<Node>();

    cerr << "do_insert_as: node = " << node << " depth = " << state.depth
         << endl;

    int parent = state.nparents;

    if (!ptr) node = state.allocator.create<Node>();

    typename Node::iterator found
        = node->insert(state.key);

    if (node->not_null(found)) {
        TriePtr child = TriePtr(node->dereference(found));
        state.matched(node->width());
        state.push_back(child);
        
        TriePtr new_child = child.insert(state);

        //cerr << "new_child = " << new_child << " child = " << child << endl;

        // Do we need to update our pointer?
        if (new_child != child) {
            //cerr << "new_child update" << endl;
            node->set_ptr(found, new_child);
            state.parents[parent] = new_child;
        }

        //cerr << "finished parent " << parent << " node = " << node
        //     << " this = " << *this << endl;

        return TriePtr(node);
    }
    
    throw Exception("node insert failed; need to change to new kind of node");
}

TriePtr
TriePtr::
insert(TrieState & state)
{
    if (is_leaf) return *this;

    TRIE_SWITCH_ON_TYPE(state.depth, return do_insert_as, (state));
}

uint64_t &
TriePtr::
leaf(TrieState & state) const
{
    if (state.depth != 8)
        throw Exception("state depth wrong");
    uint64_t * p = as<uint64_t>();
    return *p;
}

size_t
TriePtr::
size(int depth) const
{
    if (!ptr) return 0;
    
    TRIE_SWITCH_ON_TYPE(depth, return as, ()->size(depth));
}

template<typename T>
void destroy_as(TriePtr & obj, TrieAllocator & allocator, int depth)
{
    T * o = obj.as<T>();
    o->free_children(allocator, depth);
    o->~T();
    allocator.deallocate(o, sizeof(T));
    obj = TriePtr();
}

void
TriePtr::
free(TrieAllocator & allocator, int depth)
{
    if (!ptr) return;
    TRIE_SWITCH_ON_TYPE(depth, destroy_as, (*this, allocator, depth));
}


/*****************************************************************************/
/* TRIE                                                                      */
/*****************************************************************************/

template<typename Alloc = std::allocator<char> >
struct Trie {
    Trie(const Alloc & alloc = Alloc())
        : itl(alloc)
    {
    }

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

    ~Trie()
    {
        TrieAllocatorAdaptor<Alloc> allocator(itl.allocator());
        itl.root.free(allocator, 0);
    }

    uint64_t & operator [] (uint64_t key_)
    {
        const char * key = (const char *)&key_;
        
        TrieState state(key, itl.root, itl.allocator());
        TriePtr val = itl.root.insert(state);
        if (itl.root != val)
            itl.root = val;

        return state.back().leaf(state);
    }

    size_t size() const
    {
        return itl.root.size(0);
    }
    
    size_t memusage() const
    {
        return itl.root.memusage(0);
    }
};

template<typename Alloc>
size_t memusage(const Trie<Alloc> & trie)
{
    return trie.memusage();
}

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
