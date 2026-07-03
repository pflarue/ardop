// ARDOPModem.swift - Swift / Objective-C wrapper around the embedded ARDOP
// modem C library (CArdop / ardop_lib.h).
//
// ARDOPModem owns a single modem instance.  Configure it (callsign, grid
// square, protocol mode, audio route), call start(), then send and receive
// ARQ / FEC data.  Received data and asynchronous modem events are delivered
// to the delegate on `delegateQueue` (main queue by default).
//
// The library exposes a single modem instance because the underlying C core
// uses process-wide state; create only one ARDOPModem at a time.

import Foundation
import CArdop

// MARK: - Types

/// Classification of a received data buffer (mirrors `ardop_data_tag`).
@objc public enum ARDOPDataTag: Int {
    case arq
    case fec
    case err
    case id
    case other

    init(_ c: ardop_data_tag) {
        switch c {
        case ARDOP_DATA_ARQ: self = .arq
        case ARDOP_DATA_FEC: self = .fec
        case ARDOP_DATA_ERR: self = .err
        case ARDOP_DATA_ID: self = .id
        default: self = .other
        }
    }
}

/// A snapshot of the modem's status and decode metrics (mirrors `ardop_status`).
@objc public final class ARDOPStatus: NSObject {
    @objc public let protocolState: Int
    @objc public let receiveState: Int
    @objc public let arqSubstate: Int
    @objc public let protocolMode: Int
    @objc public let bufferBytes: Int
    @objc public let rxEnabled: Bool
    @objc public let txEnabled: Bool
    @objc public let soundPlaying: Bool
    @objc public let capturing: Bool
    @objc public let leaderDetects: Int
    @objc public let frameSyncs: Int
    @objc public let goodDataDecodes: Int
    @objc public let failedDataDecodes: Int
    @objc public let leaderSNR: Float
    @objc public let averageQuality: Int

    /// Human-readable protocol state ("OFFLINE", "DISC", "ISS", ...).
    @objc public var protocolStateName: String {
        let names = ["OFFLINE", "DISC", "ISS", "IRS", "IDLE",
                     "IRStoISS", "FECSend", "FECRcv"]
        return (0..<names.count).contains(protocolState) ? names[protocolState]
            : "UNKNOWN"
    }

    fileprivate init(_ s: ardop_status) {
        protocolState = Int(s.protocol_state)
        receiveState = Int(s.receive_state)
        arqSubstate = Int(s.arq_substate)
        protocolMode = Int(s.protocol_mode)
        bufferBytes = Int(s.buffer_bytes)
        rxEnabled = s.rx_enabled
        txEnabled = s.tx_enabled
        soundPlaying = s.sound_playing
        capturing = s.capturing
        leaderDetects = Int(s.leader_detects)
        frameSyncs = Int(s.frame_syncs)
        goodDataDecodes = Int(s.good_data_decodes)
        failedDataDecodes = Int(s.failed_data_decodes)
        leaderSNR = s.leader_snr
        averageQuality = Int(s.avg_quality)
    }
}

/// Errors thrown by ARDOPModem.  Bridged to NSError (domain "ARDOPKit") for
/// Objective-C callers.
public enum ARDOPError: Error {
    case alreadyRunning
    case startFailed(code: Int32)
    case audioSession(Error)
}

// MARK: - Delegate

@objc public protocol ARDOPModemDelegate: AnyObject {
    /// Application data received from the air.
    func ardopModem(_ modem: ARDOPModem, didReceive data: Data, tag: ARDOPDataTag)

    /// Raw asynchronous event/status line from the modem command channel.
    /// Delivered for every line; the parsed convenience callbacks below are
    /// invoked in addition to this one.
    func ardopModem(_ modem: ARDOPModem, didReceiveEvent line: String)

    /// Protocol state changed (from a NEWSTATE/STATE event).
    @objc optional func ardopModem(_ modem: ARDOPModem, didChangeState state: String)
    /// PTT keyed (true) or unkeyed (false).
    @objc optional func ardopModem(_ modem: ARDOPModem, pttDidChange keyed: Bool)
    /// An ARQ session connected to `remote` at the given bandwidth.
    @objc optional func ardopModem(_ modem: ARDOPModem, didConnect remote: String, bandwidth: String)
    /// The ARQ session disconnected.
    @objc optional func ardopModemDidDisconnect(_ modem: ARDOPModem)
}

// MARK: - ARDOPModem

@objc public final class ARDOPModem: NSObject {
    /// The current live instance, used by the C callback trampolines.  The C
    /// core is a singleton, so there is at most one.
    fileprivate static weak var current: ARDOPModem?

    private var handle: OpaquePointer?

    /// Delegate receiving data and events.
    @objc public weak var delegate: ARDOPModemDelegate?
    /// Queue on which delegate methods are invoked.  Defaults to the main queue.
    @objc public var delegateQueue: DispatchQueue = .main

    /// True between a successful start() and stop().
    @objc public var isRunning: Bool { ardop_is_running(handle) }

    @objc public override init() {
        super.init()
        ARDOPModem.current = self
        var cb = ardop_callbacks()
        cb.ctx = nil
        cb.on_data = ardopOnDataTrampoline
        cb.on_event = ardopOnEventTrampoline
        handle = ardop_create(&cb)
    }

    deinit {
        if let h = handle {
            ardop_destroy(h)
        }
        if ARDOPModem.current === self {
            ARDOPModem.current = nil
        }
    }

    // MARK: Lifecycle

    /// Configure and activate the audio session (iOS), then start the modem
    /// thread and open audio.  Throws on failure.
    @objc public func start() throws {
        guard !isRunning else { throw ARDOPError.alreadyRunning }
        do {
            try AudioSession.activate(route: pendingRoute)
        } catch {
            throw ARDOPError.audioSession(error)
        }
        let rc = ardop_start(handle)
        if rc != 0 {
            AudioSession.deactivate()
            throw ARDOPError.startFailed(code: rc)
        }
    }

    /// Stop the modem thread and close audio.
    @objc public func stop() {
        ardop_stop(handle)
        AudioSession.deactivate()
    }

    // MARK: Configuration

    @objc public var callsign: String = "" {
        didSet { ardop_set_callsign(handle, callsign) }
    }

    @objc public var gridSquare: String = "" {
        didSet { ardop_set_gridsquare(handle, gridSquare) }
    }

    /// "ARQ", "FEC", or "RXO".
    @objc public func setProtocolMode(_ mode: String) {
        ardop_set_protocolmode(handle, mode)
    }

    /// Send an arbitrary host command (see docs/Host_Interface_Commands.md),
    /// e.g. "DRIVELEVEL 80", "ARQBW 500MAX".  Must not contain a carriage return.
    @objc @discardableResult public func command(_ cmd: String) -> Bool {
        return ardop_command(handle, cmd) == 0
    }

    // MARK: ARQ

    @objc @discardableResult
    public func connect(to target: String, attempts: Int = 0) -> Bool {
        return ardop_arq_connect(handle, target, Int32(attempts)) == 0
    }

    @objc @discardableResult public func disconnect() -> Bool {
        return ardop_arq_disconnect(handle) == 0
    }

    @objc @discardableResult public func abort() -> Bool {
        return ardop_arq_abort(handle) == 0
    }

    @objc @discardableResult public func sendARQ(_ data: Data) -> Bool {
        guard !data.isEmpty else { return false }
        return data.withUnsafeBytes { raw in
            ardop_arq_send(handle, raw.bindMemory(to: UInt8.self).baseAddress,
                           Int32(data.count)) == 0
        }
    }

    // MARK: FEC

    @objc @discardableResult public func sendFEC(_ data: Data) -> Bool {
        guard !data.isEmpty else { return false }
        return data.withUnsafeBytes { raw in
            ardop_fec_send(handle, raw.bindMemory(to: UInt8.self).baseAddress,
                           Int32(data.count)) == 0
        }
    }

    @objc @discardableResult public func purgeBuffer() -> Bool {
        return ardop_purge_buffer(handle) == 0
    }

    // MARK: Status

    @objc public var status: ARDOPStatus {
        var s = ardop_status()
        ardop_get_status(handle, &s)
        return ARDOPStatus(s)
    }

    // MARK: Audio route

    /// Route to apply when start() activates the audio session.  Set via
    /// selectAudioRoute(_:).
    fileprivate var pendingRoute: ARDOPAudioRoute?

    /// Available audio input routes (built-in mic, USB, Bluetooth, ...).
    @objc public static func availableAudioRoutes() -> [ARDOPAudioRoute] {
        return AudioSession.availableRoutes()
    }

    /// Choose the input route to use.  If the modem is running, applies it
    /// immediately; otherwise it is applied when start() activates the session.
    @objc public func selectAudioRoute(_ route: ARDOPAudioRoute) {
        pendingRoute = route
        if isRunning {
            AudioSession.select(route: route)
        }
    }

    // MARK: Callback fan-out (called by the trampolines, on the modem thread)

    fileprivate func handleData(_ data: Data, tag: ARDOPDataTag) {
        guard let d = delegate else { return }
        delegateQueue.async { d.ardopModem(self, didReceive: data, tag: tag) }
    }

    fileprivate func handleEvent(_ line: String) {
        guard let d = delegate else { return }
        delegateQueue.async { [weak self] in
            guard let self = self else { return }
            d.ardopModem(self, didReceiveEvent: line)
            self.parseEvent(line, to: d)
        }
    }

    /// Recognize a few common event lines and invoke the optional delegate
    /// conveniences.  Runs on delegateQueue.
    private func parseEvent(_ line: String, to d: ARDOPModemDelegate) {
        let parts = line.split(separator: " ", omittingEmptySubsequences: true)
                        .map(String.init)
        guard let verb = parts.first else { return }
        switch verb {
        case "PTT":
            if parts.count >= 2 {
                d.ardopModem?(self, pttDidChange: parts[1].uppercased() == "TRUE")
            }
        case "CONNECTED":
            let remote = parts.count >= 2 ? parts[1] : ""
            let bw = parts.count >= 3 ? parts[2] : ""
            d.ardopModem?(self, didConnect: remote, bandwidth: bw)
        case "DISCONNECTED":
            d.ardopModemDidDisconnect?(self)
        case "NEWSTATE", "STATE":
            if parts.count >= 2 {
                d.ardopModem?(self, didChangeState: parts[1])
            }
        default:
            break
        }
    }
}

// MARK: - C callback trampolines
//
// These are plain C function pointers (@convention(c)) and so cannot capture
// context.  They recover the live ARDOPModem via the static weak reference
// (the C core is a singleton) and copy the transient C buffers before handing
// off, since the buffers are only valid for the duration of the call.

private func ardopOnDataTrampoline(_ ctx: UnsafeMutableRawPointer?,
                                   _ tag: ardop_data_tag,
                                   _ data: UnsafePointer<UInt8>?,
                                   _ len: Int32) {
    guard let modem = ARDOPModem.current else { return }
    let bytes: Data
    if let data = data, len > 0 {
        bytes = Data(bytes: data, count: Int(len))
    } else {
        bytes = Data()
    }
    modem.handleData(bytes, tag: ARDOPDataTag(tag))
}

private func ardopOnEventTrampoline(_ ctx: UnsafeMutableRawPointer?,
                                    _ line: UnsafePointer<CChar>?) {
    guard let modem = ARDOPModem.current, let line = line else { return }
    modem.handleEvent(String(cString: line))
}
