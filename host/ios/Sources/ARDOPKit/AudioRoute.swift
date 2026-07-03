// AudioRoute.swift - AVAudioSession audio route selection for ARDOPKit.
//
// On iOS the modem's audio I/O follows the shared AVAudioSession route.  This
// file enumerates the available input routes (built-in mic, USB-C / Lightning
// audio interfaces, Bluetooth, ...), lets the application choose one, and
// configures + activates the session for low-latency, unprocessed audio
// suitable for a data modem.
//
// On platforms without AVAudioSession (e.g. macOS, used for development builds)
// these become no-ops and the modem uses the system default audio device.

import Foundation

#if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
import AVFoundation
#endif

/// An available audio input route.
@objc public final class ARDOPAudioRoute: NSObject {
    /// Human-readable name, e.g. "iPhone Microphone", "USB Audio Device".
    @objc public let name: String
    /// Stable unique identifier of the port.
    @objc public let uid: String
    /// Port type string, e.g. "MicrophoneBuiltIn", "USBAudio", "BluetoothHFP".
    @objc public let portType: String

    #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
    fileprivate let port: AVAudioSessionPortDescription?

    fileprivate init(port: AVAudioSessionPortDescription) {
        self.name = port.portName
        self.uid = port.uid
        self.portType = port.portType.rawValue
        self.port = port
    }
    #endif

    @objc public init(name: String, uid: String, portType: String) {
        self.name = name
        self.uid = uid
        self.portType = portType
        #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
        self.port = nil
        #endif
        super.init()
    }

    public override var description: String {
        return "ARDOPAudioRoute(\(name) [\(portType)])"
    }
}

/// Wraps AVAudioSession configuration for the modem.
enum AudioSession {
    /// ARDOP processes audio at 12 kHz internally; request a hardware rate that
    /// the AudioQueue can cleanly resample from.
    static let preferredSampleRate: Double = 48000

    static func availableRoutes() -> [ARDOPAudioRoute] {
        #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
        let session = AVAudioSession.sharedInstance()
        // availableInputs is only populated once the category permits recording.
        try? session.setCategory(.playAndRecord, mode: .measurement,
                                 options: [.allowBluetooth])
        return (session.availableInputs ?? []).map { ARDOPAudioRoute(port: $0) }
        #else
        return []
        #endif
    }

    /// Configure and activate the session, optionally preferring `route`.
    static func activate(route: ARDOPAudioRoute?) throws {
        #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
        let session = AVAudioSession.sharedInstance()
        // .measurement mode disables AGC and other system audio processing that
        // would distort modem tones.
        try session.setCategory(.playAndRecord, mode: .measurement,
                                options: [.allowBluetooth])
        try? session.setPreferredSampleRate(preferredSampleRate)
        if let route = route {
            select(route: route)
        }
        try session.setActive(true)
        #else
        _ = route
        #endif
    }

    static func deactivate() {
        #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
        try? AVAudioSession.sharedInstance().setActive(
            false, options: [.notifyOthersOnDeactivation])
        #endif
    }

    static func select(route: ARDOPAudioRoute) {
        #if canImport(AVFoundation) && (os(iOS) || os(tvOS) || os(watchOS) || os(visionOS))
        // Re-resolve the port from the current available inputs by uid in case
        // the cached description is stale (e.g. device re-enumerated).
        let session = AVAudioSession.sharedInstance()
        let port = (session.availableInputs ?? []).first { $0.uid == route.uid }
            ?? route.port
        try? session.setPreferredInput(port)
        #else
        _ = route
        #endif
    }
}
