/* serialization.h                                                 -*- C++ -*-
   Jeremy Barnes, 9 March 2010
   Copyright (c) 2010 Jeremy Barnes.  All rights reserved.

   Serialization primitives.
*/

#ifndef __jgraph__serialization_h__
#define __jgraph__serialization_h__

#include "memory_manager.h"
#include "jml/db/serialization_order.h"
#include "jml/arch/exception.h"
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_arithmetic.hpp>

namespace JMVCC {

template<typename T>
struct SerializeAs {
};

template<> struct SerializeAs<uint32_t> { typedef uint32_t type; };
template<> struct SerializeAs<int32_t> { typedef int32_t type; };
//template<> struct SerializeAs<int> { typedef int32_t type; };
//template<> struct SerializeAs<unsigned> { typedef uint32_t type; };

template<> struct SerializeAs<int64_t> { typedef int64_t type; };
template<> struct SerializeAs<uint64_t> { typedef uint64_t type; };
//template<> struct SerializeAs<long> { typedef int64_t type; };
//template<> struct SerializeAs<unsigned long> { typedef uint64_t type; };
//template<> struct SerializeAs<long long> { typedef int64_t type; };
//template<> struct SerializeAs<unsigned long long> { typedef uint64_t type; };

template<> struct SerializeAs<float> { typedef float type; };
template<> struct SerializeAs<double> { typedef double type; };

template<typename T, typename Enable = void>
struct Serializer {
};

template<typename T>
struct Serializer<T,
                  typename boost::enable_if
                  <typename boost::is_arithmetic<T>::type>::type> {

    typedef typename SerializeAs<T>::type SA;

    static void * serialize(const T & val, MemoryManager & mm)
    {
        if (val != SA(val))
            throw ML::Exception("attempt to serialize type that doesn't fit");
        size_t sz = sizeof(SA);
        size_t al = __alignof__(SA);
        
        // Convert to network (serialization) order
        SA to_serialize = ML::DB::serialization_order(SA(val));
        
        void * mem = mm.allocate_aligned(sz, al);
        SA * v = reinterpret_cast<SA *>(mem);
        *v = to_serialize;
        
        return mem;
    }
    
    static void deallocate(void * mem, MemoryManager & mm)
    {
        size_t sz = sizeof(SA);
        mm.deallocate(mem, sz);
    }
    
};

} // namespace JMVCC

#endif /* __jgraph__serialization_h__ */
