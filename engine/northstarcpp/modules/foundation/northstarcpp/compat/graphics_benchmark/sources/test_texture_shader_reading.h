////////////////////////////////////////////////////////////////////////////
//	Created		: 15.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_TEXTURE_SHADER_READING_H_INCLUDED
#define TEST_TEXTURE_SHADER_READING_H_INCLUDED

#include "benchmark_test.h"
#include "northstarcpp/compat/math_randoms_generator.h"

namespace northstarcpp {
namespace graphics_benchmark {

////////////////////////////////////////////////////////////////////////////
class test_texture_shader_reading: public benchmark_test, public creator_base<test_texture_shader_reading>
{
public:
	virtual bool initialize	( benchmark&, char const*, char const*);
	virtual bool initialize	( benchmark&, char const*, std::map<std::string,std::string>& parameters, std::string&);
	virtual void execute	( benchmark&, u32, u32, gpu_timer&);
private:
	std::string m_ps_code;
	northstarcpp::render_dx10::ref_effect	 m_sh;

	northstarcpp::render_dx10::constant_host*			m_X;
	northstarcpp::render_dx10::constant_host*			m_Y;
	northstarcpp::render_dx10::constant_host*			m_Z;
	northstarcpp::render_dx10::constant_host*			m_W;
	northstarcpp::render_dx10::res_texture*				m_texture;
	northstarcpp::render_dx10::res_texture_list			m_texture_list;
	northstarcpp::render_dx10::constant_host*			m_texture_host;

	northstarcpp::math::random32						m_random;
}; // class test_texture_shader_reading

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef TEST_TEXTURE_SHADER_READING_H_INCLUDED