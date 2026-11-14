////////////////////////////////////////////////////////////////////////////
//	Created 	: 26.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_EDITOR_BUILDING


#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"editor"
#include <northstarcpp/editor/base/editor_extensions.h>

#pragma managed(push,off)
#include "editor_memory.h"
#pragma managed(pop)

namespace northstarcpp {
namespace editor {
#	include <northstarcpp/editor/base/unmanaged_string.h>
} // namespace editor
} // namespace northstarcpp

using Microsoft::Win32::RegistryKey;
RegistryKey^ get_sub_key(RegistryKey^ root, System::String^ name);

#endif // #ifndef PCH_H_INCLUDED