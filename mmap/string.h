/* string.h                                                        -*- C++ -*-
   Jeremy Barnes, 20 August 2010
   Copyright (c) 2010 Recoset Inc.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#ifndef __jmvcc__string_h__
#define __jmvcc__string_h__

#include "array.h"
#include "structure.h"
#include <string>
#include <cstring>


namespace JMVCC {

/** Metadata for a string. */
struct StringMetadataEntry {
    StringMetadataEntry()
        : length(0), offset(0)
    {
    }
    
    unsigned length;
    unsigned offset;
};

/** How to serialize the metadata for a string. */
template<>
struct Serializer<StringMetadataEntry>
    : public StructureSerializer<StringMetadataEntry,
                                 Extractor<StringMetadataEntry, unsigned,
                                           &StringMetadataEntry::length>,
                                 Extractor<StringMetadataEntry, unsigned,
                                           &StringMetadataEntry::offset> > {
};


/** Stored null terminated but with a length as well that allows us to
    convert to a normal string efficiently.
*/
struct String {
    String(const long * base, const StringMetadataEntry & metadata)
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

    typedef CollectionSerializer<StringMetadataEntry> EntrySerializer;

    struct WorkingMetadata {
        typedef std::vector<StringMetadataEntry> Entries;
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
        typedef Array<StringMetadataEntry> Entries;
        Entries entries;  // contains its own metadata

        CollectionSerializer<Entries>::ImmutableMetadata
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
        md.entries[index].length = info.second;
        md.total_length += info.second + 1;
        if (index != md.entries.size() - 1)
            md.entries[index].offset
                = md.entries[index - 1].offset + info.second + 1;
    }

    // How much memory do we need to allocate to store the strings?
    static size_t words_for_children(WorkingMetadata & md)
    {
        return BitwiseMemoryManager::
            words_to_cover(Bits(8 * md.total_length)).first;
    }

    template<typename Metadata>
    static Bits bits_per_entry(const Metadata & md)
    {
        return EntrySerializer::bits_per_entry(md.entries_md);
    }
    
    template<typename Value>
    static 
    void serialize(long * mem,
                   BitWriter & writer,
                   const Value & value,
                   WorkingMetadata & md,
                   ImmutableMetadata & imd,
                   int index)
    {
        // Find where the data goes
        char * write_to
            = reinterpret_cast<char *>(mem)
            // + md.data_offset // TODO WE NEED THIS
            + md.entries[index].offset;

        std::pair<const char *, size_t> info
            = get_info(value);

        // Write it in place
        strncpy(write_to, info.first, info.second + 1);

        StringMetadataEntry imde;  // dummy; not needed

        // Now write our entry
        EntrySerializer::serialize(mem, writer, value, md.entries[index],
                                   imde, index);
    }

    static String
    reconstitute(const long * base,
                 BitReader & reader,
                 ImmutableMetadata metadata)
    {
        throw ML::Exception("string reconstitute");
    }

    template<typename Metadata>
    static size_t bits_per_entry(const Metadata & metadata)
    {
        return EntrySerializer::bits_per_entry(metadata.entries_md);
    }

    static void
    finish_collection(WorkingMetadata & md, ImmutableMetadata & imd)
    {
        throw ML::Exception("string finish_collection");
    }
};

} // namespace JMVCC

#endif /* __jmvcc__string_h__ */
