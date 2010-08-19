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

        size_t nwords = Serializer::prepare_collection(first, last, metadata);
        long * mem = mm.allocate(nwords);
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
struct Serializer<ArrayMetadataEntry<ChildMetadata> > {
    typedef CollectionSerializer<unsigned> UnsignedSerializer;
    typedef CollectionSerializer<ChildMetadata> MetadataSerializer;

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
};


template<typename T>
struct Serializer<Array<T> > {

    typedef Array<T> Value;

    typedef CollectionSerializer<T> ChildSerializer;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    // Type of the immutable metadata to go with this entry
    typedef ArrayAndData<ArrayMetadataEntry<ChildImmutableMetadata>,
                         unsigned>
        ImmutableMetadata;
    
    typedef CollectionSerializer<ArrayMetadataEntry<ChildImmutableMetadata> >
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

    static size_t words_required(const WorkingMetadata & metadata,
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
        typename WorkingMetadata::Entry & entry = metadata[item_number];
        entry.length = val.size();
        entry.offset = entry.total_words;
        entry.metadata = ChildSerializer::new_metadata(val.size());
        size_t nwords = ChildSerializer::
            prepare_collection(val.begin(), val.end(), entry.metadata);
        entry.total_words += nwords;
    }

    // Convert metadata to immutable metadata
    static ImmutableMetadata to_immutable(WorkingMetadata metadata);

#if 0

    // Prepare to serialize.  We mostly work out the size of the metadata
    // here.
    template<typename Iterator>
    static size_t prepare(Iterator first, Iterator last, WorkingMetadata & md)
    {
        size_t length = last - first;
        
        // Serialize each of the sub-arrays, taking the absolute offset
        // of each one
        size_t total_words = 0;

        for (int i = 0; first != last;  ++first, ++i) {
            typename WorkingMetadata::Entry & entry = md.entries[i];

            const typename std::iterator_traits<Iterator>::reference val
                = *first;

            entry.length = val.size();
            entry.metadata = ChildSerializer::new_metadata(val.size());
            size_t nwords = ChildSerializer::prepare(val.begin(), val.end(),
                                                     entry.metadata);
            
            entry.offset = total_words;
            total_words += nwords;
        }

        // Add to that the words necessary to serialize the metadata array
        md.entries_md
            = ChildMetadataSerializer::
        typename ChildMetadataSerializer::WorkingMetadata entries_md;

        
        typedef CollectionSerializer<ArrayImmutableMetadataEntry<ChildMetadata> > {
        

        md.data_offset = 0;


        md.offsets_metadata = LengthSerializer::new_metadata(length);
        size_t offset_words = LengthSerializer::prepare(md.offsets.begin(),
                                                        md.offsets.end(),
                                                        md.offsets_metadata);

        md.lengths_metadata = LengthSerializer::new_metadata(length);
        size_t length_words = LengthSerializer::prepare(md.lengths.begin(),
                                                        md.lengths.end(),
                                                        md.lengths_metadata);

        md.metadata_metadata = ChildMetadataSerializer::new_metadata(length);
        size_t metadata_words
            = ChildMetadataSerializer
            ::prepare(md.metadata.begin(),
                      md.metadata.end(),
                      md.metadata_metadata);

        md.length_offset = offset_words;
        md.metadata_offset = offset_words + length_words;
        md.data_offset = md.metadata_offset + metadata_words;

        cerr << "data words " << total_words
             << " offset words " << offset_words
             << " length words " << length_words
             << " md words " << metadata_words
             << " total words " << total_words + md.data_offset
             << endl;

        total_words += md.data_offset;

        return total_words;
    }

    // Serialize a homogeneous collection where each of the elements is a
    // vector<T>.  We don't serialize any details of the collection itself.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last, WorkingMetadata & md)
    {
        //cerr << "offsets = " << md.offsets << endl;
        //cerr << "lengths = " << md.lengths << endl;
        //cerr << "md      = " << md.metadata << endl;

        int length = md.offsets.size();

        ImmutableMetadata result;
        result.data_offset = md.data_offset;
        
        // First: the three metadata arrays
        result.offsets.init(length, mem,
                            LengthSerializer::
                            serialize_collection(mem,
                                                 md.offsets.begin(),
                                                 md.offsets.end(),
                                                 md.offsets_metadata));
        
        result.lengths.init(length, mem + md.length_offset,
                            LengthSerializer::
                            serialize_collection(mem + md.length_offset,
                                                 md.lengths.begin(),
                                                 md.lengths.end(),
                                                 md.lengths_metadata));

        vector<typename ChildSerializer::ImmutableMetadata> imds(length);

        // And now the data from each of the child arrays
        for (int i = 0; first != last;  ++first, ++i) {
            const typename std::iterator_traits<Iterator>::reference val
                = *first;
            typename ChildSerializer::WorkingMetadata & wmd = md.metadata[i];

            imds[i]
                = ChildSerializer::
                serialize_collection(mem + md.data_offset + md.offsets[i],
                                     val.begin(), val.end(),
                                     wmd);
        }

        result.metadata.init(length, mem + md.metadata_offset,
                             ChildMetadataSerializer::
                             serialize_collection(mem + md.metadata_offset,
                                                  imds.begin(),
                                                  imds.end(),
                                                  md.metadata_metadata));
        

        return result;
    }

    // Extract entry n out of the total
    static Array<T> extract(const long * mem, int n,
                            const ImmutableMetadata & metadata)
    {
        //cerr << "offsets = " << metadata.offsets << endl;
        //cerr << "lengths = " << metadata.lengths << endl;
        //cerr << "md      = " << metadata.metadata << endl;

        int length = metadata.size();
        if (n < 0 || n >= length)
            throw Exception("index out of range extracting vector element");

        // Get the offset, the length and the child metadata
        ImmutableMetadataEntry entry = metadata[n];
        size_t el_offset = metadata.data_offset + entry.offset;
        
        Array<T> result(entry.length, mem + el_offset, entry.metadata);
        return result;
    }
#endif

};
    
} // namespace JMVCC

#endif /* __jstorage__mmap__array_h__ */

