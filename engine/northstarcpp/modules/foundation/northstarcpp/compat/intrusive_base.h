////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_INTRUSIVE_BASE_H_INCLUDED
#define NORTHSTARCPP_INTRUSIVE_BASE_H_INCLUDED

#include <northstarcpp/compat/threading_policies.h>

namespace northstarcpp {

class intrusive_base {
public:
	inline			intrusive_base	( );

	template < typename T >
	inline	void	destroy			( T* object ) const;

	inline	u32		reference_count	( ) const { return m_reference_count; }

private:
	friend class threading::multi_threading_mutex_policy;
	friend class threading::single_threading_policy;
	u32		m_reference_count;
}; // class intrusive_base

class editor_intrusive_base {
public:
	inline			editor_intrusive_base	( );

	template < typename T >
	inline	void	destroy			( T* object ) const;

	inline	u32		reference_count	( ) const { return m_reference_count; }

private:
	friend class threading::multi_threading_mutex_policy;
	friend class threading::single_threading_policy;
	u32		m_reference_count;
}; // class editor_intrusive_base

class intrusive_interlocked_base {
public:
	inline intrusive_interlocked_base( );

	template < typename T >
	inline	void	destroy			( T* object ) const;

	inline	u32		reference_count	( ) const { return m_reference_count; }

private:
	friend class threading::single_threading_policy;
	threading::atomic32_type	m_reference_count;
}; // class intrusive_interlocked_base

} // namespace northstarcpp

#include <northstarcpp/compat/intrusive_base_inline.h>

#endif // #ifndef NORTHSTARCPP_INTRUSIVE_BASE_H_INCLUDED