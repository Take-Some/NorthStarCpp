////////////////////////////////////////////////////////////////////////////
//	Created		: 10.02.2009
//	Author		: Mykhailo Parfeniuk
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef RES_COMMON_INLINE_H_INCLUDED
#define RES_COMMON_INLINE_H_INCLUDED

//namespace northstarcpp {
//namespace render {


template<typename T>
void res_base::destroy(T* object)
{
	DELETE(object);
}

//} // namespace render 
//} // namespace northstarcpp 


#endif // #ifndef RES_COMMON_INLINE_H_INCLUDED