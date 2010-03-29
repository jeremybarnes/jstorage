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
#include <boost/iterator/iterator_facade.hpp>
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

    unsigned char operator [] (unsigned index) const
    {
        if (index >= 8)
            throw Exception("TrieKey: invalid index");
        return chars_[index];
    }

    union {
        unsigned char chars_[8];
        uint64_t bits_;
    };

    std::string print(int width = -1) const
    {
        std::string result;

        for (unsigned i = 0;  i < 8;  ++i) {
            if (i == width) result += "| ";
            result += format("%02x ", chars_[i]);
        }

        return result;
    }
};

std::ostream & operator << (std::ostream & stream, const TrieKey & key)
{
    return stream << key.print();
}


// Operations structure to do with the leaves.  Allows the client to control
// how the data is stored in the leaves.

struct TrieOps : public TrieAllocator {
    virtual ~TrieOps() {}

    // Create a new leaf branch that will contain the rest of the key
    // from here.
    virtual TriePtr new_branch(TrieOps & ops, TrieState & state) = 0;

    // Free the given branch
    virtual void free(TriePtr ptr, TrieOps & ops) = 0;

    // How many leaves are in this leaf branch?
    virtual uint64_t size(TriePtr ptr) const = 0;

    // Insert the new leaf into the given branch
    virtual TriePtr insert(TriePtr ptr, TrieOps & ops, TrieState & state) = 0;

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

    Entry & back() { return entries_[depth_ - 1]; }
    const Entry & back() const { return entries_[depth_ - 1]; }

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

    TrieState(const TrieKey & key, TriePtr root, TrieOps & leaf_ops)
        : TriePath(root, root.width(leaf_ops)),
          key(key)
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

template<typename Payload>
struct SingleTrieBase {

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
        cerr << "key = " << key.print(done_width) << endl;

        for (unsigned i = 0;  i < width_;  ++i)
            if (key[i + done_width] != key_[i]) return -1;

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
        string result = format("Single node: width %d key ", width_);
        for (unsigned i = 0;  i < width_;  ++i)
            result += format("%02x ", (key_[i] < 0 ? key_[i] + 256 : key_[i]));
        //result += " payload " + ostream_format(payload_);
        return result;
    }

    uint8_t width_;
    char key_[8];
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
/* TRIEPTR                                                                   */
/*****************************************************************************/

// Macro that will figure out what the type of the node is and coerce it into
// the correct type before calling the given method on it

#define TRIE_SWITCH_ON_NODE(action, args)                               \
    do {                                                                \
    switch (type) {                                                     \
    case DenseTrieNode::node_type: action<DenseTrieNode> args;  break;  \
    case SingleTrieNode::node_type: action<SingleTrieNode> args;  break; \
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

    if (!*this) return ops.new_branch(ops, state);
    else if (is_leaf) return ops.insert(*this, ops, state);
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
    if (is_leaf) ops.free(*this, ops);
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

struct SingleTrieLeaf : public SingleTrieBase<uint64_t> {

    size_t memusage()
    {
        return sizeof(*this);
    }

    size_t size()
    {
        return 1;
    }
};

#define TRIE_SWITCH_ON_LEAF(action, type, args)                         \
    do {                                                                \
    if (!ptr)                                                           \
        throw Exception("null leaf pointer");                           \
    if (!ptr.is_leaf)                                                   \
        throw Exception("not leaf");                                    \
    switch (type) {                                                     \
    case DenseTrieNode::node_type: action<DenseTrieLeaf> args;  break;  \
    case SingleTrieNode::node_type: action<SingleTrieLeaf> args;  break;\
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
        
        virtual TriePtr new_branch(TrieOps & ops, TrieState & state)
        {
            cerr << "new_branch: state " << endl;
            state.dump(cerr, *this);

            // A single thing to insert, so we do a single leaf
            SingleTrieLeaf * new_leaf
                = ops.create<SingleTrieLeaf>();
            
            new_leaf->width_ = 9 - state.depth();
            for (unsigned i = 0;  i < new_leaf->width_;  ++i)
                new_leaf->key_[i] = state.key[i];
            new_leaf->payload_ = 0;
            
            TriePtr result;
            result.set_leaf(new_leaf);
            return result;
        }
        
        void nothing(TrieOps & ops, TrieState & state)
        {
            if (state.depth() == 7) {
                // We need a leaf
                TriePtr result;  // null to make sure it's created
                return insert(result, state);
            }

            TriePtr result;
            result = result.do_insert_as<DenseTrieNode>(ops, state);
            return result;
        }

        template<typename Leaf>
        void free_as(TriePtr ptr, TrieOps & ops)
        {
            Leaf * l = ptr.as<Leaf>();
            if (!l) return;
            ops.destroy(l);
        }

        virtual void free(TriePtr ptr, TrieOps & ops)
        {
            TRIE_SWITCH_ON_LEAF(free_as, ptr.type, (ptr, ops));
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
        TriePtr insert_as(TriePtr ptr, TrieOps & ops, TrieState & state)
        {
            Leaf * l = ptr.as<Leaf>();
            if (!l) l = ops.create<Leaf>();
            
            l->insert(state.key, state.width());
            
            TriePtr result;
            result.set_leaf(l);
            return result;
        }
        
        virtual TriePtr insert(TriePtr ptr, TrieOps & ops, TrieState & state)
        {
            TRIE_SWITCH_ON_LEAF(return insert_as, ptr.type, (ptr, ops, state));
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
        TrieState state(key, itl.root, ops);

        cerr << "state init" << endl;
        state.dump(cerr, ops);

        TriePtr val = itl.root.insert(ops, state);

        if (itl.root != val) {
            itl.root = val;
            state.replace_at_depth(0, val, val.width(ops));
        }

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
