////////////////////////////////////////////////////////////////////////////
//	Created		: 26.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_DIALOG_EDITOR_BUILDING

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"dialog_editor"
#include <northstarcpp/editor/base/editor_extensions.h>

#pragma managed(push,off)
#include "dialog_editor_memory.h"
#pragma managed(pop)

namespace northstarcpp {
namespace dialog_editor {
#	include <northstarcpp/editor/base/unmanaged_string.h>
} // namespace dialog_editor
} // namespace northstarcpp
#endif // #ifndef PCH_H_INCLUDED