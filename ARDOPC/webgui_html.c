// This file was created from 'webgui.html' by txt2c.
// Rather than editing this file directly, it may better to edit
// that source file, and then use txt2c to rebuild this file.

const char webgui_html[] = (
"<!DOCTYPE HTML>\n"
"<html>\n"
"  <head>\n"
"    <meta charset=\"utf-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <title>Ardopcf Web GUI</title>\n"
"    <style type=\"text/css\">\n"
"      #spectrum {vertical-align: bottom;}\n"
"      #busy {background-color: gold;}\n"
"      #ptt {background-color: red;}\n"
"      #iss, #irs {background-color: lime;}\n"
"      #lostcon {background-color: hotpink;}\n"
"      #rcvoverflow {background-color: hotpink;}\n"
"      #hostcommand, #text-log {width: 95%;}\n"
"      #info {background-color: lightgray;}\n"
"      .hidden {visibility: hidden;}\n"
"      .dnone {display: none;}\n"
"      .quality {background-color: lime;}\n"
"      .rxstate_none {background-color: white;}\n"
"      .rxstate_pending, #rcvunderflow {background-color: yellow;}\n"
"      .rxstate_ok {background-color: lime;}\n"
"      .rxstate_fail {background-color: red;}\n"
"      .txnak {background-color: lightpink;}\n"
"      .txack {background-color: aquamarine;}\n"
"      .state {background-color: #D0D0D0;}\n"
"      .sltext {vertical-align: top;}\n"
"      dt {font-weight: bold;}\n"
"    </style>\n"
"    <script src=\"webgui.js\"></script>\n"
"  </head>\n"
"  <body>\n"
"    <strong>ardopcf</strong>\n"
"    <div id=\"lostcon\" class=\"dnone\"><br />\n"
"      Connection to ardopcf lost.  Try reloading this page.<br /><br />\n"
"    </div>\n"
"    <div>\n"
"      <!-- TODO: Should be able to get same effect with simpler html and better css -->\n"
"      ProtocolMode:\n"
"      <span class=\"state\">&nbsp;</span><span id=\"protocolmode\" class=\"state\">UNKNOWN</span><span class=\"state\">&nbsp;</span>\n"
"      State:\n"
"      <span class=\"state\">&nbsp;</span><span id=\"state\" class=\"state\">UNKNOWN</span><span class=\"state\">&nbsp;</span>\n"
"    </div>\n"
"    <div>\n"
"      <span id=\"ptt\" class=\"hidden\">&nbsp;PTT&nbsp;</span>\n"
"      <span id=\"busy\" class=\"hidden\">Channel Busy</span>\n"
"    </div>\n"
"    <div>\n"
"      <span id=\"mycall\">(MYCALL NOT SET)</span>\n"
"      <span id=\"irs\" class=\"dnone\">IRS <<<<</span>\n"
"      <span id=\"iss\" class=\"dnone\">ISS >>>></span>\n"
"      <span id=\"rcall\"></span>\n"
"    </div>\n"
"    <div>\n"
"      <canvas id=\"spectrum\" width=\"205\" height=\"50\"></canvas>\n"
"    </div>\n"
"    <div>\n"
"      <canvas id=\"waterfall\" width=\"205\" height=\"100\"></canvas>\n"
"      <canvas id=\"constellation\" width=\"90\" height=\"90\"></canvas>\n"
"    </div>\n"
"    <div>\n"
"      <span id=\"quality\">Quality: 0/100</span>\n"
"      [<span id=\"rserrs\">RS Errors: 0/0</span>]\n"
"      <span id=\"rcvoverflow\" class=\"dnone\">\n"
"      WARNING: Audio from radio is too loud!\n"
"      </span>\n"
"    </div>\n"
"    <div>\n"
"      Rcv Level:\n"
"      <!-- rcvlvl is linear fraction of full scale audio-->\n"
"      <meter id=\"rcvlvl\" min=\"0\" max=\"150\" high=\"120\" value=\"0\"></meter>\n"
"      <span id=\"rcvunderflow\" class=\"dnone\">(Low Audio)</span>\n"
"    </div>\n"
"    <div>RX Frame Type: <span id=\"rxtype\" class=\"rxstate_none\"></span></div>\n"
"    <div>TX Frame Type: <span id=\"txtype\"></span></div>\n"
"    <div>\n"
"      <span class=\"sltext\">TX DriveLevel:</span>\n"
"      <input type=\"range\" min=\"0\" max=\"100\" value=\"100\" class=\"slider\" id=\"drivelevelslider\">\n"
"      <span class=\"sltext\" id=\"driveleveltext\"></span>\n"
"    </div>\n"
"    <div>\n"
"      <span class=\"sltext\">Plot Averaging:</span>\n"
"      <input type=\"range\" min=\"1\" max=\"10\" value=\"1\" class=\"slider\" id=\"avglenslider\">\n"
"      <span class=\"sltext\" id=\"avglentext\"></span>\n"
"    </div>\n"
"    <div>\n"
"      <span class=\"sltext\">Plot Scale:</span>\n"
"      <input type=\"range\" min=\"1\" max=\"8\" value=\"1\" class=\"slider\" id=\"plotscaleslider\">\n"
"    </div>\n"
"    <div>\n"
"      <button id=\"send2tone\">Send2Tone</button>\n"
"      <button id=\"sendid\">SendID</button>\n"
"    </div>\n"
"    <div id=\"logdiv\" class=\"dnone\">\n"
"      <button id=\"clearlog\">Clear Log</button><br />\n"
"      <textarea id=\"text-log\" rows=\"10\"></textarea>\n"
"    </div>\n"
"    <div>\n"
"      <button id=\"loghider\">Show Log</button>\n"
"      <button id=\"infohider\">Show Help</button>\n"
"    </div>\n"
"    <div id=\"info\" class=\"dnone\">\n"
"      <p>\n"
"      If you see a bright pink box at the top of the page saying <b>'Connection to\n"
"      ardopcf lost. Try reloading this page.'</b>, then this page is unable to establish\n"
"      a connection with ardopcf, or that connection has been closed.  This can occur\n"
"      for a number of reasons.  This will be displayed if the ardopcf program is\n"
"      terminated (intentionally or not).  If you are displaying this page on a\n"
"      device (computer, table, phone, etc.) other that the one that is running the\n"
"      ardopcf program, then this can also be caused by a problem with the network (often\n"
"      WiFi) connection between them.  It is normal for this message to very briefly\n"
"      flash at the top of this page when it first loads.\n"
"      </p><p>\n"
"      A few different devices can view copies of this Web GUI for that same running\n"
"      ardopcf instance.  However, ardopcf will refuse additional connections if too\n"
"      many copies try to connect simultaneously.\n"
"      </p><p>\n"
"      The top of this page shows the current <b>'ProtocolMode'</b>.  This will be one of\n"
"      'ARQ', 'FEC', or 'RXO'.  Next is shown the current Ardop protocol <b>'State'</b>\n"
"      This may be:\n"
"      <dl>\n"
"        <dt>'DISC'</dt><dd>disconnected</dd>\n"
"        <dt>'FECRcv'</dt><dd>receiving Forward Error Correction (FEC) frame</dd>\n"
"        <dt>'FECSend'</dt><dd>sending FEC frame</dd>\n"
"        <dt>'ISS'</dt><dd>station is the Information Sending Station</dd>\n"
"        <dt>'IRS'</dt><dd>station is the Information Receiving Station</dd>\n"
"        <dt>'IRStoISS'</dt><dd>transition from 'IRS' to 'ISS'</dd>\n"
"        <dt>'IDLE'</dt><dd>idle</dd>\n"
"      </dl>\n"
"      </p><p>\n"
"      The next blank line will show <b>'PTT'</b> when transmitting and <b>'Channel Busy'</b> when\n"
"      the busy detector detects a signal within Ardop's current bandwidth settings.\n"
"      </p><p>\n"
"      The next line shows the value of <b>MYCALL</b> and the callsign of the station you\n"
"      are connected to or sending Connection Requests (ConReq) to.  MYCALL is\n"
"      normally set by the Host program such as Pat Winlink or gARIM.  This should\n"
"      be your callsign.  If it has not been set, then it will show <b>'(MYCALL NOT\n"
"      SET)'</b>.\n"
"      </p><p>\n"
"      The <b>spectrum and waterfall plots</b> show from about 300 to 2700 Hz.  If this is\n"
"      narrower than the receive filter of your radio, high and/or low pitched audio\n"
"      that you hear from your radio may not be visible on these plots.  The thin\n"
"      black line in the center of the waterfall display is at 1500 Hz, the center\n"
"      frequency for Ardop signals.  The gray or pink lines on each side of the plots\n"
"      indicate the approximate edges of the currently selected ARQ bandwidth (200,\n"
"      500, 1000, or 2000 Hz wide).  These lines are not labeled, but their location\n"
"      relative to the total bandwidth should make it obvious which of these bandwidths\n"
"      they represent.\n"
"      </p><p>\n"
"      The levels shown in the spectrum and waterfall displays are log scale (dB),\n"
"      and a very slow automatic gain control is used to try to clearly show the\n"
"      relative power levels of signals and noise detected in the available audio.\n"
"      User adjustable <b>'Plot Averaging'</b> may optionally be applied.  Averaging smooths\n"
"      out the appearance of noise, which may make it easier to distinguish low\n"
"      power signals.  However, it also makes the plots less responsive to quickly\n"
"      changing received signals.  If the busy detector fails to recognize a signal\n"
"      that you can see or hear within the Ardop bandwidth you intend to use, you\n"
"      should not attempt to establish a connection at this time on this frequency,\n"
"      because you may interfere with the existing signal.\n"
"      </p><p>\n"
"      <b>'Plot Scale'</b> may be used to increase the size of all of the plots.  This does\n"
"      not change the amount of data being used for the plots.  It just magnifies them.\n"
"      </p><p>\n"
"      Scaling of data for plotting, include averaging, does NOT affect the 'Rcv Level'\n"
"      indicator, the busy detector, or ardopcf's abilility to detect or decode data.\n"
"      These adjustments are purely for display purposes.\n"
"      </p><p>\n"
"      The <b>constellation plot</b> provides a visual indicator of the quality of the most\n"
"      recently received PSK/QAM frame.  The display shown for FSK frames provides\n"
"      less information, but may still be somewhat useful.\n"
"      </p><p>\n"
"      The <b>'Quality'</b> value provides an indication of the quality of the most recently\n"
"      received frame, whether correctly decoded or not.  Ideally, this value would be\n"
"      independent of ardopcf's ability to demodulate and decode the signal, but since it uses\n"
"      intermediate values from the demodulators, this is not the case.  Some frame types\n"
"      typically require higher 'Quality' values than others to ensure correct decoding.\n"
"      </p><p>\n"
"      The <b>'RS Errors'</b> value provides a measure of ardopcf's level of success in\n"
"      accurately demodulating and decoding the most recently received frame.  All data frame\n"
"      types, as well as some control frame types, use Reed Solomon (RS) error correction to\n"
"      allow the frame to be correctly decoded even if a limited number of bytes were\n"
"      initially decoded incorrectly.  By reporting how many bytes of the frame had to be\n"
"      corrected in this way, compared to the maximum number of such corrections the frame\n"
"      type supports, the level of success of the demodulators can be inferred.  A small\n"
"      number of errors indicates a high level of success at demodulating the raw signal.\n"
"      When a frame cannot be correctly decoded, the number of RS errors shown will always be\n"
"      one more than the nuumber that could have been corrected.  At least this many errors\n"
"      were present, or else they would have been corrected.  It is not possible to determine\n"
"      how many additional errors may have been present.  A high number of RS Errors may be an\n"
"      indication of poor quality of the received signal, a deficiency in ardopcf's\n"
"      demodulators and decoders, or some combination of the two.\n"
"      </p><p>\n"
"      The <b>'Rcv Level'</b> graph shows the percentage of full scale used by the received\n"
"      audio on a linear scale.  If this level is too high, the audio will be clipped,\n"
"      making it harder for ardopcf to decode received signals.  The 'Rcv Level'\n"
"      indicates the combined amplitude of everything in your radio's receive\n"
"      bandwidth, and is thus not influenced by the Ardop bandwidth settings indicated\n"
"      on the spectrum and waterfall plots.  A strong signal outside of the Ardop\n"
"      bandwidth can still cause audio clipping.\n"
"      </p><p>\n"
"      A warning message is shown when <b>excessive audio levels</b> are detected.  This\n"
"      message will disappear a few seconds after audio amplitudes drop to more reasonable\n"
"      levels.  Low to moderate audio levels seem to usually work well.  However, audio\n"
"      levels that are too low may also make it more difficult for ardopcf to decode received\n"
"      signals.  A warning message is shown when received audio is very low.  However, if\n"
"      noise levels are low, <u>the <b>'Low Audio'</b> warning may be ignored when no signals\n"
"      are present.</u>  If the 'Low Audio' warning remains on when ardop signals are present,\n"
"      then increasing the audio level may improve decoding.  Increasing your received audio\n"
"      level may also help if you see faint indication of possible ardop signals centered on\n"
"      the waterfall display, but ardopcf does not appear to be detecting these signals.\n"
"      </p><p>\n"
"      Received audio levels are dependent on the actual strength of the received signal,\n"
"      on some of your radio's settings, and on the 'microphone' settings for your computer's\n"
"      soundcard.  Details of how to adjust these various settings depends on the model of\n"
"      radio you are using and on what operating system your computer uses.\n"
"      </p><p>\n"
"      The <b>'RX Frame Type'</b> and <b>'TX Frame Type'</b> displays show what Ardop frame\n"
"      type is being received or sent respectively.  These values also persist for a short\n"
"      time after a frame has been received or sent.  <u>In some cases, ardopcf attempts to\n"
"      decode a frame that is not really there.</u>  This is most likely to occur when your\n"
"      radio is receiving a lot of noise or a non-ardop digital signal.  While receiving,\n"
"      the 'RX Frame Type' is initially shown with a yellow background.  If this frame is\n"
"      not successfully decoded, the background will change to red.  If it is successfully\n"
"      decoded, the background will change to green.  The 'TX Frame Type' is normally shown\n"
"      with a white background, but a light green background is used when sending a DataACK\n"
"      frame telling the other station that you correctly received some data.  A light pink\n"
"      background is used when sending a DataNAK frame telling the other station that you\n"
"      were unable to decode some received data.\n"
"      </p><p>\n"
"      The <b>'TX DriveLevel'</b> slider linearly scales the amplitude of the audio sent to\n"
"      your radio by ardopcf.  This, along with your radio's settings and the 'speaker'\n"
"      settings for your computer's soundcard, influences the strength and quality of\n"
"      your transmitted radio signal.  By default, this slider control will be set to to\n"
"      100 when ardopcf is first started, though the --hostcommands option may be used to\n"
"      specify a different initial DRIVELEVEL.  You may want to adjust your radio and\n"
"      soudcard settings to levels that allow you to use this convenient slider control\n"
"      to make final adjustments to your transmit audio level.\n"
"      </p><p>\n"
"      Reduced audio amplitude can be used to <b>decrease the RF power</b> of your\n"
"      transmitted signals when using a single sideband radio transmitter.  In addition to\n"
"      limiting your power to only what is required to carry out the desired communications\n"
"      [for US Amateur operators this is required by Part 97.313(a)], reducing your power\n"
"      output may also be necessary if your radio cannot handle extended transmissions at\n"
"      its full rated power when using high duty cycle digital modes.  While sending data,\n"
"      Ardop can have a very high duty cycle as it sends long data frames with only brief\n"
"      breaks to hear short DataACK or DataNAK frames from the other station.\n"
"      </p><p>\n"
"      If the output audio is too loud, your radio's <b>Automatic Level Control (ALC)</b>\n"
"      will adjust this audio before using it to modulate the RF signal.  This adjustment\n"
"      may distort the signal making it more difficult for other stations to correctly\n"
"      receive your transmissions.  Some frame types used by Ardop may be more sensitive\n"
"      to these distortions than others.\n"
"      </p><p>\n"
"      Details of how to monitor the action of your radio's ALC, and how to adjust\n"
"      your radio and soundcard settings depend on the model of radio you are using\n"
"      and on what operating system your computer uses.  On some radios, the maximum\n"
"      output audio levels that can be used without causing problems may depend on the\n"
"      band of operation as well as other radio settings.  So, ALC action should be\n"
"      checked after changing frequencies or making other changes to the radio's settings.\n"
"      On some radios, appropriate audio levels are highly dependent on the power output\n"
"      settings of the radio, while on other radios they are mostly independent of these\n"
"      settings.\n"
"      </p><p>\n"
"      The <b>'Send2Tone'</b> button causes ardopcf to transmit a 5 second two tone signal\n"
"      similar, but longer than what is used as the leader at the start of all Ardop\n"
"      frames.  This will not be sent if the Protocol 'STATE', as shown near the top\n"
"      of this page, is not 'DISC' for disconnected.  An alert is displayed for this\n"
"      failure.\n"
"      </p><p>\n"
"      The <b>'SendID'</b> button causes ardopcf to transmit an ID frame containing your\n"
"      callsign.  If MYCALL has not been set, such that '(MYCALL NOT SET)', then this\n"
"      frame cannot be sent, and an alert is displayed instead.  Similarly, this will\n"
"      not be sent if the Protocol 'STATE', as shown near the top of this page, is\n"
"      not 'DISC' for disconnected.  An alert is displayed for this failure as well.\n"
"      If a Maidenhead grid square indicating your location has been set by the Host\n"
"      program, this is also included in an ID frame.\n"
"      </p><p>\n"
"      Either of these transmissions may be useful for antenna tuning or for making\n"
"      initial adjustments to DRIVELEVEL settings.  However, <u>the various frame types\n"
"      sent by ardopcf do not all produce the same audio power levels.</u>  This is a\n"
"      known issue that will hopefully be improved in a future release.  For now, this\n"
"      means that additional adjustments may be useful based on actual data transmissions.\n"
"      </p><p>\n"
"      The various items displayed here can change quickly during an Ardop ARQ\n"
"      connection.  The debug log file created by ardopcf contains most of this same\n"
"      information and more, though it does not contain data comparable to the waterfall\n"
"      or constellation plots.  Reviewing the contents of the log file may be of interest\n"
"      after an exchange that failed, or during which you saw something that seemed unusual.\n"
"      The <b>'Show Log'</b> button also provides a list of most of what has been displayed.\n"
"      This log window shows less detail than the debug log file, but provides a\n"
"      convenient way to review what has occured.  Some entries in the log window are\n"
"      timestamped.  These timestamps use the clock of the device you are viewing this\n"
"      page on, which may be different from the clock on the device that is running\n"
"      the ardopcf program.  Due to this, times shown in the log window may be slightly\n"
"      different from those written to the debug log file.\n"
"      </p><p>\n"
"      The <b>'Clear Log'</b> button (visible only when the log window is visible) may be used\n"
"      to discard the contents of this window.  This might be useful before initiating\n"
"      a new connection.  The 'Clear Log' button does not influence what is written to\n"
"      the debug log file.\n"
"      </p>\n"
"    </div>\n"
"    <div id=\"devmode\" class=\"dnone\">\n"
"      Host Command: <input type=\"text\"  id=\"hostcommand\"></input>\n"
"    </div>\n"
"  </body>\n"
"</html>\n"
"");
