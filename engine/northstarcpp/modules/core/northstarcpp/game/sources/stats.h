////////////////////////////////////////////////////////////////////////////
//	Created		: 18.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////
#ifndef STATS_H_INCLUDED
#define STATS_H_INCLUDED

namespace northstarcpp {

namespace render{ struct world; } // namespace render

namespace ui{
	struct text;
	struct window;
	struct world;
} // namespace ui

} // namespace northstarcpp

namespace stalker2{

class stats : private boost::noncopyable
{
public:
					stats				(northstarcpp::ui::world& ui_world);
					~stats				();

	void			draw				(northstarcpp::render::world& w);
	void			set_fps_stats		(float fps);
	void			set_camera_stats	(float3 const& pos, float3 const& dir);
protected:
	void			create				();
	ui::world&		m_ui_world;
	ui::window*		m_main_window;
	ui::text*		m_fps;
	ui::text*		m_camera_position;
	ui::text*		m_camera_direction;
}; //class stats

} //namespace stalker2

#endif //STATS_H_INCLUDED