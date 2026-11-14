//-------------------------------------------------------------------------------------------
//	Created		: 04.02.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
//-------------------------------------------------------------------------------------------
#pragma once

namespace northstarcpp {
namespace dialog_editor {
	public enum class ChunkType: int 
	{
		Alphanumeric = 0,
		Numeric = 1,
	};

	using namespace System;

	public ref class AlphanumComparator: Collections::Generic::IComparer<String^>
	{
		private:
					bool	InChunk		(char ch, char otherCh);

		public:
			virtual	int		Compare		(System::String^ x, System::String^ y);
	}; // ref class AlphanumComparator
} // namespace dialog_editor
} // namespace northstarcpp