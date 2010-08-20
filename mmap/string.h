/* string.h                                                        -*- C++ -*-
   Jeremy Barnes, 20 August 2010
   Copyright (c) 2010 Recoset Inc.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#ifndef __jmvcc__string_h__
#define __jmvcc__string_h__

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

} // namespace JMVCC

#endif /* __jmvcc__string_h__ */
