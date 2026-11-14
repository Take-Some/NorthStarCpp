#ifndef FS_PATH_H_INCLUDED
#define FS_PATH_H_INCLUDED

#include <northstarcpp/compat/fs_path_string.h>

namespace northstarcpp	{
namespace fs	{

NORTHSTARCPP_CORE_API	path_string		convert_to_native	(pcstr path);
NORTHSTARCPP_CORE_API	path_string		convert_to_portable (pcstr path);
NORTHSTARCPP_CORE_API	path_string		convert_to_portable (pcstr path);
NORTHSTARCPP_CORE_API	pstr convert_to_portable_in_place	(pstr const path);

} // namespace fs
} // namespace northstarcpp

#endif // FS_PATH_H_INCLUDED