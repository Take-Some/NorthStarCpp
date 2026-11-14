////////////////////////////////////////////////////////////////////////////
//	Created		: 18.09.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#include <string>
#include <set>
#include <map>
#include <vector>
#include <algorithm>

typedef std::vector< std::string >					definitions_type;
typedef std::set< std::string >						unique_definitions_type;
typedef std::set< std::string >						include_ids_type;

struct file_info_type {
	unique_definitions_type							external_definitions;
	definitions_type								personal_unique_definitions;
	definitions_type								unique_definitions;
	include_ids_type								include_ids;
	std::string										body;
}; // file_info_type

typedef std::map< std::string, file_info_type >		total_info_type;

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h>

bool shader_option_enabled(char const* option);
int compile_shaders(total_info_type const& total_info, char const* root);

#endif // #ifndef PCH_H_INCLUDED