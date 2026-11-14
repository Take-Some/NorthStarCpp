////////////////////////////////////////////////////////////////////////////
//	Created		: 19.03.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEXTURE_COMPRESSOR_TEST_APPLICATION_H_INCLUDED
#define TEXTURE_COMPRESSOR_TEST_APPLICATION_H_INCLUDED

namespace northstarcpp {
namespace texture_compressor_test {

class application {
public:
			void	initialize		( );
			void	execute			( );
			void	finalize		( );
	inline	u32		get_exit_code	( ) const	{ return m_exit_code; }

private:
	u32		m_exit_code;
}; // class application

} // namespace texture_compressor_test
} // namespace northstarcpp

#endif // #ifndef TEXTURE_COMPRESSOR_TEST_APPLICATION_H_INCLUDED