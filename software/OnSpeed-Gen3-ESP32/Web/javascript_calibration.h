const char jsCalibration[] PROGMEM = R"=====(
var wsUri                 = "ws://192.168.0.1:81";
var lastUpdate            = Date.now();
var lastDisplay           = Date.now();
var OSFastMultiplier      = 1.35; // IAS multiple of Vs — NAOA fraction = 1/multiplier^2
var OSSlowMultiplier      = 1.25;
var StallWarnMargin       = 5; // knots
var LDmaxIAS              = 100; // will be calculated later based on flap position
var acVfe                 = 0;  // max flap extension speed, set from config
var AOA                   = 0;
var IASsmoothed           = 0;
var IAS                   = 0;
var PAlt                  = 0;
var GLoad                 = 1;
var GLoadLat              = 0;
var PitchAngle            = 0;
var RollAngle             = 0;
var smoothingAlpha        = 0.9;
var smoothingAlphaFwdAcc  = 0.04;
var liveConnecting        = false;
var flightPath            = 0;
var iVSI                  = 0;
var derivedAOA            = 0;
var pitchRate             = 0;
var decelRate             = 0;
var smoothDecelRate       = -1.0;
var cP                    = 0;
var flapsPos              = 0;
var flapsPosCalibrated    = 0;
var dataRecording         = false;
var flightData            = new Object();
flightData.IAS            = [];
flightData.DerivedAOA     = [];
flightData.CP             = [];
flightData.PitchRate      = [];
flightData.smoothedIAS    = [];
flightData.smoothedCP     = [];
flightData.Pitch          = [];
flightData.Flightpath     = [];
flightData.DecelRate      = [];
var CPtoAOAcurve          = "";
var CPtoAOAr2             = "";
var LDmaxSetpoint         = 0;
var OSFastSetpoint        = 0;
var OSSlowSetpoint        = 0;
var StallWarnSetpoint     = 0;
var calDate;
var stallIAS;
var resultCPtoAOA; // CP to AOA regression curve {equation: [a2, a1, a0]}
var alpha0         = 0; // zero-lift fuselage AOA from IAS-to-AOA fit
var alphaStall     = 0; // stall AOA from IAS-to-AOA fit
var K_fit          = 0; // lift sensitivity from IAS-to-AOA fit
var IAStoAOAr2   = 0; // R-squared of IAS-to-AOA fit

// ---- Stacked calibration state ----
var stackedData = {IAS:[], DerivedAOA:[], CP:[], smoothedIAS:[], smoothedCP:[],
                   Pitch:[], Flightpath:[], DecelRate:[], stallIASvals:[]};
var pendingRun  = null;  // the most recent run, waiting for keep/discard
var stackedRunCount = 0;
var stackedR2       = 0; // R^2 of fit on stacked data only (without pending run)
var stackedFlapsPos = -1; // flap setting for the current stack

// ---- WLS helpers ----

// Compute rolling standard deviation of arr[] with a centered window of size winLen.
// Returns an array the same length as arr.
function rollingSigma(arr, winLen)
  {
  var n = arr.length;
  var sigma = new Array(n);
  var half = Math.floor(winLen / 2);
  for (var i = 0; i < n; i++)
    {
    var lo = Math.max(0, i - half);
    var hi = Math.min(n - 1, i + half);
    var cnt = hi - lo + 1;
    var sum = 0, sum2 = 0;
    for (var j = lo; j <= hi; j++) { sum += arr[j]; sum2 += arr[j] * arr[j]; }
    var variance = sum2 / cnt - (sum / cnt) * (sum / cnt);
    sigma[i] = Math.sqrt(Math.max(variance, 0));
    }
  return sigma;
  }

// WLS quadratic fit: y = a2*x^2 + a1*x + a0
// Returns {equation: [a2, a1, a0], r2: weighted_R2}
function wlsQuadratic(x, y, w)
  {
  var n = x.length;
  // Accumulate X'WX (3x3 symmetric) and X'Wy (3x1)
  var s22=0,s21=0,s20=0,s11=0,s10=0,s00=0;
  var t2=0,t1=0,t0=0;
  var sumW=0, sumWy=0;
  for (var i = 0; i < n; i++)
    {
    var xi = x[i], yi = y[i], wi = w[i];
    var x2 = xi*xi;
    s22 += wi*x2*x2; s21 += wi*x2*xi; s20 += wi*x2;
    s11 += wi*xi*xi; s10 += wi*xi;    s00 += wi;
    t2  += wi*x2*yi; t1  += wi*xi*yi; t0  += wi*yi;
    sumW += wi; sumWy += wi*yi;
    }
  // Solve 3x3 via Cramer's rule
  // det of [[s22,s21,s20],[s21,s11,s10],[s20,s10,s00]]
  var det = s22*(s11*s00 - s10*s10) - s21*(s21*s00 - s10*s20) + s20*(s21*s10 - s11*s20);
  if (Math.abs(det) < 1e-30) return {equation:[0,0,0], r2:0};
  // Adjugate columns
  var a2 = (t2*(s11*s00-s10*s10) - t1*(s21*s00-s10*s20) + t0*(s21*s10-s11*s20)) / det;
  var a1 = (s22*(t1*s00-t0*s10) - s21*(t2*s00-t0*s20) + s20*(t2*s10-t1*s20)) / det;
  var a0 = (s22*(s11*t0-s10*t1) - s21*(s21*t0-s10*t2) + s20*(s21*t1-s11*t2)) / det;
  // Weighted R^2
  var ybar = sumWy / sumW;
  var ssRes = 0, ssTot = 0;
  for (var i = 0; i < n; i++)
    {
    var pred = a2*x[i]*x[i] + a1*x[i] + a0;
    ssRes += w[i] * (y[i] - pred) * (y[i] - pred);
    ssTot += w[i] * (y[i] - ybar) * (y[i] - ybar);
    }
  var r2 = ssTot > 0 ? 1 - ssRes / ssTot : 0;
  return {equation: [a2, a1, a0], r2: r2};
  }

// WLS linear fit: y = slope*x + intercept
// Returns {slope, intercept, r2}
function wlsLinear(x, y, w)
  {
  var n = x.length;
  var sW=0, sWx=0, sWy=0, sWxx=0, sWxy=0;
  for (var i = 0; i < n; i++)
    {
    var wi = w[i];
    sW += wi; sWx += wi*x[i]; sWy += wi*y[i];
    sWxx += wi*x[i]*x[i]; sWxy += wi*x[i]*y[i];
    }
  var det = sW*sWxx - sWx*sWx;
  if (Math.abs(det) < 1e-30) return {slope:0, intercept:0, r2:0};
  var slope     = (sW*sWxy - sWx*sWy) / det;
  var intercept = (sWxx*sWy - sWx*sWxy) / det;
  // Weighted R^2
  var ybar = sWy / sW;
  var ssRes = 0, ssTot = 0;
  for (var i = 0; i < n; i++)
    {
    var pred = slope*x[i] + intercept;
    ssRes += w[i] * (y[i] - pred) * (y[i] - pred);
    ssTot += w[i] * (y[i] - ybar) * (y[i] - ybar);
    }
  var r2 = ssTot > 0 ? 1 - ssRes / ssTot : 0;
  return {slope: slope, intercept: intercept, r2: r2};
  }

function init()
  {
  writeToStatus("CONNECTING...");
  connectWebSocket();
  }

function connectWebSocket()
  {
  liveConnecting=true
  websocket = new WebSocket(wsUri);
  websocket.onopen = function(evt) { onOpen(evt) };
  websocket.onclose = function(evt) { onClose(evt) };
  websocket.onmessage = function(evt) { onMessage(evt) };
  websocket.onerror = function(evt) { onError(evt) };
  }


function onOpen(evt)
  {
  writeToStatus("CONNECTED");
  liveConnecting=false
  }


function onClose(evt)
  {
  writeToStatus("Reconnecting...");
  setTimeout(connectWebSocket, 1000);
  }


function map(x, in_min, in_max, out_min, out_max)
  {
  if ((in_max - in_min) + out_min ==0) return 0;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
  }


function constrain(x,out_min,out_max)
  {
  if      (x<out_min) x=out_min;
  else if (x>out_max) x=out_max;
  return x;
  }


function onMessage(evt)
  {
  // smoother values are display with the formula: value = measurement*alpha + previous value*(1-alpha)
  try {
    // console.log(evt.data);
    var OnSpeed = JSON.parse(evt.data);

    // I have had a problem with sometimes (probably due to some race condition) the
    // first time through a smoothing loop, the variable to be smoothed is NaN. In this
    // case NaN continues to propigate. These tests make sure the initial smoothed
    // value is not NaN.
    if (Number.isNaN(AOA))             { AOA             = 0.0; }
    if (Number.isNaN(IAS))             { IAS             = 0.0; }
    if (Number.isNaN(PAlt))            { PAlt            = 0.0; }
    if (Number.isNaN(GLoad))           { GLoad           = 0.0; }
    if (Number.isNaN(GLoadLat))        { GLoadLat        = 0.0; }
    if (Number.isNaN(PitchAngle))      { PitchAngle      = 0.0; }
    if (Number.isNaN(RollAngle))       { RollAngle       = 0.0; }
    if (Number.isNaN(iVSI))            { iVSI            = 0.0; }
    if (Number.isNaN(flightPath))      { flightPath      = 0.0; }
    if (Number.isNaN(smoothDecelRate)) { smoothDecelRate = 0.0; }

    AOA                  = (OnSpeed.AOA*smoothingAlpha+AOA*(1-smoothingAlpha)).toFixed(2);
    IAS                  = OnSpeed.IAS;
    IASsmoothed          = (OnSpeed.IAS*smoothingAlpha+IASsmoothed*(1-smoothingAlpha)).toFixed(2);
    PAlt                 = (OnSpeed.PAlt*smoothingAlpha+PAlt*(1-smoothingAlpha)).toFixed(2);
    GLoad                = (OnSpeed.verticalGLoad*smoothingAlpha+GLoad*(1-smoothingAlpha)).toFixed(2);
    GLoadLat             = (OnSpeed.lateralGLoad*smoothingAlpha+GLoadLat*(1-smoothingAlpha)).toFixed(2);
    PitchAngle           = OnSpeed.Pitch;
    RollAngle            = OnSpeed.Roll;
    LDmax                = OnSpeed.LDmax;
    OnspeedFast          = OnSpeed.OnspeedFast;
    OnspeedSlow          = OnSpeed.OnspeedSlow;
    OnspeedWarn          = OnSpeed.OnspeedWarn;
    smoothingAlphaFwdAcc = parseFloat(document.getElementById("smoothingValue").value);
    document.getElementById("currentSmoothing").innerHTML = smoothingAlphaFwdAcc.toFixed(2);

    iVSI                 = OnSpeed.kalmanVSI;
    flightPath           = OnSpeed.flightPath;
    derivedAOA           = PitchAngle - flightPath;
    cP                   = OnSpeed.coeffP;
    pitchRate            = OnSpeed.PitchRate;
    decelRate            = OnSpeed.DecelRate;

    flapsPos             = OnSpeed.flapsPos;
    flapIndex            = OnSpeed.flapIndex;

    if (flapIndex == 0)
      {
      // Best glide weight correction= Sqrt(current weight / gross weight) * glide speed at gross weight.
      LDmaxIAS = Math.sqrt(acCurrentWeight/acGrossWeight) * acVldmax;
      }

    smoothDecelRate = decelRate*smoothingAlphaFwdAcc+smoothDecelRate*(1-smoothingAlphaFwdAcc);
    decelTranslate = constrain(56*smoothDecelRate + 38,-186,150);

    // Update decel needle
    document.getElementById("decelneedle").setAttribute("transform", "translate(0," + decelTranslate + ")");
    document.getElementById("currentFlaps").innerHTML = flapsPos;
    document.getElementById("currentIAS").innerHTML   = IASsmoothed;
    document.getElementById("currentDecel").innerHTML = smoothDecelRate.toFixed(1);

    if (dataRecording)
        {
        // Save incoming data in arrays
        flightData.IAS.push(IAS);
        flightData.DerivedAOA.push(derivedAOA);
        flightData.CP.push(cP);
        flightData.PitchRate.push(pitchRate);
        flightData.Pitch.push(PitchAngle);
        flightData.Flightpath.push(flightPath);
        flightData.DecelRate.push(decelRate);

        // Current trigger is 5 deg/sec in either direction AND a negative pitch angle.
        if (Math.abs((pitchRate)) > 5 && PitchAngle < 0) recordData(false);
        }

    lastUpdate=Date.now();
    }

  catch (e)
    {
    console.log('JSON parsing error:'+e.name+': '+e.message);
    }

  } // end onMessage()


function onError(evt)
  {
  console.log(evt.data);
  writeToStatus(evt.data);
  //console.error("WebSocket error observed:", evt);
  }


function writeToStatus(message)
  {
  var status = document.getElementById("connectionstatus");
  status.innerHTML = message;
  }


function recordData(on)
  {
  dataRecording=on;
  calDate = new Date();
  if (on)
    {
    console.log("Recording Start, flaps "+flapsPos);
    document.getElementById("idStartInstructions").style.display = "none";
    document.getElementById("idStopInstructions").style.display = "block";
    document.getElementById('CPchart').style="display:none";
    document.getElementById('curveResults').style.display="none";
    document.getElementById('saveCalButtons').style.display="none";
    document.getElementById('stackButtons').style.display="none";

    flightData.IAS=[];
    flightData.DerivedAOA=[];
    flightData.CP=[];
    flightData.PitchRate=[];
    flightData.smoothedIAS=[];
    flightData.smoothedCP=[];
    flightData.Pitch=[];
    flightData.Flightpath=[];
    flightData.DecelRate=[];
    }

  else
    {
    console.log("Recording Stop");
    document.getElementById("idStopInstructions").style.display = "none";

    flapsPosCalibrated = flapsPos;
    flightData.smoothedIAS[0] = flightData.IAS[0];
    flightData.smoothedCP[0]  = flightData.CP[0];
    var stallCP = 0;
    var runStallIAS = 100;
    var stallIndex = 0;

    for (i=1;i<flightData.IAS.length;i++)
      {
      flightData.smoothedIAS[i] = flightData.IAS[i]*.98+flightData.smoothedIAS[i-1]*.02;
      flightData.smoothedCP[i]  = flightData.CP[i] *.90+flightData.smoothedCP[i-1] *.10;
      if (flightData.smoothedCP[i]>stallCP)
        {
        stallCP  = flightData.smoothedCP[i];
        runStallIAS = flightData.smoothedIAS[i];
        stallIndex = i;
        }
      }

    console.log('Stall_CP='+stallCP+', Stall_IAS='+runStallIAS);

    var iasIdx = [], iasVals = [], iasOnes = [];
    for (i=0;i<=stallIndex;i++)
      { iasIdx.push(i); iasVals.push(flightData.IAS[i]); iasOnes.push(1); }
    var resultIAS = wlsLinear(iasIdx, iasVals, iasOnes);

    if (stallCP==0)
      {
      alert("Stall not detected, try again, pitch down for stall recovery");
      document.getElementById("idStartInstructions").style.display = "block";
      if (stackedRunCount > 0)
        { document.getElementById('saveCalButtons').style.display="block"; document.getElementById('stackButtons').style.display="block"; }
      }
    else if (resultIAS.slope>=0)
      {
      alert("Airspeed is increasing, try again");
      document.getElementById("idStartInstructions").style.display = "block";
      if (stackedRunCount > 0)
        { document.getElementById('saveCalButtons').style.display="block"; document.getElementById('stackButtons').style.display="block"; }
      }
    else
      {
      // Check flap consistency
      if (stackedRunCount > 0 && flapsPos != stackedFlapsPos)
        {
        alert("Flap position changed from "+stackedFlapsPos+" to "+flapsPos+". Cannot stack across flap settings. Discard or save current stack first.");
        document.getElementById("idStartInstructions").style.display = "block";
        document.getElementById('saveCalButtons').style.display="block";
        document.getElementById('stackButtons').style.display="block";
        return;
        }

      // Store this run as pending
      pendingRun = {
        IAS:         flightData.IAS.slice(0, stallIndex+1),
        DerivedAOA:  flightData.DerivedAOA.slice(0, stallIndex+1),
        CP:          flightData.CP.slice(0, stallIndex+1),
        smoothedIAS: flightData.smoothedIAS.slice(0, stallIndex+1),
        smoothedCP:  flightData.smoothedCP.slice(0, stallIndex+1),
        Pitch:       flightData.Pitch.slice(0, stallIndex+1),
        Flightpath:  flightData.Flightpath.slice(0, stallIndex+1),
        DecelRate:   flightData.DecelRate.slice(0, stallIndex+1),
        stallIAS:    runStallIAS
        };

      if (stackedRunCount == 0) stackedFlapsPos = flapsPos;

      // Fit stacked + pending combined
      var combined = mergeRunData(stackedData, pendingRun);
      var combinedFit = fitAllData(combined);

      // If there's stacked data, also show the stacked-only R^2 for comparison
      if (stackedRunCount > 0)
        {
        document.getElementById('idStackComparison').style.display="block";
        document.getElementById('idStackR2').innerHTML = stackedR2.toFixed(4);
        document.getElementById('idCombinedR2').innerHTML = combinedFit.cpR2.toFixed(4);
        var el = document.getElementById('idCombinedR2');
        el.style.color = combinedFit.cpR2 >= stackedR2 ? '#008800' : '#cc0000';
        }
      else
        document.getElementById('idStackComparison').style.display="none";

      // Apply the combined fit as current results
      applyCombinedFit(combinedFit, combined);

      // Show results, chart, and stacking buttons
      document.getElementById("idStartInstructions").style.display = "none";
      document.getElementById('idRunCount').innerHTML = stackedRunCount + " kept + 1 pending";
      document.getElementById('curveResults').style.display="block";
      document.getElementById('CPchart').style.display="block";
      document.getElementById('saveCalButtons').style.display="block";
      document.getElementById('stackButtons').style.display="block";
      }
    }
  }


function mergeRunData(base, run)
  {
  if (!run) return base;
  var m = {};
  var keys = ['IAS','DerivedAOA','CP','smoothedIAS','smoothedCP','Pitch','Flightpath','DecelRate'];
  for (var k = 0; k < keys.length; k++)
    m[keys[k]] = base[keys[k]].concat(run[keys[k]]);
  m.stallIASvals = base.stallIASvals.concat([run.stallIAS]);
  return m;
  }


function fitAllData(data)
  {
  var n = data.smoothedCP.length;
  if (n < 10) return null;

  var useWLS = document.getElementById('chkWLS').checked;
  var weights = new Array(n);
  if (useWLS)
    {
    var sigma = rollingSigma(data.DerivedAOA, 29);
    for (var i = 0; i < n; i++)
      weights[i] = 1.0 / Math.max(sigma[i] * sigma[i], 1e-9);
    }
  else
    {
    for (var i = 0; i < n; i++)
      weights[i] = 1.0;
    }

  var cpResult = wlsQuadratic(data.smoothedCP, data.DerivedAOA, weights);

  var xIAS = [], yIAS = [], wIAS = [];
  for (var j = 0; j < n; j++)
    {
    var v = data.IAS[j];
    if (v > 0) { xIAS.push(1.0/(v*v)); yIAS.push(data.DerivedAOA[j]); wIAS.push(weights[j]); }
    }
  var iasResult = wlsLinear(xIAS, yIAS, wIAS);

  // Average stall IAS from all runs
  var sIAS = data.stallIASvals;
  var avgStallIAS = 0;
  for (var i = 0; i < sIAS.length; i++) avgStallIAS += sIAS[i];
  avgStallIAS /= sIAS.length;

  return {
    cpEq: cpResult.equation, cpR2: cpResult.r2,
    K: iasResult.slope, a0: iasResult.intercept, iasR2: iasResult.r2,
    stallIAS: avgStallIAS,
    alphaStall: iasResult.slope / (avgStallIAS * avgStallIAS) + iasResult.intercept
    };
  }


function applyCombinedFit(fit, data)
  {
  resultCPtoAOA = {equation: fit.cpEq};
  CPtoAOAr2 = fit.cpR2;
  CPtoAOAcurve = "AOA = "+fit.cpEq[0].toFixed(4)+" * CP^2 ";
  if (fit.cpEq[1]>0) CPtoAOAcurve += "+";
  CPtoAOAcurve += fit.cpEq[1].toFixed(4)+" * CP ";
  if (fit.cpEq[2]>0) CPtoAOAcurve += "+";
  CPtoAOAcurve += fit.cpEq[2].toFixed(4);

  K_fit      = fit.K;
  alpha0     = fit.a0;
  stallIAS   = fit.stallIAS;
  alphaStall = fit.alphaStall;
  IAStoAOAr2 = fit.iasR2;

  ManeuveringIAS = stallIAS * Math.sqrt(acGlimit);
  if (flapIndex>0 && acVfe>0) LDmaxIAS = acVfe;

  LDmaxSetpoint       = (K_fit / (LDmaxIAS * LDmaxIAS) + alpha0).toFixed(2);
  var alphaRange       = alphaStall - alpha0;
  OSFastSetpoint      = (1.0/(OSFastMultiplier*OSFastMultiplier) * alphaRange + alpha0).toFixed(2);
  OSSlowSetpoint      = (1.0/(OSSlowMultiplier*OSSlowMultiplier) * alphaRange + alpha0).toFixed(2);
  var stallWarnIAS     = stallIAS + StallWarnMargin;
  StallWarnSetpoint   = (K_fit / (stallWarnIAS * stallWarnIAS) + alpha0).toFixed(2);
  StallSetpoint       = alphaStall.toFixed(2);
  ManeuveringSetpoint = (K_fit / (ManeuveringIAS * ManeuveringIAS) + alpha0).toFixed(2);

  // Update display
  document.getElementById('idStallSpeed').innerHTML = stallIAS.toFixed(2);
  document.getElementById('idCPtoAOACurve').innerHTML = CPtoAOAcurve;
  document.getElementById('idLDmaxSetpoint').innerHTML = LDmaxSetpoint;
  document.getElementById('idOSFastSetpoint').innerHTML = OSFastSetpoint;
  document.getElementById('idOSSlowSetpoint').innerHTML = OSSlowSetpoint;
  document.getElementById('idStallWarnSetpoint').innerHTML = StallWarnSetpoint;
  document.getElementById('idManeuveringSetpoint').innerHTML = ManeuveringSetpoint;
  document.getElementById('idStallSetpoint').innerHTML = StallSetpoint;
  document.getElementById('idCPtoAOAr2').innerHTML = CPtoAOAr2.toFixed(4);
  document.getElementById('idAlpha0').innerHTML = alpha0.toFixed(2);
  document.getElementById('idAlphaStall').innerHTML = alphaStall.toFixed(2);
  document.getElementById('idIAStoAOAr2').innerHTML = IAStoAOAr2.toFixed(4);
  var useWLS = document.getElementById('chkWLS').checked;
  var methodLabel = useWLS ? 'WLS' : 'OLS';
  var r2Label = useWLS ? 'Weighted R<sup>2</sup>' : 'R<sup>2</sup>';
  document.getElementById('idFitMethod').innerHTML = methodLabel;
  document.getElementById('idFitMethod2').innerHTML = methodLabel;
  document.getElementById('idCPtoAOAr2Label').innerHTML = r2Label;
  document.getElementById('idIAStoAOAr2Label').innerHTML = r2Label;

  // Build chart: stacked data (blue) + pending run (orange) + fitted curve
  var chartData = {series:[]};
  chartData.series[0] = {name:'stackedAOA', data:[]};
  chartData.series[1] = {name:'pendingAOA', data:[]};
  chartData.series[2] = {name:'predictedAOA', data:[]};

  // Stacked data points
  for (i = stackedData.smoothedCP.length - 1; i >= 0; i--)
    chartData.series[0].data.push({x:stackedData.smoothedCP[i], y:stackedData.DerivedAOA[i]});

  // Pending run points
  if (pendingRun)
    for (i = pendingRun.smoothedCP.length - 1; i >= 0; i--)
      chartData.series[1].data.push({x:pendingRun.smoothedCP[i], y:pendingRun.DerivedAOA[i]});

  // Fitted curve over all combined data
  var allCP = data.smoothedCP.slice().sort(function(a,b){return a-b;});
  var step = Math.max(1, Math.floor(allCP.length / 200));
  for (i = 0; i < allCP.length; i += step)
    {
    var cp = allCP[i];
    chartData.series[2].data.push({x:cp, y:(fit.cpEq[0]*cp*cp + fit.cpEq[1]*cp + fit.cpEq[2]).toFixed(2)});
    }

  var options = {
    showLine: false,
    axisX: {
      labelInterpolationFnc: function(value, index) { return index % 100 === 0 ? value : null; },
      type: Chartist.AutoScaleAxis
      },
    series:{
      'stackedAOA':   {showPoint:true, showLine:false},
      'pendingAOA':   {showPoint:true, showLine:false},
      'predictedAOA': {showPoint:false, showLine:true, lineSmooth:Chartist.Interpolation.simple()}
      }
    };
  var responsiveOptions = [
    ['screen and (min-width: 640px)', { axisX: { labelInterpolationFnc: function(v){return v;} } }]
    ];
  new Chartist.Line('.ct-chart', chartData, options, responsiveOptions);
  }


function keepRun()
  {
  if (!pendingRun) return;
  var keys = ['IAS','DerivedAOA','CP','smoothedIAS','smoothedCP','Pitch','Flightpath','DecelRate'];
  for (var k = 0; k < keys.length; k++)
    stackedData[keys[k]] = stackedData[keys[k]].concat(pendingRun[keys[k]]);
  stackedData.stallIASvals.push(pendingRun.stallIAS);
  stackedRunCount++;
  pendingRun = null;

  // Recompute stacked-only fit for future comparison
  var stackFit = fitAllData(stackedData);
  if (stackFit) stackedR2 = stackFit.cpR2;

  document.getElementById('idRunCount').innerHTML = stackedRunCount + " kept";
  document.getElementById('idStackComparison').style.display="none";
  document.getElementById("idStartInstructions").style.display = "block";
  console.log("Run kept. Stack has " + stackedRunCount + " runs, " + stackedData.IAS.length + " points");
  }


function discardRun()
  {
  pendingRun = null;
  document.getElementById('idStackComparison').style.display="none";
  document.getElementById("idStartInstructions").style.display = "block";

  if (stackedRunCount > 0)
    {
    // Re-show stacked results
    var stackFit = fitAllData(stackedData);
    if (stackFit) { applyCombinedFit(stackFit, stackedData); stackedR2 = stackFit.cpR2; }
    document.getElementById('idRunCount').innerHTML = stackedRunCount + " kept";
    }
  else
    {
    document.getElementById('curveResults').style.display="none";
    document.getElementById('CPchart').style.display="none";
    document.getElementById('saveCalButtons').style.display="none";
    document.getElementById('stackButtons').style.display="none";
    }
  console.log("Run discarded.");
  }



function saveData()
  {
  // Export all data: stacked + pending (if any)
  var allData = pendingRun ? mergeRunData(stackedData, pendingRun) : stackedData;
  // Fall back to current flightData if nothing is stacked
  if (allData.IAS.length == 0) allData = flightData;

  var fileContent = "";
  fileContent += ";Calibration run Date/Time="+calDate+"\n";
  fileContent += ";Flap position="+flapsPosCalibrated+" deg\n";
  fileContent += ";Runs in stack="+stackedRunCount+(pendingRun ? "+1 pending" : "")+"\n";
  fileContent += ";StallSpeed="+stallIAS.toFixed(2)+" kts\n";
  fileContent += ";AOA Setpoint angles:\n";
  fileContent += ";LDmaxSetpoint="+LDmaxSetpoint+"\n";
  fileContent += ";OSFastSetpoint="+OSFastSetpoint+"\n";
  fileContent += ";OSSlowSetpoint="+OSSlowSetpoint+"\n";
  fileContent += ";StallWarnSetpoint="+StallWarnSetpoint+"\n";
  fileContent += ";ManeuveringSetpoint="+ManeuveringSetpoint+"\n";
  fileContent += ";StallAngle="+StallSetpoint+"\n";
  fileContent += ";CPtoAOACurve: "+CPtoAOAcurve+"\n";
  fileContent += ";CPtoAOAr2="+CPtoAOAr2+"\n";
  fileContent += ";alpha0="+alpha0.toFixed(4)+"\n";
  fileContent += ";alphaStall="+alphaStall.toFixed(4)+"\n";
  fileContent += ";IAStoAOAr2="+IAStoAOAr2.toFixed(4)+"\n";
  fileContent += ";\n";
  fileContent += "; Data:\n";
  fileContent += "IAS,CP,DerivedAOA,Pitch,FlightPath,DecelRate\n";
  for (i=0; i<allData.IAS.length; i++)
    fileContent += allData.IAS[i]+","+allData.CP[i]+","+allData.DerivedAOA[i]+","+allData.Pitch[i]+","+allData.Flightpath[i]+","+allData.DecelRate[i]+"\n";

  var bb = new Blob([fileContent], { type: 'application/csv' });
  var a = document.createElement('a');
  a.download = 'calibration-flap'+flapsPosCalibrated+'_'+calDate.toISOString().substring(0, 10)+'-'+calDate.getHours()+'_'+calDate.getMinutes()+'.csv';
  a.href = window.URL.createObjectURL(bb);
  a.click();
  }


function saveCalibration()
  {
  params = "flapsPos="+flapsPosCalibrated+"&curve0="+resultCPtoAOA.equation[0]+"&curve1="+resultCPtoAOA.equation[1]+"&curve2="+resultCPtoAOA.equation[2]+"&LDmaxSetpoint="+LDmaxSetpoint+"&OSFastSetpoint="+OSFastSetpoint+"&OSSlowSetpoint="+OSSlowSetpoint+"&StallWarnSetpoint="+StallWarnSetpoint+"&ManeuveringSetpoint="+ManeuveringSetpoint+"&StallSetpoint="+StallSetpoint+"&alpha0="+alpha0+"&alphaStall="+alphaStall;
  var xhr = new XMLHttpRequest();
  xhr.open("POST", "/calwiz?step=save", true);
  xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');
  xhr.onreadystatechange = function() {   //Call a function when the state changes.
    if(xhr.readyState == 4 && xhr.status == 200) {
      console.log(params);
      alert(xhr.responseText);
      }
    }
  xhr.send(params);
  }

)=====";
