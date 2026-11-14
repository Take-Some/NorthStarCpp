////////////////////////////////////////////////////////////////////////////
//	Created		: 09.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TEST_SIMPLE_SHADER_OP_H_INCLUDED
#define TEST_SIMPLE_SHADER_OP_H_INCLUDED

#include "benchmark_test.h"

namespace northstarcpp {
namespace graphics_benchmark {


class test_simple_shader_op: public benchmark_test
{
public:
	virtual bool initialize_shader ( benchmark&, char const*, char const*);
	virtual bool initialize	( benchmark&, char const*, std::map<std::string,std::string>&, std::string&)	{ return true; }
	virtual void execute	( benchmark&, u32, u32, gpu_timer&);
	virtual void finalize   ( benchmark&);

protected:
	std::string m_vs_code;
	std::string m_ps_code;
//private:
	northstarcpp::render_dx10::ref_effect	 m_sh;
}; // class test_simple_shader_op


////////////////////////////////////////////////////////////////////////////
class test_simple_vs_shader_op: public test_simple_shader_op, public creator_base<test_simple_vs_shader_op>
{
public:
	virtual bool initialize	( benchmark&, char const*, std::map<std::string,std::string>& parameters, std::string&);
}; // class test_simple_vs_shader_op


////////////////////////////////////////////////////////////////////////////
class test_simple_ps_shader_op: public test_simple_shader_op, public creator_base<test_simple_ps_shader_op>
{
public:
	virtual bool initialize	( benchmark&, char const*, std::map<std::string,std::string>& parameters, std::string&);
}; // class test_simple_ps_shader_op

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef TEST_SIMPLE_SHADER_OP_H_INCLUDED