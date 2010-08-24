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
/* ARRAY DATA                                                                */
/*****************************************************************************/

/** Metadata for an array
*/

template<typename ChildMetadata>
struct ArrayData {
    ArrayData()
        : length(0), offset(0), metadata(ChildMetadata())
    {
    }

    unsigned length;
    unsigned offset;
    ChildMetadata metadata;
};

/** How to serialize the metadata for an array. */
template<typename ChildMetadata>
struct Serializer<ArrayData<ChildMetadata> >
    : public StructureSerializer<ArrayData<ChildMetadata> ,
                                 Extractor<ArrayData<ChildMetadata>,
                                           unsigned,
                                           &ArrayData<ChildMetadata>::length>,
                                 Extractor<ArrayData<ChildMetadata>,
                                           unsigned,
                                           &ArrayData<ChildMetadata>::offset>,
                                 Extractor<ArrayData<ChildMetadata>,
                                           ChildMetadata,
                                           &ArrayData<ChildMetadata>::metadata> > {
};


/*****************************************************************************/
/* ARRAY                                                                     */
/*****************************************************************************/

template<typename T, typename EntrySerializerT = CollectionSerializer<T> >
struct Array {
    typedef EntrySerializerT EntrySerializer;
    typedef ArrayData<typename EntrySerializer::ImmutableMetadata> Data;
    
    Data data_;
    const long * mem_;

    Array()
        : mem_(0)
    {
    }

    Array(const long * mem, const Data & data)
    {
        init(mem, data);
    }

    // Create and populate with data from a range
    template<typename T2>
    Array(BitwiseMemoryManager & mm,
          const std::vector<T2> & vec)
    {
        init(mm, vec.begin(), vec.end());
    }

    void init(const long * mem, const Data & data)
    {
        data_ = data;
        mem_ = mem;
    }

    template<typename Iterator>
    void init(BitwiseMemoryManager & mm, Iterator first, Iterator last)
    {
        Data md;
        data_.length = last - first;
        data_.offset = 0;

        typename EntrySerializer::WorkingMetadata metadata
            = EntrySerializer::new_metadata(data_.length);

        EntrySerializer::prepare_collection(first, last, metadata);

        size_t child_words = EntrySerializer::words_for_children(metadata);
        size_t base_words  = EntrySerializer::words_for_base(metadata, data_.length);

        long * mem = mm.allocate(base_words + child_words);
        mem_ = mem;

        data_.metadata
            = EntrySerializer::serialize_collection(mem, first, last, metadata);
    }

    size_t size() const
    {
        return data_.length;
    }

    T operator [] (int index) const
    {
        return EntrySerializer::extract_from_collection(mem_ + data_.offset,
                                                        index, data_.metadata);
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
        return const_iterator(*this, data_.length);
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
/* SERIALIZER<ARRAY<T> >                                                     */
/*****************************************************************************/

/** How to serialize an array of arrays.
    
    We serialize as an array of metadata arrays, each one of which points to
    a child array data.
*/
template<typename T, typename ChildSerializer>
struct ArraySerializer {

    typedef Array<T> Value;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    typedef ArrayData<ChildWorkingMetadata> WorkingMetadataEntry;
    typedef ArrayData<ChildImmutableMetadata> ImmutableMetadataEntry;

    typedef CollectionSerializer<ImmutableMetadataEntry> EntrySerializer;

    struct WorkingMetadata {
        WorkingMetadata(size_t length)
            : entries(length), total_words(0)
        {
        }

        std::vector<WorkingMetadataEntry> entries;

        // how many words in the various entries start
        size_t data_offset;

        size_t total_words;

        typename EntrySerializer::WorkingMetadata entries_md;
    };

    // Type of the immutable metadata to go with this entry
    struct ImmutableMetadata {
        Array<ImmutableMetadataEntry> Entries;
        size_t data_offset;
    };
    
    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result(length);
        return result;
    }

    static
    size_t words_for_children(WorkingMetadata)
    {
        throw ML::Exception("words_for_children");
    }

    template<typename Value>
    static 
    void prepare(Value value, WorkingMetadata & metadata,
                 int item_number)
    {
        throw ML::Exception("prepare");
    }

    template<typename Value>
    static 
    void serialize(long * mem, BitWriter & writer, const Value & value,
                   WorkingMetadata, ImmutableMetadata, int)
    {
        throw ML::Exception("SERIALIZE");
    }

    static Value
    reconstitute(const long * base,
                 BitReader & reader,
                 ImmutableMetadata metadata)
    {
    }

    template<typename Metadata>
    static size_t bits_per_entry(const Metadata & metadata)
    {
        throw ML::Exception("bits_per_entry");
    }

    static void
    finish_collection(long * mem, WorkingMetadata & md, ImmutableMetadata & imd)
    {
        throw ML::Exception("finish_collection");
    }
};

template<typename T, typename EntrySerializerT>
struct Serializer<Array<T, EntrySerializerT> >
    : public ArraySerializer<T, EntrySerializerT> {
};

} // namespace JMVCC

#endif /* __jstorage__mmap__array_h__ */

