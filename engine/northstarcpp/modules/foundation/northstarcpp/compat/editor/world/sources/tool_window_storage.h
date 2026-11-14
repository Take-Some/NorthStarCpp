////////////////////////////////////////////////////////////////////////////
//	Created		: 09.04.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef TOOL_WINDOW_STORAGE_H_INCLUDED
#define TOOL_WINDOW_STORAGE_H_INCLUDED

namespace northstarcpp {

namespace render {
	struct world;
} // namespace render

namespace editor {

ref class sound_editor;
ref class particle_editor;
ref class resource_editor;
class editor_world;

class tool_window_storage  :public boost::noncopyable
{
public:
	void								initialize			(northstarcpp::editor::editor_world* editor_world);
	void								destroy				();
	void								on_render			(northstarcpp::render::world& render_world);

//	gcroot<control_properties_tab^>		m_control_properties_tab;
	gcroot<sound_editor^>				m_sound_editor;
	gcroot<particle_editor^>			m_particle_editor;
	gcroot<resource_editor^>			m_texture_editor;

	WeifenLuo::WinFormsUI::Docking::IDockContent^	get_by_typename(System::String^ name);
}; // class tool_window_storage

} // namespace northstarcpp
} // namespace editor

#endif // #ifndef TOOL_WINDOW_STORAGE_H_INCLUDED