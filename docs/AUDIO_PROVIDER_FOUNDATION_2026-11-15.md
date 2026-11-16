# engine.audio — extensible provider foundation (2026-11-15)

## Status

This change **extends** the existing engine.audio capability. It does not
introduce a competing loader, break the WinMM provider, or bundle FMOD/Wwise.

Existing baseline:
- `NorthStarAudioApi` ABI 1: create/destroy, one PCM16 play, stop.
- Selected via the shared `NorthStarModule` loader; external DLL/SO and
  builtin providers use the same capability contract.
- Host `AudioOutput` remains source- and ABI-compatible.

New optional extension:
- `NORTHSTARCPP_AUDIO_VOICES_ABI` = 2;
- `NorthStarAudioVoicesApi` is queried through `Module::query_extension`
  *on the same selected provider* and is not a separate module;
- create individual voices, stop by ID, gain, 3D position, listener, frame update;
- `AudioEngine` C++ facade owns the provider context and keeps the DLL loaded
  until destruction, exposing `supports_voices()` for capability negotiation;
- extension methods fail explicitly if the active provider remains ABI 1 only;
- opt-in provider name selection is possible via the `AudioEngine` constructor;
- old WinMM builds remain valid. WinMM does **not** claim multivoice/3D support.

## Provider authoring contract

- Every provider must implement ABI 1 to be selected.
- ABI 2 is optional. If implemented, the provider query function returns an
  independent `NorthStarModule` descriptor for api version 2 and the same
  `engine.audio` capability ID.
- Context pointers and voice IDs are owned by the selected provider.
  `NorthStarAudioVoiceId == 0` is reserved for failure.
- All ABI 2 voice callbacks operate on the ABI 1 context pointer.
- The provider must copy or consume PCM input before returning from
  `play_voice`. Caller memory is borrowed only during the call.
- No C++ exceptions may escape an exported C ABI callback. Report failure by
  returning 0, and expose diagnostic text through `last_error`.
- The provider is responsible for sample mixing, device I/O, voice lifetime,
  spatialization and thread safety. The host merely dispatches commands.
- Stop/unload on the host side before destroying the provider context; do not
  retain a voice handle after its associated `AudioEngine` is destroyed.
- For ABI changes, introduce a new extension version rather than modifying
  the layout of a shipped table.

Example of a replacement provider:

```cpp
#include <northstarcpp/audio.hpp>

northstarcpp::AudioEngine audio("my_provider"); // selected by provider identity
if (audio.supports_voices()) {
    NorthStarAudioVoiceOptions opts{sizeof(NorthStarAudioVoiceOptions),
                                    1.0f, 0, 1, {0, 0, 2}};
    auto voice = audio.play_voice(pcm, opts);
    audio.set_voice_gain(voice, 0.5f);
    audio.stop_voice(voice);
} else {
    audio.play(pcm);  // original ABI 1 compatibility path
}
```

## Validation

`tests/capability_fixture.cpp` advertises both ABI 1 and ABI 2 for a
headless in-memory fixture. `tests/capability_smoke.cpp` exercises the same
provider selection, audio listener, voice creation, gain, position, update and
rejection of stale voice IDs. No real audio device is required for the fixture.

## Verified checks

Targeted Ninja builds succeeded for runtime, WinMM and the fixture, followed by
`northstarcpp_audio_provider_smoke`. CTest passed 2/2:
`northstarcpp_audio_provider_extension` and `northstarcpp_audio_provider_legacy`.
Full editor/player regression, release deployment and Git commit were not run.

## Next stages

1. Implement a *real* free multivoice backend (miniaudio or equivalent), with
   voice mixing, output device fallback, streaming, resampling and teardown.
2. Integrate `AudioEngine` into the editor/player runtime rather than only the
   existing `engine.audio.play` script host binding. Add ECS emitters/listeners
   and V8 event methods without exposing backend-specific types.
3. Add asset decoding/import separate from audio output: WAV/OGG first, AWC
   as a container importer later; work through the current VFS/AssetManager.
4. Add async decode queues, buses, limits/stealing, looping, 3D attenuation,
   physics-based occlusion and device hotplug tests.
5. Ensure optional proprietary providers are distributed only with permissions
   compatible with NorthStar's GPLv3 policy. Neither FMOD nor Wwise is bundled.

This stage is API and dispatch infrastructure, **not** a production mixer.
