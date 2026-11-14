////////////////////////////////////////////////////////////////////////////
//	Created		: 26.03.2010
//	Author		: Alexander Maniluk
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef WRITERS_H_INCLUDED
#define WRITERS_H_INCLUDED

#include "writer.h"
#include "buffer_writer_device.h"

namespace northstarcpp
{

typedef writer<buffer_writer_device>		buffer_writer;

} // namespace northstarcpp

#endif // #ifndef WRITERS_H_INCLUDED