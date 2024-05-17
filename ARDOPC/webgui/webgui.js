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
	[0xD0, 0xD0, 0xD0, alpha], // 18 light gray (for bandwidth lines when isbusy false)
	[0xFF, 0x00, 0xFF, alpha], // 19 fuscia (for bandwidth lines when isbusy true)
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
		// large enough to hold up to two complete messages.  Thus, allowing for
		// a single WebSocket frame to include the last few bytes of one message
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
				console.log("WebSocket connection already open.  Do nothing.");
			} else {
				console.log("Open WebSocket connection to ", url);
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
					console.log("Response received from WebSocket server to notification of closing.");
					is_closing = false;
					is_connected = false;
				} else if (is_connected) {
					console.log("WebSocket server is closing connection.");
					// Respond with confirmation of close.
					ws.close();
					is_connected = false;
				} else {
					console.log("Received unexpected close msg from WebSocket server."); // already closed
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
				console.log("Already sent notification to WebSocket server of closing.  No response yet.");
				document.getElementById("lostcon").classList.remove("dnone");
			} else if (is_connected) {
				console.log("Sending notification to WebSocket server of closing.");
				ws.close();
				is_closing = true;
			} else {
				console.log("No open WebSocket connection to close.");
			}
		};

		exports.onMessage = null;
		exports.onOpen = null;
		return exports;
	}());
	// buffer is an ArrayBuffer.  Returns string
	const buf2hex = (buffer) =>
		[...new Uint8Array(buffer)].map(x => x.toString(16).padStart(2, '0')).join('');

	// hexString is string.  Returns an Array of uint8 values
	// Optional whitespace is removed before conversion.
	// Terminate at end of hexString or upon encountering a
	// character that is neither whitespace nor valid hex.
	const fromHexString = (hexString) =>
		hexString.replace(/\s+/g, "").match(/.{1,2}/g).map((byte) => parseInt(byte, 16));

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
	// Each message type specifies the requried format for the remainder
	// of the message, if any.  Where a message includes a string, no
	// terminating NULL will be included at the end of the string.  The
	// length of the message provided by the lower level protocol shall
	// be used to determine the length of a string or other variabl
	// length data at the end of a message.
	//
	// To simplify the printing of raw messages for debugging purposes,
	// message type bytes that represent printable ASCII characters in
	// the range of 0x20 (space) to 0x7D (closing brace) shall be used
	// for messages whose entire raw content may be printed as human
	// readable text (though they will not include a NULL terminator),
	// while the remaining type bytes indicate that for logging
	// purposes, the message should be displayed as a string of
	// hexidecimal values.
	// "0~" no additional data: Client connected/reconnected
	// "2~" no additional data: Request ardopcf to send 5 send two tone signal.
	// "I~" no additional data: Request ardopcf to send ID frame.
	// 0x8D7E followed by one additional byte interpreted as an unsigned
	//     char in the range 0 to 100: Set DriveLevel
	// 0x9A7E followed by one additional byte interpreted as an unsigned
	//     char in the range 1 to max_avglen: Set display averaging length

	// Send data of length data_len to the server.
	// This function implements the lower level protocol of adding a
	// uvint header to the start of each message indicating the
	// length of its body (excluding the header length).
	const send_msg = (data, data_len) => {
		if (data[1] != "~".charCodeAt(0)) {
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

	// Use a throttle to limit the number of updates sent to ardopcf in
	// response to the user adjusting the AvgLen slider.
	let avglencontroltimer = false;
	const throttlecontrol = (callback, time, controltimer) => {
		if (controltimer) return;
		controltimer = true;
		setTimeout(() => {
			callback();
			controltimer = false;
		}, time);
	}
	const setavglen = () => {
		document.getElementById("avglentext").innerHTML = "" + document.getElementById("avglenslider").value;
		let msgdata = new Uint8Array([0x9A, 0x7E, document.getElementById("avglenslider").value]);
		send_msg(msgdata, 3);
	}

	// Use a throttle to limit the number of updates sent to ardopcf in
	// response to the user adjusting the DriveLevel slider.
	let drivelevelcontroltimer = false;
	const throttledrivelevelcontrol = (callback, time) => {
		if (drivelevelcontroltimer) return;
		drivelevelcontroltimer = true;
		setTimeout(() => {
			callback();
			drivelevelcontroltimer = false;
		}, time);
	}
	const setdrivelevel = () => {
		document.getElementById("driveleveltext").innerHTML = "" + document.getElementById("drivelevelslider").value;
		let msgdata = new Uint8Array([0x8D, 0x7E, document.getElementById("drivelevelslider").value]);
		send_msg(msgdata, 3);
	}


	let clearrxtimer = null;
	let rcvoverflowtimer = null;
	let rcvunderflowtimer = null;
	WebSocketClient.onOpen = function() {
		// Connection to ardopcf established.
		console.log("Websocket connection to ardopcf opened.  Notify ardopcf.");
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
		// Each message type specifies the requried format for the remainder
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
		// purposes, the message should be displayed as a string of
		// hexidecimal values.
		// "a|" followed by string: Alert for user.
		// "B|" no additional data: BUSY true
		// "b|" no additional data: BUSY false
		// "C|" followed by a string: My callsign
		//     If string has zero length, clear my callsign.
		// "c|" followed by a string: remote callsign
		//     If string has zero length, clear remote callsign.
		// "D|" no additional data: Enable Dev Mode
		// "F|" followed by a string: TX Frame type
		// "f|" followed by a character 'P' for pending, 'O' for OK, or 'F' for fail,
		//     followed by a string: RX Frame state and type
		// "H|" followed by a character 'Q' for QueueCommandToHost,
		//		'C' for SendCommandToHost, 'T' for SendCommandToHostQuiet,
		//		'R' for SendReplyToHost, or 'F' for Command from Host.
		//		followed by a string: Message to host
		// "h|" followed by a string: Host text data (to host)
		// "m|" followed by a string: Protocol Mode
		// "P|" no additional data: PTT true
		// "p|" no additional data: PTT false
		// "R|" no additional data: IRS true
		// "r|" no additional data: IRS false
		// "S|" no additional data: ISS true
		// "s|" no additional data: ISS false
		// "t|" followed by a string: Protocol State
		// 0x817C followed by one additional byte interpreted as an unsgiend
		//     char in the range of 0 to 150: Set CurrentLevel
		// 0x8A7C followed by one unsigned char: Quality (0-100)
		// 0x8B7C followed by one unsigned char: Bandwidth in Hz divided by 10.
		// 0x8C7C followed by 103 bytes of data: FFT power (dBfs 4 bits each).
		// 0x8D7C followed by one additional byte interpreted as an unsigned
		//     char in the range 0 to 100: Set DriveLevel
		// 0x8E7C followed by unsigned char data: Pixel data (x, y, color)
		//      color is 1, 2, or 3 for poor, ok, good.
		// 0x8F7C followed by unsigned char data: Host non-text data (to host)
		//		The first three characters of data are text data indicating
		//		data type 'FEC', 'ARQ', 'RXO', 'ERR', etc.

		if(!(wsdata instanceof ArrayBuffer)) {
			alert("Unexpected WebSocket data format.  Closing connection.");
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
		let startoffset = rdata.offset;
		let tmptxt;
		while((msglen = decodeUvint(rdata)) >= 0) {
			if (msglen == 0) {
				// An empty message.  Do nothing.
				// update startoffset before parsing next msglen
				startoffset = rdata.offset;
				continue;
			}
			if ((rdata.len - rdata.offset) < msglen) {
				// The complete message has not yet been recieved.
				// rewind parsing of msglen
				rdata.offset = startoffset;
				break;
			}
			if (rdata.buf[rdata.offset + 1] != "|".charCodeAt(0)) {
				console.log("no '|'", rdata.buf.slice(rdata.offset, rdata.offset + msglen));
				alert("Invalid message structure.  Closing connection.");
				WebSocketClient.close();
				return;
			}
			switch (rdata.buf[rdata.offset]) {
				case "a".charCodeAt(0):
					// Alert message (display)
					alert(decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen)));
					break;
				case "B".charCodeAt(0):
					// BUSY true
					isbusy = 1;
					// txtlog.value += "BUSY = true\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("busy").classList.remove("hidden");
					break;
				case "b".charCodeAt(0):
					// BUSY false
					isbusy = 0;
					// txtlog.value += "BUSY = false\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("busy").classList.add("hidden");
					break;
				case "C".charCodeAt(0):
					// My callsign
					if (msglen == 2) {
						// clear mycall
						txtlog.value += "Clear my callsign\n";
						document.getElementById("mycall").innerHTML = "(MYCALL NOT SET)";
					} else {
						tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
						txtlog.value += "My callsign = " + tmptxt + "\n";
						document.getElementById("mycall").innerHTML = tmptxt;
					}
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case "c".charCodeAt(0):
					// Remote callsign
					if (msglen == 2) {
						// clear rcall
						txtlog.value += "[" + (new Date().toISOString()) + "]  Clear remote callsign.  Was "
							+ document.getElementById("rcall").innerHTML + "\n";
						document.getElementById("rcall").innerHTML = "";
					} else {
						tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
						txtlog.value += "[" + (new Date().toISOString()) + "]  Remote callsign = " + tmptxt + "\n";
						document.getElementById("rcall").innerHTML = tmptxt;
					}
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case "D".charCodeAt(0):
					// Enable Dev Mode
					devmode = true;
					document.getElementById("devmode").classList.remove("dnone");
					break;
				case "F".charCodeAt(0):
					// TX Frame type
					tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen))
					txtlog.value += "TX Frame Type = " + tmptxt + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("txtype").innerHTML = tmptxt;
					if (tmptxt == "DataNAK")
						document.getElementById("txtype").classList.add("txnak");
					if (tmptxt == "DataACK")
						document.getElementById("txtype").classList.add("txack");
					break;
				case "f".charCodeAt(0):
					// RX Frame type
					tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 3, rdata.offset + msglen));
					let rxt = document.getElementById("rxtype");
					rxt.classList.remove("rxstate_pending");
					rxt.classList.remove("rxstate_ok");
					rxt.classList.remove("rxstate_fail");
					rxt.classList.remove("rxstate_none");
					clearTimeout(clearrxtimer);  // eliminate any existing timer to clear rx frame
					switch (rdata.buf[rdata.offset + 2]) {
					  case "P".charCodeAt(0):
						rxt.classList.add("rxstate_pending");
						// Assume that this will always be folowed
						// by a rxstate_ok or rxstate_fail, so don't
						// set a timer to clear it.
						// Don't write pending rx to txtlog
						break;
					  case "O".charCodeAt(0):
						rxt.classList.add("rxstate_ok");
						clearrxtimer = setTimeout(() => {
							rxt.innerHTML = "";
							rxt.classList.remove("rxstate_ok");
							rxt.classList.add("rxstate_none");
						}, 5000);  /// clear after 5 seconds
						txtlog.value += "RX: " + tmptxt + " PASS\n";
						txtlog.scrollTo(0, txtlog.scrollHeight);
						break;
					  case "F".charCodeAt(0):
						rxt.classList.add("rxstate_fail");
						clearrxtimer = setTimeout(() => {
							rxt.innerHTML = "";
							rxt.classList.remove("rxstate_fail");
							rxt.classList.add("rxstate_none");
						}, 5000);  /// clear after 5 seconds
						txtlog.value += "RX: " + tmptxt + " FAIL\n";
						txtlog.scrollTo(0, txtlog.scrollHeight);
						break;
					  default:
						alert("Invalid state for RX Frame Type.");
						rxt.classList.add("rxstate_none");
						break;
					}
					document.getElementById("rxtype").innerHTML = tmptxt;
					break;
				case "H".charCodeAt(0):
					// Host message
					if (!devmode)
						console.log("WARNING: receiving Host messages when not in devmode");
					tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 3, rdata.offset + msglen));
					switch (rdata.buf[rdata.offset + 2]) {
						case 'Q'.charCodeAt(0):
							txtlog.value += "QueueCommandToHost(): " + tmptxt + "\n";
							break;
						case 'C'.charCodeAt(0):
							txtlog.value += "SendCommandToHost(): " + tmptxt + "\n";
							break;
						case 'T'.charCodeAt(0):
							txtlog.value += "SendCommandToHostQuiet(): " + tmptxt + "\n";
							break;
						case 'R'.charCodeAt(0):
							txtlog.value += "SendReplyToHost(): " + tmptxt + "\n";
							break;
						case 'F'.charCodeAt(0):
							txtlog.value += "CommandFromHost(): " + tmptxt + "\n";
							break;
						default:
							txtlog.value += "Unknown msg to Host: " + tmptxt + "\n";
							break;
					}
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case "h".charCodeAt(0):
					// Host text data
					if (!devmode)
						console.log("WARNING: receiving text data Host messages when not in devmode");
					txtlog.value += "Data (text) to Host: ("
						+ decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + 5))
						+ ") " + decoder.decode(rdata.buf.slice(rdata.offset + 5, rdata.offset + msglen))
						+ "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case "m".charCodeAt(0):
					// Protocol Mode
					tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
					txtlog.value += "Protocol Mode = " + tmptxt + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("protocolmode").innerHTML = tmptxt;
					break;
				case "P".charCodeAt(0):
					// PTT true
					// txtlog.value += "PTT = true\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("ptt").classList.remove("hidden");
					break;
				case "p".charCodeAt(0):
					// PTT false
					// txtlog.value += "PTT = false\n";
					// txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("ptt").classList.add("hidden");
					// Clear txtype when done transmitting
					document.getElementById("txtype").innerHTML = "";
					document.getElementById("txtype").classList.remove("txnak");
					document.getElementById("txtype").classList.remove("txack");
					break;
				case "R".charCodeAt(0):
					// IRS true
					txtlog.value += "IRS = true\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("irs").classList.remove("dnone");
					break;
				case "r".charCodeAt(0):
					// IRS false
					txtlog.value += "IRS = false\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("irs").classList.add("dnone");
					break;
				case "S".charCodeAt(0):
					// ISS true
					txtlog.value += "ISS = true\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("iss").classList.remove("dnone");
					break;
				case "s".charCodeAt(0):
					// ISS false
					txtlog.value += "ISS = false\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("iss").classList.add("dnone");
					break;
				case "t".charCodeAt(0):
					tmptxt = decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
					if (tmptxt == "DISC")
						txtlog.value += "[" + (new Date().toISOString()) + "]  State = " + tmptxt + "\n";
					else
						txtlog.value += "State = " + tmptxt + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("state").innerHTML = tmptxt;
					break;
				case 0x81:
					// CurrentLevel update
					// linear fraction of fullscale audio scale scaled to 0-150
					document.getElementById("rcvlvl").value = rdata.buf[rdata.offset + 2];
					if (rdata.buf[rdata.offset + 2] >= 148) {
						clearTimeout(rcvoverflowtimer);  // eliminate existing timer
						document.getElementById("rcvoverflow").classList.remove("dnone");
						rcvoverflowtimer = setTimeout(() => {
							document.getElementById("rcvoverflow").classList.add("dnone");
						}, 5000);  /// display for 5 seconds
					}
					if (rdata.buf[rdata.offset + 2] <= 2) {
						clearTimeout(rcvunderflowtimer);  // eliminate existing timer
						document.getElementById("rcvunderflow").classList.remove("dnone");
						rcvunderflowtimer = setTimeout(() => {
							document.getElementById("rcvunderflow").classList.add("dnone");
						}, 1000);  /// display for 1 second
					}
					break;
				case 0x8A:
					// Quality
					txtlog.value += "Quality = " + rdata.buf[rdata.offset + 2] + "/100\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("quality").innerHTML = "Quality: " + rdata.buf[rdata.offset + 2] + "/100";
					break;
				case 0x8B:
					// Bandwidth update
					bandwidth = 10 * rdata.buf[rdata.offset + 2];
					txtlog.value += "Bandwidth = " + bandwidth + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break;
				case 0x8C:
					// 4-bit spectral data for waterfall plot
					addWaterfallLine(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
					drawSpectrum(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
					break;
				case 0x8D:
					// DriveLevel update
					txtlog.value += "DriveLevel = " + rdata.buf[rdata.offset + 2] + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("driveleveltext").innerHTML = "" + rdata.buf[rdata.offset + 2];
					document.getElementById("drivelevelslider").value = rdata.buf[rdata.offset + 2];
					break;
				case 0x8E:
					// Pixel data for constellation plot (x, y, color)
					drawConstellation(rdata.buf.slice(rdata.offset + 2, rdata.offset + msglen));
					break;
				case 0x8F:
					// Data non-text to Host
					if (!devmode)
						console.log("WARNING: receiving non-text data Host messages when not in devmode");
					txtlog.value += "Data (non-text) to Host: ("
						+ decoder.decode(rdata.buf.slice(rdata.offset + 2, rdata.offset + 5))
						+ ") " + buf2hex(rdata.buf.slice(rdata.offset + 5, rdata.offset + msglen))
						+ "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					break
				case 0x9A:
					// AvgLen update
					txtlog.value += "AvgLen = " + rdata.buf[rdata.offset + 2] + "\n";
					txtlog.scrollTo(0, txtlog.scrollHeight);
					document.getElementById("avglentext").innerHTML = "" + rdata.buf[rdata.offset + 2];
					document.getElementById("avglenslider").value = rdata.buf[rdata.offset + 2];
					break;
				default:
					txtlog.value += "WARNING: Received an unexpected message of type=" + rdata.buf[rdata.offset] + " and length=" + msglen + "\n";
					if (rdata.buf[rdata.offset] >= 0x20 && rdata.buf[rdata.offset] <= 0x7D)
						// This message can be printed as text (though it doesn't have a terminating NULL)
						txtlog.value += "message (text): " + decoder.decode(rdata.buf.slice(rdata.offset, rdata.offset + msglen)) + "\n";
					else
						// This message should be printed as a string of hex values.
						txtlog.value += "message (hex): " + buf2hex(rdata.buf.slice(rdata.offset, rdata.offset + msglen)) + "\n";
					break;
			}
			rdata.offset += msglen;
			// update startoffset before parsing next msglen
			startoffset = rdata.offset;
		}
		rdata.condense();
	}
	document.getElementById("send2tone").onclick = function() {send_send2tone();};
	document.getElementById("sendid").onclick = function() {send_sendid();};
	document.getElementById("clearlog").onclick = function() {txtlog.value = "";};
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
		// text-log is intended to be readonly, but rather than set it readonly
		// in the html, just ignore all key presses except for cursor movement
		// keys (to allow keyboard scrolling).
		if (evt.keyCode < 33 || evt.keyCode > 40)
			evt.preventDefault();
	};

	document.getElementById("hostcommand").onkeydown = function(evt) {
		if (!devmode)
			// This function should only work in Dev Mode, which is enabled by a msg from ardopcf.
			return;
		// Up and Down arrows can be used to scroll back and forward through past commands.
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
			spCtx.lineTo(plotscale*2*i, plotscale * (spHeight - (values[i] >> 4)*(spHeight/16)));
			spCtx.lineTo(plotscale*(2*i + 1), plotscale * (spHeight - (values[i] & 0x0F)*(spHeight/16)));
		}
		spCtx.lineTo(plotscale * spWidth, plotscale * spHeight);
		spCtx.moveTo(0, plotscale * spHeight); // close for fill
		spCtx.fillStyle = "#CCC";
		spCtx.strokeStyle = "#CCC";
		spCtx.fill();
		// draw bandwidth markers
		spCtx.beginPath();
		spCtx.moveTo(plotscale * (values.length - (bandwidth/2) / 11.719), 0);
		spCtx.lineTo(plotscale * (values.length - (bandwidth/2) / 11.719), plotscale * spHeight)
		spCtx.moveTo(plotscale * (values.length + (bandwidth/2) / 11.719), plotscale * spHeight);
		spCtx.lineTo(plotscale * (values.length + (bandwidth/2) / 11.719), 0);
		if (isbusy)
			spCtx.strokeStyle = "#F0F";
		else
			spCtx.strokeStyle = "#DDD";
		spCtx.stroke();
	};

	const addWaterfallLine = (values) => {
		// shift the existing image down by plotscale pixels
		wfCtx.drawImage(wfCtx.canvas, 0, 0, plotscale * wfWidth, plotscale * wfHeight, 0, plotscale, plotscale * wfWidth,  plotscale * wfHeight);
		// expand values (4-bit uint per pixel) to colormap values (RGBA per pixel)
		let colorValues = new Uint8ClampedArray(plotscale * (2 * values.length) * 4); // filled with 0
		for(var i=0; i<values.length; i++) {  // 2 frequency values per i
			for (var k=0; k<plotscale; k++) {
				for (var j=0; j<4; j++) {  // r, g, b
					// first of two freqencies encoded in this byte
					colorValues[((plotscale*(2*i) + k) * 4) + j] = colormap[(values[i] >> 4)][j]; // RGBA
					// second of two freqencies encoded in this byte
					colorValues[((plotscale*(2*i + 1) + k) * 4) + j] = colormap[(values[i] & 0x0F)][j]; // RGBA
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
			colorValues[plotscale * values.length*4 + j] = colormap[16][j]; // black centerline
			colorValues[Math.round(plotscale * (values.length + (bandwidth/2) / 11.719)) * 4 + j] = colormap[bwcolor][j];
			colorValues[Math.round(plotscale * (values.length + (bandwidth/2) / 11.719) + 1) * 4 + j] = colormap[bwcolor][j];
			colorValues[Math.round(plotscale * (values.length - (bandwidth/2) / 11.719)) * 4 + j] = colormap[bwcolor][j];
			colorValues[Math.round(plotscale * (values.length - (bandwidth/2) / 11.719) + 1) * 4 + j] = colormap[bwcolor][j];
		}
		let imageData = new ImageData(colorValues, plotscale * 2 * values.length, 1);
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
		let imageData = cnstCtx.getImageData(0, 0, plotscale * cnstWidth, plotscale * cnstHeight);
		let data = imageData.data
		for (var i=0; i<pixels.length/3; i++) {
			for (var kx=0; kx<plotscale; kx++) {
				for (var ky=0; ky<plotscale; ky++) {
					for (var j=0; j<3; j++) {  // r, g, b
						// ignore color data in pixels[3*i + 2], since this is an index
						// to a palette not currently defined here. plot everything as white
						// alpha is unchanged at 0xFF
						data[(plotscale * pixels[3*i] + kx + ((plotscale * pixels[3*i+1] + ky) * plotscale * cnstWidth)) * 4 + j] = 0xFF // white
					}
				}
			}
		}
		cnstCtx.putImageData(imageData, 0, 0);
		drawCnstGridlines();
	};

	document.getElementById("avglenslider").oninput = function() {
		throttlecontrol(setavglen, 250, avglencontroltimer);
	}
	document.getElementById("plotscaleslider").oninput = function() {
		plotscale = document.getElementById("plotscaleslider").value;
		document.getElementById("waterfall").width = plotscale * wfWidth;
		document.getElementById("waterfall").height = plotscale * wfHeight;
		wfCtx.fillStyle = "#000000";
		wfCtx.fillRect(0, 0, plotscale * wfWidth, plotscale * wfHeight);
		document.getElementById("spectrum").width = plotscale * spWidth;
		document.getElementById("spectrum").height = plotscale * spHeight;
		document.getElementById("constellation").width = plotscale * cnstWidth;
		document.getElementById("constellation").height = plotscale * cnstHeight;
		cnstCtx.fillStyle = "#000000";
		cnstCtx.fillRect(0, 0, plotscale * cnstWidth, plotscale * cnstHeight);
		drawCnstGridlines();
	}
	document.getElementById("drivelevelslider").oninput = function() {
		throttledrivelevelcontrol(setdrivelevel, 250);
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
