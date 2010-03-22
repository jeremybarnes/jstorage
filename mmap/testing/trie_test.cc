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

#include <signal.h>


using namespace ML;
using namespace std;

struct TrieNode;
struct TrieLeaf;
struct TrieState;
struct DenseTrieNode;

struct TriePtr {

    TriePtr()
        : bits(0)
    {
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
    std::pair<TriePtr, int> match(const char * key) const;

    // Insert the given key, updating the state as we go
    void insert(TrieState & state);

    // Get the leaf for a leaf node
    uint64_t & leaf() const;

    // Make null
    void clear()
    {
        bits = 0;
    }

    TriePtr & operator = (DenseTrieNode * node)
    {
        ptr = (uint64_t)node;
        return *this;
    }

    template<typename T>
    T * as() const
    {
        return (T *)ptr;
    }

    size_t memusage() const;
};

struct TrieState {
    TrieState(const char * key, TriePtr root)
        : key(key), depth(0), nparents(1)
    {
        parents[0] = root;
        find();
    }
    
    const char * key;
    int depth;
    int nparents;
    TriePtr parents[9];
    TriePtr & back() { return parents[nparents - 1]; }

    void push_back(TriePtr parent)
    {
        if (!parent)
            throw Exception("no parent");
        parents[nparents++] = parent;
    }

    void matched(int nchars)
    {
        depth += nchars;
        key += nchars;
    }
    
    void find()
    {
        while (depth < 8) {
            std::pair<TriePtr, int> matched = back().match(key);
            const TriePtr & next = matched.first;
            if (next) return;
            int nmatched = matched.second;
            this->matched(nmatched);
            push_back(next);
        }
    }
};

#if 0
// Dense node: only one character; 256 pointers
// Bitmap node: only one character entries + some pointers
// Sparse node: from 1 to 8 characters; up to 128 (?) entries

template<int N>
struct TrieNode {

    enum {
        TOTAL_BYTES = 512,
        DATA_BYTES  = TOTAL_BYTES - 4,  // for the sizes and padding
        BYTES_PER_ENTRY = N + 8,        // key, payload
        NENTRIES = DATA_BYTES / BYTES_PER_ENTRY
    };

    size_t memusage() const
    {
        size_t result = 0;
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage();
        return result;
    }

    std::pair<TriePtr, int>
    match(const char * key) const
    {
        int index = -1;
        // Binary search on the keys
        int i0 = 0, i1 = size;
        
        for (;;) {
            int i = (i0 + i1) / 2;
            const char * k = decodes + width * i;
            
            int cmp = strncmp(key, k, width);
            
            if (cmp == 0) {
                index = i;
                break;
            }
            else if (cmp == -1) i1 = i;
            else i0 = i;
            
            if (i0 == i1) return std::make_pair(TreePtr, 0);
        }
        
        if (index < 0) index += 256;

        if (!payloads()[index])
            return 0;
        
        if (last_level)
            return payloads()[index];
        else {
            // Try to match width characters starting at key and return the
            // pointer if it matches
            const char * next_key = key + width;

            return payloads()[index].node->lookup(key + width);
        }
        
        return 0;
    }

    /// How many characters does it match?
    uint8_t nmatched;
    
    /// How many entries are in the array?
    uint8_t size;

    char decodes[0];

    TriePtr * payloads() const
    {
        return decodes + size * 3;
    }

    /// The entries
    struct Entry {
        uint64_t prefix;
        TriePtr payload;
    };

    Entry entries[0];
};

#endif

struct DenseTrieNode {

    DenseTrieNode()
    {
        for (unsigned i = 0;  i < 256;  ++i)
            children[i].clear();
    }

    std::pair<TriePtr, int>
    match(const char * key)
    {
        int index = *key;
        if (index < 0) index += 256;
        return make_pair(children[index], 1);
    }

    void insert(TrieState & state)
    {
        if (state.depth == 8)
            throw Exception("insert too deep");
        if (state.depth == 7)
            return;

        int index = *state.key;
        if (index < 0) index += 256;
        
        if (!children[index]) {
            children[index] = new DenseTrieNode();
            // TODO: we know the type of the next one; could avoid redirection
        }
        
        state.push_back(children[index]);
        state.matched(1);
        children[index].insert(state);
    }
    
    size_t memusage()
    {
        size_t result = sizeof(*this);
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage();
        return result;
    }

    TriePtr children[256];
};

struct DenseTrieLeaf {
    uint64_t presence[4];  // one bit per leaf
    uint64_t leaves[256];

    size_t memusage()
    {
        size_t result = sizeof(*this);
        return result;
    }
};


/*****************************************************************************/
/* TRIEPTR                                                                   */
/*****************************************************************************/

size_t
TriePtr::
memusage() const
{
    if (ptr == 0) return 0;
    DenseTrieNode * p = as<DenseTrieNode>();
    return p->memusage();
}

std::pair<TriePtr, int>
TriePtr::
match(const char * key) const
{
    DenseTrieNode * p = as<DenseTrieNode>();
    return p->match(key);
}

void
TriePtr::
insert(TrieState & state)
{
    DenseTrieNode * p = as<DenseTrieNode>();
    p->insert(state);
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
        state.find();
        
        if (state.depth != 8)
            state.back().insert(state);
        
        return state.back().leaf();
    }
    
    size_t memusage() const
    {
        return root.memusage();
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
