#include "pch.h"
#include "about_dialog.h"

#pragma managed(push, off)
namespace northstarcpp {
namespace build {
	NORTHSTARCPP_CORE_API	u32		calculate_build_id			( pcstr current_date );
	NORTHSTARCPP_CORE_API	pcstr	build_date					( );
} // namespace build

namespace core {
namespace debug {
	NORTHSTARCPP_CORE_API	u32		build_station_build_id		( );
} // namespace debug
} // namespace core
} // namespace northstarcpp
#pragma managed(pop)

using namespace System;

namespace northstarcpp {
namespace editor {

Void about_dialog::button_ok_Click		(System::Object^  sender, System::EventArgs^  e)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS			( sender, e );
	this->Close();
}

Void about_dialog::about_dialog_Load	(System::Object^  sender, System::EventArgs^  e)
{
	NORTHSTARCPP_UNREFERENCED_PARAMETERS		( sender, e );

	u32 const build_station_build_id	= northstarcpp::core::debug::build_station_build_id();
	u32 const internal_build_id			= northstarcpp::build::calculate_build_id( northstarcpp::build::build_date() );
	label1->Text	= 
		"         NorthStarCpp Editor\r\n"
		"\r\n"
		"           build " +
			build_station_build_id +
			"(internal " + internal_build_id + ")\r\n"
		"\r\n"
		"Copyright(C) Kayla's Systems - 2010";
}

} // namespace editor 
} // namespace northstarcpp