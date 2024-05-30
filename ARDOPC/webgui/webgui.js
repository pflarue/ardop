"use strict"
/////////////////////////////////////////////////////////////////////////////
//
//  The protocol used to communicate with ardopcf via a WebSocket connection
//  should be considered unstable and undocumented.  It is subject to change
//  in future releases of ardopcf.
//
/////////////////////////////////////////////////////////////////////////////


// Define a 4-bit (16 values) RGB modified rainbow colormap from blue to red
// to use for the waterfall display.  A few additional colormap entries are
// also included.
let alpha = 0xFF
let colormap = [
	[0x30, 0x12, 0x3b, alpha], [0x41, 0x43, 0xa7, alpha],
	[0x47, 0x71, 0xe9, alpha], [0x3e, 0x9b, 0xfe, alpha],
	[0x22, 0xc5, 0xe2, alpha], [0x1a, 0xe4, 0xb6, alpha],
	[0x46, 0xf8, 0x84, alpha], [0x88, 0xff, 0x4e, alpha],
	[0xb9, 0xf6, 0x35, alpha], [0xe1, 0xdd, 0x37, alpha],
	[0xfa, 0xba, 0x39, alpha], [0xfd, 0x8d, 0x27, alpha],
	[0xf0, 0x5b, 0x12, alpha], [0xd6, 0x35, 0x06, alpha],
	[0xaf, 0x18, 0x01, alpha], [0x7a, 0x04, 0x03, alpha],
	// special values
	[0x00, 0x00, 0x00, alpha], // 16 black
	[0xFF, 0xFF, 0xFF, alpha], // 17 white
	[0xD0, 0xD0, 0xD0, alpha], // 18 light gray (for bw lines when !isbusy)
	[0xFF, 0x00, 0xFF, alpha], // 19 fuscia (for bw lines when isbusy)
];

let bandwidth = -1;  // in Hz.  -1 = unknown
let isbusy = 0;
let devmode = false;
let cmdhistory = [];
let cmdhistory_index = -1;
let cmdhistory_limit = 100;
let plotscale = 1;

window.addEventListener("load", function(evt) {
	let txtlog = document.getElementById("text-log");
	let decoder = new TextDecoder("utf-8");
	let encoder = new TextEncoder("utf-8");

	// rsize should match WG_SSIZE in Webgui.c.
	// This is the maximum length of messages received from ardopcf
	const rsize = 2048;
	// ssize should match WG_RSIZE in Webgui.c.
	// This is the maximum length of messages sent to ardopcf
	const ssize = 512;
	// max_avglen should match MAX_AVGLEN in Webgui.c.
	// This is the maximum number of FFT results to average.
	const max_avglen = 10;
	document.getElementById("avglenslider").max = max_avglen;
	let rdata = {
		// Assuming a single message may have a length up to rsize, create buf
		// large enough to hold up to two complete messages.  Thus allowing
		// for a single WS frame to include the last few bytes of one message
		// as well as all of a following message.
		buf: new Uint8Array(2 * rsize),
		len: 0,  // number of bytes of data in buf
		offset: 0,  // offset of next unparsed value in buf.
		condense: function() {
			// discard the portion of buf that has been parsed.
			if (this.offset == 0)
				return;
			if (this.offset == this.len)
				this.len = 0;
			else {
				this.buf.copyWithin(0, this.offset, this.len);
				this.len -= this.offset;
			}
			this.offset = 0;
		}
	};

	// return the number of bytes used to encode uvalue
	// Return -1 on failure
	const encodeUvint = (buf, size, uvalue) => {
		let i = 0;
		while(uvalue >= 0x80) {
			buf[i++] = (uvalue & 0x7f) + 0x80;
			if (i >= size)
				return (-1);
			uvalue >>= 7;
		}
		buf[i++] = uvalue;
		return (i);
	};

	// Decode a variable length unsigned integer (uvint)
	// Return the decoded value and update rdata.offset.
	// But, if unable to decode a uvint from rdata because it does not contain
	// sufficient data, then return -1 and make no changes to rdata.
	const decodeUvint = (rdata) => {
		let result = 0;
		let shift = 0;
		let original_offset = rdata.offset;
		while (true) {
			if (rdata.offset >= rdata.len) {
				// unable to decode uvint
				rdata.offset = original_offset;
				return -1;
			}
			const byte = rdata.buf[rdata.offset];
			rdata.offset += 1;
			result |= (byte & 0x7f) << shift;
			//console.log("With byte=" + byte + " result becomes " + result);
			shift += 7;
			if ((0x80 & byte) === 0) {
				return result;
			}
		}
	};

	// Decode a single byte (unsigned char)
	// Return the decoded value and update rdata.offset.
	// But, if no data is available (rdata.offset == rdata.len),
	// then return -1 and make no changes to rdata.
	// (To get a single byte as a string, use decodestr(rdata, 1))
	const decodebyte = (rdata) => {
		if (rdata.offset >= rdata.len)
			// unable to decode value
			return -1;
		rdata.offset += 1;
		return rdata.buf[rdata.offset - 1];
	}

	// Decode a string
	// Return the decoded string and update rdata.offset.
	// If strlen is -1, then return the remainder of the message.
	// But, if strlen is not -1, and insufficient data is available,
	// then return an empty string and make no changes to rdata.
	const decodestr = (rdata, strlen) => {
		if (strlen >= 0 && rdata.offset + strlen > rdata.len)
			// unable to decode value
			return "";
		if (strlen == -1)
			strlen = rdata.len - rdata.offset;
		rdata.offset += strlen;
		return decoder.decode(
			rdata.buf.slice(rdata.offset - strlen, rdata.offset));
	}

	// Return a slice of rdata and update rdata.offset.
	// If sllen is -1, then return the remainder of the message.
	// But, if sllen is not -1, and insufficient data is available,
	// then return null and make no changes to rdata.
	const decodeslice = (rdata, sllen) => {
		if (sllen >= 0 && rdata.offset + sllen > rdata.len)
			return null;
		if (sllen == -1)
			sllen = rdata.len - rdata.offset;
		rdata.offset += sllen;
		return rdata.buf.slice(rdata.offset - sllen, rdata.offset);
	}

	var WebSocketClient = (function () {
		var exports = {};
		var is_connected = false;
		var is_closing = false;
		var ws = null;

		exports.init = function(url) {
			if (!WebSocket in window){
				throw "WebSocket NOT supported by your Browser!"
			}

			if (is_connected) {
				console.log("WS connection already open.  Do nothing.");
			} else {
				console.log("Open WS connection to ", url);
				ws = new WebSocket(url);
			}
			ws.binaryType = "arraybuffer"; // otherwise defaults to blob
			ws.onopen = function() {
				is_connected = true;
				if(exports.onOpen){
					exports.onOpen();
				}
			};

			ws.onmessage = function (evt) {
				var msg = evt.data;

				if(exports.onMessage) {
					exports.onMessage(msg);
				}
			};

			ws.onerror = function (err) {
				is_connected = false;
				console.log(err);
				document.getElementById("lostcon").classList.remove("dnone");
			};

			ws.onclose = function() {
				if (is_closing) {
					console.log("Response to close received from WS server.");
					is_closing = false;
					is_connected = false;
				} else if (is_connected) {
					console.log("WS server is closing connection.");
					// Respond with confirmation of close.
					ws.close();
					is_connected = false;
				} else {
					// already closed
					console.log("Received unexpected close from WS server.");
				}
				document.getElementById("lostcon").classList.remove("dnone");
			};

		}

		exports.sendMessage = function (msg) {
			if (is_connected) {
				ws.send(msg);
			} else {
				alert("Not connected!");
			}
		};

		exports.close = function () {
			if (is_closing) {
				console.log("Close already sent to server.  No response yet.");
				document.getElementById("lostcon").classList.remove("dnone");
			} else if (is_connected) {
				console.log("Sending close to WS server.");
				ws.close();
				is_closing = true;
			} else {
				console.log("No open WS connection to close.");
			}
		};

		exports.onMessage = null;
		exports.onOpen = null;
		return exports;
	}());
	// buffer is an ArrayBuffer.  Returns string
	const buf2hex = (buffer) =>
		[...new Uint8Array(buffer)].map(
			x => x.toString(16).padStart(2, '0')
		).join('');

	// hexString is string.  Returns an Array of uint8 values
	// Optional whitespace is removed before conversion.
	// Terminate at end of hexString or upon encountering a
	// character that is neither whitespace nor valid hex.
	const fromHexString = (hexString) =>
		hexString.replace(/\s+/g, "").match(/.{1,2}/g).map((byte) =>
		parseInt(byte, 16));

	// Process messages from Webgui clients
	// Two protocol layers are defined for messages from Webgui clients.
	// The lower level protocol prefaces each message with a variable
	// length unsigned integer (uvint) which specifies the length of the
	// message excluding the length of this header.
	// The higher level protocol specifies that the first byte of each
	// message indicates the message type, and this shall always be
	// followed by a tilde '~' 0x7E.  This printable and easily typed
	// character that is not widely used provides an imperfect but
	// reasonably reliable way to detect a message handling error that
	// has caused the start of a message to be incorrectly located.
	// Each message type specifies the required format for the remainder
	// of the message, if any.  Where a message includes a string, no
	// terminating NULL will be included at the end of the string.  The
	// length of the message provided by the lower level protocol shall
	// be used to determine the length of a string or other variable
	// length data at the end of a message.
	//
	// To simplify the printing of raw messages for debugging purposes,
	// message type bytes that represent printable ASCII characters in
	// the range of 0x20 (space) to 0x7D (closing brace) shall be used
	// for messages whose entire raw content may be printed as human
	// readable text (though they will not include a NULL terminator),
	// while the remaining type bytes indicate that for logging
	// purposes, the message should be displayed as a sequence of
	// hexidecimal values.
	// "0~" no additional data: Client connected/reconnected
	// "2~" no additional data: Request ardopcf to send 5 send two tone signal.
	// "I~" no additional data: Request ardopcf to send ID frame.
	// 0x8D7E followed by one additional byte interpreted as an unsigned
	//   char in the range 0 to 100: Set DriveLevel
	// 0x9A7E followed by one additional byte interpreted as an unsigned
	//   char in the range 1 to max_avglen: Set display averaging length

	// Send data of length data_len to the server.
	// This function implements the lower level protocol of adding a
	// uvint header to the start of each message indicating the
	// length of its body (excluding the header length).
	const send_msg = (data, data_len) => {
		if (String.fromCharCode(data[1]) != "~") {
			alert("ERROR: Invalid message.  second byte is not '~'.");
			return (0);
		}
		let smsg = new Uint8Array(ssize);
		let headlen;
		if ((headlen = encodeUvint(smsg, ssize, data_len)) == -1)
			return (0);
		if (data_len + headlen > ssize) {
			alert("ERROR: data to send to ardopcf is too big.  Discarding.");
			return (0);
		}
		smsg.set(new Uint8Array(data.slice(0, data_len)), headlen);
		WebSocketClient.sendMessage(smsg.slice(0, data_len + headlen));
	}

	const send_connected = () => {
		// Notify ardopcf that this Webgui client has connected.
		send_msg(encoder.encode("0~"), 2);
	}

	const send_send2tone = () => {
		// Tell ardopcf to send an two tone test signal.
		send_msg(encoder.encode("2~"), 2);
	}

	const send_sendid = () => {
		// Tell ardopcf to send an ID frame.
		send_msg(encoder.encode("I~"), 2);
	}

	// Use throttles to limit the number of updates sent to ardopcf in
	// response to the user adjusting the AvgLen or DriveLevel sliders.
	let avglencontroltimer = false;
	let drivelevelcontroltimer = false;
	const throttledcontrol = (callback, time, controltimer) => {
		if (controltimer) return;
		controltimer = true;
		setTimeout(() => {
			callback();
			controltimer = false;
		}, time);
	}
	const setavglen = () => {
		document.getElementById("avglentext").innerHTML = ""
			+ document.getElementById("avglenslider").value;
		let msgdata = new Uint8Array(
			[0x9A, 0x7E, document.getElementById("avglenslider").value]);
		send_msg(msgdata, 3);
	}
	const setdrivelevel = () => {
		document.getElementById("driveleveltext").innerHTML =
			"" + document.getElementById("drivelevelslider").value;
		let msgdata = new Uint8Array(
			[0x8D, 0x7E, document.getElementById("drivelevelslider").value]);
		send_msg(msgdata, 3);
	}

	let clearrxtimer = null;
	let rcvoverflowtimer = null;
	let rcvunderflowtimer = null;
	WebSocketClient.onOpen = function() {
		// Connection to ardopcf established.
		console.log("WS connection to ardopcf opened.  Notify ardopcf.");
		send_connected();
	}
	WebSocketClient.onMessage = function(wsdata) {
		// Two protocol layers are defined for messages to Webgui clients.
		// The lower level protocol prefaces each message with a variable
		// length unsigned integer (uvint) which specifies the length of the
		// message excluding the length of this header.
		// The higher level protocol specifies that the first byte of each
		// message indicates the message type, and this shall always be
		// followed by a pipe '|' 0x7C.  This printable and easily typed
		// character that is not widely used provides an imperfect but
		// reasonably reliable way to detect a message handling error that
		// has caused the start of a message to be incorrectly located.
		// Each message type specifies the required format for the remainder
		// of the message, if any.  Where a message includes a string, no
		// terminating NULL will be included at the end of the string.  The
		// length of the message provided by the lower level protocol shall
		// be used to determine the length of a string or other variable
		// length data at the end of a message.
		//
		// To simplify the printing of raw messages for debugging purposes,
		// message type bytes that represent printable ASCII characters in
		// the range of 0x20 (space) to 0x7D (closing brace) shall be used
		// for messages whose entire raw content may be printed as human
		// readable text (though they will not include a NULL terminator),
		// while the remaining type bytes indicate that for logging
		// purposes, the message should be displayed as a sequence of
		// hexidecimal values.
		// "a|" followed by string: Alert for user.
		// "B|" no additional data: BUSY true
		// "b|" no additional data: BUSY false
		// "C|" followed by a string: My callsign
		//   If string has zero length, clear my callsign.
		// "c|" followed by a string: remote callsign
		//   If string has zero length, clear remote callsign.
		// "D|" no additional data: Enable Dev Mode
		// "F|" followed by a string: TX Frame type
		// "f|"
		//   followed by a character:
		//    'P' for pending, 'O' for OK, or 'F' for fail
		//   followed by a string: RX Frame state and type
		// "H|"
		//   followed by a character:
		//    'Q' for QueueCommandToHost
		//    'C' for SendCommandToHost
		//    'T' for SendCommandToHostQuiet
		//	  'R' for SendReplyToHost
		//    'F' for Command from Host.
		//	 followed by a string: Message to host
		// "h|" followed by a string: Host text data (to host)
		// "m|" followed by a string: Protocol Mode
		// "P|" no additional data: PTT true
		// "p|" no additional data: PTT false
		// "R|" no additional data: IRS true
		// "r|" no additional data: IRS false
		// "S|" no additional data: ISS true
		// "s|" no additional data: ISS false
		// "t|" followed by a string: Protocol State
		// 0x817C followed by one additional byte interpreted as an unsigned
		//   char in the range of 0 to 150: Set CurrentLevel
		// 0x8A7C
		//   followed by an unsigned char: Quality (0-100)
		//   followed by a uvint: The total number of errors detected by RS
		//   followed by a uvint: The max number of errors correctable by RS
		//   (For a failed decode, total will be set equal to max + 1)
		// 0x8B7C followed by one unsigned char: Bandwidth in Hz divided by 10.
		// 0x8C7C followed by 103 bytes of data: FFT power (dBfs 4 bits each).
		// 0x8D7C followed by one additional byte interpreted as an unsigned
		//   char in the range 0 to 100: Set DriveLevel
		// 0x8E7C followed by unsigned char data: Pixel data (x, y, color)
		//   color is 1, 2, or 3 for poor, ok, good.
		// 0x8F7C followed by unsigned char data: Host non-text data (to host)
		//	 The first three characters of data are text data indicating
		//	 data type 'FEC', 'ARQ', 'RXO', 'ERR', etc.
		// 0x9A7C followed by one additional byte interpreted as an unsigned
		//   char in the range 1 to MAX_AVGLEN: Set display averaging length

		if(!(wsdata instanceof ArrayBuffer)) {
			alert("Unexpected WS data format.  Closing connection.");
			WebSocketClient.close();
			return;
		}
		if (wsdata.byteLength + rdata.len > 2 * rsize) {
			alert("ERROR.  rdata.buf overrun.  Closing connection.");
			WebSocketClient.close();
			return;
		}
		rdata.buf.set(new Uint8Array(wsdata), rdata.len);
		rdata.len += wsdata.byteLength;
		let msglen;
		let startoffset = rdata.offset; // offset of msglen
		while((msglen = decodeUvint(rdata)) >= 0) {
			let msgoffset = rdata.offset; // offset after msglen
			if (msglen == 0) {
				// An empty message.  Do nothing.
				// update startoffset before parsing next msglen
				startoffset = rdata.offset;
				continue;
			}
			if ((rdata.len - rdata.offset) < msglen) {
				// The complete message has not yet been received.
				// rewind parsing of msglen
				rdata.offset = startoffset;
				break;
			}
			// For readability, use equivalent string value for msgtype
			// Using decodestr(rdata, 1) wouldn't work for non-text types.
			let msgtype = String.fromCharCode(decodebyte(rdata));
			if (msgtype == "\uffff") {
				console.log("Message type not available.  Message too short.",
					rdata.buf.slice(startoffset, msgoffset + msglen));
				alert("Message type not available.  Closing connection.");
				WebSocketClient.close();
				return;
			}
			let pipe = decodestr(rdata, 1);
			console.log("pipe", pipe);
			if (pipe != "|") {
				console.log("Expected '|' after message type byte not found.",
					rdata.buf.slice(startoffset, msgoffset + msglen));
				alert("Invalid message structure.  Closing connection.");
				WebSocketClient.close();
				return;
			}
			switch (msgtype) {
				case "a": {
					// Alert message (display)
					let str = decodestr(rdata, -1);
					if (str == "")
						str = "ERROR: Invalid 'Alert' message received."
					alert(str);
					break;
				}
				case "B":
					// BUSY true
					isbusy = 1;
					// txtlog.value += "BUSY = true\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("busy").classList.remove("hidden");
					break;
				case "b":
					// BUSY false
					isbusy = 0;
					// txtlog.value += "BUSY = false\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("busy").classList.add("hidden");
					break;
				case "C": {
					// My callsign
					let mycall = decodestr(rdata, -1);
					if (mycall == "") {
						// clear mycall
						txtlog.value += "Clear my callsign\n";
						document.getElementById("mycall").innerHTML =
							"(MYCALL NOT SET)";
					} else {
						txtlog.value += "My callsign = " + mycall + "\n";
						document.getElementById("mycall").innerHTML = mycall;
					}
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				}
				case "c": {
					// Remote callsign
					let rcall = decodestr(rdata, -1);
					if (rcall == "") {
						// clear rcall
						txtlog.value += "[" + (new Date().toISOString()) + "]"
							+ "  Clear remote callsign.  Was "
							+ document.getElementById("rcall").innerHTML
							+ "\n";
						document.getElementById("rcall").innerHTML = "";
					} else {
						txtlog.value += "[" + (new Date().toISOString()) + "]"
							+ "  Remote callsign = " + rcall + "\n";
						document.getElementById("rcall").innerHTML = rcall;
					}
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				}
				case "D":
					// Enable Dev Mode
					devmode = true;
					document.getElementById("devmode")
						.classList.remove("dnone");
					txtlog.value += "DevMode Enabled\n";
					break;
				case "F": {
					// TX Frame type
					let txfrtype = decodestr(rdata, -1);
					let txe = document.getElementById("txtype");
					if (txfrtype != "") {
						txtlog.value += "TX Frame Type = " + txfrtype + "\n";
						txtlog.scrollTo(0, txtlog.scrollHeight);
					}
					txe.innerHTML = txfrtype;
					if (txfrtype == "DataNAK")
						txe.classList.add("txnak");
					if (txfrtype == "DataACK")
						txe.classList.add("txack");
					break;
				}
				case "f": {
					// RX Frame type
					let rxstatus = decodestr(rdata, 1);
					let rxfrtype = decodestr(rdata, -1);
					let rxe = document.getElementById("rxtype");
					rxe.classList.remove("rxstate_pending");
					rxe.classList.remove("rxstate_ok");
					rxe.classList.remove("rxstate_fail");
					rxe.classList.remove("rxstate_none");
					clearTimeout(clearrxtimer);  // eliminate old timer
					if (rxstatus == ""
						|| "POF".indexOf(rxstatus) == -1
					) {
						alert("Invalid state for RX Frame Type.");
						rxe.classList.add("rxstate_none");
					}
					switch (rxstatus) {
					  case "P":
						rxe.classList.add("rxstate_pending");
						// Assume that this will always be folowed
						// by a rxstate_ok or rxstate_fail, so don't
						// set a timer to clear it.
						// Don't write pending rx to txtlog
						break;
					  case "O":
						rxe.classList.add("rxstate_ok");
						clearrxtimer = setTimeout(() => {
							rxe.innerHTML = "";
							rxe.classList.remove("rxstate_ok");
							rxe.classList.add("rxstate_none");
						}, 5000);  /// clear after 5 seconds
						txtlog.value += "RX: " + rxfrtype + " PASS\n";
						txtlog.scrollTo(0, txtlog.scrollHeight);
						break;
					  case "F":
						rxe.classList.add("rxstate_fail");
						clearrxtimer = setTimeout(() => {
							rxe.innerHTML = "";
							rxe.classList.remove("rxstate_fail");
							rxe.classList.add("rxstate_none");
						}, 5000);  /// clear after 5 seconds
						txtlog.value += "RX: " + rxfrtype + " FAIL\n";
						txtlog.scrollTo(0, txtlog.scrollHeight);
						break;
					}
					rxe.innerHTML = rxfrtype;
					break;
				}
				case "H": {
					// Host message
					if (!devmode)
						console.log(
							"WARNING: receiving Host messages when not in"
							+ " devmode");
					let hostmsgtype = decodestr(rdata, 1);
					let hostmsg = decodestr(rdata, -1);
					let prefix = ""
					switch (hostmsgtype) {
						case 'Q':
							prefix = "QueueCommandToHost(): ";
							break;
						case 'C':
							prefix = "SendCommandToHost(): ";
							break;
						case 'T':
							prefix = "SendCommandToHostQuiet(): ";
							break;
						case 'R':
							prefix = "SendReplyToHost(): ";
							break;
						case 'F':
							prefix = "CommandFromHost(): ";
							break;
					}
					txtlog.value += prefix + hostmsg + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				}
				case "h":
					// Host text data
					if (!devmode)
						console.log(
							"WARNING: receiving text data Host messages when"
							+ " not in devmode");
					txtlog.value += "Data (text) to Host: ("
						+ decodestr(rdata, 3) + ") " + decodestr(rdata, -1) + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case "m": {
					// Protocol Mode
					let mode = decodestr(rdata, -1);
					txtlog.value += "Protocol Mode = " + mode + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("protocolmode").innerHTML = mode;
					break;
				}
				case "P":
					// PTT true
					// txtlog.value += "PTT = true\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("ptt").classList.remove("hidden");
					break;
				case "p": {
					// PTT false
					// txtlog.value += "PTT = false\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("ptt").classList.add("hidden");
					// Clear txtype when done transmitting
					let txe = document.getElementById("txtype");
					txe.innerHTML = "";
					txe.classList.remove("txnak");
					txe.classList.remove("txack");
					break;
				}
				case "R":
					// IRS true
					txtlog.value += "IRS = true\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("irs").classList.remove("dnone");
					break;
				case "r":
					// IRS false
					txtlog.value += "IRS = false\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("irs").classList.add("dnone");
					break;
				case "S":
					// ISS true
					txtlog.value += "ISS = true\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("iss").classList.remove("dnone");
					break;
				case "s":
					// ISS false
					txtlog.value += "ISS = false\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("iss").classList.add("dnone");
					break;
				case "t": {
					let state = decodestr(rdata, -1);
					if (state == "DISC")
						txtlog.value += "[" + (new Date().toISOString()) + "]"
							+ "  State = " + state + "\n";
					else
						txtlog.value += "State = " + state + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("state").innerHTML = state;
					break;
				}
				case "\x81": {
					// CurrentLevel update
					// linear fraction of recieved fullscale audio scale
					// rescaled to 0-150
					let rlevel = decodebyte(rdata);
					document.getElementById("rcvlvl").value = rlevel;
					if (rlevel >= 148) {
						clearTimeout(rcvoverflowtimer);  // eliminate old timer
						let rcvoe = document.getElementById("rcvoverflow");
						rcvoe.classList.remove("dnone");
						rcvoverflowtimer = setTimeout(() => {
							rcvoe.classList.add("dnone");
						}, 5000);  /// display for 5 seconds
					}
					if (rlevel <= 2) {
						clearTimeout(rcvunderflowtimer); // eliminate old timer
						let rcvue = document.getElementById("rcvunderflow");
						rcvue.classList.remove("dnone");
						rcvunderflowtimer = setTimeout(() => {
							rcvue.classList.add("dnone");
						}, 1000);  /// display for 1 second
					}
					break;
				}
				case "\x8A": {
					// Quality
					let quality = decodebyte(rdata);
					let rserrors = decodeUvint(rdata);
					let rsmax = decodeUvint(rdata);
					if (rsmax < 0) {
						alert("ERROR: Invalid quality data.");
						break;
					}
					txtlog.value += "Quality = " + quality + "/100\n";
					txtlog.value += "RS Errors = " + rserrors
						+ "/" + rsmax + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("quality").innerHTML =
						"Quality: " + quality + "/100";
					document.getElementById("rserrs").innerHTML =
						"RS Errors: " + rserrors + "/" + rsmax;
					break;
				}
				case "\x8B": {
					// Bandwidth update
					let bw = decodebyte(rdata);
					if (bw == -1) {
						alert("Error: Invalid bandwidth.");
						break;
					}
					bandwidth = 10 * bw;
					txtlog.value += "Bandwidth = " + bw + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				}
				case "\x8C": {
					// 4-bit spectral data for waterfall plot
					let sdata = decodeslice(rdata, -1);
					if (sdata == null) {
						alert("ERROR: Invalid spectral data.");
						break;
					}
					addWaterfallLine(sdata);
					drawSpectrum(sdata);
					break;
				}
				case "\x8D": {
					// DriveLevel update
					let dl = decodebyte(rdata);
					txtlog.value += "DriveLevel = " + dl + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("driveleveltext").innerHTML =
						"" + dl;
					document.getElementById("drivelevelslider").value = dl;
					break;
				}
				case "\x8E": {
					// Pixel data for constellation plot (x, y, color)
					let pixeldata = decodeslice(rdata, -1);
					drawConstellation(pixeldata);
					break;
				}
				case "\x8F":
					// Data non-text to Host
					if (!devmode)
						console.log(
							"WARNING: receiving non-text data Host messages"
							+ " when not in devmode");
					txtlog.value += "Data (non-text) to Host: ("
						+ decodestr(rdata, 3) + ") "
						+ buf2hex(decodeslice(rdata, -1))
						+ "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break
				case "\x9A": {
					// AvgLen update
					let avglen = decodebyte(rdata);
					txtlog.value += "AvgLen = " + avglen + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("avglentext").innerHTML =
						"" + avglen;
					document.getElementById("avglenslider").value = avglen;
					break;
				}
				default:
					txtlog.value +=
						"WARNING: Received an unexpected message of type="
						+ msgtype + " and length=" + msglen + "\n";
					if (typeof(msgtype) == "string")
						// This message can be printed as text (though it
						// doesn't have a terminating NULL)
						txtlog.value +=
							"message (text): " + decodestr(rdata, -1) + "\n";
					else
						// This message should be printed as a sequence of
						// hex values.
						txtlog.value +=
							"message (hex): " + buf2hex(decodeslice(rdata, -1))
							+ "\n";
					break;
			}
			if (rdata.offset != msgoffset + msglen) {
				console.log(
					"WARNING: msg of type " + msgtype + " was not fully"
					+ " parsed.");
				rdata.offset == msgoffset + msglen;
			}
			// update startoffset before parsing next msglen
			startoffset = rdata.offset;
		}
		rdata.condense();
	}
	document.getElementById("send2tone").onclick = function() {
		send_send2tone();
	};
	document.getElementById("sendid").onclick = function() {
		send_sendid();
	};
	document.getElementById("clearlog").onclick = function() {
		txtlog.value = "";
	};
	document.getElementById("loghider").onclick = function() {
		if (document.getElementById("loghider").innerHTML == "Show Log") {
			document.getElementById("logdiv").classList.remove("dnone");
			document.getElementById("loghider").innerHTML = "Hide Log";
		} else {
			document.getElementById("logdiv").classList.add("dnone");
			document.getElementById("loghider").innerHTML = "Show Log";
		}
	};
	document.getElementById("infohider").onclick = function() {
		if (document.getElementById("infohider").innerHTML == "Show Help") {
			document.getElementById("info").classList.remove("dnone");
			document.getElementById("infohider").innerHTML = "Hide Help";
		} else {
			document.getElementById("info").classList.add("dnone");
			document.getElementById("infohider").innerHTML = "Show Help";
		}
	};

	document.getElementById("text-log").onkeydown = function(evt) {
		// text-log is intended to be readonly, but rather than set it
		// readonly in the html, just ignore all key presses except for
		// cursor movement keys (to allow keyboard scrolling).
		if (evt.keyCode < 33 || evt.keyCode > 40)
			evt.preventDefault();
	};

	document.getElementById("hostcommand").onkeydown = function(evt) {
		if (!devmode)
			// This function should only work in Dev Mode, which is enabled
			// by a msg from ardopcf.
			return;
		// Up and Down arrows can be used to scroll back and forward through
		// past commands.
		if (evt.keyCode == 38) { // up arrow
			if (cmdhistory.length == 0)
				return;
			if (cmdhistory_index == -1)
				cmdhistory_index = cmdhistory.length - 1;
			else if (cmdhistory_index != 0)
				cmdhistory_index -= 1;
			evt.target.value = cmdhistory[cmdhistory_index];
			return;
		}
		if (evt.keyCode == 40) { // down arrow
			if (cmdhistory.length == 0 || cmdhistory_index == -1)
				return;
			if (cmdhistory_index == cmdhistory.length - 1) {
				evt.target.value = "";
				cmdhistory_index = -1;
				return;
			}
			cmdhistory_index += 1;
			evt.target.value = cmdhistory[cmdhistory_index];
			return;
		}
		if (evt.keyCode != 13)
			// User has not pressed Enter.  Do nothing.
			return;
		// User has pressed Enter.  Send the command, update command histroy.
		var text = evt.target.value;
		if(text.length > 0) {
			send_msg(encoder.encode("H~" + text), 2 + text.length);
		}
		cmdhistory.push(text);
		if (cmdhistory.length > cmdhistory_limit)
			// discard the oldest entry in cmdhistory.
			cmdhistory.shift();
		evt.target.value = "";
		cmdhistory_index = -1
	};


	const wfWidth = 205;
	const wfHeight = 100;
	const wfCanvas = document.getElementById("waterfall");
	const wfCtx = wfCanvas.getContext("2d");
	wfCtx.fillStyle = "#000000";
	wfCtx.fillRect(0, 0, wfWidth, wfHeight);

	const spWidth = 205;
	const spHeight = 50;
	const spCanvas = document.getElementById("spectrum");
	const spCtx = spCanvas.getContext("2d");
	spCtx.fillStyle = "#000000";
	spCtx.fillRect(0, 0, spWidth, spHeight);

	const cnstWidth = 90;
	const cnstHeight = 90;
	const cnstCanvas = document.getElementById("constellation");
	const cnstCtx = cnstCanvas.getContext("2d");
	cnstCtx.fillStyle = "#000000";
	cnstCtx.fillRect(0, 0, cnstWidth, cnstHeight);

	const drawSpectrum = (values) => {
		spCtx.fillStyle = "#000";
		spCtx.fillRect(0, 0, plotscale * spWidth, plotscale * spHeight);
		spCtx.beginPath();
		spCtx.moveTo(0, plotscale * spHeight);
		for(var i=0; i<values.length; i++) {  // 2 frequency values per i
			spCtx.lineTo(
				plotscale*2*i,
				plotscale * (spHeight - (values[i] >> 4)*(spHeight/16)));
			spCtx.lineTo(
				plotscale*(2*i + 1),
				plotscale * (spHeight - (values[i] & 0x0F)*(spHeight/16)));
		}
		spCtx.lineTo(plotscale * spWidth, plotscale * spHeight);
		spCtx.moveTo(0, plotscale * spHeight); // close for fill
		spCtx.fillStyle = "#CCC";
		spCtx.strokeStyle = "#CCC";
		spCtx.fill();
		// draw bandwidth markers
		spCtx.beginPath();
		spCtx.moveTo(
			plotscale * (values.length - (bandwidth/2) / 11.719),
			0);
		spCtx.lineTo(
			plotscale * (values.length - (bandwidth/2) / 11.719),
			plotscale * spHeight)
		spCtx.moveTo(
			plotscale * (values.length + (bandwidth/2) / 11.719),
			plotscale * spHeight);
		spCtx.lineTo(
			plotscale * (values.length + (bandwidth/2) / 11.719),
			0);
		if (isbusy)
			spCtx.strokeStyle = "#F0F";
		else
			spCtx.strokeStyle = "#DDD";
		spCtx.stroke();
	};

	const addWaterfallLine = (values) => {
		// shift the existing image down by plotscale pixels
		wfCtx.drawImage(
			wfCtx.canvas,
			0,
			0,
			plotscale * wfWidth,
			plotscale * wfHeight,
			0,
			plotscale,
			plotscale * wfWidth,
			plotscale * wfHeight);
		// expand values (4-bit uint per pixel) to colormap values (RGBA per pixel)
		let colorValues = new Uint8ClampedArray(
			plotscale * (2 * values.length) * 4); // filled with 0
		for(var i=0; i<values.length; i++) {  // 2 frequency values per i
			for (var k=0; k<plotscale; k++) {
				for (var j=0; j<4; j++) {  // r, g, b
					// first of two freqencies encoded in this byte
					colorValues[
						((plotscale*(2*i) + k) * 4) + j
					] = colormap[(values[i] >> 4)][j]; // RGBA
					// second of two freqencies encoded in this byte
					colorValues[
						((plotscale*(2*i + 1) + k) * 4) + j
					] = colormap[(values[i] & 0x0F)][j]; // RGBA
				}
			}
		}
		// overwrite centerline and bandwidth lines
		let bwcolor;
		if (isbusy)
			bwcolor = 19 // fuscia bandwidth
		else
			bwcolor = 17
		for (var j=0; j<4; j++) {  // r, g, b
			colorValues[
				plotscale * values.length*4 + j
			] = colormap[16][j]; // black centerline
			colorValues[
				Math.round(
					plotscale * (values.length + (bandwidth/2) / 11.719)
				) * 4 + j
			] = colormap[bwcolor][j];
			colorValues[
				Math.round(
					plotscale * (values.length + (bandwidth/2) / 11.719) + 1
				) * 4 + j
			] = colormap[bwcolor][j];
			colorValues[
				Math.round(
					plotscale * (values.length - (bandwidth/2) / 11.719)
				) * 4 + j
			] = colormap[bwcolor][j];
			colorValues[
				Math.round(
					plotscale * (values.length - (bandwidth/2) / 11.719) + 1
				) * 4 + j
			] = colormap[bwcolor][j];
		}
		let imageData = new ImageData(
			colorValues, plotscale * 2 * values.length, 1);
		for (k=0; k<plotscale; k++) {
			wfCtx.putImageData(imageData, 0, k);
		}
	};

	const drawCnstGridlines = () => {
		cnstCtx.beginPath();
		cnstCtx.moveTo(0, plotscale * cnstHeight / 2);
		cnstCtx.lineTo(plotscale * cnstWidth, plotscale * cnstHeight / 2);
		cnstCtx.moveTo(plotscale * cnstWidth / 2, 0);
		cnstCtx.lineTo(plotscale * cnstWidth / 2, plotscale * cnstHeight);
		cnstCtx.strokeStyle = "#F0F";
		cnstCtx.stroke();
	}
	const drawConstellation = (pixels) => {
		cnstCtx.fillStyle = "#000000";
		cnstCtx.fillRect(0, 0, plotscale * cnstWidth, plotscale * cnstHeight);
		let imageData = cnstCtx.getImageData(
			0, 0, plotscale * cnstWidth, plotscale * cnstHeight);
		let data = imageData.data
		for (var i=0; i<pixels.length/3; i++) {
			for (var kx=0; kx<plotscale; kx++) {
				for (var ky=0; ky<plotscale; ky++) {
					for (var j=0; j<3; j++) {  // r, g, b
						// ignore color data in pixels[3*i + 2], since this
						// is an index to a palette not currently defined
						//  here. plot everything as white.
						// alpha is unchanged at 0xFF
						data[
							(plotscale * pixels[3*i] + kx
							+ ((plotscale * pixels[3*i+1] + ky) * plotscale
							* cnstWidth)) * 4 + j] = 0xFF // white
					}
				}
			}
		}
		cnstCtx.putImageData(imageData, 0, 0);
		drawCnstGridlines();
	};

	document.getElementById("avglenslider").oninput = function() {
		throttledcontrol(setavglen, 250, avglencontroltimer);
	}
	document.getElementById("plotscaleslider").oninput = function() {
		plotscale = document.getElementById("plotscaleslider").value;
		wfCanvas.width = plotscale * wfWidth;
		wfCanvas.height = plotscale * wfHeight;
		wfCtx.fillStyle = "#000000";
		wfCtx.fillRect(0, 0, plotscale * wfWidth, plotscale * wfHeight);
		spCanvas.width = plotscale * spWidth;
		spCanvas.height = plotscale * spHeight;
		cnstCanvas.width = plotscale * cnstWidth;
		cnstCanvas.height = plotscale * cnstHeight;
		cnstCtx.fillStyle = "#000000";
		cnstCtx.fillRect(0, 0, plotscale * cnstWidth, plotscale * cnstHeight);
		drawCnstGridlines();
	}
	document.getElementById("drivelevelslider").oninput = function() {
		throttledcontrol(setdrivelevel, 250, drivelevelcontroltimer);
	}

	WebSocketClient.init("ws://" + document.location.host + "/ws");
	document.getElementById("hostcommand").value = "";
	txtlog.value = "[" + (new Date().toISOString()) + "]\n";
	txtlog.scrollTo(0, 0);
	plotscale = 1;
	document.getElementById("plotscaleslider").value = 1;
	wfCtx.fillStyle = "#000000";
	wfCtx.fillRect(0, 0, wfWidth, wfHeight);
	drawCnstGridlines();
});
