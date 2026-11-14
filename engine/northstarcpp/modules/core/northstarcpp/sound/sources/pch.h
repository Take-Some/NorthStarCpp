////////////////////////////////////////////////////////////////////////////
//	Created 	: 22.12.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PCH_H_INCLUDED
#define PCH_H_INCLUDED

#define NORTHSTARCPP_SOUND_BUILDING

#ifndef NORTHSTARCPP_STATIC_LIBRARIES
#	define NORTHSTARCPP_ENGINE_BUILDING
#endif // #ifndef NORTHSTARCPP_STATIC_LIBRARIES

#define NORTHSTARCPP_LOG_MODULE_INITIATOR	"sound"
#include <northstarcpp/extensions.h>

#include <northstarcpp/os_preinclude.h>
#undef NOUSER
#undef NOMSG
#include <northstarcpp/os_include.h>

#ifndef SN_TARGET_PS3
#	include <xaudio2.h>
#	include <x3daudio.h>
#else // #ifndef SN_TARGET_PS3
	struct IXAudio2SourceVoice;
	struct IXAudio2;
	struct IXAudio2MasteringVoice;
	struct IXAudio2VoiceCallback { };
	struct WAVEFORMATEX { };
	struct X3DAUDIO_HANDLE { };
	struct X3DAUDIO_LISTENER { };
	struct XAUDIO2_VOICE_STATE { };
	typedef u32 UINT32;
	typedef long HRESULT;
#endif // #ifndef SN_TARGET_PS3

#include <vorbis/vorbisfile.h>

#include "sound_memory.h"

#endif // #ifndef PCH_H_INCLUDED