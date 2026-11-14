////////////////////////////////////////////////////////////////////////////
//	Created		: 11.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_MEMORY_WRITER_INLINE_H_INCLUDED
#define NORTHSTARCPP_MEMORY_WRITER_INLINE_H_INCLUDED

namespace northstarcpp {
namespace memory {

inline void i_writer::w_u32(u32 d)
{	w(&d,sizeof(u32));	}

inline void i_writer::w_u16(u16 d)
{	w(&d,sizeof(u16));	}

inline void i_writer::w_u8(u8 d)
{	w(&d,sizeof(u8));	}

inline void i_writer::w_s32(s32 d)
{	w(&d,sizeof(s32));	}

inline void i_writer::w_s16(s16 d)
{	w(&d,sizeof(s16));	}

inline void i_writer::w_s8(s8 d)
{	w(&d,sizeof(s8));	}

inline void i_writer::w_float(float d)
{	w(&d,sizeof(float));}

inline void i_writer::w_stringZ(const char *p)
{	w(p,(u32)strlen(p)+1);					}

inline void i_writer::w_fvector3(const math::float3 &v)	
{	w(&v, sizeof(math::float3));	}

inline void i_writer::w_fvector2(const northstarcpp::math::float2 &v)	
{	w(&v,sizeof(northstarcpp::math::float2));	}


} // namespace memory
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_MEMORY_WRITER_INLINE_H_INCLUDED