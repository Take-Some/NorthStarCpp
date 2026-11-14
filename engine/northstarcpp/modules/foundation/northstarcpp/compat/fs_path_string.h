#ifndef PATH_STRING_H_INCLUDED
#define PATH_STRING_H_INCLUDED

#include <northstarcpp/compat/fixed_string.h>

namespace northstarcpp	{
namespace fs	{

static	u32	const max_path_length			= sizeof(string_path) / sizeof(char);
typedef	fixed_string< max_path_length >		path_string;

template class NORTHSTARCPP_CORE_API fixed_string< max_path_length >;

} // namespace fs
} // namespace northstarcpp

#endif // FS_PATH_H_INCLUDED