////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MEMBER_TO_FUNCTOR_H_INCLUDED
#define MEMBER_TO_FUNCTOR_H_INCLUDED

namespace northstarcpp {

template <class C, class M, class Functor>
class member_to_functor;

template <class C, class M, class Functor>
member_to_functor<C, M, Functor>
pass_member_to_functor	(M C::*member_, Functor const& func);

} // namespace northstarcpp

#include "member_to_functor_inline.h"

#endif // MEMBER_TO_FUNCTOR_H_INCLUDED