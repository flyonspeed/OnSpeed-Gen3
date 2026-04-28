const char htmlLiveView[] PROGMEM = R"=====(
<style>
  html, body { margin:0; padding: 0 5px; overflow:hidden;min-height:100% }
  #container {width:100%}
  svg {position:absolute;margin:0 auto; display: block;}
  #datafields_aoa {position:relative; margin-top:  5px;font-family: "Arial, Helvetica, sans-serif";font-size:16px; left:0px;}
  #datafields_att {position:relative; margin-top:  5px;font-family: "Arial, Helvetica, sans-serif";font-size:14px; left:0px;}
  #footer {display: block;;position:fixed; bottom:2px; width: 100%;font-family: "Arial, Helvetica, sans-serif";font-size:14px;}
  #footer-warning {text-align: center;margin-left: -10px; width: 100%;font-family: "Arial, Helvetica, sans-serif";font-size:12px;background-color:white}
  #status {display:flex;justify-content: space-between;padding-bottom:10px;}
  #switch {margin-right: 20px;}
  #status-label {align-self: flex-end;}
  /*! XS */
  @media (orientation: portrait)
    {
    svg {  margin: 0 auto 0 0px; }
    }
  @media (orientation: landscape)
    {
    /* svg { height: 60vh;}*/
    }
</style>

<script language="javascript" type="text/javascript">
var wsUri = "ws://192.168.0.1:81";

var lastUpdate  = Date.now();
var lastDisplay = Date.now();

// Initialize AOA to the N/A sentinel (-100) rather than 0, so the page
// renders in the "no data yet" state before the WebSocket delivers its
// first message.  The onMessage block gates the SVG bar and the numeric
// AOA readout on `AOA > -20` — starting below that keeps both hidden
// until real data arrives.
var AOA            = -100;
var IAS            = 0;
var PAlt           = 0;
var GLoad          = 1;
var GLoadLat       = 0;
var PitchAngle     = 0;
var RollAngle      = 0;
var flightPath     = 0;
var iVSI           = 0;
var derivedAOA     = 0;
var pitchRate      = 0;

var liveConnecting = false;
setInterval(updateAge,500);
var websocket;

function init()
  {
  // console.log("Init()");

  // First-load view setup: default to the AOA indexer and paint the
  // numeric AOA fields as N/A immediately, since onMessage() only runs
  // once the WebSocket delivers data.  View setup lives here (NOT in
  // reconnect()) so transient WiFi drops don't yank the pilot back to
  // the indexer while they're looking at the attitude page.
  toggleAOA(true);
  document.getElementById("aoa_aoa").innerHTML = 'N/A';
  document.getElementById("aoa_att").innerHTML = 'N/A';

  reconnect();
  }

// Start (or re-start) the WebSocket connection attempt without touching
// the current page view state.  Called both on first load (via init())
// and from the 3-second staleness fallback in updateAge().
function reconnect()
  {
  writeToStatus("CONNECTING...");
  liveConnecting = true;
  setTimeout(connectWebSocket, 300);
  }

function connectWebSocket()
  {
  // The init()→setTimeout(connectWebSocket, 300) debounce has now
  // fired, so the "connection attempt is pending" flag should
  // clear. Without this, if the first attempt hangs in CONNECTING
  // (handshake never completes, onOpen/onClose never fire),
  // liveConnecting stays true forever and the 3-second reconnect
  // path in updateAge — gated by !liveConnecting — can't fire.
  // Result: page hangs in "Reconnecting..." indefinitely.  Observed
  // in the wild on flaky WiFi as "sometimes fails to connect."
  liveConnecting = false;

  // Close any existing socket before opening a new one.  Without
  // this, each 3-second reconnect cycle leaks a WebSocket whose
  // onclose handler can still fire later and call
  // writeToStatus("Reconnecting...") or spawn additional reconnect
  // attempts — producing cascading reconnect storms.
  //
  // Also clear the old socket's onclose first so its upcoming
  // close event does not trigger another retry from within the
  // old handler.
  if (websocket &&
      websocket.readyState !== WebSocket.CLOSED &&
      websocket.readyState !== WebSocket.CLOSING)
    {
    websocket.onclose = null;
    websocket.close();
    }

  websocket = new WebSocket(wsUri);
  websocket.onopen    = function(evt) { onOpen(evt)    };
  websocket.onclose   = function(evt) { onClose(evt)   };
  websocket.onmessage = function(evt) { onMessage(evt) };
  websocket.onerror   = function(evt) { onError(evt)   };
  }

function onOpen(evt)
  {
  console.log("onOpen()");
  writeToStatus("CONNECTED");
  }

function onClose(evt)
  {
  console.log("onClose()");
  writeToStatus("Reconnecting...");
  if (!liveConnecting)
    setTimeout(connectWebSocket, 10);
  }

function map(x, in_min, in_max, out_min, out_max)
  {
  // Degenerate-range guard: if the input span collapses, linear
  // interpolation divides by zero.  The previous guard
  // `(in_max - in_min) + out_min == 0` only fired when out_min
  // happened to be zero — every real call site here passes a
  // non-zero out_min (278/228/178/113), so the old guard was
  // effectively dead and let divide-by-zero through whenever two
  // adjacent setpoints were configured equal.
  //
  // Fallback returns out_min (the lower-edge Y of the segment)
  // rather than 0.  This keeps the bar at the bottom of the
  // collapsed band on a misconfigured unit.  Returning 0 (as
  // onspeed_core::mapfloat does in its percent-lift context)
  // would snap the bar to the top of the SVG viewBox — the same
  // position as a stall warning — which is exactly the wrong
  // visual cue for a misconfig.
  if (Math.abs(in_max - in_min) < 0.0001)
    return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  }

// toFixed() preserves the sign on values that round to zero, so a
// pitch of -0.04 renders as "-0.0". Collapse any such result to its
// unsigned form before display.
function fmtFixed(value, digits)
  {
  var s = value.toFixed(digits);
  if (parseFloat(s) === 0) s = Math.abs(value).toFixed(digits);
  return s;
  }

function onMessage(evt)
  {
  //console.log("onMessage()");

  // reply to prevent timeouts
  liveConnecting = false;

  // The DataServer broadcasts JSON text frames AND binary #1 display
  // frames on the same socket; LiveView only consumes JSON.  Default
  // binaryType is "blob", so JSON.parse(blob) would throw 20×/s into
  // the catch below — wasted work plus DevTools noise.
  if (typeof evt.data !== 'string') return;

  // connected, got data
  try
    {
    var OnSpeed = JSON.parse(evt.data);

    AOA           = OnSpeed.AOA;
    IAS           = OnSpeed.IAS;
    PAlt          = OnSpeed.PAlt;
    GLoad         = OnSpeed.verticalGLoad;
    GLoadLat      = OnSpeed.lateralGLoad;
    PitchAngle    = OnSpeed.Pitch;
    RollAngle     = OnSpeed.Roll;
    iVSI          = OnSpeed.kalmanVSI;
    flightPath    = OnSpeed.flightPath;
    derivedAOA    = parseFloat(OnSpeed.DerivedAOA);
    pitchRate     = parseFloat(OnSpeed.PitchRate);
    lastUpdate    = Date.now();

    // Per-flap percent anchors come over the wire, computed firmware-side
    // by the canonical onspeed_core/aoa/PercentLift helper.  This
    // mirrors the M5 #1 wire format byte-for-byte in semantic content
    // — a future shared indexer renderer can run identically off
    // either transport.  Two cues per Vac's ld_max.pdf §8:
    //   tonesOnPctLift snaps per active detent (operational, audio gate).
    //   pipPctLift slides smoothly clean→fullflap (visual, aerodynamic
    //   reference) — read this for the L/Dmax pip dots below.
    var aoaPct       = parseInt(OnSpeed.percentLift,        10);
    var ldmaxPct     = parseInt(OnSpeed.pipPctLift,         10);
    var fastPct      = parseInt(OnSpeed.onSpeedFastPctLift, 10);
    var slowPct      = parseInt(OnSpeed.onSpeedSlowPctLift, 10);
    var stallWarnPct = parseInt(OnSpeed.stallWarnPctLift,   10);

    // Map percent-lift to the SVG y-coordinate.  Two ramps with the
    // OnSpeed donut anchored in the middle (115..78 in screen-y on
    // the SVG, just like the M5).  Each ramp slope changes per flap
    // because the band-edge percents change per flap.
    function pct2y(p) {
      if      (p <= 0)            return 278;                                  // bottom (alpha_0 floor)
      else if (p <= fastPct)      return map(p, 0, fastPct, 278, 178);         // floor → OnSpeedFast
      else if (p <= slowPct)      return map(p, fastPct, slowPct, 178, 113);   // donut band
      else if (p <= stallWarnPct) return map(p, slowPct, stallWarnPct, 113, 15); // OnSpeedSlow → StallWarn
      else                        return 15;                                   // top
    }

    // Move the AOA line on the indexer.  Gate on AOA > -20 so the
    // N/A sentinel (-100) leaves the bar hidden instead of mapping it
    // to a nonsense coordinate.
    if (AOA > -20)
      {
      document.getElementById("aoaline").setAttribute("y", pct2y(aoaPct));
      document.getElementById("aoaline").style.visibility = "visible";
      }
    else
      {
      document.getElementById("aoaline").style.visibility="hidden";
      }

    // L/Dmax pip dots — sit at the screen-y for the visual pip percent
    // (pipPctLift), which lerps smoothly across the entire pot range
    // from cleanest detent's L/Dmax to most-deployed detent's OnSpeed
    // center.  Aerodynamic reference cue, not the audio threshold.
    // +3 offset keeps the dot centered against the 6.69-px-high
    // aoaline rect.
    var ldmax_y = pct2y(ldmaxPct) + 3;

    document.getElementById("ldmaxleft").setAttribute("cy", ldmax_y);
    document.getElementById("ldmaxright").setAttribute("cy", ldmax_y);

    // update attitude
    updateAttitude(OnSpeed.Pitch,OnSpeed.Roll);

    // show onspeed dot — bar is in the donut band when aoaPct is
    // between OnSpeedFast and OnSpeedSlow percent.
    if (aoaPct > fastPct && aoaPct <= slowPct && document.getElementById("aoaindexer").style.visibility === "visible")
        document.getElementById("onspeeddot").style.visibility="visible";
    else
        document.getElementById("onspeeddot").style.visibility="hidden";

    } // end try

  catch (e)
    {
    console.log('JSON parsing error:'+e.name+': '+e.message);
    }

  if (Date.now() - lastDisplay >= 500)
    {
    if (AOA > -20)
        {
        document.getElementById("aoa_aoa").innerHTML=fmtFixed(AOA,2);
        document.getElementById("aoa_att").innerHTML=fmtFixed(AOA,2);
        }
    else
        {
        document.getElementById("aoa_aoa").innerHTML='N/A';
        document.getElementById("aoa_att").innerHTML='N/A';
        }

    document.getElementById("ias_aoa").innerHTML        = fmtFixed(IAS,0) +' kts';
    document.getElementById("ias_att").innerHTML        = fmtFixed(IAS,0) +' kts';
    document.getElementById("palt_aoa").innerHTML       = fmtFixed(PAlt,0)+' ft';
    document.getElementById("palt_att").innerHTML       = fmtFixed(PAlt,0)+' ft';
    document.getElementById("ivsi_aoa").innerHTML       = fmtFixed(iVSI,0)+' fpm';
    document.getElementById("ivsi_att").innerHTML       = fmtFixed(iVSI,0)+' fpm';
    document.getElementById("gload_aoa").innerHTML      = fmtFixed(GLoad,2)+' G';
    document.getElementById("gload_att").innerHTML      = fmtFixed(GLoad,2)+' G';
    document.getElementById("gloadLat_aoa").innerHTML   = fmtFixed(GLoadLat,2)+' G';
    document.getElementById("gloadLat_att").innerHTML   = fmtFixed(GLoadLat,2)+' G';
    document.getElementById("pitch_aoa").innerHTML      = fmtFixed(PitchAngle,1)+'&#176;';
    document.getElementById("pitch_att").innerHTML      = fmtFixed(PitchAngle,1)+'&#176;';
    document.getElementById("roll_aoa").innerHTML       = fmtFixed(RollAngle,1)+'&#176;';
    document.getElementById("roll_att").innerHTML       = fmtFixed(RollAngle,1)+'&#176;';
    document.getElementById("flightpath_aoa").innerHTML = fmtFixed(flightPath,1)+'&#176;';
    document.getElementById("flightpath_att").innerHTML = fmtFixed(flightPath,1)+'&#176;';
    document.getElementById("derivedaoa_aoa").innerHTML = fmtFixed(derivedAOA,2)+'&#176;';
    document.getElementById("derivedaoa_att").innerHTML = fmtFixed(derivedAOA,2)+'&#176;';
    document.getElementById("datamark_aoa").innerHTML   = fmtFixed(OnSpeed.dataMark,0);
    document.getElementById("datamark_att").innerHTML   = fmtFixed(OnSpeed.dataMark,0);
    document.getElementById("flapspos_aoa").innerHTML   = fmtFixed(OnSpeed.flapsPos,0)+'&#176;';
    document.getElementById("flapspos_att").innerHTML   = fmtFixed(OnSpeed.flapsPos,0)+'&#176;';

    lastDisplay = Date.now();
    } // end if time to display
  } // end onMessage()


function onError(evt)
  {
  // WebSocket error events carry no payload by spec (evt.data is
  // `undefined`).  Log the event for diagnostics and leave the
  // user-visible status to the onclose handler that fires right after
  // — it writes "Reconnecting..." which is the accurate next state.
  console.log("WebSocket error:", evt);
  }


function updateAge()
  {
  var age = ((Date.now() - lastUpdate) / 1000).toFixed(1);
  var color = (age >= 1) ? "red" : "black";

  // The data tables have two Age spans — one in the AOA-mode panel
  // and one in the attitude-mode panel — switched by toggleAOA().
  // The previous code read `document.getElementById("age")`, which
  // does not exist; the guard silently skipped the update AND the
  // 3-second reconnect fallback below.
  var ids = ["age_aoa", "age_att"];
  for (var i = 0; i < ids.length; i++)
    {
    var el = document.getElementById(ids[i]);
    if (el)
      {
      el.innerHTML = age + ' sec';
      el.style.color = color;
      }
    }

  // If no WebSocket message has arrived in 3 seconds, re-attempt the
  // connection.  Calls reconnect() (not init()) so we don't reset the
  // view to the AOA indexer on every WiFi hiccup — a pilot watching
  // the attitude page would otherwise get bounced back to the
  // indexer every 3 s until the link comes back.
  if (age >= 3 && !liveConnecting)
    reconnect();
  }


function writeToStatus(message)
  {
  var status = document.getElementById("connectionstatus");

  status.innerHTML = message;
  }


function updateAttitude(pitch,roll)
  {
  if (document.getElementById("attitude")==null || document.getElementById("attitude").style.visibility!="visible")
    return; //don't update attitude if attitude indicator is not visible

  roll=roll*-1;

  var p = (pitch * 2.50);
  var t = 'translate(50,50) rotate(' + roll + ') translate(0,' + p + ')' ;
  var id_roll = document.querySelector('#onspeed-attitude-pos');

  if (id_roll!=null)
    id_roll.setAttribute('transform',t);

  var id_dial = document.querySelector('#onspeed-attitude-dial');

  x = 50;
  y = 50;
  var t = 'translate(' + x + ',' + y + ')' + 'rotate(' + roll + ')';

  id_dial.setAttribute('transform',t);
  }


function toggleAOA(state)
  {
  //console.log(state);

  if (state)
    {
    document.getElementById("aoaindexer").style.visibility="visible";
    document.getElementById("attitude").style.visibility="hidden";
    document.getElementById("datafields_aoa").style.visibility="visible";
    document.getElementById("datafields_att").style.visibility="hidden";
    }
  else
    {
    document.getElementById("aoaindexer").style.visibility="hidden";
    document.getElementById("attitude").style.visibility="visible";
    document.getElementById("datafields_aoa").style.visibility="hidden";
    document.getElementById("datafields_att").style.visibility="visible";
    }
  }

updateAttitude(0,0);

// init() is called by the end-of-body script tag below, after the DOM
// is fully parsed.  (Deliberately not writing the literal close-script
// sequence here: the HTML tokenizer terminates <script> content when
// it sees that substring even inside a JS // comment, which would
// orphan the rest of the comment as rendered page text.)

</script>

<div id="container">
  <div id="aoaindexer">
    <svg version="1.1" viewBox="0 0 210 297" width="100%" style="height: 70vh">
      <g>
        <circle cx="98.171" cy="787.76" r="133.84" fill="#07a33f" stroke-width="1.8089"/>
        <rect transform="rotate(-15.029)" x="50.364" y="33.747" width="15.615" height="89.228" fill="#cc3837"/>
        <rect transform="matrix(-.9658 -.2593 -.2593 .9658 0 0)" x="-148.06" y="-19.129" width="15.615" height="89.228" fill="#cc3837"/>
        <rect transform="matrix(.9658 .2593 .2593 -.9658 0 0)" x="127.53" y="-255.15" width="15.615" height="89.228" fill="#07a33f"/>
        <rect transform="rotate(164.97)" x="-70.153" y="-307.83" width="15.615" height="89.228" fill="#07a33f"/>
        <path d="m101.8 113.26c-17.123 0-31.526 12.231-34.92 28.385 5.203-2.6e-4 10.398 1.6e-4 15.598 0 2.9178-7.8426 10.401-13.371 19.323-13.371 8.922 0 16.413 5.5289 19.336 13.371 5.198 1.6e-4 10.403-2.8e-4 15.602 0-3.397-16.154-17.815-28.385-34.938-28.385zm-35.121 41.774c2.9176 16.746 17.577 29.609 35.121 29.609 17.544 0 32.218-12.863 35.138-29.609-5.1216-2.8e-4 -10.25 1.6e-4 -15.371 0-2.5708 8.4824-10.391 14.574-19.767 14.574-9.3764 0-17.183-6.0913-19.75-14.574-5.1246 1.4e-4 -10.244-2.8e-4 -15.371 0z" color="#000000" color-rendering="auto" dominant-baseline="auto" fill="#07a33f" image-rendering="auto" shape-rendering="auto" solid-color="#000000" style="font-feature-settings:normal;font-variant-alternates:normal;font-variant-caps:normal;font-variant-ligatures:normal;font-variant-numeric:normal;font-variant-position:normal;isolation:auto;mix-blend-mode:normal;paint-order:markers fill stroke;shape-padding:0;text-decoration-color:#000000;text-decoration-line:none;text-decoration-style:solid;text-indent:0;text-orientation:mixed;text-transform:none;white-space:normal"/>
        <circle id="onspeeddot" cx="101.8" cy="148.91" r="18" fill="#07a33f" stroke-width=".81089" visibility="hidden"/>
      </g>
      <g fill="#241f1c">
        <rect id="aoaline" x="52.187" y="144.91" width="100" height="6.6921" style="paint-order:markers fill stroke;visibility:hidden"/>
        <circle id="ldmaxleft" cx="46.801" cy="228" r="3.346" stroke-width="1.0419"/>
        <circle id="ldmaxright" cx="157.53" cy="228" r="3.346" stroke-width="1.0419"/>
      </g>
    </svg>
  </div>

  <div id="attitude" style="visibility:hidden;">
    <svg id="onspeed-attitude" xmlns="http://www.w3.org/2000/svg" width="100%" height="300px" viewBox="0 0 100 100">
      <g id="onspeed-attitude-pos"
         stroke-width="0.5" stroke="#fff" fill="#fff"
         transform="translate(50,50) rotate(0)"
         text-anchor="middle" font-family="sans-serif" font-size="6">
        <rect fill="#29B6F6" stroke-width="0" x="-100" y="-200" width="200" height="200"></rect>
        <rect fill="#8B4513" stroke-width="0" x="-100" y="0"  width="200" height="200"></rect>

        <!-- pitch up -->
        <line x1="-8"  y1="-6.25"   x2="8"  y2="-6.25"/>
        <line x1="-16" y1="-12"     x2="-4" y2="-12"  />
        <text stroke-width="0.1" x="0" y="-10">5</text>
        <line x1="4"   y1="-12"     x2="16" y2="-12"  />

        <line x1="-8"  y1="-18.75"  x2="8"  y2="-18.75"/>
        <line x1="-16" y1="-25"     x2="-4" y2="-25"  />
        <text stroke-width="0.1" x="0" y="-23">10</text>
        <line x1="4"   y1="-25"     x2="16" y2="-25"  />

        <line x1="-8"  y1="-31.25"   x2="8"  y2="-31.25"/>
        <line x1="-16" y1="-37.5"    x2="-4" y2="-37.5"  />
        <text stroke-width="0.1" x="0" y="-35">15</text>
        <line x1="4"   y1="-37.5"    x2="16" y2="-37.5"  />

        <line x1="-8"  y1="-43.75"   x2="8"  y2="-43.75"/>
        <line x1="-16" y1="-50.0"    x2="-4" y2="-50.0"  />
        <text stroke-width="0.1" x="0" y="-48">20</text>
        <line x1="4"   y1="-50.0"    x2="16" y2="-50.0"  />

        <line x1="-8"  y1="-56.25"   x2="8"  y2="-56.25"/>
        <line x1="-16" y1="-62.5"    x2="-4" y2="-62.5"  />
        <text stroke-width="0.1" x="0" y="-60.5">25</text>
        <line x1="4"   y1="-62.5"    x2="16" y2="-62.5"  />

        <line x1="-8"  y1="-68.75"   x2="8"  y2="-68.75"/>
        <line x1="-16" y1="-75.0"    x2="-4" y2="-75.0" />
        <text stroke-width="0.1" x="0" y="-73">30</text>
        <line x1="4"   y1="-75.0"    x2="16" y2="-75.0" />

        <!-- pitch down -->
        <line x1="-8"  y1="6.25"   x2="8"  y2="6.25"/>
        <line x1="-16" y1="12"     x2="-4" y2="12"  />
        <text stroke-width="0.1" x="0" y="14">5</text>
        <line x1="4"   y1="12"     x2="16" y2="12"  />

        <line x1="-8"  y1="18.75"   x2="8"  y2="18.75"/>
        <line x1="-16" y1="25"     x2="-4" y2="25"  />
        <text stroke-width="0.1" x="0" y="27">10</text>
        <line x1="4"   y1="25"     x2="16" y2="25"  />

        <line x1="-8"  y1="31.25"   x2="8"  y2="31.25"/>
        <line x1="-16" y1="37.5"    x2="-4" y2="37.5"  />
        <text stroke-width="0.1" x="0" y="39">15</text>
        <line x1="4"   y1="37.5"    x2="16" y2="37.5"  />

        <line x1="-8"  y1="43.75"   x2="8"  y2="43.75"/>
        <line x1="-16" y1="50.0"    x2="-4" y2="50.0"  />
        <text stroke-width="0.1" x="0" y="52">20</text>
        <line x1="4"   y1="50.0"    x2="16" y2="50.0"  />

        <line x1="-8"  y1="56.25"   x2="8"  y2="56.25"/>
        <line x1="-16" y1="62.5"    x2="-4" y2="62.5"  />
        <text stroke-width="0.1" x="0" y="64.5">25</text>
        <line x1="4"   y1="62.5"    x2="16" y2="62.5"  />

        <line x1="-8"  y1="68.75"   x2="8"  y2="68.75"/>
        <line x1="-16" y1="75.0"    x2="-4" y2="75.0" />
        <text stroke-width="0.1" x="0" y="77">30</text>
        <line x1="4"   y1="75.0"    x2="16" y2="75.0" />
      </g>

      <g stroke-width="2" stroke="#ff0">
        <line x1="30" y1="50" x2="43" y2="50"></line>
        <line x1="42" y1="50" x2="42" y2="53"></line>
        <line  x1="49" y1="50" x2="51" y2="50"/>
        <line x1="58" y1="53" x2="58" y2="50"></line>
        <line x1="57" y1="50" x2="70" y2="50"></line>
      </g>

      <g id='onspeed-attitude-dial' stroke-width="1" transform="translate(50,49) rotate(0)" stroke="#fff">
        <line stroke-width='1.5' x1='20.00' y1='-34.64' x2='24.50' y2='-42.44' />
        <line stroke-width='1.5' x1='34.64' y1='-20.00' x2='42.44' y2='-24.50' />
        <line stroke-width='1.5' x1='40.00' y1='0.00' x2='49.00' y2='0.00' />
        <line stroke-width='1.5' x1='-20.00' y1='-34.64' x2='-24.50' y2='-42.44' />
        <line stroke-width='1.5' x1='-34.64' y1='-20.00' x2='-42.44' y2='-24.50' />
        <line stroke-width='1.5' x1='-40.00' y1='-0.00' x2='-49.00' y2='-0.00' />
        <line x1='6.95' y1='-39.39' x2='7.81' y2='-44.32' />
        <line x1='13.68' y1='-37.59' x2='15.39' y2='-42.29' />
        <line x1='28.28' y1='-28.28' x2='31.82' y2='-31.82' />
        <line x1='-6.95' y1='-39.39' x2='-7.81' y2='-44.32' />
        <line x1='-13.68' y1='-37.59' x2='-15.39' y2='-42.29' />
        <line x1='-28.28' y1='-28.28' x2='-31.82' y2='-31.82' />
        <line x1='0.00' y1='-40.00' x2='0.00' y2='-45.00' />
        <path fill="#fff" d="M-4 -50 L4 -50 L 0 -40 z"></path>
      </g>

      <g stroke-width="1" stroke="#ff0" fill="#ff0">
        <path d="M46 18 L54 18 L50 11 z"></path>
      </g>

    </svg>

  </div>

  <div id="datafields_aoa">
    <table>
        <tr><td>AOA     </td><td><span id="aoa_aoa"></span>        </td></tr>
        <tr><td>Der AOA </td><td><span id="derivedaoa_aoa"></span> </td></tr>
        <tr><td>FltPath </td><td><span id="flightpath_aoa"></span> </td></tr>
        <tr><td>IAS     </td><td><span id="ias_aoa"></span>        </td></tr>
        <tr><td>P Alt   </td><td><span id="palt_aoa"></span>       </td></tr>
        <tr><td>iVSI    </td><td><span id="ivsi_aoa"></span>       </td></tr>
        <tr><td>Vert G  </td><td><span id="gload_aoa"></span>      </td></tr>
        <tr><td>Lat G   </td><td><span id="gloadLat_aoa"></span>   </td></tr>
        <tr><td>Pitch   </td><td><span id="pitch_aoa"></span>      </td></tr>
        <tr><td>Roll    </td><td><span id="roll_aoa"></span>       </td></tr>
        <tr><td>DataMark</td><td><span id="datamark_aoa"></span>   </td></tr>
        <tr><td>Flaps   </td><td><span id="flapspos_aoa"></span>   </td></tr>
        <tr><td>Age     </td><td><span id="age_aoa"></span>        </td></tr>
    </table>
  </div>

  <div id="datafields_att">
    <table style="width: 100%;">
        <colgroup>
            <col style="width: 23%;">
            <col style="width: 23%;">
            <col style="width:  8%;">
            <col style="width: 23%;">
            <col style="width: 23%;">
        </colgroup>
        <tr>
            <td>AOA     </td><td><span id="aoa_att"></span>        </td>
            <td>&nbsp;</td>
            <td>IAS     </td><td><span id="ias_att"></span>        </td>
        </tr>
        <tr>
            <td>Der AOA </td><td><span id="derivedaoa_att"></span> </td>
            <td>&nbsp;</td>
            <td>P Alt   </td><td><span id="palt_att"></span>       </td>
        </tr>
        <tr>
            <td>FltPath </td><td><span id="flightpath_att"></span> </td>
            <td>&nbsp;</td>
            <td>Pitch   </td><td><span id="pitch_att"></span>      </td>
        </tr>
        <tr>
            <td>iVSI    </td><td><span id="ivsi_att"></span>       </td>
            <td>&nbsp;</td>
            <td>Roll    </td><td><span id="roll_att"></span>       </td>
        </tr>
        <tr>
            <td>Vert G  </td><td><span id="gload_att"></span>      </td>
            <td>&nbsp;</td>
            <td>Flaps   </td><td><span id="flapspos_att"></span>   </td>
        </tr>
        <tr>
            <td>Lat G   </td><td><span id="gloadLat_att"></span>   </td>
            <td>&nbsp;</td>
            <td>&nbsp;</td>
        </tr>
        <tr>
            <td>DataMark</td><td><span id="datamark_att"></span>   </td>
            <td>&nbsp;</td>
            <td>Age     </td><td><span id="age_att"></span>        </td>
        </tr>

    </table>
  </div>

  <div id="footer">
    <div id="status">
      <div id="status-label">
        <strong>Status:</strong> <span id="connectionstatus">DISCONNECTED.</span>
      </div>
      <div id="switch">
          <div class="switch-field">
            <input type="radio" id="switchAOA" name="toggleAOA"  onClick="toggleAOA(true)" value="yes" checked/>
            <label for="switchAOA">AOA</label>
            <input type="radio" id="switchAHRS" name="toggleAOA" onClick="toggleAOA(false)" value="no" />
            <label for="switchAHRS">AHRS</label>
          </div>
      </div>
    </div>

    <div id="footer-warning">
   For diagnostic purposes only. NOT SAFE FOR FLIGHT
    </div>

  </div>
</div>

<script>init()</script>
)=====";
