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

    template<typename T>
    void destroy(T * value)
    {
        value->~T();
        size_t bytes = sizeof(T);
        deallocate((char *)value, bytes);
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

// Operations structure to do with the leaves.  Allows the client to control
// how the data is stored in the leaves.

struct TrieLeafOps {
    virtual ~TrieLeafOps() {}

    // Create a new leaf branch that will contain the rest of the key
    // from here.
    virtual TriePtr new_branch(TrieState & state) = 0;

    // Free the given branch
    virtual void free(TriePtr ptr, TrieAllocator & allocator) = 0;

    // How many leaves are in this leaf branch?
    virtual uint64_t size(TriePtr ptr)  = 0;

    // Insert the new leaf into the given branch
    virtual TriePtr insert(TriePtr ptr, TrieState & state) = 0;

    // How much memory does this leaf use?
    virtual size_t memusage(TriePtr ptr) const = 0;
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
    void match(TrieState & state) const;
    
    // Insert the given key, updating the state as we go.  The returned
    // value is the new value that this TriePtr should take; it's used
    // (for example) when the current node gets too full and needs to be
    // reallocated.
    TriePtr insert(TrieState & state);

    // How big is the tree?
    size_t size(TrieLeafOps & ops) const;

    // Free everything associated with it
    void free(TrieAllocator & allocator, TrieLeafOps & ops);

    size_t memusage(TrieLeafOps & ops) const;


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
    do_size_as() const;

    std::string print() const
    {
        return format("%012x %c %d", ptr, (is_leaf ? 'l' : 'n'), type);
    }
};

std::ostream & operator << (std::ostream & stream, const TriePtr & p)
{
    return stream << p.print();
}

struct TrieState {
    template<typename Alloc>
    TrieState(const char * key, TriePtr root,
              TrieLeafOps & leaf_ops, Alloc & allocator)
        : key(key), depth(0), nparents(1),
          leaf_ops(leaf_ops),
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

    TrieLeafOps & leaf_ops;

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

    void free_children(TrieAllocator & allocator, TrieLeafOps & ops)
    {
        for (unsigned i = 0;  i < 256;  ++i)
            children[i].free(allocator, ops);
    }

    size_t memusage(TrieLeafOps & ops) const
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage(ops);
        return result;
    }

    size_t size(TrieLeafOps & ops) const
    {
        size_t result = 0;
        for (unsigned i = 0;  i < 256;  ++i)
            if (presence[i])
                result += children[i].size(ops);
        return result;
    }

};


/*****************************************************************************/
/* TRIEPTR                                                                   */
/*****************************************************************************/

// Macro that will figure out what the type of the node is and coerce it into
// the correct type before calling the given method on it

#define TRIE_SWITCH_ON_NODE(action, args)                               \
    do {                                                                \
        action<DenseTrieNode> args;                                     \
    } while (0)

size_t
TriePtr::
memusage(TrieLeafOps & ops) const
{
    if (ptr == 0) return 0;
    else if (is_leaf) return ops.memusage(*this);
    else TRIE_SWITCH_ON_NODE(return as, ()->memusage(ops));
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
do_insert_as(TrieState & state)
{
    Node * node = as<Node>();

    //cerr << "do_insert_as: node = " << node << " depth = " << state.depth
    //     << endl;

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

        TriePtr result;
        result.set_node(node);
        return result;
    }
    
    throw Exception("node insert failed; need to change to new kind of node");
}

TriePtr
TriePtr::
insert(TrieState & state)
{
    // If there's nothing there then we ask for a new branch.  If there's a
    // node there then we ask for it to be inserted, otherwise we ask for
    // a new leaf to be inserted.

    //cerr << "insert: this = " << this << " is_leaf = " << is_leaf
    //     << " type = " << type << " ptr = " << ptr << endl;
    //cerr << "state = " << state << endl;

    if (!*this) return state.leaf_ops.new_branch(state);
    else if (is_leaf) return state.leaf_ops.insert(*this, state);
    else TRIE_SWITCH_ON_NODE(return do_insert_as, (state));
}

size_t
TriePtr::
size(TrieLeafOps & ops) const
{
    if (!ptr) return 0;
    if (is_leaf) return ops.size(*this);

    TRIE_SWITCH_ON_NODE(return as, ()->size(ops));
}

template<typename T>
void destroy_as(TriePtr & obj, TrieAllocator & allocator, TrieLeafOps & ops)
{
    T * o = obj.as<T>();
    o->free_children(allocator, ops);
    allocator.destroy(o);
    obj = TriePtr();
}

void
TriePtr::
free(TrieAllocator & allocator, TrieLeafOps & ops)
{
    if (!ptr) return;
    if (is_leaf) ops.free(*this, allocator);
    else TRIE_SWITCH_ON_NODE(destroy_as, (*this, allocator, ops));
}


/*****************************************************************************/
/* TRIE                                                                      */
/*****************************************************************************/

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

template<typename Alloc = std::allocator<char> >
struct Trie {
    Trie(const Alloc & alloc = Alloc())
        : itl(alloc)
    {
    }

    struct LeafOps : public TrieLeafOps {
        virtual ~LeafOps() {}
        
        virtual TriePtr new_branch(TrieState & state)
        {
            // To create a new branch, we create nodes all the way up to a leaf,
            // and finish it off with the leaf.

            if (state.depth == 7) {
                // We need a leaf
                TriePtr result;  // null to make sure it's created
                return insert(result, state);
            }
            
            TriePtr result;
            result = result.do_insert_as<DenseTrieNode>(state);
            return result;
        }
        
        virtual void free(TriePtr ptr, TrieAllocator & allocator)
        {
            if (!ptr)
                throw Exception("insert(): null pointer");
            if (!ptr.is_leaf)
                throw Exception("insert(): not leaf");

            DenseTrieLeaf * l = ptr.as<DenseTrieLeaf>();
            if (!l) return;
            allocator.destroy(l);
        }
        
        virtual uint64_t size(TriePtr ptr)
        {
            if (!ptr)
                throw Exception("insert(): null pointer");
            if (!ptr.is_leaf)
                throw Exception("insert(): not leaf");

            DenseTrieLeaf * l = ptr.as<DenseTrieLeaf>();
            if (!l) return 0;
            return l->presence.count();
        }
        
        virtual TriePtr insert(TriePtr ptr, TrieState & state)
        {
            if (ptr && !ptr.is_leaf)
                throw Exception("insert(): not leaf");
            if (state.depth != 7)
                throw Exception("leaf insert with wrong depth");

            DenseTrieLeaf * l = ptr.as<DenseTrieLeaf>();
            if (!l) l = state.allocator.create<DenseTrieLeaf>();
            
            l->insert(state.key);

            TriePtr result;
            result.set_leaf(l);
            return result;
        }
        
        virtual size_t memusage(TriePtr ptr) const
        {
            DenseTrieLeaf * l = ptr.as<DenseTrieLeaf>();
            if (!l) return 0;
            return sizeof(DenseTrieLeaf);
        }
    };

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
        LeafOps ops;
        itl.root.free(allocator, ops);
    }

    uint64_t & operator [] (uint64_t key_)
    {
        const char * key = (const char *)&key_;

        LeafOps ops;

        TrieState state(key, itl.root, ops, itl.allocator());
        TriePtr val = itl.root.insert(state);
        if (itl.root != val)
            itl.root = val;

        //cerr << "final state: " << state << endl;

        return *state.back().as<DenseTrieLeaf>()->insert(state.key);
    }

    size_t size() const
    {
        LeafOps ops;
        return itl.root.size(ops);
    }
    
    size_t memusage() const
    {
        LeafOps ops;
        return itl.root.memusage(ops);
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
