////////////////////////////////////////////////////////////////////////////
//	Created		: 27.01.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

namespace northstarcpp {
namespace animation {

inline bone	const& 	skeleton::get_bone( bone_index_type idx ) const
{
	ASSERT( idx < m_bone_count );
	return m_bones[idx];
}

} // namespace animation
} // namespace northstarcpp