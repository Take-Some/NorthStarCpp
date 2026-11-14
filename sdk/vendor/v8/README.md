# Versioned V8 SDKs

This is the canonical third-party V8 SDK root. Each version contains
its own include/, lib/, build-config/, LICENSE, and provenance manifest.

Currently: 15.7.112.23-7-windows-x64-msvc (development-only).
Exact V8 15.8 source is in sdk/vendor/sources/.

Do not copy vendor headers into sdk/include/northstarcpp, combine
different V8 major/minor libraries or GN configurations, or include
unattested vendor DLLs in dist. See docs/V8_VENDOR_SDK_DOWNLOAD_2026-11-14.md.
