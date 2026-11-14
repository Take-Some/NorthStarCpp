////////////////////////////////////////////////////////////////////////////
//	Created		: 18.05.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SHADER_COMPILER_APPLICATION_H_INCLUDED
#define SHADER_COMPILER_APPLICATION_H_INCLUDED

#include <northstarcpp\compat\memory_extensions.h>

namespace northstarcpp {
namespace shader_compiler {



class application {
public:
			void	initialize		( );
			void	execute			( );
			void	finalize		( );
	inline	u32		get_exit_code	( ) const	{ return m_exit_code; }

private:
	u32		m_exit_code;
}; // class application

} // namespace shader_compiler
} // namespace northstarcpp

#endif // #ifndef SHADER_COMPILER_APPLICATION_H_INCLUDED