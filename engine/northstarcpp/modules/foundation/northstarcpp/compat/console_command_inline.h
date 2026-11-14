////////////////////////////////////////////////////////////////////////////
//	Created		: 13.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CONSOLE_COMMAND_INLINE_H_INCLUDED
#define CONSOLE_COMMAND_INLINE_H_INCLUDED

namespace northstarcpp {
namespace console_commands{

template<class T>
inline cc_value<T>::cc_value(pcstr name, T& value, T const min, T const max)
:super(name), m_value(value), m_min(min), m_max(max)
{
	m_need_args		= true;
	m_serializable	= true;
}

} // namespace console_commands
} // namespace northstarcpp

#endif // #ifndef CONSOLE_COMMAND_INLINE_H_INCLUDED