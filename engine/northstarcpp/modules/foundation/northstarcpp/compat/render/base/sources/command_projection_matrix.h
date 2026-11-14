////////////////////////////////////////////////////////////////////////////
//	Created		: 09.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_PROJECTION_MATRIX_H_INCLUDED
#define COMMAND_PROJECTION_MATRIX_H_INCLUDED

#include <northstarcpp/compat/render/base/engine_wrapper.h>

namespace northstarcpp {
namespace render {

struct platform;
class command_projection_matrix :
	public engine::command,
	private boost::noncopyable
{



public:
	
	command_projection_matrix	( platform& platform, float4x4 const& projection );
	virtual	void	execute		( );

private:
	float4x4		m_projection;
	platform&		m_platform;
}; // class command_projection_matrix

} // namespace render
} // namespace northstarcpp

#endif // #ifndef COMMAND_PROJECTION_MATRIX_H_INCLUDED