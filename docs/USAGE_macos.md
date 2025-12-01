# USAGE (macOS Preview)

This short guide covers the current (work‑in‑progress) macOS CoreAudio backend for `ardopcf`.

> Status: Experimental. Interfaces and log strings may change. Linux & Windows remain the reference implementations.

## 1. Quick Start

1. Ensure you have built the binary (see [BUILDING.md](docs/BUILDING.md) for macOS build prerequisites). The executable will be at `build/macos/ardopcf`.
2. Run selecting host port 8515 and explicit input/output devices (positional form):

   ```bash
   ./build/macos/ardopcf 8515 "Built-in Microphone" "MacBook Pro Speakers"
   ```

   Option form (equivalent):

   ```bash
   ./build/macos/ardopcf -i "Built-in Microphone" -o "MacBook Pro Speakers" 8515
   ```

3. Connect your host (e.g. Pat) to `localhost:8515`.

If you omit `-i` and/or `-o` you can set devices later via the WebGUI or host commands (`CAPTURE`, `PLAYBACK`).

## 2. Listing Devices

On startup the macOS backend enumerates CoreAudio devices and logs something like:

```text
macOS audio devices
   Built-in Microphone (capture)
   MacBook Pro Speakers (playback)
   USB Audio CODEC (capture) (playback)
   NOSOUND (capture) (playback)
```

Device names are matched case‑sensitively when passed to `-i` / `-o`. A device UID alias can also work (shown in brackets if different).

You can re‑emit the list by restarting `ardopcf` or using the WebGUI devices panel.

## 3. Playback Device Binding (`-o`)

The macOS port creates a HAL Output `AudioUnit` and (if specified) attempts to set the current output device to the named string before starting audio. Example (representative) log sequence from the current code path:

```text
Playback device 'USB Audio CODEC' opening (channels=2)
CoreAudio: Found device ID 87 for 'USB Audio CODEC'
CoreAudio: Output device sample rate negotiated to 48000.0 Hz (2 channels)
CoreAudio: AudioUnit initialized successfully - inputEnabled=0 outputEnabled=1
CoreAudio: AudioUnits started successfully
```

If the requested device name cannot be resolved you will see a warning and CoreAudio falls back to the system default output device, e.g.:

```text
CoreAudio: Could not find device ID for playback device 'NotADevice'
```

Selecting the special sentinel `NOSOUND` disables TX while preserving the prior real device for a future `RESTORE` (see Section 4).

## 4. Capture Path Summary & RESTORE/NOSOUND Semantics

The current backend requests a float32 mono stream at the device's native rate (often 48 kHz) and performs linear‑interpolation downsampling to 12 kHz internally. (The earlier documented “negotiate 12 kHz S16 interleaved” path is not yet implemented.) Diagnostic DEBUG logs show the negotiated input rate and any significant callback frame size variation.

`NOSOUND` on capture disables RX without discarding the last working device. Later issuing `CAPTURE RESTORE` (or selecting `RESTORE` via host/WebGUI) returns to that previous device. The same semantics apply to playback: `PLAYBACK NOSOUND` disables TX; `PLAYBACK RESTORE` re‑enables the last real device.

## 5. Headless / Offline Operation

- Use `-i -1` / `-o -1` (or `CAPTURE NOSOUND` / `PLAYBACK NOSOUND`) to keep ARDOP’s state machines alive without touching CoreAudio. The macOS backend now normalizes `-1` to `NOSOUND`, so host commands and scripts can rely on the Linux/Windows behavior.
- When `--decodewav` is present, both capture and playback paths are forced into a *virtual* mode: CoreAudio is not probed and 12 kHz samples from the WAV reader are pushed through the same RX buffers used during live operation. This prevents the "CaptureDevice= is silent" warning during offline demodulation.
- Implementation detail: the CoreAudio callback plumbing is still exercised via `MacVirtualCaptureFeed()`, so the AGC, busy detector, and ARQ state machine receive samples exactly as if CoreAudio were running. This makes `--decodewav` runs representative for regression testing.
- A quick sanity check:

   ```bash
   ./build/macos/ardopcf --nologfile --decodewav test/python/tmp/sample.wav -i -1 -o -1 --hostcommands 'CONSOLELOG 2'
   ```

   The command finishes without trying to open CoreAudio, prints `[DecodeFrame]` lines for any frames in the WAV, and leaves the RX diagnostics quiet.
- The end-to-end Python suite (`python test/python/test_wav_io.py`) now succeeds on macOS using the default `-i -1 -o -1` settings, matching the Linux CI workflow.

## 6. Known Limitations / Next Steps

- TX underrun/overflow counters & periodic statistics (planned).
- Optional self‑test transmit tone flag (planned) to verify routing without a host.
- RX debug WAV dump remains a future idea for developer diagnostics.

## 7. Known macOS Differences

- **Audio conversion strategy:** The macOS backend always negotiates whatever sample rate the selected CoreAudio device prefers (often 44.1/48 kHz) and uses in-process `AudioConverter` resamplers to bridge that rate to ARDOP’s fixed 12 kHz modem domain. Linux (ALSA) and Windows (Waveform) instead request 12 kHz directly from the driver and rely on the OS/device to cope when that rate is unavailable, so the macOS path delivers consistent SRC quality even when hardware cannot clock at 12 kHz.

## 8. Getting Help

For issues specific to macOS bring logs (run with higher verbosity) to the project issue tracker or user group. Include:

- Command line used
- First 100 lines of log
- Any `CoreAudio:` WARN/ERROR lines
