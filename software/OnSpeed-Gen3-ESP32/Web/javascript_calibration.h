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
var resultCPtoAOA; // CP to AOA regression curve
var alpha0         = 0; // zero-lift fuselage AOA from IAS-to-AOA fit
var alphaStall     = 0; // stall AOA from IAS-to-AOA fit
var K_fit          = 0; // lift sensitivity from IAS-to-AOA fit
var IAStoAOAr2   = 0; // R-squared of IAS-to-AOA fit

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
    // hide chart and results
    document.getElementById('CPchart').style="display:none";
    document.getElementById('curveResults').style.display="none";
    document.getElementById('saveCalButtons').style.display="none";

    // init data recording arrays
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
    document.getElementById("idStartInstructions").style.display = "block";
    document.getElementById("idStopInstructions").style.display = "none";

    // Calculate smoothed IAS and CP, find max CP and min IAS.
    flapsPosCalibrated = flapsPos;
    flightData.smoothedIAS[0] = flightData.IAS[0];
    flightData.smoothedCP[0]  = flightData.CP[0];
    var stallCP = 0;
    stallIAS = 100;
    var stallIndex = 0;

    for (i=1;i<flightData.IAS.length;i++)
      {
      flightData.smoothedIAS[i] = flightData.IAS[i]*.98+flightData.smoothedIAS[i-1]*.02;
      flightData.smoothedCP[i]  = flightData.CP[i] *.90+flightData.smoothedCP[i-1] *.10;
      if (flightData.smoothedCP[i]>stallCP)
        {
        stallCP  = flightData.smoothedCP[i];
        stallIAS = flightData.smoothedIAS[i];
        stallIndex = i;
        }
      }

    // calculate manuvering speed
    ManeuveringIAS=stallIAS*Math.sqrt(acGlimit);

    console.log('Stall_CP='+stallCP+', Stall_IAS='+stallIAS);
    // calculate polynomial regressions for CP to Derived AOA curve (used at runtime by firmware)
    // prepare data points for regression
    var dataCPtoAOA=[];
    var dataIAS=[]; // IAS linear regression to verify that IAS is decreasing
    for (i=0;i<=stallIndex;i++)
      {
      dataCPtoAOA.push([flightData.smoothedCP[i],flightData.DerivedAOA[i]]);
      dataIAS.push([i,flightData.IAS[i]]);
      }
    const resultIAS = regression.polynomial(dataIAS, { order: 1, precision:2 });

    if (stallCP==0)
      {
      alert("Stall not detected, try again, pitch down for stall recovery");
      console.log("Stall not detected, try again, pitch down for stall recovery");
      }
    else if (resultIAS.equation[0]<0)
      {
      // Airspeed is decreasing
      resultCPtoAOA = regression.polynomial(dataCPtoAOA, { order: 2, precision:4 });
      CPtoAOAcurve="AOA = "+resultCPtoAOA.equation[0].toFixed(4)+" * CP^2 ";
      if (resultCPtoAOA.equation[1]>0) CPtoAOAcurve=CPtoAOAcurve+"+";
      CPtoAOAcurve=CPtoAOAcurve+resultCPtoAOA.equation[1].toFixed(4)+" * CP ";
      if (resultCPtoAOA.equation[2]>0) CPtoAOAcurve=CPtoAOAcurve+"+";
      CPtoAOAcurve=CPtoAOAcurve+resultCPtoAOA.equation[2].toFixed(4);
      CPtoAOAr2=resultCPtoAOA.r2;
      // IAS-to-AOA fit: DerivedAOA = K / IAS^2 + alpha_0  (lift equation)
      // Linear regression of DerivedAOA vs 1/IAS^2 — uses ALL pre-stall data.
      // Extracts alpha_0 (zero-lift fuselage AOA) and K (lift sensitivity).
      // All wizard setpoints are computed from this fit.
      var dataIAStoAOA = [];
      for (var j = 0; j <= stallIndex; j++)
        {
        var iasVal = flightData.IAS[j];
        if (iasVal > 0)
          dataIAStoAOA.push([1.0 / (iasVal * iasVal), flightData.DerivedAOA[j]]);
        }
      var resultIAStoAOA = regression.polynomial(dataIAStoAOA, { order: 1, precision: 6 });
      K_fit        = resultIAStoAOA.equation[0];     // slope = K (lift sensitivity)
      alpha0       = resultIAStoAOA.equation[1];     // intercept = alpha_0 (zero-lift AOA)
      alphaStall   = K_fit / (stallIAS * stallIAS) + alpha0;
      IAStoAOAr2 = resultIAStoAOA.r2;
      console.log("IAS-to-AOA fit: K=" + K_fit + ", alpha0=" + alpha0 + ", alphaStall=" + alphaStall + ", R2=" + IAStoAOAr2);

      // Update LDmaxIAS for flapped calibrations — use configured Vfe
      if (flapIndex>0 && acVfe>0) LDmaxIAS=acVfe;

      // All setpoints computed from the IAS-to-AOA fit: AOA = K / IAS^2 + alpha_0
      // LDmax and Maneuvering use their IAS directly; OSFast/OSSlow use NAOA fractions.
      LDmaxSetpoint       = (K_fit / (LDmaxIAS * LDmaxIAS) + alpha0).toFixed(2);

      // NAOA-based setpoints: NAOA = 1/multiplier^2 (from lift equation)
      var alphaRange = alphaStall - alpha0;
      var NAOAfast   = 1.0 / (OSFastMultiplier * OSFastMultiplier);
      var NAOAslow   = 1.0 / (OSSlowMultiplier * OSSlowMultiplier);
      OSFastSetpoint      = (NAOAfast * alphaRange + alpha0).toFixed(2);
      OSSlowSetpoint      = (NAOAslow * alphaRange + alpha0).toFixed(2);

      // Stall warning from fit at Vs + margin
      var stallWarnIAS    = stallIAS + StallWarnMargin;
      StallWarnSetpoint   = (K_fit / (stallWarnIAS * stallWarnIAS) + alpha0).toFixed(2);

      // Stall from fit (more accurate than polynomial at peak CP)
      StallSetpoint       = alphaStall.toFixed(2);

      // Maneuvering: Va = Vs * sqrt(G-limit), AOA from fit
      ManeuveringSetpoint = (K_fit / (ManeuveringIAS * ManeuveringIAS) + alpha0).toFixed(2);
      console.log("CPtoAOA:",resultCPtoAOA);

      // Build scatterplot data

      var chartData=new Object();
      chartData.series=[];
      chartData.series[0] = new Object();
      chartData.series[0].name = "measuredAOA";
      chartData.series[0].data = [];

      chartData.series[1] = new Object();
      chartData.series[1].name = "predictedAOA";
      chartData.series[1].data = [];

      for (i=stallIndex; i>0;i--)
        {
        dataPoint=new Object();
        dataPoint.x=flightData.smoothedCP[i];
        dataPoint.y=flightData.DerivedAOA[i];
        chartData.series[0].data.push(dataPoint);
        dataPoint=new Object();
        dataPoint.x=flightData.smoothedCP[i];
        dataPoint.y=(resultCPtoAOA.equation[0]*flightData.smoothedCP[i]*flightData.smoothedCP[i]+resultCPtoAOA.equation[1]*flightData.smoothedCP[i]+resultCPtoAOA.equation[2]).toFixed(2);
        chartData.series[1].data.push(dataPoint);
        }
      console.log(chartData);

      // Update chart
      var options = {
        showLine: false,
        axisX: {
          labelInterpolationFnc: function(value, index) {
            return index % 100 === 0 ? value : null;
            },
          type: Chartist.AutoScaleAxis,
          },
        series:{
          'measuredAOA':{
            showPoint: true,
            showLine: false
            },
          'predictedAOA':{
            showPoint: false,
            showLine: true,
            lineSmooth: Chartist.Interpolation.simple()
            }
          }
        };

      var responsiveOptions = [
        ['screen and (min-width: 640px)', {
          axisX: {
            labelInterpolationFnc: function(value, index) {
              return value;
              }
            }
          }]
        ];

      // Show chart and results
      document.getElementById('idStallSpeed').innerHTML=stallIAS.toFixed(2);
      document.getElementById('idCPtoAOACurve').innerHTML=CPtoAOAcurve;
      document.getElementById('idLDmaxSetpoint').innerHTML=LDmaxSetpoint;
      document.getElementById('idOSFastSetpoint').innerHTML=OSFastSetpoint;
      document.getElementById('idOSSlowSetpoint').innerHTML=OSSlowSetpoint;
      document.getElementById('idStallWarnSetpoint').innerHTML=StallWarnSetpoint;
      document.getElementById('idManeuveringSetpoint').innerHTML=ManeuveringSetpoint;
      document.getElementById('idStallSetpoint').innerHTML=StallSetpoint;
      document.getElementById('idCPtoAOAr2').innerHTML=CPtoAOAr2;
      document.getElementById('idAlpha0').innerHTML=alpha0.toFixed(2);
      document.getElementById('idAlphaStall').innerHTML=alphaStall.toFixed(2);
      document.getElementById('idIAStoAOAr2').innerHTML=IAStoAOAr2.toFixed(4);
      document.getElementById('CPchart').style.display="block";
      document.getElementById('curveResults').style.display="block";
      document.getElementById('saveCalButtons').style.display="block";

      new Chartist.Line('.ct-chart', chartData, options, responsiveOptions);
      }

    else
      {
      alert("Airspeed is increasing, try again");
      console.log("Airspeed is increasing, try again");
      }
    }
  }



function saveData()
  {
  var fileContent = "";
  fileContent += ";Calibration run Date/Time="+calDate+"\n";
  fileContent += ";Flap position="+flapsPos+" deg\n";
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
  for (i=0;i<=flightData.IAS.length-1 ;i++)
    {
    fileContent+=flightData.IAS[i]+","+flightData.CP[i]+","+flightData.DerivedAOA[i]+","+flightData.Pitch[i]+","+flightData.Flightpath[i]+","+flightData.DecelRate[i]+"\n";
    }

  var bb = new Blob([fileContent ], { type: 'application/csv' });
  var a = document.createElement('a');
  a.download = 'calibration-flap'+flapsPos+'_'+calDate.toISOString().substring(0, 10)+'-'+calDate.getHours()+'_'+calDate.getMinutes()+'.csv';
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
