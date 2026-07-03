# ARDOPKit — ARDOP modem for iOS / iPadOS

A Swift Package that embeds the [ardopcf](../../README.md) ARDOP modem as a
library for iOS and iPadOS apps, callable from both **Swift** and
**Objective-C**.

It exposes only what an app needs: select the audio interface, start/stop the
modem, send and receive ARQ and FEC data, and read status/metrics. There are no
TCP sockets, no Web UI, and no command-line front end — the modem is driven
entirely in-process through a C API.

## Layout

```
host/ios/
├── Package.swift
└── Sources/
    ├── CArdop/                 C library target
    │   ├── csrc -> ../../../../src   (symlink: modem core, embed shim, iOS audio)
    │   ├── clib -> ../../../../lib   (symlink: Reed-Solomon, etc.)
    │   └── include/            public C header + module map (ardop_lib.h)
    └── ARDOPKit/               Swift wrapper
        ├── ARDOPModem.swift    ARDOPModem class + ARDOPModemDelegate
        └── AudioRoute.swift    AVAudioSession route selection
```

The C sources live in the repository's `src/` and `lib/` and are pulled in
through the `csrc`/`clib` symlinks, so there is a single source of truth shared
with the desktop builds. The platform-neutral embedded layer in `src/embed/`
(C API facade + host shim) is designed to be reused by a future Android/Kotlin
binding via JNI.

## Building

```sh
# macOS (for development / CI smoke test)
swift build

# iOS device / simulator
xcodebuild -scheme ARDOPKit -destination 'generic/platform=iOS Simulator' build
```

Add it to an app via Xcode → *Add Package Dependencies…* → this directory, or in
a `Package.swift` dependency on the `ARDOPKit` product.

## Required Info.plist keys

```xml
<key>NSMicrophoneUsageDescription</key>
<string>Used to receive ARDOP radio audio.</string>
<!-- For RX/TX while the app is backgrounded: -->
<key>UIBackgroundModes</key>
<array><string>audio</string></array>
```

## Swift usage

```swift
import ARDOPKit

final class Radio: ARDOPModemDelegate {
    let modem = ARDOPModem()

    func begin() throws {
        modem.delegate = self
        modem.callsign = "N0CALL"
        modem.gridSquare = "CN87"
        modem.setProtocolMode("ARQ")

        // Optionally pick a specific input (USB audio interface, etc.)
        if let usb = ARDOPModem.availableAudioRoutes()
            .first(where: { $0.portType == "USBAudio" }) {
            modem.selectAudioRoute(usb)
        }

        try modem.start()
        modem.connect(to: "N1CALL")
        modem.sendARQ(Data("hello".utf8))
    }

    // MARK: ARDOPModemDelegate
    func ardopModem(_ m: ARDOPModem, didReceive data: Data, tag: ARDOPDataTag) {
        print("RX \(tag): \(data.count) bytes")
    }
    func ardopModem(_ m: ARDOPModem, didReceiveEvent line: String) {
        print("event: \(line)")
    }
    func ardopModem(_ m: ARDOPModem, didConnect remote: String, bandwidth: String) {
        print("connected to \(remote) @ \(bandwidth)")
    }
}
```

For connectionless FEC datagrams, use `setProtocolMode("FEC")` and
`modem.sendFEC(data)`.

## Objective-C usage

```objc
@import ARDOPKit;

ARDOPModem *modem = [[ARDOPModem alloc] init];
modem.delegate = self;          // conform to ARDOPModemDelegate
modem.callsign = @"N0CALL";
[modem setProtocolMode:@"ARQ"];

NSError *err = nil;
if (![modem startAndReturnError:&err]) {
    NSLog(@"start failed: %@", err);
}
[modem connectTo:@"N1CALL" attempts:0];
[modem sendARQ:[@"hello" dataUsingEncoding:NSUTF8StringEncoding]];
```

## Notes

- **One instance.** The underlying modem core uses process-wide state; create a
  single `ARDOPModem` at a time.
- **Delegate threading.** Callbacks are delivered on `delegateQueue` (main queue
  by default).
- **PTT.** iOS has no serial/CM108/GPIO PTT. Key the radio with VOX, or drive an
  external/Bluetooth keyer from the `pttDidChange` delegate callback.
- **Audio route.** `.measurement` mode is used to disable system audio
  processing (AGC, etc.) that would distort modem tones. The route may also be
  changed by the user from iOS Control Center / Settings.
