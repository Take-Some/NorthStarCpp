#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_UI_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"ui"
#include <northstarcpp/extensions.h>

#include "ui_memory.h"
#include <northstarcpp/ui/ui.h>

using namespace northstarcpp;
using namespace northstarcpp::ui;

#endif // #ifndef PCH_H_INCLUDED