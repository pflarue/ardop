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

Environment variable flags previously documented for macOS (`ARDOP_MAC_FORCE_12K`, `ARDOP_MAC_FORCE_SINT16`, `ARDOP_MAC_FORCE_INTERLEAVED`, `ARDOP_MAC_MIX_BOTH`) are **not yet implemented**. They remain planned and may be added; for now they have no effect.

## 5. RX Debug WAV (Planned)

`CA_DUMP_RX_WAV` / `DEBUG_RX=1` is **not yet wired into the macOS backend**. A future implementation may emit a rolling `/tmp/mac_rx.wav` (12 kHz mono S16) file for diagnostics. At present enabling these flags has no effect.

## 6. Known Limitations / Next Steps

- TX underrun/overflow counters & periodic statistics (planned).
- Optional self‑test transmit tone flag (planned) to verify routing without a host.
- Environment variable feature set (listed above) and RX debug WAV dump are planned but currently inactive.

## 7. Known macOS Differences

- **Audio conversion strategy:** The macOS backend always negotiates whatever sample rate the selected CoreAudio device prefers (often 44.1/48 kHz) and uses in-process `AudioConverter` resamplers to bridge that rate to ARDOP’s fixed 12 kHz modem domain. Linux (ALSA) and Windows (Waveform) instead request 12 kHz directly from the driver and rely on the OS/device to cope when that rate is unavailable, so the macOS path delivers consistent SRC quality even when hardware cannot clock at 12 kHz.

## 8. Getting Help

For issues specific to macOS bring logs (run with higher verbosity) to the project issue tracker or user group. Include:

- Command line used
- First 100 lines of log
- Any `CoreAudio:` WARN/ERROR lines
