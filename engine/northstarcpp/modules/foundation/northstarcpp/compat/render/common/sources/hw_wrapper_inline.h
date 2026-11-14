////////////////////////////////////////////////////////////////////////////
//	Created		: 05.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef HW_WRAPPER_INLINE_H_INCLUDED
#define HW_WRAPPER_INLINE_H_INCLUDED


u32 hw_wrapper::get_width() const
{
	return m_dev_pparams.BackBufferWidth;
}

u32 hw_wrapper::get_height() const
{
	return m_dev_pparams.BackBufferHeight;
}

math::uint2 hw_wrapper::get_size() const
{
	return math::uint2( m_dev_pparams.BackBufferWidth, m_dev_pparams.BackBufferHeight);
}


#endif // #ifndef HW_WRAPPER_INLINE_H_INCLUDED