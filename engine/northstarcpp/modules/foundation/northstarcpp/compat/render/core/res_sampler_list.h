////////////////////////////////////////////////////////////////////////////
//	Created		: 06.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef SAMPLER_LIST_H_INCLUDED
#define SAMPLER_LIST_H_INCLUDED

#include "sampler_slot.h"
#include "res_common.h"

namespace northstarcpp {
namespace render_dx10 {

typedef		sampler_slot		(sampler_slots)	[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT];

class res_sampler_list	:
	public res_flagged,
	public render::vector<ref_sampler_state>,
	public res_id
{
	friend class resource_manager;

public:
	res_sampler_list	( sampler_slots const & slots);
	~res_sampler_list	() {}
	void _free			() const;

	bool equal(const res_sampler_list& base) const;
	bool equal( sampler_slots const & base) const;
};
typedef intrusive_ptr<res_sampler_list, res_base, threading::single_threading_policy> ref_sampler_list;
typedef intrusive_ptr<res_sampler_list const, res_base const, threading::single_threading_policy> ref_sampler_list_const;


} // namespace render
} // namespace northstarcpp

#endif // #ifndef SAMPLER_LIST_H_INCLUDED