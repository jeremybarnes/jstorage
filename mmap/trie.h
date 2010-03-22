/* trie.h                                                          -*- C++ -*-
   Jeremy Barnes, 20 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   A trie data structure.
*/

#ifndef __jmvcc__trie_h__
#define __jmvcc__trie_h__

namespace JMVCC {

/** A trie node has one function: accept a single character, and return
    the next state.

    The next state can be:
    - No state (ie, the character wasn't matched);
    - Another node (ie, the character was matched but the sequence didn't
      terminate);
    - A leaf

    It also puts information about the type of the next state.
*/

// A pointer to either a node or a leaf of the trie
// It may either be in memory or on disk
// The this pointer gives its encoded version
// Anything that operates looks at this() for its data; the data is encoded
// within the pointer as well as around it
// Every node does, somewhere, have some data stored for it
// The data always contains a reference count
// Reference counts are
struct TriePointer {
    union {
        uint64_t bits;
        struct {
            uint64_t is_leaf:1;
            uint64_t type:3;
            uint64_t on_disk:2;
            uint64_t payload:60;
        };
    };

    TrieNode node();
};

struct TrieState {
    TriePointer current;
};

struct TrieNode {
    TrieNode(TriePointer ptr);
    State insert;
    
};

struct DenseNode {
    TriePointer entries[256];
};

struct LookupNode {
    uint8_t population;
    
};

/** Used to perform operations on a trie */
struct TrieAccessor {
    TrieAccessor(TriePointer root);

    
};

/** Used to access a single version of a trie. */
struct MutableTrie : public TrieAccessor {
};

/** We specialize the TypedPVO for the trie. */
template<typename Payload, typename PayloadTraits>
struct TypedPVO<Trie<Payload, PayloadTraits> > : public PVO, boost::noncopyable {
    
    TriePointer on_disk;
    TriePointer in_memory;

    Trie & mutate()
    {
        // Create a trie accessor that records changes to a local version
        // of the object
    }

    const Trie & read() const
    {
    }

    virtual void * setup(Epoch old_epoch, Epoch new_epoch, void * new_value)
    {
    }

    // These all rely on the possibility to perform an efficient delta of two
    // versions by recursively going through two trees and looking for the
    // common nodes.

    void setup()
    {
        // In order to setup the commit, we need to do the following:
        // 1.  Find a list of all nodes that aren't in the on-disk version
        //     and commit them to disk.
        // 2.  Find all nodes that are currently on disk (for the old version)
        //     but need to be cleaned up afterwards, and copy them into
        //     memory.
    }

    void commit()
    {
        // To actually make the commit, we:
        // 1.  Modify the old version (the one that was on disk) to point
        //     to the nodes that were copied into memory rather than the
        //     nodes that were copied onto disk.
        // 2.  Make the new version point to the on-disk version
        // 3.  Arrange for the old on-disk nodes (that are no longer
        //     referenced) to be cleaned up
    }

    void rollback()
    {
        // To rollback, we undo the effect of the setup:
        // 1.  Free all disk nodes that were written to disk;
        // 2.  Free the memory used for copying the on-disk version into
        //     memory.
    }

    void cleanup()
    {
        // Clean up an old version of the trie.
        // Any nodes that are ONLY used by 
    }
};

} // namespace JMVCC

#endif /* __jmvcc__trie_h__ */
