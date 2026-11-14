////////////////////////////////////////////////////////////////////////////
//	Created		: 28.01.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MODEL_MANAGER_INLINE_H_INCLUDED
#define MODEL_MANAGER_INLINE_H_INCLUDED


ref_geometry	model_manager::create_geom(u32 vb_id, u32 ib_id)
{
	return create_geom(vb_id, ib_id, m_declarators[vb_id]);
}

ref_geometry	model_manager::create_geom(u32 vb_id, u32 ib_id, ref_declaration decl)
{
	return shader_manager::get_ref().create_geometry(
		decl,
		m_vertex_buffers[vb_id],
		m_index_buffers[ib_id]
	);
}


#endif // #ifndef MODEL_MANAGER_INLINE_H_INCLUDED