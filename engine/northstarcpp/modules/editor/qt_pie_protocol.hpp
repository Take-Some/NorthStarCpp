// NorthStar Editor <-> Player: versioned, pointer-free Win32 IPC identifiers.
// Shared by both executables. Never renumber an existing .v1 message.
#pragma once

namespace northstarcpp::editor::pie_protocol {
inline constexpr wchar_t exit_request[] = L"NorthStarCpp.PlayInEditor.Exit.v1";
inline constexpr wchar_t pause_request[] = L"NorthStarCpp.PlayInEditor.PauseRequest.v1";
inline constexpr wchar_t resume_request[] = L"NorthStarCpp.PlayInEditor.ResumeRequest.v1";
inline constexpr wchar_t eject_request[] = L"NorthStarCpp.PlayInEditor.EjectRequest.v1";
inline constexpr wchar_t pause[] = L"NorthStarCpp.PlayInEditor.Pause.v1";
inline constexpr wchar_t resume[] = L"NorthStarCpp.PlayInEditor.Resume.v1";
inline constexpr wchar_t step_request[] = L"NorthStarCpp.PlayInEditor.StepRequest.v1";
inline constexpr wchar_t step[] = L"NorthStarCpp.PlayInEditor.Step.v1";
inline constexpr wchar_t eject[] = L"NorthStarCpp.PlayInEditor.Eject.v1";
inline constexpr wchar_t possess[] = L"NorthStarCpp.PlayInEditor.Possess.v1";
} // namespace northstarcpp::editor::pie_protocol
