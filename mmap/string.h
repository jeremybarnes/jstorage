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

#include "jml/utils/vector_utils.h"

namespace JMVCC {

/** Metadata for a string. */
struct StringMetadataEntry {
    StringMetadataEntry()
        : length(0), offset(0)
    {
    }
    
    unsigned length;
    unsigned offset;

    std::string print() const
    {
        return ML::format("(length: %d, offset: %d)", length, offset);
    }
};

inline std::ostream &
operator << (std::ostream & stream, const StringMetadataEntry & entry)
{
    return stream << entry.print();
}

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
          value_(reinterpret_cast<const char *>(base) + metadata.offset)
    {
    }

    String(unsigned length, const char * value)
        : length_(length), value_(value)
    {
    }

    size_t length() const { return length_; }
    const char * value() const { return value_; }

    operator std::string() const
    {
        return std::string(value_, value_ + length_);
    }
    
    operator const char * () const
    {
        return value_;
    }

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
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        return WorkingMetadata(length);
    }

    static
    std::pair<const char *, size_t>
    get_info(const std::string & str)
    {
        return std::make_pair(str.c_str(), str.length());
    }

    static
    std::pair<const char *, size_t>
    get_info(const String & str)
    {
        return std::make_pair(str.value(), str.length());
    }

    static
    std::pair<const char *, size_t>
    get_info(const char * str)
    {
        return std::make_pair(str, strlen(str));
    }

    template<typename Value>
    static void prepare(const Value & value, WorkingMetadata & md, int index,
                        size_t length)
    {
        // These are stored as null terminated values, one after another
        std::pair<const char *, size_t> info
            = get_info(value);
        md.entries[index].length = info.second;
        md.entries[index].offset = md.total_length;
        md.total_length += info.second + 1;
    }

    static void finish_prepare(WorkingMetadata & md, size_t length)
    {
        EntrySerializer::prepare_collection(md.entries.begin(),
                                            md.entries.end(),
                                            md.entries_md);
    }

    // How much memory do we need to allocate to store the strings?
    static size_t words_for_children(WorkingMetadata & md)
    {
        return BitwiseMemoryManager::
            words_to_cover(Bits(8 * md.total_length)).first;
    }

    static Bits bits_per_entry(const WorkingMetadata & md)
    {
        return EntrySerializer::bits_per_entry(md.entries_md);
    }
    
    static Bits bits_per_entry(const ImmutableMetadata & md)
    {
        return EntrySerializer::bits_per_entry(md.entries.data_.metadata);
    }
    
    template<typename Value>
    static 
    void serialize(BitWriter & writer,
                   long * child_mem,
                   const Value & value,
                   WorkingMetadata & md,
                   ImmutableMetadata & imd,
                   int index,
                   size_t length)
    {
        // Find where the data goes
        char * write_to
            = reinterpret_cast<char *>(child_mem)
            + md.entries[index].offset;

        std::pair<const char *, size_t> info
            = get_info(value);

        using namespace std;
        cerr << "writing " << index << " element \"" << info.first << "\" to "
             << (void *)write_to << " (offset "
             << ((const char *)write_to - (const char *)child_mem)
             << ")"
             << " base data " << writer.data
             << " base bit " << writer.bit_ofs << endl;

        // Write it in place; we don't use strncpy since we might need to
        // copy nulls over
        std::copy(info.first, info.first + info.second, write_to);
        write_to[info.second] = 0;

        // Now write our entry
        EntrySerializer::serialize(writer, 0 /* child_mem */,
                                   md.entries[index],
                                   md.entries_md, imd.entries.data_.metadata,
                                   index, length);
    }

    static String
    reconstitute(BitReader & reader,
                 const long * child_mem,
                 const ImmutableMetadata & metadata,
                 size_t length)
    {
        StringMetadataEntry entry = EntrySerializer::
            reconstitute(reader, child_mem, metadata.entries.data_.metadata,
                         length);

        using namespace std;
        cerr << "getting string: entry = " << entry << " child_mem = "
             << child_mem << " base data " << reader.data
             << " base bit " << reader.bit_ofs << " data " << endl;

        return String(child_mem, entry);
    }

    static void
    finish_collection(long * mem, long * child_mem,
                      WorkingMetadata & md, ImmutableMetadata & imd,
                      size_t length)
    {
        EntrySerializer::finish_collection(mem, 0 /* child_mem */,
                                           md.entries_md,
                                           imd.entries.data_.metadata,
                                           length);
        imd.entries.mem_ = mem;
        imd.entries.data_.length = md.entries.size();
        imd.entries.data_.offset = 0;

        using namespace std;
        cerr << "md.entries = " << md.entries << endl;
        cerr << "imd = " << imd.entries << endl;
    }
};

template<>
struct Serializer<std::string> : public StringSerializer {
};

template<>
struct Serializer<String> : public StringSerializer {
};

template<>
struct Serializer<const char *> : public StringSerializer {
};

} // namespace JMVCC

#endif /* __jmvcc__string_h__ */
