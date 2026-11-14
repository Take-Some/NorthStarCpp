////////////////////////////////////////////////////////////////////////////
//	Created		: 17.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_3D_SHADER_OP_H_INCLUDED
#define TEST_3D_SHADER_OP_H_INCLUDED

#include "benchmark_test.h"

namespace northstarcpp {
namespace graphics_benchmark {


class test_3d_shader_op: public benchmark_test, public creator_base<test_3d_shader_op>
{
public:
	virtual bool initialize_shader     ( benchmark&, char const*, char const*);
	virtual bool initialize			   ( benchmark&, char const*, std::map<std::string,std::string>&, std::string&);
	virtual void execute			   ( benchmark&, u32, u32, gpu_timer&);
	virtual void finalize			   ( benchmark&);
			void on_test_visual_loaded ( northstarcpp::resources::queries_result& data);

protected:
	std::string						 m_vs_code;
	std::string						 m_ps_code;
private:
	northstarcpp::render_dx10::ref_effect	 m_sh;
	u32								 m_mesh_index;
	northstarcpp::math::float4x4			 m_sphere_matrix;

}; // class test_3d_shader_op

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef TEST_3D_SHADER_OP_H_INCLUDED