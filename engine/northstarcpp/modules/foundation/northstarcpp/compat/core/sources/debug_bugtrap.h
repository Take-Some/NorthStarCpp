////////////////////////////////////////////////////////////////////////////
//	Created 	: 09.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef DEBUG_BUGTRAP_H_INCLUDED
#define DEBUG_BUGTRAP_H_INCLUDED

#if NORTHSTARCPP_COMPILER_DOESNT_SUPPORT_ENUM_FORWARD_DECLARATIONS
#	include <northstarcpp/compat/core.h>
#endif // #if NORTHSTARCPP_COMPILER_DOESNT_SUPPORT_ENUM_FORWARD_DECLARATIONS

namespace northstarcpp {
namespace core {
namespace debug {

enum error_mode;
enum bugtrap_usage;

namespace bugtrap {

bool initialized		( );

void change_usage		( error_mode error_mode, bugtrap_usage bugtrap_usage );
void initialize			( );
void finalize			( );

bool on_thread_spawn	( );

void add_file			( pcstr full_path_file_name );

} // namespace bugtrap
} // namespace debug
} // namespace core
} // namespace northstarcpp

#endif // #ifndef DEBUG_BUGTRAP_H_INCLUDED