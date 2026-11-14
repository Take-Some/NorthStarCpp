// NorthStarCpp native crash capture; 2026 Kayla's Systems — Kayla Verner.
#pragma once

namespace northstarcpp::diagnostics {

// Installs the process-wide Windows unhandled-exception/terminate handlers.
// Dumps and reports remain on the user's machine; nothing is uploaded.
void install_crash_handler() noexcept;

} // namespace northstarcpp::diagnostics
