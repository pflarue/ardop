# USAGE (macOS Preview)

This short guide covers the current (work‑in‑progress) macOS CoreAudio backend for `ardopcf`.

> Status: Experimental. Interfaces and log strings may change. Linux & Windows remain the reference implementations.

## 1. Quick Start

1. Build (from repo root): `make macos`
2. Run selecting host port 8515 and explicit input/output devices:

   ```bash
   ./build/macos/ardopcf 8515 "Built-in Microphone" "MacBook Pro Speakers"
   ```

   Or using options (equivalent):

   ```bash
   ./build/macos/ardopcf -i "Built-in Microphone" -o "MacBook Pro Speakers" 8515
   ```

3. Connect your host (e.g. Pat) to localhost:8515.

If you omit `-i` and/or `-o` you can set devices later via the WebGUI or host commands (`CAPTURE`, `PLAYBACK`).

## 2. Listing Devices

On startup the macOS backend enumerates CoreAudio devices and logs something like:

```text
CoreAudio audio devices
   Built-in Microphone (capture)
   MacBook Pro Speakers (playback)
   USB Audio CODEC (capture) (playback)
```

Device names are matched case‑sensitively when passed to `-i` / `-o`. A device UID alias can also work (shown in brackets if different).

You can re‑emit the list by restarting `ardopcf` or using the WebGUI devices panel.

## 3. Playback Device Binding (`-o`)

The macOS port uses a HAL Output AudioUnit so it can bind directly to the named playback device before starting. Example log sequence:

```text
CoreAudio TX: requested playback 'USB Audio CODEC' -> FOUND
CoreAudio TX: bound requested device id=87
CoreAudio TX: started (req 12k mono) -> fmt 12000 Hz 1 ch 16-bit bound=1
```

If the requested device cannot be resolved you will see a warning during startup and the system default output will be used:

```text
Playback device 'NotADevice' not found; using default output.
```

(Exact wording may change to include a `CoreAudio:` prefix.)

## 4. Capture Path Summary

The backend attempts to negotiate a simple 12 kHz signed 16‑bit interleaved mono format. If the hardware runs at 48 kHz it performs integer decimation (factor 4). Other rates fall back to a light fractional resampler. Diagnostic logs (at DEBUG) show the negotiated input format and any fallbacks.

Environment variables (set before launching) can force behaviors:

- `ARDOP_MAC_FORCE_12K=1` – Force 12 kHz input format request.
- `ARDOP_MAC_FORCE_SINT16=1` – Force S16.
- `ARDOP_MAC_FORCE_INTERLEAVED=1` – Force interleaved buffers.
- `ARDOP_MAC_MIX_BOTH=1` – Always average stereo channels for RX even if only one is selected.

## 5. RX Debug WAV (Optional)

When compiled with `-DCA_DUMP_RX_WAV` (e.g. `make macos DEBUG_RX=1`) a rolling dump is written to `/tmp/mac_rx.wav` (12 kHz mono S16) to aid troubleshooting.

## 6. Known Limitations / Next Steps

- TX underrun/overflow counters & periodic statistics (planned).
- Optional self‑test transmit tone flag (planned) to verify routing without a host.
- Additional documentation sections (PTT integration, WebGUI screenshots) will be added as macOS support matures.

## 7. Getting Help

For issues specific to macOS bring logs (run with higher verbosity) to the project issue tracker or user group. Include:

- Command line used
- First 100 lines of log
- Any `CoreAudio:` WARN/ERROR lines

---

(End of preliminary macOS usage notes)
