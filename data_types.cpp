#include "data_types.h"

using namespace shark;
 
const int32	    shark::MIN_INT32   =   (int32_t)0xffffffff; // (-2147483646);
const int32	    shark::MAX_INT32   =   (int32_t)0x7fffffff; // ( 2147483647);
const int16	    shark::MIN_INT16   =   (int16_t)0xffff; // (-32768);
const int16	    shark::MAX_INT16   =   (int16_t)0x7fff; // ( 32767);
const int8	    shark::MIN_INT8    =   (int8_t)0xff; // (-128);
const int8	    shark::MAX_INT8    =   (int8_t)0x7f; // ( 127);
const uint32   	shark::MIN_UINT32  =   (uint32_t)0;
const uint32    shark::MAX_UINT32  =   (uint32_t)0xffffffff;
const uint16    shark::MIN_UINT16  =   (uint16_t)0;
const uint16    shark::MAX_UINT16  =   (uint16_t)0xffff;
const uint8	    shark::MIN_UINT8   =   (uint8_t)0;
const uint8	    shark::MAX_UINT8   =   (uint8_t)0xff;

const float	shark::MAX_REAL32  =   static_cast<float>(3.4E+38);
const float	shark::MIN_REAL32  =   static_cast<float>(-3.4E+38);
const float	shark::TINY_REAL32  =   static_cast<float>(3.4E-38);

