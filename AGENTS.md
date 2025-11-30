# Project Overview

- ardopcf is a cross-platform C implementation of the Amateur Radio Digital Open Protocol (ARDOP) for audio modem data exchange; minimal dependencies and interoperability with the ARDOP spec are priorities.
- Code layout: `src/common` cross-platform core (protocol, DSP, host/web interfaces); `src/linux` ALSA audio; `src/windows` WinMM audio; `src/macos` CoreAudio implementation; web GUI assets in `webgui/` are embedded into C at build time.
- Bundled libs under `lib/`: Reed-Solomon (`rockliff`), WebSocket server (`ws_server`), logging (`zf_log`), and `txt2c` for HTML/JS embedding.
- Utilities: Python diagnostic host in `host/python/`; Python integration/WAV tests in `test/python/`; C unit tests in `test/ardop/`.
- Build outputs: `build/<platform>/ardopcf[.exe]`; default TCP host command port is 8515 (data port +1) used by host programs and WebGUI.

## Setup & Environment

- Linux: `sudo apt install build-essential libasound2-dev` (required); add `libcmocka-dev` for tests.
- macOS: install Xcode Command Line Tools (`xcode-select --install`), then `brew install cmocka` (and optionally `pkg-config` if missing); Homebrew headers/libs live under `/opt/homebrew` (arm64) or `/usr/local` (x86_64) and are auto-detected by the Makefile.
- Windows: MinGW toolchain (e.g., winlibs or MSYS2 `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-make`); cmocka via MSYS2 (`mingw-w64-x86_64-cmocka`) for tests.
- Cross-compile Windows on Linux: `sudo apt install mingw-w64`; use `make CC_NATIVE=gcc CC=i686-w64-mingw32-gcc-posix WIN32=1`.
- Python 3 (stdlib only) for host/integration scripts; no pip dependencies declared.
- `TXT2C` env var may point to an existing `txt2c` binary; otherwise Makefile builds `lib/txt2c/txt2c` automatically.

## Build & Run Commands

- `make -j$(nproc)` (Linux), `make -j$(sysctl -n hw.ncpu)` (macOS), or `mingw32-make -j` (Windows) builds ardopcf for the detected platform into `build/<platform>/ardopcf`.
- `mingw32-make` builds on Windows with MinGW; ensure the MinGW `bin` dir is on `PATH`.
- `make CC_NATIVE=gcc CC=i686-w64-mingw32-gcc-posix WIN32=1` cross-compiles Windows binaries from Linux.
- Sanity check: `./build/linux/ardopcf -h`, `./build/macos/ardopcf -h`, or `build/windows/ardopcf.exe -h` depending on the target.
- Cleaning: `make clean` removes platform build artifacts; `make cleanall` removes all of `build/`.

## Testing Instructions

- `make buildtest` compiles C unit tests; `make test` builds and runs them (cmocka required). On macOS ensure Homebrew's `cmocka` is installed and visible (the Makefile already adds `/opt/homebrew` paths).
- CI (GitHub Actions) runs `make` then `make test` on Ubuntu (installs build-essential, libasound2-dev, libcmocka-dev) and on Windows via MSYS2 (gcc, mingw32-make, cmocka); macOS testing is performed locally for now until CI runners are added.
- Python integration/WAV tests: from `test/python/`, run `python test_wav_io.py` (or other `test_*.py`); requires a fresh `ardopcf` binary in the repo root; outputs logs/WAVs to `test/python/tmp` and keeps failure artifacts.
- Diagnostic host: `python host/python/diagnostichost.py --host <addr> --port 8515` for manual TCP control/testing (stdin-driven, stdlib only).

## Code Style Guidelines

- C code uses Hungarian-like prefixes (`byt*`, `int*`, `bln*`) and shared protocol state; match existing naming/type patterns.
- Keep cross-platform logic in `src/common`; isolate platform-specific code in `src/linux`, `src/windows`, or `src/macos`. The macOS CoreAudio backend is now first-class and should stay aligned with the Linux/Windows feature set.
- Favor readability for radio experimenters: add concise comments for DSP/algorithmic changes and reference relevant specs.
- Maintain minimal external deps; avoid introducing new libraries/tools.
- Preserve ARDOP spec interoperability; avoid protocol deviations without coordination.
- Use existing logging/host/WebGUI paths (`log.c`, `log_file.c`, `ws_server`, WebGUI messaging) instead of ad-hoc I/O.

## Tooling & Scripts

- Makefile is authoritative for build/test; it also runs `txt2c` to embed `webgui` assets as generated C sources under `build/<platform>/src/common/gen-*`.
- `lib/rockliff` supplies Reed-Solomon; `lib/ws_server` powers WebGUI WebSocket; `lib/zf_log` provides logging helpers—keep their interfaces stable.
- Diagnostic host script: `host/python/diagnostichost.py` for interactive or scripted commands via `--files`/`--strings`.
- Python integration utilities in `test/python` write temp WAVs/logs to `test/python/tmp`; clean up as needed but retain failure artifacts for debugging.

## Security Considerations

- Host/WebGUI interfaces listen on TCP (default 8515/8516); avoid exposing to untrusted networks and adjust firewall rules accordingly.
- Do not commit generated logs or WAV captures (ignored via `.gitignore`); scrub sensitive station info from shared assets.
- Builds should not require elevated privileges beyond installing system packages; avoid adding privileged steps to scripts or tests.

## Repository Conventions

- Build artifacts live in `build/<platform>`; when switching branches after a build, run `make clean` if you hit build issues.
- Temporary outputs: `test/python/tmp` for WAVs/logs; log/WAV files are ignored—keep them out of commits.
- Documentation PRs for released behavior target `master`; other changes typically target `develop` (see `docs/CONTRIBUTING.md`).
- Inline documentation is encouraged for clarity; keep code approachable to newcomers.
- Line-by-line review of any AI-assisted changes is expected; submit only code you understand and will support.
- This AGENTS.md supersedes the legacy CLAUDE.md guidance; keep this file authoritative going forward.

## Assumptions

- No formal formatter/linter config is present; follow prevailing style in touched files.
- macOS support is currently unverified/minimal; treat mac builds as experimental unless documented otherwise.
