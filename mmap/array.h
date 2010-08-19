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
/* ARRAY                                                                    */
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

} // namespace JMVCC

#endif /* __jstorage__mmap__array_h__ */

