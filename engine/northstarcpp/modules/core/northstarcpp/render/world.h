////////////////////////////////////////////////////////////////////////////
//	Created 	: 28.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RENDER_WORLD_H_INCLUDED
#define NORTHSTARCPP_RENDER_WORLD_H_INCLUDED

namespace northstarcpp {
namespace render {

namespace engine	{	struct renderer; struct command;	}
namespace game		{	struct renderer;					}
namespace ui		{	struct renderer;					}
namespace editor	{	struct renderer;					}

struct NORTHSTARCPP_NOVTABLE world {
	virtual	void				clear_resources	( ) = 0;

	virtual	engine::renderer&	engine			( ) = 0;
	virtual	game::renderer&		game			( ) = 0;
	virtual	ui::renderer&		ui				( ) = 0;
	virtual	editor::renderer&	editor			( ) = 0;

	virtual	void				push_command	( engine::command* command ) = 0;
	virtual	void				destroy_command	( engine::command* command ) = 0;
	virtual	void			set_editor_render_mode	( bool beditor, bool bgame ) = 0;

protected:
	NORTHSTARCPP_DECLARE_PURE_VIRTUAL_DESTRUCTOR( world )
}; // class world

} // namespace render
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RENDER_WORLD_H_INCLUDED