////////////////////////////////////////////////////////////////////////////
//	Created		: 29.03.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef READERS_H_INCLUDED
#define READERS_H_INCLUDED

#include "reader.h"
#include "buffer_reader_device.h"

namespace northstarcpp
{

typedef reader<buffer_reader_device>	buffer_reader;

} // namespace northstarcpp

#endif // #ifndef READERS_H_INCLUDED