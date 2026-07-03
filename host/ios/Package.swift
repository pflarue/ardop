// swift-tools-version:5.9
import PackageDescription

// ARDOP embeddable modem library for iOS / iPadOS.
//
// The `CArdop` target compiles the platform-neutral ardopcf modem core
// (src/common), the embedded host shim (src/embed), the iOS CoreAudio backend
// (src/ios), the shared Unix OS utilities (src/unix), and the Reed-Solomon
// library (lib/rockliff) directly from this repository.  The repository's `src`
// and `lib` directories are reached through the `csrc` and `clib` symlinks
// under Sources/CArdop so that there is a single source of truth.
//
// The TCP host interface, Web UI / WebSocket server, generated WebGUI assets,
// and the command-line front end are intentionally excluded; the library is
// driven entirely through the C API in src/embed/ardop_lib.h.

let coreSources = [
    "ARDOPC", "ARDOPCommon", "ardopSampleArrays", "ARQ", "BusyDetect", "FEC",
    "FFT", "HostInterface", "Locator", "log_file", "log", "Modulate", "Packed6",
    "RXO", "sdft", "SoundInput", "StationId", "txframe", "wav", "noise", "ptt",
    "eutf8",
].map { "csrc/common/\($0).c" }

let sources = coreSources + [
    // Embedded host shim + facade (shared with a future Android binding).
    "csrc/embed/ardop_lib.c",
    "csrc/embed/host_shim.c",
    "csrc/embed/Webgui_stub.c",
    // iOS platform backends.
    "csrc/ios/CoreAudio.c",
    "csrc/ios/os_util.c",
    // Shared Unix OS utilities (timing, serial control, TCP helpers).
    "csrc/unix/os_util.c",
    // Reed-Solomon FEC.
    "clib/rockliff/rrs.c",
]

let package = Package(
    name: "ARDOPKit",
    platforms: [
        .iOS(.v14),
        .macOS(.v11),
    ],
    products: [
        // The Swift API most consumers use.  It re-exports the C module.
        .library(name: "ARDOPKit", targets: ["ARDOPKit"]),
        // The raw C library, for consumers that prefer the C API directly.
        .library(name: "CArdop", targets: ["CArdop"]),
    ],
    targets: [
        .target(
            name: "CArdop",
            path: "Sources/CArdop",
            sources: sources,
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("csrc"),
                .headerSearchPath("clib"),
                .define("ARDOP_EMBED"),
                .define("ARDOP_IOS"),
            ],
            linkerSettings: [
                .linkedFramework("AudioToolbox"),
                .linkedFramework("CoreAudio"),
                .linkedFramework("CoreFoundation"),
            ]
        ),
        .target(
            name: "ARDOPKit",
            dependencies: ["CArdop"],
            path: "Sources/ARDOPKit",
            linkerSettings: [
                .linkedFramework("AVFoundation", .when(platforms: [.iOS, .tvOS, .watchOS])),
            ]
        ),
        // Hardware-free smoke test / usage example.  Run with `swift run ardop-demo`.
        .executableTarget(
            name: "ardop-demo",
            dependencies: ["CArdop"],
            path: "Sources/ardop-demo"
        ),
    ]
)
