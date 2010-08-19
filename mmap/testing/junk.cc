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

#if 0
template<typename T>
struct CollectionSerializer<Vector<T> > {

    typedef CollectionSerializer<T> ChildSerializer;

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
