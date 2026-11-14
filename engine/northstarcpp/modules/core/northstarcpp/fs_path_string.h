#ifndef PATH_STRING_H_INCLUDED
#define PATH_STRING_H_INCLUDED

#include <northstarcpp/fixed_string.h>

namespace northstarcpp	{
namespace fs	{

static	u32	const max_path_length			= sizeof(string_path) / sizeof(char);
typedef	fixed_string< max_path_length >		path_string;

// The original explicit instantiation was inside namespace fs, not the
// owning namespace northstarcpp; a conforming C++17 compiler rejects it.
// Header-only fixed_string templates are instantiated at point of use.
#ifndef NORTHSTARCPP_STANDALONE_MATH
template class NORTHSTARCPP_CORE_API fixed_string< max_path_length >;
#endif

} // namespace fs
} // namespace northstarcpp

#endif // FS_PATH_H_INCLUDED