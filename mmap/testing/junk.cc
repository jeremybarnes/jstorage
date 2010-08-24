#if 0
template<typename ElementT, typename MetadataT, typename SerializerT>
struct CollectionSerializer<ArrayAndMetadata<ElementT, MetadataT, SerializerT> > {

    typedef SerializerT ChildSerializer;

    typedef typename ChildSerializer::WorkingMetadata ChildWorkingMetadata;
    typedef typename ChildSerializer::ImmutableMetadata ChildImmutableMetadata;

    struct WorkingMetadata {
        WorkingMetadata(size_t length)
            : entries(length)
        {
        }

        struct Entry {
            size_t offset;
            size_t length;
            ChildWorkingMetadata metadata;
        };

        vector<Entry> entries;

        // how many words in the various entries start
        size_t data_offset;
        
        // Metadata for serialization of the entries array
        typename ChildMetadataSerializer::WorkingMetadata entries_md;
    };

    typedef VectorImmutableMetadataEntry<ChildImmutableMetadata>
        ImmutableMetadataEntry;

    typedef VectorImmutableMetadata<ChildImmutableMetadata>
        ImmutableMetadata;
    
    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result(length);
        return result;
    }

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

        
        typedef CollectionSerializer<VectorImmutableMetadataEntry<ChildMetadata> > {
        

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
    static Vector<T> extract(const long * mem, int n,
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
        
        Vector<T> result(entry.length, mem + el_offset, entry.metadata);
        return result;
    }
};
#endif


#if 0
template<typename ChildMetadata>
struct VectorImmutableMetadataEntry {
    unsigned offset;
    unsigned length;
    ChildMetadata metadata;
};

template<typename ChildMetadata>
struct CollectionSerializer<VectorImmutableMetadataEntry<ChildMetadata> > {
    typedef CollectionSerializer<ChildMetadata> ChildMetadataSerializer;

    struct WorkingMetadata {
        Bits offset_md;
        Bits length_md;
        typename ChildMetadataSerializer::WorkingMetadata metadata_md;
    };

    struct ImmutableMetadata {
        Bits offset_bits;
        Bits length_bits;
        typename ChildMetadataSerializer::ImmutableMetadata metadata_md;
    };
};

template<typename ChildMetadata>
struct VectorImmutableMetadata
    : public ArrayAndMetadata<VectorImmutableMetadataEntry<ChildMetadata>,
                              unsigned> {
    unsigned data_offset;
};

#endif


#endif



#if 0
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
#endif





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

#if 0
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
#endif


#if 0
/*****************************************************************************/
/* BASEANDCOLLECTIONSERIALIZER                                               */
/*****************************************************************************/

/** Combine two serializers:
    - a base serializer, that serializes a fixed-width array;
    - a collection serializer, that serializes some variable length data in some
      memory that's allocated further on.
*/

template<typename BaseT,
         typename BaseSerializerT,
         typename ElementT,
         typename ElementSerializerT>
struct BaseAndCollectionSerializer {

    typedef typename ElementSerializerT::WorkingMetadata
        ChildWorkingMetadata;
    typedef typename ElementerializerT::ImmutableMetadata
        ChildImmutableMetadata;

    // Our base array contains:
    // - The base data;
    // - Offset
    // - Length
    // - 

    typedef BaseArrayEntry<BaseT, ChildImmutableMetadata>
        ImmutableMetadataEntry;
    typedef BaseArrayEntry<BaseT, ChildWorkingMetadata>
        WorkingMetadataEntry;

    // Type of the serializer to deal with the base data
    typedef BaseSerializerT BaseSerializer;

    // Type of the immutable metadata to go with this entry
    typedef ArrayAndData<ImmutableMetadataEntry, unsigned>
        ImmutableMetadata;
    
    typedef CollectionSerializer<ImmutableMetadataEntry>
        EntrySerializer;

    struct WorkingMetadata : public std::vector<WorkingMetadataEntry> {
        WorkingMetadata(size_t length)
            : std::vector<WorkingMetadataEntry>(length),
              base_md(length)
        {
        }

        // how many words in the various entries start
        size_t data_offset;

        // The total number of words that we need to store
        size_t total_words;

        // Metadata about how the working entries are serialized
        typename EntrySerializer::WorkingMetadata entries_md;

        // Metadata about how the base entries are serialized
        typename BaseSerializer::WorkingMetadata base_md;
    };

    static WorkingMetadata new_metadata(size_t length)
    {
        WorkingMetadata result;
        return result;
    }

    // Scan a series of entries to figure out how to efficiently serialize
    // them.
    template<typename Iterator>
    static size_t
    prepare_collection(Iterator first, Iterator last,
                       WorkingMetadata & md)
    {
        int length = last - first;

        typename WorkingMetadata::Entry & entry = metadata.entries[item_number];
        entry.length = val.size();
        entry.offset = metadata.total_words;
        entry.metadata = ChildSerializer::new_metadata(val.size());
        size_t nwords = ChildSerializer::
            prepare_collection(val.begin(), val.end(), entry.metadata);
        metadata.total_words += nwords;


        for (int i = 0; first != last;  ++first, ++i)
            Base::prepare(*first, md, i);

        return Base::words_required(md, length);
    }

    // Extract entry n out of the total
    static T
    extract_from_collection(const long * mem, int n,
                            ImmutableMetadata md)
    {
        Bits bit_offset = Base::get_element_offset(n, md);
        BitReader reader(mem, bit_offset);
        return Base::reconstitute(mem, reader, md);
    }

    // Serialize a homogeneous collection where each of the elements is of
    // the same type.  We don't serialize any details of the collection itself,
    // only its elements.
    //
    // Returns an immutable metadata object that can be later used to access
    // the elements using the extract_from_collection() function.
    template<typename Iterator>
    static ImmutableMetadata
    serialize_collection(long * mem,
                         Iterator first, Iterator last,
                         const WorkingMetadata & md)
    {
        BitWriter writer(mem);
        for (int i = 0; first != last;  ++first, ++i)
            Base::serialize(mem, writer, *first, md, i);

        return Base::to_immutable(md);
    }
};

// If there's no child, then we don't need to do all this junk...
template<typename BaseSerializer>
struct BaseAndCollectionSerializer<BaseSerializer, void>
    : public CollectionSerializer<typename BaseSerializer::Value, BaseSerializer> {
};
#endif
