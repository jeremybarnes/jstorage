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
#include <bitset>
#include <signal.h>


using namespace ML;
using namespace std;

struct TrieNode;
struct TrieLeaf;
struct TrieState;
struct DenseTrieNode;
struct DenseTrieLeaf;

struct TriePtr {

    TriePtr()
        : bits(0)
    {
    }

    template<typename T>
    TriePtr(T * val)
        : type(0), ptr((uint64_t)val)
    {
        cerr << "TriePtr initialized to " << *this << " from "
             << val << endl;
    }

    template<typename T>
    TriePtr & operator = (T * val)
    {
        type = 0;
        ptr = (uint64_t)val;

        cerr << "TriePtr initialized to " << *this << " from "
             << val << endl;

        return *this;
    }

    union {
        struct {
            uint64_t type:2;
            uint64_t ptr:62;
        };
        uint64_t bits;
    };

    JML_IMPLEMENT_OPERATOR_BOOL(ptr);

    // Match the next part of the key, returning the pointer that matched.
    // If no pointer matched then return the null pointer.
    TriePtr match(TrieState & state) const;
    
    // Insert the given key, updating the state as we go.  The returned
    // value is the new value that this TriePtr should take; it's used
    // (for example) when the current node gets too full and needs to be
    // reallocated.
    TriePtr insert(TrieState & state);

    // Get the leaf for a leaf node
    uint64_t & leaf(TrieState & state) const;

    // Make null
    void clear()
    {
        bits = 0;
    }

    size_t memusage(int depth) const;


    //private:
    void set_node(DenseTrieNode * node)
    {
        ptr = (uint64_t)node;
    }

    void set_node(DenseTrieLeaf * node)
    {
        ptr = (uint64_t)node;
    }

    void set_node(uint64_t * val)
    {
        ptr = (uint64_t)val;
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
    TriePtr
    do_match_as(TrieState & state) const;

    template<typename Node>
    TriePtr
    do_insert_as(TrieState & state);

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
    TrieState(const char * key, TriePtr root)
        : key(key), depth(0), nparents(1)
    {
        parents[0] = root;
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
};

std::ostream & operator << (std::ostream & stream, const TrieState & state)
{
    state.dump(stream);
    return stream;
}

struct DenseTrieNode {

    DenseTrieNode()
    {
        cerr << "new node" << endl;
    }

    static int width()
    {
        return 1;
    }

    typedef TriePtr value_type;
    typedef value_type * iterator;
    typedef const value_type * const_iterator;

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    const_iterator match(const char * key) const
    {
        int index = *key;
        if (index < 0) index += 256;
        return &children[index];
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    iterator insert(const char * key)
    {
        int index = *key;
        if (index < 0) index += 256;
        return &children[index];
    }

    void set_ptr(iterator it, TriePtr new_ptr)
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

    size_t memusage(int depth) const
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage(depth + 1);
        return result;
    }

    TriePtr children[256];
};

struct DenseTrieLeaf {
    DenseTrieLeaf()
    {
        cerr << "new leaf" << endl;
        memset(this, 0, sizeof(this));
    }
    
    size_t memusage(int depth)
    {
        size_t result = sizeof(*this);
        return result;
    }

    static int width() { return 1; }

    typedef TriePtr value_type;
    typedef uint64_t * iterator;
    typedef const uint64_t * const_iterator;

    // Attempt to match width() characters from the key.  If it matches, then
    // return a pointer to the next node.  If there was no match, return a
    // null pointer.
    const_iterator match(const char * key) const
    {
        int index = *key;
        if (index < 0) index += 256;
        if (!presence[index]) return 0;
        return &leaves[index];
    }

    // Insert width() characters from the key into the map.  Returns the
    // iterator to access the next level.  Note that the iterator may be
    // null; in this case the node was full and will need to be expanded.
    iterator insert(const char * key)
    {
        int index = *key;
        if (index < 0) index += 256;
        if (!presence[index]) presence.set(index);
        return &leaves[index];
    }

    void set_ptr(iterator it, TriePtr new_ptr)
    {
        throw Exception("leaf doesn't support set_ptr");
    }

    bool not_null(const_iterator it) const
    {
        return it;
    }

    value_type dereference(const_iterator it) const
    {
        return TriePtr(it);
    }

    std::bitset<256> presence;  // one bit per leaf; says if it's there or not
    uint64_t leaves[256];
};


/*****************************************************************************/
/* TRIEPTR                                                                   */
/*****************************************************************************/

size_t
TriePtr::
memusage(int depth) const
{
    if (ptr == 0) return 0;
    if (depth > 7)
        throw Exception("memusage(): invalid depth");

    bool is_leaf = depth == 7;

    if (is_leaf)
        return as<DenseTrieLeaf>()->memusage(depth);
    else return as<DenseTrieNode>()->memusage(depth);
}

template<typename Node>
TriePtr
TriePtr::
do_match_as(TrieState & state) const
{
    if (!ptr) return *this;

    const Node * node = as<Node>();

    typename Node::const_iterator found
        = node->match(state.key);

    TriePtr result;
    if (node->not_null(found) && (result = node->dereference(found))) {
        state.matched(node->width());
        state.push_back(result);
        return result.match(state);
    }
    
    return result;
}

TriePtr
TriePtr::
match(TrieState & state) const
{
    if (!ptr) return *this;

    if (state.depth == 8)
        return *this;

    bool is_leaf = state.depth == 7;
    
    if (is_leaf)
        return do_match_as<DenseTrieLeaf>(state);
    else return do_match_as<DenseTrieNode>(state);
}

template<typename Node>
TriePtr
TriePtr::
do_insert_as(TrieState & state)
{
    Node * node = as<Node>();

    cerr << "node = " << node << endl;

    int parent = state.nparents;

    if (!ptr) node = new Node();

    typename Node::iterator found
        = node->insert(state.key);

    if (node->not_null(found)) {
        TriePtr child = TriePtr(node->dereference(found));
        state.matched(node->width());
        state.push_back(child);

        TriePtr new_child = child.insert(state);

        cerr << "new_child = " << new_child << " child = " << child << endl;

        // Do we need to update our pointer?
        if (new_child != child) {
            cerr << "new_child update" << endl;
            node->set_ptr(found, new_child);
            state.parents[parent] = new_child;
        }

        cerr << "finished parent " << parent << " node = " << node
             << " this = " << *this << endl;

        return TriePtr(node);
    }
    
    throw Exception("node insert failed; need to change to new kind of node");
}

TriePtr
TriePtr::
insert(TrieState & state)
{
    cerr << "insert" << endl;
    cerr << state << endl;

    if (state.depth == 8)
        return *this;

    if (state.depth > 7)
        throw Exception("create: invalid depth");

    bool is_leaf = state.depth == 7;

    if (is_leaf)
        return do_insert_as<DenseTrieLeaf>(state);
    else return do_insert_as<DenseTrieNode>(state);
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


/*****************************************************************************/
/* TRIE                                                                      */
/*****************************************************************************/

struct Trie {
    TriePtr root;

    uint64_t & operator [] (uint64_t key_)
    {
        const char * key = (const char *)&key_;
        
        TrieState state(key, root);
        TriePtr val = root.insert(state);
        if (root != val)
            root = val;

        cerr << "after insert" << endl;
        cerr << "state = " << state << endl;

        return state.back().leaf(state);
    }
    
    size_t memusage() const
    {
        return root.memusage(0);
    }
};

size_t memusage(const Trie & trie)
{
    return trie.memusage();
}

BOOST_AUTO_TEST_CASE( test_trie )
{
    Trie trie;

    BOOST_CHECK_EQUAL(memusage(trie), 0);

    trie[0] = 10;

    BOOST_CHECK_EQUAL(trie[0], 10);
}
