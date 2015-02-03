#ifndef DATATYPES_H_20141203
#define DATATYPES_H_20141203
#include <stdint.h>

namespace shark
{

typedef signed long long int64;
typedef signed long     int32;
typedef signed short    int16;
typedef signed char     int8;
typedef unsigned long long uint64;
typedef unsigned long   uint32;
typedef unsigned short  uint16;
typedef unsigned char   uint8;

extern const int32		MIN_INT32;
extern const int32		MAX_INT32;
extern const int16		MIN_INT16;
extern const int16		MAX_INT16;
extern const int8		MIN_INT8;
extern const int8		MAX_INT8;

extern const uint32	MIN_UINT32;
extern const uint32	MAX_UINT32;
extern const uint16	MIN_UINT16;
extern const uint16	MAX_UINT16;
extern const uint8		MIN_UINT8;
extern const uint8		MAX_UINT8;

extern const float		MIN_REAL32;
extern const float		MAX_REAL32;
extern const float		TINY_REAL32;

// implicit_cast would have been part of the C++ standard library,
// but the proposal was submitted too late.  It will probably make
// its way into the language in the future.
template<typename To, typename From>
inline To implicit_cast(From const &f) {
  return f;
}


}

#endif
