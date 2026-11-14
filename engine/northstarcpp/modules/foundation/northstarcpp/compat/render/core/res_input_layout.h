////////////////////////////////////////////////////////////////////////////
//	Created		: 15.04.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RES_INPUT_LAYOUT_H_INCLUDED
#define RES_INPUT_LAYOUT_H_INCLUDED

#include "res_vs_hw.h"

namespace northstarcpp {
namespace render_dx10 {

class res_declaration;

class res_input_layout :
	public res_flagged,
	public boost::noncopyable
{
	friend class resource_manager;
public:
	res_input_layout	( res_declaration const * decl, res_signature const * shader);
	~res_input_layout	();
	void _free			() const;

	bool equal( res_declaration const * decl, res_signature const * signature) const
	{
		return ( m_declaration == decl && m_signature == signature);
	}
	
	ID3DInputLayout * hw_layout() { return m_hw_input_layout;}

private:

	ID3DInputLayout *		m_hw_input_layout;
	res_declaration const *	m_declaration;
	ref_signature_const		m_signature;

}; // class res_input_layout

typedef northstarcpp::intrusive_ptr<res_input_layout, res_base, threading::single_threading_policy>			ref_input_layout;
typedef northstarcpp::intrusive_ptr<res_input_layout const, res_base const, threading::single_threading_policy>	ref_input_layout_const;


} // namespace render
} // namespace northstarcpp

#endif // #ifndef RES_INPUT_LAYOUT_H_INCLUDED