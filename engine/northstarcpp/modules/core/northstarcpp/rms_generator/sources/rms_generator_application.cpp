#include "pch.h"
#include "rms_generator_application.h"
#include "rms_generator_memory.h"
#include "northstarcpp/buffer_string.h"
#include <northstarcpp/core/core.h>
#include <northstarcpp/os_include.h>
#include "rms_evaluator.h"
#include <northstarcpp/core/simple_engine.h>

using namespace northstarcpp;

static northstarcpp::command_line::key				s_input_filename	("input_file_name",		"i", "RMS Generator", "set input file name");

namespace rms_generator
{

	northstarcpp::memory::doug_lea_allocator_type	g_rms_generator_allocator;
	static northstarcpp::core::simple_engine		s_core_engine;

	void application::initialize	( u32 argc, pstr const* const argv )
	{
		m_argv								=	argv;
		m_argc								=	argc;

		northstarcpp::core::preinitialize			(
			&s_core_engine,
			GetCommandLine(), 
			command_line::contains_application_true,
			"rms_generator",
			__DATE__
		);

		g_rms_generator_allocator.do_register			(64*1024*1024,	"rms_generator");
		g_rms_generator_allocator.user_current_thread_id();

		northstarcpp::memory::allocate_region		( );
		northstarcpp::core::initialize				( "rms_generator", core::create_log, core::perform_debug_initialization );
	}

	void   application::finalize	()
	{
		northstarcpp::core::finalize				( );
	}

	void   application::execute		()
	{
		m_exit_code							=	0;

		if(!s_input_filename.is_set())
		{
			printf							("Input file name is not typed! Please type input file name for process.");
			return;
		}
		char buffer[256];
		northstarcpp::buffer_string input_file_name(&buffer[0], array_size(buffer));

		s_input_filename.is_set_as_string	(&input_file_name);
		rms_generator::process_file			(input_file_name.c_str());

		m_exit_code							=	s_core_engine.get_exit_code();
	}

} // namespace rms_generator