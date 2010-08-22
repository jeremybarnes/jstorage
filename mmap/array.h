/* array.h                                                         -*- C++ -*-
   Jeremy Barnes, 19 August 2010
   Copyright (c) 2010 Recoset.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Code to implement arrays.
*/

#ifndef __jstorage__mmap__array_h__
#define __jstorage__mmap__array_h__

#include "jstorage/mmap/bitwise_memory_manager.h"
#include "jstorage/mmap/bitwise_serializer.h"
#include <boost/iterator/iterator_facade.hpp>
#include <iostream>
#include <vector>


namespace JMVCC {


/*****************************************************************************/
/* ARRAY                                                                     */
/*****************************************************************************/

template<typename T>
struct Array {

    size_t length_;
    typedef CollectionSerializer<T> Serializer;
    typedef typename Serializer::ImmutableMetadata Metadata;
    Metadata metadata_;
    const long * mem_;

    Array()
        : length_(0), mem_(0)
    {
    }

    Array(size_t length, const long * mem,
           const Metadata & metadata)
    {
        init(length, mem, metadata);
    }

    // Create and populate with data from a range
    template<typename T2>
    Array(BitwiseMemoryManager & mm, const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }

    void init(size_t length, const long * mem,
              const Metadata & metadata)
    {
        length_ = length;
        mem_ = mem;
        metadata_ = metadata;
    }

    template<typename Iterator>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last)
    {
        length_ = last - first;
        typename Serializer::WorkingMetadata metadata
            = Serializer::new_metadata(length_);

        Serializer::prepare_collection(first, last, metadata);

        size_t child_words = Serializer::words_for_children(metadata);
        size_t base_words  = Serializer::words_for_base(metadata, length_);

        long * mem = mm.allocate(base_words + child_words);
        mem_ = mem;

        metadata_
            = Serializer::serialize_collection(mem, first, last, metadata);
    }

    size_t size() const
    {
        return length_;
    }

    T operator [] (int index) const
    {
        return Serializer::extract_from_collection(mem_, index, metadata_);
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
/* ARRAY_AND_DATA                                                            */
/*****************************************************************************/

template<typename T, typename Data>
class ArrayAndData : public Array<T> {
    Data data_;
    typedef Array<T> Base;
    typedef typename Base::Metadata Metadata;

public:
    ArrayAndData()
    {
    }

    ArrayAndData(size_t length, const long * mem,
                 const Metadata & metadata, const Data & data)
    {
        init(length, mem, metadata, data);
    }

    // Create and populate with data from a range
    template<typename T2, typename D>
    ArrayAndData(BitwiseMemoryManager & mm,
                 const std::vector<T2> & vec,
                 const D & data)
    {
        init(mm, vec.begin(), vec.end(), data);
    }

    void init(size_t length, const long * mem,
              const Metadata & metadata, const Data & data)
    {
        Base::init(length, mem, metadata);
        data_ = data;
    }

    template<typename Iterator, typename D>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last,
              const D & data)
    {
        Base::init(mm, first, last);
        data_ = data;
    }

    using Base::size;
    using Base::operator [];
    typedef typename Base::const_iterator const_iterator;
    using Base::begin;
    using Base::end;

    Data data() const { return data_; }
};

template<typename T, typename D>
std::ostream &
operator << (std::ostream & stream, const ArrayAndData<T, D> & vec)
{
    stream << "{ " << vec.data() << ", [ " ;
    for (unsigned i = 0;  i < vec.size();  ++i) {
        stream << vec[i] << " ";
    }
    return stream << "] }";
}


/*****************************************************************************/
/* COLLECTIONSERIALIZER<ARRAY<T> >                                           */
/*****************************************************************************/

template<typename ChildMetadata>
struct ArrayMetadataEntry {
    unsigned offset;
    unsigned length;
    ChildMetadata metadata;
};

template<typename ChildMetadata>
struct CollectionSerializer<ArrayMetadataEntry<ChildMetadata> > {
    typedef CollectionSerializer<unsigned> UnsignedSerializer;
    typedef CollectionSerializer<ChildMetadata> MetadataSerializer;

    typedef ArrayMetadataEntry<ChildMetadata> Value;

    struct WorkingMetadata {
        typename UnsignedSerializer::WorkingMetadata offset_md;
        typename UnsignedSerializer::WorkingMetadata length_md;
        typename MetadataSerializer::WorkingMetadata metadata_md;
    };

    struct ImmutableMetadata {
        typename UnsignedSerializer::ImmutableMetadata offset_md;
        typename UnsignedSerializer::ImmutableMetadata length_md;
        typename MetadataSerializer::ImmutableMetadata metadata_md;
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result;
        return result;
    }

    template<typename Value>
    static void prepare(const Value & val, WorkingMetadata & metadata,
                        int item_number)
    {
        UnsignedSerializer::prepare(val.offset, metadata.offset_md,
                                    item_number);
        UnsignedSerializer::prepare(val.length, metadata.length_md,
                                    item_number);
        MetadataSerializer::prepare(val.metadata, metadata.metadata_md,
                                    item_number);
    }

    static size_t words_required(WorkingMetadata & metadata,
                                 size_t length)
    {
        Bits bits_per_entry
            = UnsignedSerializer::bits_per_entry(metadata.offset_md)
            + UnsignedSerializer::bits_per_entry(metadata.length_md)
            + MetadataSerializer::bits_per_entry(metadata.metadata_md);

        size_t result
            = BitwiseMemoryManager::words_required(bits_per_entry, length);

        return result;
    }

    template<typename ValueT>
    static void serialize(BitWriter & writer, const ValueT & value,
                          WorkingMetadata metadata, int object_num)
    {
        serialize(writer, value.offset, value.length, value.metadata,
                  metadata);
    }

    static void serialize(BitWriter & writer, unsigned offset, unsigned length,
                          ChildMetadata child_metadata,
                          WorkingMetadata metadata)
    {
        UnsignedSerializer::serialize(writer, offset, metadata.offset_md);
        UnsignedSerializer::serialize(writer, length, metadata.length_md);
        UnsignedSerializer::serialize(writer, child_metadata, metadata.metadata_md);
    }

    template<typename Iterator>
    static size_t
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md);
#if 0
    {
        // We have an array of arrays, that's going to be serialized to a
        // contiguous array of memory somewhere.
        //
        // Its own length and offset is never serialized; it's the job of the
        // owning collection to take care of that, as with the sizing
        // information for the entries.
        // 
        // Its data is serialized as:
        // - First, an array of entries, each of which contains (packed):
        //   - The length of the sub-array
        //   - The offset of the sub-array (taken from the end of the
        //     entries array)
        //   - The information necessary to size the array elements
        //
        //   Next, there's a big blob of memory that contains the serialized
        //   version of each of the arrays.

        size_t length = last - first;
        
        // Figure out how big the sub-arrays are so that we can get their
        // offsets.
        for (int i = 0; first != last;  ++first, ++i)
            prepare(*first, md, i);

        // Figure out how much memory we need to serialize the entries
        // themselves.
        md.entries_md = EntrySerializer::new_metadata(length);
        size_t entries_words = EntrySerializer::
            prepare_collection(md.entries.begin(),
                               md.entries.end(),
                               md.entries_md);

        // Record where the boundary will be
        md.data_offset = entries_words;

        // Return the total memory
        return md.total_words + entries_words;
    }
#endif

    // Serialize a homogeneous collection where each of the elements is a
    // vector<T>.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last, WorkingMetadata & md);
#if 0
    {
        int length = md.entries.size();

        // Writer for the entries
        BitWriter entry_writer(mem);

        std::vector<ImmutableMetadataEntry> entries(length);

        for (int i = 0; first != last;  ++first, ++i) {
            // child array
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            ChildWorkingMetadata & wmd = md.entries[i].metadata;

            long * vmem = mem + md.data_offset + md.entries[i].offset;

            entries[i].offset = md.entries[i].offset;
            entries[i].length = md.entries[i].length;
            entries[i].metadata = ChildSerializer::
                serialize_collection(vmem, val.begin(), val.end(), wmd);
        }

        typename EntrySerializer::ImmutableMetadata entries_md
            = EntrySerializer::serialize_collection(mem,
                                                    entries.begin(),
                                                    entries.end(),
                                                    md.entries_md);
        
        ImmutableMetadata result(length, mem, entries_md, md.data_offset);
        return result;
    }
#endif

    // Extract entry n out of the total
    static Value extract_from_collection(const long * mem, int n,
                                         const ImmutableMetadata & metadata);
#if 0
    {
        // Get the offset, the length and the child metadata
        Bits bit_offset = Base::get_element_offset(n, md);
        BitReader reader(mem, bit_offset);

        Value result;

        result.offset = UnsignedSerializer::
            reconstitute(reader, metadata.offset_md);
        result.length = UnsignedSerializer::
            reconstitute(reader, metadata.length_md);
        result.metadata = UnsignedSerializer::
            reconstitute(reader, metadata.metadata_md);

        return result;
    }
#endif
};

template<typename T>
struct CollectionSerializer<Array<T> > {

    typedef Array<T> Value;

    typedef CollectionSerializer<T> ChildSerializer;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    typedef ArrayMetadataEntry<ChildImmutableMetadata> ImmutableMetadataEntry;

    // Type of the immutable metadata to go with this entry
    typedef ArrayAndData<ImmutableMetadataEntry, unsigned>
        ImmutableMetadata;
    
    typedef CollectionSerializer<ImmutableMetadataEntry>
        EntrySerializer;

    struct WorkingMetadata {
        WorkingMetadata(size_t length)
            : entries(length), total_words(0)
        {
        }

        typedef ArrayMetadataEntry<ChildWorkingMetadata> Entry;

        std::vector<Entry> entries;

        // how many words in the various entries start
        size_t data_offset;

        size_t total_words;

        typename EntrySerializer::WorkingMetadata entries_md;
    };
    
    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result(length);
        return result;
    }

    static size_t words_required(WorkingMetadata & metadata,
                                 size_t length)
    {
        size_t result = metadata.total_words;
        // We need:
        // a) the words to put the elements in the collection;
        // b) the words to store our metadata
        
        metadata.entries_md = EntrySerializer::new_metadata(length);
        size_t offset_words = EntrySerializer::
            prepare_collection(metadata.entries.begin(),
                               metadata.entries.end(),
                               metadata.entries_md);
        result += offset_words;
        
        return result;
    }

    // Size and otherwise prepare for the next value
    template<typename VectorLike>
    static void prepare(const VectorLike & val, WorkingMetadata & metadata,
                        int item_number)
    {
        typename WorkingMetadata::Entry & entry = metadata.entries[item_number];
        entry.length = val.size();
        entry.offset = metadata.total_words;
        entry.metadata = ChildSerializer::new_metadata(val.size());
        size_t nwords = ChildSerializer::
            prepare_collection(val.begin(), val.end(), entry.metadata);
        metadata.total_words += nwords;
    }

    // Convert metadata to immutable metadata
    static ImmutableMetadata to_immutable(WorkingMetadata metadata)
    {
        ImmutableMetadata result;
        throw ML::Exception("to_immutable");
        return result;
    }

    // Write an element as part of a collection
    template<typename ValueT>
    static void serialize(BitWriter & writer, const ValueT & value,
                          WorkingMetadata metadata, int object_num);

    // Read an element from a collection
    static Bits get_element_offset(int n, ImmutableMetadata metadata);

    // Reconstitute a single object, given metadata
    static Value reconstitute(BitReader & reader, ImmutableMetadata metadata);

    // Prepare to serialize.  We mostly work out the size of the metadata
    // here.
    template<typename Iterator>
    static size_t
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md)
    {
        // We have an array of arrays, that's going to be serialized to a
        // contiguous array of memory somewhere.
        //
        // Its own length and offset is never serialized; it's the job of the
        // owning collection to take care of that, as with the sizing
        // information for the entries.
        // 
        // Its data is serialized as:
        // - First, an array of entries, each of which contains (packed):
        //   - The length of the sub-array
        //   - The offset of the sub-array (taken from the end of the
        //     entries array)
        //   - The information necessary to size the array elements
        //
        //   Next, there's a big blob of memory that contains the serialized
        //   version of each of the arrays.

        size_t length = last - first;
        
        // Figure out how big the sub-arrays are so that we can get their
        // offsets.
        for (int i = 0; first != last;  ++first, ++i)
            prepare(*first, md, i);

        // Figure out how much memory we need to serialize the entries
        // themselves.
        md.entries_md = EntrySerializer::new_metadata(length);
        size_t entries_words = EntrySerializer::
            prepare_collection(md.entries.begin(),
                               md.entries.end(),
                               md.entries_md);

        // Record where the boundary will be
        md.data_offset = entries_words;

        // Return the total memory
        return md.total_words + entries_words;
    }

    // Serialize a homogeneous collection where each of the elements is a
    // vector<T>.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last, WorkingMetadata & md)
    {
        int length = md.entries.size();

        // Writer for the entries
        BitWriter entry_writer(mem);

        std::vector<ImmutableMetadataEntry> entries(length);

        for (int i = 0; first != last;  ++first, ++i) {
            // child array
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            ChildWorkingMetadata & wmd = md.entries[i].metadata;

            long * vmem = mem + md.data_offset + md.entries[i].offset;

            entries[i].offset = md.entries[i].offset;
            entries[i].length = md.entries[i].length;
            entries[i].metadata = ChildSerializer::
                serialize_collection(vmem, val.begin(), val.end(), wmd);
        }

        typename EntrySerializer::ImmutableMetadata entries_md
            = EntrySerializer::serialize_collection(mem,
                                                    entries.begin(),
                                                    entries.end(),
                                                    md.entries_md);
        
        ImmutableMetadata result(length, mem, entries_md, md.data_offset);
        return result;
    }

    // Extract entry n out of the total
    static Array<T> extract_from_collection(const long * mem, int n,
                                            const ImmutableMetadata & metadata)
    {
        //cerr << "offsets = " << metadata.offsets << endl;
        //cerr << "lengths = " << metadata.lengths << endl;
        //cerr << "md      = " << metadata.metadata << endl;

        int length = metadata.size();
        if (n < 0 || n >= length)
            throw ML::Exception("index out of range extracting vector element");

        // Get the offset, the length and the child metadata
        ImmutableMetadataEntry entry = metadata[n];
        size_t el_offset = metadata.data() + entry.offset;
        
        Array<T> result(entry.length, mem + el_offset, entry.metadata);
        return result;
    }

};
    
} // namespace JMVCC

#endif /* __jstorage__mmap__array_h__ */

