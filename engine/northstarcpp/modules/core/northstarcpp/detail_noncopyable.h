////////////////////////////////////////////////////////////////////////////
//	Created 	: 31.10.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_DETAIL_NONCOPYABLE_H_INCLUDED
#define NORTHSTARCPP_DETAIL_NONCOPYABLE_H_INCLUDED

namespace northstarcpp {
namespace detail {

class NORTHSTARCPP_CORE_API noncopyable {
	protected:
		inline						noncopyable	( ) { }
		inline						~noncopyable( ) { }

	private:  // emphasize the following members are private
									noncopyable	( const noncopyable& );
		inline	noncopyable const&	operator=	( const noncopyable& );
};

} // namespace detail
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_DETAIL_NONCOPYABLE_H_INCLUDED