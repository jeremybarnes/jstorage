/* array.h                                                         -*- C++ -*-
   Jeremy Barnes, 19 August 2010
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Code to implement arrays.
*/

#ifndef __jstorage__mmap__array_h__
#define __jstorage__mmap__array_h__

#include "bitwise_memory_manager.h"
#include "bitwise_serializer.h"
#include "structure.h"

#include <boost/iterator/iterator_facade.hpp>
#include <iostream>
#include <vector>


namespace JMVCC {


/*****************************************************************************/
/* ARRAY                                                                     */
/*****************************************************************************/

template<typename T, typename EntrySerializerT = CollectionSerializer<T> >
struct Array {
    typedef EntrySerializerT EntrySerializer;
    typedef typename EntrySerializer::ImmutableMetadata ChildMetadata;
    
    const long * mem_;
    size_t length_;
    ChildMetadata metadata_;

    Array()
        : mem_(0)
    {
    }

    Array(const long * mem, size_t length, const ChildMetadata & metadata)
    {
        init(mem, length, metadata);
    }

    // Create and populate with data from a range
    template<typename T2>
    Array(BitwiseMemoryManager & mm,
          const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }

    void init(const long * mem, size_t length, const ChildMetadata & metadata)
    {
        mem_ = mem;
        length_ = length;
        metadata_ = metadata;
    }

    template<typename Iterator>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last)
    {
        length_ = last - first;

        typename EntrySerializer::WorkingMetadata metadata
            = EntrySerializer::new_metadata(length_);

        EntrySerializer::prepare_collection(first, last, metadata);

        size_t child_words = EntrySerializer::words_for_children(metadata);
        size_t base_words  = EntrySerializer::words_for_base(metadata, length_);

        long * mem = mm.allocate(base_words + child_words);
        mem_ = mem;

        metadata_
            = EntrySerializer::serialize_collection(mem, first, last, metadata);
    }

    size_t size() const
    {
        return length_;
    }

    T operator [] (int index) const
    {
        return EntrySerializer::
            extract_from_collection(mem_, index, metadata_, length_);
    }

    struct const_iterator
        : public boost::iterator_facade<const_iterator,
                                        const T,
                                        boost::random_access_traversal_tag,
                                        const T> {
    private:
        const_iterator(Array vec, size_t element)
            : vec(vec), element(element)
        {
        }

        Array vec;
        ssize_t element;

        friend class boost::iterator_core_access;
        friend class Array;

        T dereference() const
        {
            if (element < 0 || element >= vec.size())
                throw ML::Exception("invalid array dereference attempt");
            return vec[element];
        }

        bool equal(const const_iterator & other) const
        {
            return other.element == element;
        }
        
        void increment()
        {
            element += 1;
        }

        void decrement()
        {
            element -= 1;
        }

        void advance(ssize_t n)
        {
            element += n;
        }

        ssize_t distance_to(const const_iterator & other) const
        {
            return other.element - element;
        }
    };

    const_iterator begin() const
    {
        return const_iterator(*this, 0);
    }

    const_iterator end() const
    {
        return const_iterator(*this, length_);
    }
};

template<typename T>
std::ostream & operator << (std::ostream & stream, const Array<T> & vec)
{
    stream << "[ ";
    for (unsigned i = 0;  i < vec.size();  ++i) {
        stream << vec[i] << " ";
    }
    return stream << "]";
}


/*****************************************************************************/
/* ARRAY METADATA ENTRY                                                      */
/*****************************************************************************/

/** Metadata entry for an array.
*/

template<typename ChildMetadata>
struct ArrayMetadataEntry {
    ArrayMetadataEntry()
        : length(0), offset(0), metadata(ChildMetadata())
    {
    }

    unsigned length;
    unsigned offset;
    ChildMetadata metadata;
};

template<typename ChildMetadata>
std::ostream &
operator << (std::ostream & stream, ArrayMetadataEntry<ChildMetadata> entry)
{
    return stream << "(length: " << entry.length
                  << ", offset: " << entry.offset
                  << ", metadata: " << entry.metadata << ")";
}
                            

/** How to serialize the metadata for an array. */
template<typename ChildMetadata>
struct Serializer<ArrayMetadataEntry<ChildMetadata> >
    : public StructureSerializer<ArrayMetadataEntry<ChildMetadata> ,
                                 Extractor<ArrayMetadataEntry<ChildMetadata>,
                                           unsigned,
                                           &ArrayMetadataEntry<ChildMetadata>::length>,
                                 Extractor<ArrayMetadataEntry<ChildMetadata>,
                                           unsigned,
                                           &ArrayMetadataEntry<ChildMetadata>::offset>,
                                 Extractor<ArrayMetadataEntry<ChildMetadata>,
                                           ChildMetadata,
                                           &ArrayMetadataEntry<ChildMetadata>::metadata> > {
};


/*****************************************************************************/
/* SERIALIZER<ARRAY<T> >                                                     */
/*****************************************************************************/

/** How to serialize an array of arrays.
    
    We serialize as an array of metadata arrays, each one of which points to
    a child array data.
*/
template<typename T, typename ChildSerializer>
struct ArraySerializer {

    typedef Array<T, ChildSerializer> Value;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    typedef ArrayMetadataEntry<ChildWorkingMetadata> WorkingMetadataEntry;
    typedef ArrayMetadataEntry<ChildImmutableMetadata> ImmutableMetadataEntry;

    typedef CollectionSerializer<ImmutableMetadataEntry> EntrySerializer;

    struct WorkingMetadata {
        WorkingMetadata(size_t length)
            : entries(length), imm_entries(length), total_child_words(0)
        {
        }

        std::vector<WorkingMetadataEntry> entries;
        std::vector<ImmutableMetadataEntry> imm_entries;

        size_t total_child_words;

        typename EntrySerializer::WorkingMetadata entries_md;
    };

    // Type of the immutable metadata to go with this entry
    struct ImmutableMetadata {
        typedef Array<ImmutableMetadataEntry> Entries;
        Entries entries;
        size_t total_child_words;
    };
    
    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result(length);
        return result;
    }

    static
    size_t words_for_children(const WorkingMetadata & metadata)
    {
        return metadata.total_child_words;
    }

    template<typename Value>
    static 
    void prepare(const Value & value, WorkingMetadata & metadata,
                 int item_number, size_t length)
    {
        ChildSerializer::
            prepare_collection(value.begin(), value.end(),
                               metadata.entries[item_number].metadata);

        size_t child_base_words = ChildSerializer::
            words_for_base(metadata.entries[item_number].metadata,
                           length);

        size_t child_child_words = ChildSerializer::
            words_for_children(metadata.entries[item_number].metadata);

        size_t child_words = child_base_words + child_child_words;

        using namespace std;
        cerr << "child " << item_number << " = " << value << " child_words = "
             << child_words << " child metadata = "
             << metadata.entries[item_number].metadata << endl;

        metadata.entries[item_number].offset = metadata.total_child_words;
        metadata.entries[item_number].length = value.size();

        metadata.total_child_words += child_words;
    }

    static void finish_prepare(WorkingMetadata & md, size_t length)
    {
        EntrySerializer::
            prepare_collection(md.entries.begin(), md.entries.end(),
                               md.entries_md);
 
        using namespace std;
        cerr << "array: total_child_words = " << md.total_child_words
             << endl;
        cerr << "array: metadata.entries = " << md.entries
             << endl;
       
        size_t md_child_words = EntrySerializer::
            words_for_children(md.entries_md);
        
        if (md_child_words > 0)
            throw ML::Exception("can't deal with array where metadata needs "
                                "child memory yet");
    }

    template<typename Value>
    static 
    void serialize(BitWriter & writer, long * child_mem, const Value & value,
                   WorkingMetadata & wmd, ImmutableMetadata & imd, int index,
                   size_t length)
    {
        // Serialize the child collection
        wmd.imm_entries[index].metadata = ChildSerializer::
            serialize_collection(child_mem + wmd.entries[index].offset,
                                 value.begin(), value.end(),
                                 wmd.entries[index].metadata);
        wmd.imm_entries[index].length = wmd.entries[index].length;
        wmd.imm_entries[index].offset = wmd.entries[index].offset;

        // Serialize the entry for this metadata
        EntrySerializer::serialize(writer, 0 /* child_mem */,
                                   wmd.entries[index], wmd.entries_md,
                                   imd.entries.metadata_, index, length);
    }

    static Value
    reconstitute(BitReader & reader,
                 const long * child_mem,
                 const ImmutableMetadata & metadata,
                 size_t length)
    {
        ImmutableMetadataEntry entry = EntrySerializer::
            reconstitute(reader, 0 /* child_mem */,
                         metadata.entries.metadata_,
                         length);

        Value result(child_mem + entry.offset, entry.length, entry.metadata);
        return result;
    }

    static Bits bits_per_entry(const WorkingMetadata & metadata)
    {
        return EntrySerializer::bits_per_entry(metadata.entries_md);
    }

    static Bits bits_per_entry(const ImmutableMetadata & metadata)
    {
        return EntrySerializer::bits_per_entry(metadata.entries.metadata_);
    }

    static void
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & wmd, ImmutableMetadata & imd,
                      size_t length)
    {
        EntrySerializer::finish_collection(mem, 0 /* child_mem */,
                                           wmd.entries_md,
                                           imd.entries.metadata_,
                                           length);
        imd.entries.mem_ = mem;
        imd.entries.length_ = wmd.entries.size();
        imd.total_child_words = wmd.total_child_words;
    }
};

template<typename T, typename EntrySerializerT>
struct Serializer<Array<T, EntrySerializerT> >
    : public ArraySerializer<T, EntrySerializerT> {
};

} // namespace JMVCC

#endif /* __jstorage__mmap__array_h__ */

