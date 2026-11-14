////////////////////////////////////////////////////////////////////////////
//	Created 	: 10.11.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_DELEGATE_H_INCLUDED
#define COMMAND_DELEGATE_H_INCLUDED

#include <northstarcpp/render/base/engine_wrapper.h>
#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace render {

class command_delegate :
	public engine::command,
	private boost::noncopyable
{
public:
	typedef boost::function< void ( ) >	Delegate;

public:
					command_delegate	( Delegate const& delegate );
	virtual	void	execute				( );

private:
	Delegate		m_delegate;
}; // struct command_delegate

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_DELEGATE_H_INCLUDED