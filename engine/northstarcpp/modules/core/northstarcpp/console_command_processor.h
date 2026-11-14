////////////////////////////////////////////////////////////////////////////
//	Created		: 12.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_CONSOLE_COMMAND_PROCESSOR_H_INCLUDED
#define NORTHSTARCPP_CONSOLE_COMMAND_PROCESSOR_H_INCLUDED

namespace northstarcpp {
namespace console_commands {

class console_command;

NORTHSTARCPP_CORE_API void	execute		(pcstr str);
NORTHSTARCPP_CORE_API void	show_help	(pcstr str);
NORTHSTARCPP_CORE_API void	save		(pcstr fn);
NORTHSTARCPP_CORE_API void	load		(memory::reader& F);
NORTHSTARCPP_CORE_API u32	get_similar	(pcstr starts_from, console_command** dst, u32 dst_size);
NORTHSTARCPP_CORE_API console_command*	find(pcstr name);

} // namespace console_commands
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_CONSOLE_COMMAND_PROCESSOR_H_INCLUDED