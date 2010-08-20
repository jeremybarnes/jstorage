/* bitwise_memory_manager.h                                        -*- C++ -*-
   Jeremy Barnes, 19 August 2010
   Copyright (c) 2010 Recoset Inc.  All rights reserved.
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

*/

#ifndef __jstorage__bitwise_memory_manager_h__
#define __jstorage__bitwise_memory_manager_h__

#include "jml/arch/bit_range_ops.h"
#include "jml/arch/bitops.h"
#include "jml/arch/format.h"
#include "jml/arch/exception.h"

namespace JMVCC {

/*****************************************************************************/
/* BITS                                                                      */
/*****************************************************************************/

/** Small class to hold a count of bits so that length and number of bits
    parameters don't get confused.
*/
struct Bits {
    explicit Bits(size_t bits = 0)
        : bits_(bits)
    {
    }

    size_t & value() { return bits_; }
    size_t value() const { return bits_; }

    Bits operator * (size_t length) const
    {
        return Bits(bits_ * length);
    }

    size_t bits_;
};

template<typename Integral>
Bits operator * (Integral i, Bits b)
{
    return b * i;
}

template<typename Integral>
Bits operator + (Integral i, Bits b)
{
    return Bits(i + b.value());
}

template<typename Integral>
Bits operator + (Bits b, Integral i)
{
    return Bits(i + b.value());
}

inline Bits operator + (Bits b, Bits b2)
{
    return Bits(b2.value() + b.value());
}

inline std::ostream & operator << (std::ostream & stream, Bits bits)
{
    return stream << ML::format("Bits(%d)", bits.value());
}


/*****************************************************************************/
/* BITWISEMEMORYMANAGER                                                      */
/*****************************************************************************/

struct BitwiseMemoryManager {

    const long * resolve(size_t offset)
    {
        return reinterpret_cast<const long *>(offset);
    }

    size_t encode(long * ptr)
    {
        return reinterpret_cast<size_t>(ptr);
    }

    /** How many words of memory do we need to cover the given number of
        bits?  Returns the result in the first entry and the number of
        wasted bits in the second.
    */
    static std::pair<size_t, size_t> words_to_cover(Bits bits)
    {
        size_t nbits = bits.value();
        size_t factor = 8 * sizeof(long);
        size_t result = nbits / factor;
        size_t wasted = result * factor - nbits;
        result += (wasted > 0);
        return std::make_pair(result, wasted);
    }

    static size_t words_required(Bits bits, size_t length)
    {
        Bits nbits = bits * length;
        return words_to_cover(nbits).first;
    }

    long * allocate(size_t nwords)
    {
        return new long[nwords];
    }

    long * allocate(Bits bits, size_t length)
    {
        Bits nbits = bits * length;
        std::pair<size_t, size_t> r = words_to_cover(nbits);

        size_t nwords = r.first, wasted = r.second;

        long * result = new long[nwords];

        // Initialize the wasted part of memory to all zeros to avoid
        // repeatability errors caused by non-initialization.
        if (wasted) result[nwords - 1] = 0;
        return result;
    }
};


/*****************************************************************************/
/* BITWRITER                                                                 */
/*****************************************************************************/

struct BitWriter {
    
    BitWriter(long * data, Bits bit_ofs = Bits(0))
        : data(data), bit_ofs(bit_ofs.value())
    {
        if (bit_ofs.value() >= sizeof(long) * 8)
            throw ML::Exception("invalid bitwriter initialization");
    }

    void write(long val, Bits bits)
    {
        ML::set_bit_range(data, val, bit_ofs, bits.value());
        bit_ofs += bits.value();
        data += (bit_ofs / (sizeof(long) * 8));
        bit_ofs %= sizeof(long) * 8;
    }

    long * data;
    int bit_ofs;
};


/*****************************************************************************/
/* BITREADER                                                                 */
/*****************************************************************************/

struct BitReader {
    
    BitReader(const long * data, Bits bit_ofs = Bits(0))
        : data(data), bit_ofs(bit_ofs.value())
    {
        if (bit_ofs.value() >= sizeof(long) * 8)
            throw ML::Exception("invalid BitReader initialization");
    }

    long read(Bits bits)
    {
        long value = ML::extract_bit_range(data, bit_ofs, bits.value());
        bit_ofs += bits.value();
        data += (bit_ofs / (sizeof(long) * 8));
        bit_ofs %= sizeof(long) * 8;
        return value;
    }

    const long * data;
    int bit_ofs;
};


} // namespace JMVCC

#endif /* __jstorage__bitwise_memory_manager_h__ */
