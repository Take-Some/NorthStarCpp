////////////////////////////////////////////////////////////////////////////
//	Created		: 22.06.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LINKAGE_HELPER_H_INCLUDED
#define NORTHSTARCPP_LINKAGE_HELPER_H_INCLUDED

#define DECLARE_LINKAGE_ID(linkage_id)  						\
	namespace northstarcpp {											\
		namespace linkage_helpers {								\
			int linkage_id;										\
		}														\
	} // namespace northstarcpp

#define INCLUDE_TO_LINKAGE(linkage_id)  						\
	namespace northstarcpp {											\
		namespace linkage_helpers {								\
			extern int linkage_id;								\
			static int* linkage_id ## refferer = &linkage_id;	\
		}														\
	} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_LINKAGE_HELPER_H_INCLUDED