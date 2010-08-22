/* string.h                                                        -*- C++ -*-
   Jeremy Barnes, 20 August 2010
   Copyright (c) 2010 Recoset Inc.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#ifndef __jmvcc__string_h__
#define __jmvcc__string_h__

#include "array.h"
#include "pair.h"
#include <string>


namespace JMVCC {

// Data to reconstitute an array element
template<typename Extra>
struct ArrayData {
    unsigned length;
    unsigned offset;
    Extra extra;
};

template<>
struct ArrayData<void> {
    unsigned length;
    unsigned offset;
};

// 

/** Stored null terminated but with a length as well that allows us to
    convert to a normal string efficiently.
*/
struct String {
    typedef ArrayData<void> Metadata;
    String(const long * base, const Metadata & metadata)
        : length_(metadata.length),
          value_(reinterpret_cast<const char *>(base + metadata.offset))
    {
    }

    String(unsigned length, const char * value)
        : length_(length), value_(value)
    {
    }

    size_t length() const { return length_; }
    const char * value() const { return value_; }

private:
    unsigned length_;
    const char * value_;
};

struct StringSerializer {

    // How to serialize a string...
    // each element has a length and an offset
    struct MetadataEntry : public std::pair<unsigned, unsigned> {
        MetadataEntry()
            : length(0), offset(0)
        {
        }

        unsigned length;
        unsigned offset;
    };

    typedef CollectionSerializer<MetadataEntry,
                                 PairSerializer<unsigned, unsigned> >
        EntrySerializer;

    struct WorkingMetadata {
        typedef std::vector<MetadataEntry> Entries;
        Entries entries;
        size_t total_length;
        typename EntrySerializer::WorkingMetadata entries_md;

        WorkingMetadata(size_t length)
            : entries(length), total_length(0),
              entries_md(EntrySerializer::new_metadata(length))
        {
        }
        
    };
    
    struct ImmutableMetadata {
        typedef Array<MetadataEntry> Entries;
        Entries entries;  // contains its own metadata

        CollectionSerializer<Array<MetadataEntry> >::ImmutableMetadata
            entries_md;
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        return WorkingMetadata(length);
    }

    template<typename Value>
    static void prepare(const Value & value, WorkingMetadata & md, int index)
    {
        // These are stored as null terminated values, one after another
        std::pair<const char *, size_t> info
            = get_info(value);
        md[index].length = info.second;
        md.total_length += info.second + 1;
        if (index != md.length() - 1)
            md[index].offset = md[index - 1].offset + length + 1;
    }

    // How much memory do we need to allocate to store the strings?
    static size_t words_for_children(WorkingMetadata & md)
    {
        return BitwiseMemoryManager::words_to_cover(md.total_length * 8).first;
    }

    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & md)
    {
        
    }
    
    static std::pair<T1, T2>
    reconstitute(const long * mem,
                 BitReader & reader,
                 const ImmutableMetadata & md)
    {
        T1 res1 = Serializer1::reconstitute(mem, reader, md.first);
        T2 res2 = Serializer1::reconstitute(mem, reader, md.first);
        return std::make_pair(res1, res2);
    }

    template<typename ValueT>
    static void
    serialize(long * mem, BitWriter & writer, const ValueT & value,
              WorkingMetadata & md, ImmutableMetadata & imd,
              int object_num)
    {
        Serializer1::serialize(mem, writer, value.first, md.first, imd.first,
                               object_num);
        Serializer2::serialize(mem, writer, value.second, md.second,
                               imd.second, object_num);
    }

    static void
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
        Serializer1::finish_collection(md.first, imd.first);
        Serializer2::finish_collection(md.second, imd.second);
    }
    
};

} // namespace JMVCC

#endif /* __jmvcc__string_h__ */
