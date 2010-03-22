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
//using namespace JGraph;
using namespace std;

struct TrieNode;
struct TrieLeaf;

struct TriePtr {

    void * ptr;

    JML_IMPLEMENT_OPERATOR_BOOL(ptr);

    // Match the next part of the key, returning the pointer that matched.
    // If no pointer matched then return the null pointer.
    TriePtr match(const char * key);

    size_t memusage() const;
};

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
    
    void find()
    {
        while (depth < 8) {
            TriePtr next = back().match(key);
            if (!next) return;
            int nmatched = back().depth();
            depth += nmatched;
            key += nmatched;
            parents[nparents++] = next;
        }
    }
};


// Dense node: only one character; 256 pointers
// Bitmap node: only one character entries + some pointers
// Sparse node: from 1 to 8 characters; up to 128 (?) entries

struct TrieNode {
    size_t memusage() const
    {
        size_t result = 0;
        for (unsigned i = 0;  i < 256;  ++i)
            result += children[i].memusage();
        return result;
    }

    uint64_t * lookup(const char * key) const
    {
        int index = -1;
        if (type == DENSE) index = *key;
        else {
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

                if (i0 == i1) return 0;
            }
        }

        if (index < 0) index += 256;

        // Last level?
        bool last_level = (depth + width == 8);

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

    uint64_t & insert(const char * key)
    {
        // 1.  Find where to insert it
        State state(key, root);
        state.find();

        // 2.  Is it already there?
        if (state.depth == 8)
            return state.back().leaf();
        
        // 3.  If not, we need to insert the new key under the last
        //     parent
        state.back().insert(state);
    }

    /// How deep?
    int depth;

    /// Which type?
    int type;  

    /// How many characters does it match?
    int width;
    
    /// How many entries are in the array?
    int size;

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

struct TrieLeaf {
    uint64_t key;
    uint64_t payload;

    size_t memusage() const
    {
        return 8;
    }
};

size_t
TriePtr::
memusage() const
{
    if (is_node) {
        if (!node) return 0;
        return node->memusage();
    }
    else {
        if (!leaf) return 0;
        return leaf->memusage();
    }
}

struct TrieIterator {
    
};

struct Trie {
    TriePtr root;

    uint64_t & operator [] (uint64_t key);

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
