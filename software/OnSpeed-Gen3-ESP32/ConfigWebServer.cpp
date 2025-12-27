////////////////////////////////////////////////////
// More details at
//      http://www.flyOnSpeed.org
//      and
//      https://github.com/flyonspeed/OnSpeed-Gen3/

// OnSpeed Wifi - Wifi file server, config manager and debug display for ONSPEED Gen 2 v2,v3 boxes.

#include <cstring>

#include <Arduino.h>

#include <WiFi.h>               // https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
#include <WiFiClient.h>
#include <WiFiAP.h>

#include <HTTP_Method.h>
#include <WebServer.h>          // https://github.com/espressif/arduino-esp32/tree/master/libraries/WebServer

#include <DNSServer.h>          // https://github.com/espressif/arduino-esp32/tree/master/libraries/DNSServer

#include <ESPmDNS.h>

#include <Update.h>             // https://github.com/espressif/arduino-esp32/tree/master/libraries/Update

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <ArduinoJson.h>

#include "Globals.h"

#include "Web/html_logo.h"
#include "Web/html_header.h"
#include "Web/html_liveview.h"
#include "Web/html_calibration.h"
#include "Web/javascript_calibration.h"
#include "Web/javascript_chartist1.h"
#include "Web/javascript_chartist2.h"
#include "Web/javascript_regression.h"
#include "Web/sg_filter.h"
#include "Web/css_chartist.h"

//#define SUPPORT_WIFI_CLIENT

// Structures
// ----------

typedef struct
  {
  char filename[11];
  long filesize;
  } filetype;

// Module variables
// ----------------

// DNS server to handle domain names instead of raw IP addresses
DNSServer           DnsServer;

// Config web server
WebServer           CfgServer(80);

const char*     szSSID     = "OnSpeed";
const char*     szPassword = "angleofattack";

#ifdef SUPPORT_WIFI_CLIENT
String sClientWifi_SSID     = "HangarWifi"; // currently not needed
String sClientWifi_Password = "test"; // currently not needed
#endif

// Calibration wizard variables
int     iAcGrossWeight;
int     iAcCurrentWeight;
float   fAcVldmax;
float   fAcGlimit;

String      pageHeader;
String      pageFooter="</body></html>";

// Variables used for upload
bool        bUploadedConfigStringGood;
SemaphoreHandle_t uploadMutex;

// GLOBALS
//String sVersion = String("4.0.0 beta");

// Function prototypes
// -------------------
void HandleIndex();
void HandleFavicon();
void HandleReboot();
void HandleLive();
void HandleConfig();
void HandleConfigSave();
void HandleDefaultConfig();
void HandleConfigUpload();
void HandleFinalUpload();
void HandleGetValue();
void HandleSensorConfig();
void HandleCalWizard();
void HandleFormat();
void HandleLogs();
void HandleDelete();
void HandleDownload();
void HandleWifiReflash();
void HandleUpgrade();
void HandleUpgradeSuccess();
void HandleUpgradeFailure();


#ifdef SUPPORT_WIFI_CLIENT
void HandleWifiSettings();
#endif

String sFormatBytes(size_t bytes);

// ----------------------------------------------------------------------------

// Service the configuration web CfgServer.

void CfgWebServerPoll()
    {
    CfgServer.handleClient();
    DnsServer.processNextRequest();
    }

// ----------------------------------------------------------------------------

void CfgWebServerInit()
    {
    uploadMutex = xSemaphoreCreateMutex();

    // Calibration wizard variables
    iAcGrossWeight   = 2700;
    iAcCurrentWeight = 2500;
    fAcVldmax        = 91.0;
    fAcGlimit        = 3.8;

    // WIFI INIT
    g_Log.print(MsgLog::EnWebServer, MsgLog::EnDebug, "Starting Access Point");

#ifdef SUPPORT_WIFI_CLIENT
    WiFi.mode(WIFI_AP_STA);
#else
    WiFi.mode(WIFI_AP);
#endif

    //WiFi.disconnect();
    delay(10);
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.softAP(szSSID, szPassword);

    // Wait after init softAP
    delay(100);


    IPAddress    Ip(192, 168,   0, 1);
    IPAddress NMask(255, 255, 255, 0);

    WiFi.softAPConfig(Ip, Ip, NMask);
    IPAddress myIP = WiFi.softAPIP();
    g_Log.print("AP IP address: ");
    g_Log.println(myIP.toString().c_str());

    delay(100);

    // Connect URL to handler routine
    CfgServer.on("/",                HTTP_GET,  HandleIndex);
    CfgServer.on("/favicon.ico",     HTTP_GET,  HandleFavicon);
    CfgServer.on("/reboot",          HTTP_GET,  HandleReboot);
    CfgServer.on("/live",            HTTP_GET,  HandleLive);
    CfgServer.on("/aoaconfig",       HTTP_GET,  HandleConfig);
    CfgServer.on("/aoaconfigsave",   HTTP_POST, HandleConfigSave);
    CfgServer.on("/defaultconfig",   HTTP_GET,  HandleDefaultConfig);
    CfgServer.on("/aoaconfigupload", HTTP_POST, HandleFinalUpload, HandleConfigUpload);
    CfgServer.on("/getvalue",        HTTP_GET,  HandleGetValue);
    CfgServer.on("/sensorconfig",    HTTP_GET,  HandleSensorConfig);
    CfgServer.on("/calwiz",          HTTP_GET,  HandleCalWizard);
    CfgServer.on("/calwiz",          HTTP_POST, HandleCalWizard);
    CfgServer.on("/format",          HTTP_GET,  HandleFormat);
    CfgServer.on("/logs",            HTTP_GET,  HandleLogs);
    CfgServer.on("/delete",          HTTP_GET,  HandleDelete);
    CfgServer.on("/download",        HTTP_GET,  HandleDownload);

//    CfgServer.on("/wifireflash",     HTTP_GET,  HandleWifiReflash);
    CfgServer.on("/upgrade",         HTTP_GET,  HandleUpgrade);

#ifdef SUPPORT_WIFI_CLIENT
    CfgServer.on("/wifi",            HTTP_GET,  HandleWifiSettings);
#endif

    // Handling uploading firmware file
    CfgServer.on("/upload", HTTP_POST,
        []()
            {
            CfgServer.sendHeader("Connection", "close");
            if (Update.hasError()) HandleUpgradeFailure();
            else                   HandleUpgradeSuccess();
            //CfgServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            delay(5000);
//            ESP.restart();
            _softRestart();
            }, // end first lambda expression
        []()
            {
            HTTPUpload& upload = CfgServer.upload();
            if (upload.status == UPLOAD_FILE_START)
                {
                g_Log.printf(MsgLog::EnWebServer, MsgLog::EnDebug, "Update: %s\n", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                    { //start with max available size
                    //Update.printError(Serial);
                    }
                }
            else if (upload.status == UPLOAD_FILE_WRITE)
                {
                /* flashing firmware to ESP*/
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                    {
                    //Update.printError(Serial);
                    }
                }
            else if (upload.status == UPLOAD_FILE_END)
                {
                if (Update.end(true))
                    { //true to set the size to the current progress
                    g_Log.printf(MsgLog::EnWebServer, MsgLog::EnDebug, "Update Success: %u\nRebooting...\n", upload.totalSize);
                    }
                else
                    {
                    //Update.printError(Serial);
                    }
                }
            } // end second lambda expression
        );
#if 0
    // Called when the url is not defined here
    CfgServer.onNotFound(
        []()
            {
            if (!HandleFileRead(CfgServer.uri()))
                CfgServer.send(404, "text/plain", "File Not Found");
            }
        );
#endif
    // Start server
    CfgServer.begin();

    if (MDNS.begin("onspeed"))
        MDNS.addService("http", "tcp", 80);
    // DnsServer.start(53, "onspeed.local",  Ip);

    } // end setup()


// ----------------------------------------------------------------------------
// URL handlers
// ----------------------------------------------------------------------------

void UpdateHeader()
    {
    pageHeader = String(szHtmlHeader);

    // rewrite firmware in the html header
    //if (sVersion == "")
    //    sVersion = "UNKNOWN";

    pageHeader.replace("wifi_fw", "OnSpeed Version: " VERSION);

#if 0
    // Update wifi status in html header
    if (WiFi.status() == WL_CONNECTED)
        {
        pageHeader.replace("wifi_status","");
        pageHeader.replace("wifi_network",clientwifi_ssid);
        }
    else
        {
        pageHeader.replace("wifi_status","offline");
        pageHeader.replace("wifi_network","Not Connected");
        }
#endif
    }

// ----------------------------------------------------------------------------

void HandleIndex()
    {
    String      sPage;

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;
    sPage += "<br><br>\n"
        "<strong>Welcome to the OnSpeed Wifi gateway.</strong><br><br>\n"
        "General usage guidelines:<br>\n"
        "<br>\n"
        "- Connect from one device at a time<br>\n"
        "- Visit one page at a time<br>\n"
        "- During log file downloads data recording and tones are disabled<br>\n"
        "- LiveView is for debugging purposes only. Do not use for flight.<br>\n";
    sPage += pageFooter;

    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

void HandleFavicon()
    {
    CfgServer.send(404, "text/plain", "FileNotFound");
    }

// ----------------------------------------------------------------------------

void HandleReboot()
  {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;

    if (CfgServer.arg("confirm").indexOf("yes") < 0)
        {
        // Display confirmation page
        sPage += "<br><br><p style=\"color:red\">Confirm that you want to reboot OnSpeed.</p>\
        <br><br><br>\
        <a href=\"reboot?confirm=yes\" class=\"button\">Reboot</a>\
        <a href=\"/\">Cancel</a>";
        sPage += pageFooter;
        CfgServer.send(200, "text/html", sPage);
        }
    else
        {
        // reboot system
        sPage += "<br><br><p>OnSpeed is rebooting. Wait a few seconds to reconnect.</p>";
        sPage += "<script>setTimeout(function () { window.location.href = \"/\";}, 7000);</script>";
        sPage += pageFooter;
        CfgServer.send(200, "text/html", sPage);
        delay(1000);
//        ESP.restart();
        _softRestart();
        } // end if reboot
    }


// ----------------------------------------------------------------------------

void HandleLive()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + sizeof(htmlLiveView) + pageFooter.length());
    sPage += pageHeader;
    sPage += htmlLiveView;
    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

void HandleConfig()
    {
    String sPage;
//    String sConfigString = "";

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 16384);
    sPage += pageHeader;

    // Get live values script
#if 0
    sPage += R"#(

<script>
function getValue(senderID, valueName, targetID)
    {
    senderOriginalValue = document.getElementById(senderID).value;
    document.getElementById(senderID).disabled = true;
    if (valueName == "AUDIOTEST" || valueName == "VNOCHIMETEST")
        {
        document.getElementById(senderID).value="Testing...";
        }
    else
        {
        document.getElementById(senderID).value="Reading...";
        }
    var xmlHttp = new XMLHttpRequest();
    xmlHttp.onreadystatechange = function()
        {
        if (this.readyState == 4 && this.status == 200 && senderID!='')
            {
            if (this.responseText.indexOf("<"+valueName+">")>=0)
                {
                if (valueName=="AUDIOTEST" || valueName=="VNOCHIMETEST")
                    {
                    document.getElementById(senderID).value=senderOriginalValue;
                    }
                else
                    {
                    document.getElementById(targetID).value = this.responseText.replace(/(<([^>]+)>)/ig,"");
                    document.getElementById(senderID).value="Updated!";
                    }
                }
            else
                {
                document.getElementById(senderID).value="Error!";
                }
            setTimeout(function(){ document.getElementById(senderID).value=senderOriginalValue;document.getElementById(senderID).disabled = false}, 1000);
            }
        };
    xmlHttp.open("GET", "/getvalue?name="+valueName, true);
    xmlHttp.send();
    }
</script>

)#";

#else
    sPage += R"#(

<script>
)#";

    // FillInValue() queries for a single value and writes it to the targetID
    sPage += R"#(
function FillInValue(SenderID, ValueName, TargetID)
    {
    document.getElementById(SenderID).disabled = true;

    if (ValueName == "AUDIOTEST" || ValueName == "VNOCHIMETEST")
        {
        let ButtonLabel = document.getElementById(SenderID).value;
        document.getElementById(SenderID).value="Testing...";

        GetValue(ValueName);

        document.getElementById(SenderID).value = ButtonLabel;
        }
    else
        {
        document.getElementById(SenderID).value="Reading...";

        ReturnValue = GetValue(ValueName);

        if (ReturnValue != null)
            {
            document.getElementById(TargetID).value = ReturnValue;
            document.getElementById(SenderID).value = "Updated!";
            }
        }

    document.getElementById(SenderID).disabled = false;
    }

)#";

    // GetValue() queries for a single value and returns it as a text string, null on error
    sPage += R"#(
function GetValue(ValueName) {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", "/getvalue?name=" + ValueName, false);
    xhr.send(null);

    if (xhr.status === 200) {
        return xhr.responseText;
        }
    else {
        console.error("GetValue() request failed:", xhr.status);
        return null;
        }
    }
)#";

    // Audio test start/stop support (toggle button + status polling)
    sPage += R"#(
let gAudioTestPollTimer = null;

function GetValueAsync(ValueName, callback)
    {
    const xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function()
        {
        if (xhr.readyState === 4)
            {
            if (xhr.status === 200)
                callback(xhr.responseText);
            else
                callback(null);
            }
        };
    xhr.open("GET", "/getvalue?name=" + ValueName, true);
    xhr.send(null);
    }

function AudioTestSetButton(SenderID, state)
    {
    const btn = document.getElementById(SenderID);
    if (!btn)
        return;

    btn.dataset.audiotestState = state;

    if (state === "running")
        {
        btn.value = "Stop";
        btn.disabled = false;
        }
    else if (state === "stopping")
        {
        btn.value = "Stopping...";
        btn.disabled = true;
        }
    else
        {
        btn.value = "Test Audio";
        btn.disabled = false;
        }
    }

function AudioTestStopPolling()
    {
    if (gAudioTestPollTimer !== null)
        {
        clearInterval(gAudioTestPollTimer);
        gAudioTestPollTimer = null;
        }
    }

function AudioTestStartPolling(SenderID)
    {
    AudioTestStopPolling();
    gAudioTestPollTimer = setInterval(function()
        {
        GetValueAsync("AUDIOTESTSTATUS", function(resp)
            {
            if (resp === null)
                return;

            if (resp.trim() === "RUNNING")
                {
                AudioTestSetButton(SenderID, "running");
                }
            else
                {
                AudioTestSetButton(SenderID, "idle");
                AudioTestStopPolling();
                }
            });
        }, 400);
    }

function ToggleAudioTest(SenderID)
    {
    const btn = document.getElementById(SenderID);
    if (!btn)
        return;

    const state = btn.dataset.audiotestState || "idle";

    if (state === "running" || state === "stopping")
        {
        AudioTestSetButton(SenderID, "stopping");
        GetValueAsync("AUDIOTESTSTOP", function(_resp)
            {
            AudioTestStartPolling(SenderID);
            });
        }
    else
        {
        btn.disabled = true;
        btn.value = "Starting...";

        GetValueAsync("AUDIOTEST", function(resp)
            {
            if (resp === null)
                {
                AudioTestSetButton(SenderID, "idle");
                return;
                }

            // Treat "busy" as already running.
            AudioTestSetButton(SenderID, "running");
            AudioTestStartPolling(SenderID);
            });
        }
    }

window.addEventListener('load', function()
    {
    const btn = document.getElementById('id_volumeTestButton');
    if (!btn)
        return;

    btn.dataset.audiotestState = "idle";

    // Sync initial state if a test was started from elsewhere (e.g. console)
    GetValueAsync("AUDIOTESTSTATUS", function(resp)
        {
        if (resp !== null && resp.trim() === "RUNNING")
            {
            AudioTestSetButton(btn.id, "running");
            AudioTestStartPolling(btn.id);
            }
        else
            {
            AudioTestSetButton(btn.id, "idle");
            }
        });
    });
)#";

    sPage += R"#(
</script>
)#";

#endif

    // Web page container
    sPage += R"#(
<div class="content-container">
    <form  id="id_configForm" action="aoaconfigsave" method="POST">
        <div class="form round-box">)#";

    // aoaSmoothing
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_aoaSmoothing">AOA Smoothing</label>
            <input id="id_aoaSmoothing" name="aoaSmoothing" type="text" value=")#" + String(g_Config.iAoaSmoothing) + R"#("/>
        </div>)#";

    // pressureSmoothing
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_pressureSmoothing">Pressure Smoothing</label>
            <input id="id_pressureSmoothing" name="pressureSmoothing" type="text" value=")#" + String(g_Config.iPressureSmoothing) + R"#(" />
        </div>)#";

    // dataSource
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_dataSource">Data Source - Operation mode</label>
            <select id="id_dataSource" name="dataSource">
            <option value="SENSORS")#";       if (g_Config.suDataSrc.enSrc == SuDataSource::EnSensors)       sPage += " selected"; sPage += R"#(>Sensors (default)</option>
            <option value="TESTPOT")#";       if (g_Config.suDataSrc.enSrc == SuDataSource::EnTestPot)       sPage += " selected"; sPage += R"#(>Test Potentiometer</option>
            <option value="RANGESWEEP")#";    if (g_Config.suDataSrc.enSrc == SuDataSource::EnRangeSweep)    sPage += " selected"; sPage += R"#(>Range Sweep</option>
            <option value="REPLAYLOGFILE")#"; if (g_Config.suDataSrc.enSrc == SuDataSource::EnReplay)        sPage += " selected"; sPage += R"#(>Replay Log File</option>
            </select>
        </div>
)#";

    // logFileToReplay
    String replayLogFileStyle;
    if (g_Config.suDataSrc.enSrc == SuDataSource::EnReplay) replayLogFileStyle = R"#(style="display:block")#";
    else                                                    replayLogFileStyle = R"#(style="display:none")#";

#if 0
    sPage += R"#(
        <div class="form-divs flex-col-12 replaylogfilesetting" )#" + replayLogFileStyle + R"#(>
            <label for="id_replayLogFile">Log file to replay</label>
            <input id="id_replayLogFile" name="logFileName" type="text" value=")#" + String(g_Config.sReplayLogFileName) + R"#(" />
        </div>
)#";
#else
    sPage += R"#(
        <div class="form-divs flex-col-12 replaylogfilesetting" )#" + replayLogFileStyle + R"#(>
            <label for="id_replayLogFile">Log file to replay</label>
            <input id="id_replayLogFile" name="logFileName" list="logFiles" value=")#" +
                String(g_Config.sReplayLogFileName) + R"#(" onfocus="this.value=''" autocomplete="off" />
                <datalist id="logFiles">
)#";
    // Populate with a list of filename on the disk
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
        {
        SdFileSys::SuFileInfoList   suFileList;

        // Get a list of file names from the disk
        if (g_SdFileSys.FileList(&suFileList))
            for (int iIdx=0; iIdx<suFileList.size(); iIdx++)
                // Only list ".csv" files
                if (strcasecmp(suFileList[iIdx].szFileName + strlen(suFileList[iIdx].szFileName) - 4, ".csv") == 0)
                    sPage += "                    <option value=\"" + String(suFileList[iIdx].szFileName) + "\">\n";
        xSemaphoreGive(xWriteMutex);
        }

    sPage += R"#(
                </datalist>
        </div>
)#";
#endif

    // flap curves
    for (int iFlapIdx=0; iFlapIdx<g_Config.aFlaps.size(); iFlapIdx++)
        {
        // start flap curve section
        sPage += R"#(
        <section id="curvesection">
)#";

        sPage += "\
            <h2> Flap Curve " + String(iFlapIdx+1) + " </h2>";

        sPage += R"#(
            <div class="form-divs flex-col-12"></div>
            <div class="form-divs flex-col-4">
                <label for="id_flapDegrees)#" + String(iFlapIdx) + R"#(">Flap Degrees</label>
                <input id="id_flapDegrees)#" + String(iFlapIdx) + R"#(" name="flapDegrees)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + String(g_Config.aFlaps[iFlapIdx].iDegrees) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapPotPositions)#" + String(iFlapIdx) + R"#(">Sensor Value</label>
                <input id="id_flapPotPositions)#" + String(iFlapIdx) + R"#(" class="curve" name="flapPotPositions)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + String(g_Config.aFlaps[iFlapIdx].iPotPosition) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapPotPositions)#" + String(iFlapIdx) + R"#(Read">&nbsp;</label>
                <input id="id_flapPotPositions)#" + String(iFlapIdx) + R"#(Read" name="flapPotPositions)#" + String(iFlapIdx) + R"#(Read" type="button" value="Read" class="greybutton" onclick="FillInValue(this.id,'FLAPS','id_flapPotPositions)#" + String(iFlapIdx) + R"#(')"/>
            </div>
            <div class="form-divs flex-col-8">
                <label for="id_flapLDMAXAOA)#" + String(iFlapIdx) + R"#(">L/Dmax AOA</label>
                <input id="id_flapLDMAXAOA)#" + String(iFlapIdx) + R"#(" class="curve" name="flapLDMAXAOA)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].fLDMAXAOA) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapLDMAXAOA)#" + String(iFlapIdx) + R"#(Read">&nbsp;</label>
                <input id="id_flapLDMAXAOA)#" + String(iFlapIdx) + R"#(Read" name="flapLDMAXAOA)#" + String(iFlapIdx) + R"#(Read" type="button" value="Use Live AOA" class="greybutton" onclick="FillInValue(this.id,'AOA','id_flapLDMAXAOA)#" + String(iFlapIdx) + R"#(')"/>
            </div>
            <div class="form-divs flex-col-8">
                <label for="id_flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(">OnSpeed Fast AOA</label>
                <input id="id_flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(" name="flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].fONSPEEDFASTAOA) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(Read">&nbsp;</label>
                <input id="id_flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(Read" name="flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(Read" type="button" value="Use Live AOA" class="greybutton" onclick="FillInValue(this.id,'AOA','id_flapONSPEEDFASTAOA)#" + String(iFlapIdx) + R"#(')"/>
            </div>
            <div class="form-divs flex-col-8">
                <label for="id_flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(">OnSpeed Slow AOA</label>
                <input id="id_flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(" name="flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].fONSPEEDSLOWAOA) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(Read">&nbsp;</label>
                <input id="id_flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(Read" name="flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(Read" type="button" value="Use Live AOA" class="greybutton" onclick="FillInValue(this.id,'AOA','id_flapONSPEEDSLOWAOA)#" + String(iFlapIdx) + R"#(')"/>
            </div>
            <div class="form-divs flex-col-8">
                <label for="id_flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(">Stall Warning AOA</label>
                <input id="id_flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(" name="flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].fSTALLWARNAOA) + R"#("/>
            </div>
            <div class="form-divs flex-col-4">
                <label for="id_flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(Read">&nbsp;</label>
                <input id="id_flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(Read" name="flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(Read" type="button" value="Use Live AOA" class="greybutton" onclick="FillInValue(this.id,'AOA','id_flapSTALLWARNAOA)#" + String(iFlapIdx) + R"#(')"/>
            </div>
            <div class="form-divs flex-col-12">
                <label for="id_aoaCurve)#" + String(iFlapIdx) + R"#(Type">AOA Curve Type</label>
                <select id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Type" name="aoaCurve)#" + String(iFlapIdx) + R"#(Type" onchange="curveTypeChange('id_aoaCurve)#" + String(iFlapIdx) + R"#(Type',)#" + String(iFlapIdx) + R"#()">
                    <option value="1")#";

        if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 1) sPage += " selected";
        sPage += R"#(>Polynomial</option>
                    <option value="2")#";
        if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 2) sPage += " selected";
        sPage += R"#(>Logarithmic</option>
                    <option value="3")#";
        if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 3) sPage += " selected";

        sPage += R"#(>Exponential</option>
                </select>
            </div>

            <div class="form-divs flex-col-12 curvelabel"><label>AOA Curve</label></div>
            <div class="form-divs flex-col-2"><input id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Coeff0" class="curve" name="aoaCurve)#" + String(iFlapIdx) + R"#(Coeff0" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[0]) + R"#("/></div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-1" id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Param0">)#";

        if      (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 1) sPage += " *X<sup>3</sup>+";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 2) sPage += " * 0 + ";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 3) sPage += " * 0 + ";

        sPage += R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-2">)#"
R"#(<input id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Coeff1" class="curve" name="aoaCurve)#" + String(iFlapIdx) + R"#(Coeff1" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[1]) + R"#("/>)#"
R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-1" id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Param1">)#";

        if      (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 1) sPage += " *X<sup>2</sup>+";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 2) sPage += " * 0 + ";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 3) sPage += " * 0 + ";

        sPage += R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-2">)#"
R"#(<input id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Coeff2" class="curve" name="aoaCurve)#" + String(iFlapIdx) + R"#(Coeff2" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[2]) + R"#("/>)#"
R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-1" id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Param2">)#";

        if      (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 1) sPage += "*X<sup></sup>+";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 2) sPage += "*ln(x)+ ";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 3) sPage += "* e^ (";

        sPage += R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-2">)#"
R"#(<input id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Coeff3" class="curve" name="aoaCurve)#" + String(iFlapIdx) + R"#(Coeff3" type="text" value=")#" + g_Config.ToString(g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[3]) + R"#("/>)#"
R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-1" id="id_aoaCurve)#" + String(iFlapIdx) + R"#(Param3">)#";

        if      (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 1) sPage += "";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 2) sPage += "";
        else if (g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType == 3) sPage += ") * x";

        sPage += R"#(</div>)#";

        sPage += "\n"
R"#(            <div class="form-divs flex-col-6"></div>)#" "\n"
R"#(            <div class="form-divs flex-col-6">)#"
R"#(<input id="id_flapDegrees)#" + String(iFlapIdx) + R"#(Delete" name="deleteFlapPos)#" + String(iFlapIdx) + R"#(" type="submit" value="Delete Flap Position" class="redbutton" onClick="return confirm('Are you sure you want to delete this flap position?') "/>)#"
R"#(</div>)#";

// end flap curve section
        sPage += "\n"
R"#(        </section>)#" "\n";
        } // end for all flap indexes

    // Add flap position
    //if (g_Config.aiFlapDegrees.Count<MAX_AOA_CURVES)
    //    {
        sPage += R"#(
        <div class="form-divs flex-col-6"><input id="id_addFlapPos" name="addFlapPos" type="submit" value="Add New Flap Position" class="greybutton"/></div>
)#";
        //}

    // Boom enable/disable
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_readBoom">Flight Test Boom</label>
            <select id="id_readBoom" name="readBoom">
                <option value="1")#"; if ( g_Config.bReadBoom) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bReadBoom) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

    String casCurveVisibility;
    if (g_Config.bCasCurveEnabled) casCurveVisibility=R"#(style="display:block")#";
    else                           casCurveVisibility=R"#(style="display:none")#";

    // CAS curve enable/disable
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_casCurveEnabled">Airspeed Calibration</label>
            <select id="id_casCurveEnabled" name="casCurveEnabled">
                <option value="1")#"; if ( g_Config.bCasCurveEnabled) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bCasCurveEnabled) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

    // Airspeed calibration curve type
    sPage += R"#(
        <div class="form-divs flex-col-12 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <label for="id_casCurveType">Airspeed Calibration Curve Type</label>
            <select id="id_casCurveType" name="casCurveType" onchange="cascurveTypeChange('id_casCurveType')">
                <option value="1")#"; if (g_Config.CasCurve.iCurveType == 1) sPage += " selected"; sPage += R"#(>Polynomial</option>
                <option value="2")#"; if (g_Config.CasCurve.iCurveType == 2) sPage += " selected"; sPage += R"#(>Logarithmic</option>
                <option value="3")#"; if (g_Config.CasCurve.iCurveType == 3) sPage += " selected"; sPage += R"#(>Exponential</option>
            </select>
        </div>)#";

    // CAS curve
    sPage += R"#(
        <div class="form-divs flex-col-12 curvelabel cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <label>Airspeed Calibration Curve</label>
        </div>
        <div class="form-divs flex-col-2 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <input id="id_casCurveCoeff0" class="curve" name="casCurveCoeff0" type="text" value=")#" + g_Config.ToString(g_Config.CasCurve.afCoeff[0]) + R"#("/>
        </div>
        <div id="id_casCurveParam0" class="form-divs flex-col-1 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>)#";

    if      (g_Config.CasCurve.iCurveType==1) sPage += " *X<sup>3</sup>+ ";
    else if (g_Config.CasCurve.iCurveType==2) sPage += " * 0 + ";
    else if (g_Config.CasCurve.iCurveType==3) sPage += " * 0 + ";

    sPage += R"#(</div>
        <div class="form-divs flex-col-2 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <input id="id_casCurveCoeff1" class="curve" name="casCurveCoeff1" type="text" value=")#" + g_Config.ToString(g_Config.CasCurve.afCoeff[1]) + R"#("/>
        </div>
        <div id="id_casCurveParam1" class="form-divs flex-col-1 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>)#";

    if      (g_Config.CasCurve.iCurveType==1) sPage += " *X<sup>2</sup>+ ";
    else if (g_Config.CasCurve.iCurveType==2) sPage += " * 0 + ";
    else if (g_Config.CasCurve.iCurveType==3) sPage += " * 0 + ";

    sPage += R"#(</div>
        <div class="form-divs flex-col-2 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <input id="id_casCurveCoeff2" class="curve" name="casCurveCoeff2" type="text" value=")#" + g_Config.ToString(g_Config.CasCurve.afCoeff[2]) + R"#("/>
        </div>
        <div id="id_casCurveParam2" class="form-divs flex-col-1 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>)#";

    if      (g_Config.CasCurve.iCurveType==1) sPage += " *X<sup></sup>+ ";
    else if (g_Config.CasCurve.iCurveType==2) sPage += "*ln(x)+";
    else if (g_Config.CasCurve.iCurveType==3) sPage += "* e^ (";

    sPage += R"#(</div>
        <div class="form-divs flex-col-2 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
            <input id="id_casCurveCoeff3" class="curve" name="casCurveCoeff3" type="text" value=")#" + g_Config.ToString(g_Config.CasCurve.afCoeff[3]) + R"#("/>
        </div>
        <div id="id_casCurveParam3" class="form-divs flex-col-1 cascurvesetting" )#" + String(casCurveVisibility) + R"#(>
)#";

    if      (g_Config.CasCurve.iCurveType==1) sPage += "";
    else if (g_Config.CasCurve.iCurveType==2) sPage += "";
    else if (g_Config.CasCurve.iCurveType==3) sPage += " * x)";

    sPage += R"#(
        </div>)#";

    // Pressure ports orientation
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_portsOrientation">Pressure ports orientation</label>
            <select id="id_portsOrientation" name="portsOrientation">
                <option value="UP")#";      if (g_Config.sPortsOrientation == "UP")      sPage += " selected"; sPage += R"#(>Up</option>
                <option value="DOWN")#";    if (g_Config.sPortsOrientation == "DOWN")    sPage += " selected"; sPage += R"#(>Down</option>
                <option value="LEFT")#";    if (g_Config.sPortsOrientation == "LEFT")    sPage += " selected"; sPage += R"#(>Left</option>
                <option value="RIGHT")#";   if (g_Config.sPortsOrientation == "RIGHT")   sPage += " selected"; sPage += R"#(>Right</option>
                <option value="FORWARD")#"; if (g_Config.sPortsOrientation == "FORWARD") sPage += " selected"; sPage += R"#(>Forward</option>
                <option value="AFT")#";     if (g_Config.sPortsOrientation == "AFT")     sPage += " selected"; sPage += R"#(>Aft</option>
            </select>
        </div>)#";

    // Box top orientation
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_boxtopOrientation">Box top Orientation</label>
            <select id="id_boxtopOrientation" name="boxtopOrientation">
                <option value="UP")#";      if (g_Config.sBoxtopOrientation == "UP")      sPage += " selected"; sPage += R"#(>Up</option>
                <option value="DOWN")#";    if (g_Config.sBoxtopOrientation == "DOWN")    sPage += " selected"; sPage += R"#(>Down</option>
                <option value="LEFT")#";    if (g_Config.sBoxtopOrientation == "LEFT")    sPage += " selected"; sPage += R"#(>Left</option>
                <option value="RIGHT")#";   if (g_Config.sBoxtopOrientation == "RIGHT")   sPage += " selected"; sPage += R"#(>Right</option>
                <option value="FORWARD")#"; if (g_Config.sBoxtopOrientation == "FORWARD") sPage += " selected"; sPage += R"#(>Forward</option>
                <option value="AFT")#";     if (g_Config.sBoxtopOrientation == "AFT")     sPage += " selected"; sPage += R"#(>Aft</option>
            </select>
        </div>)#";

    // EFIS settings
    sPage += R"#(
        <div class="form-divs flex-col-6">
            <label for="id_readEfisData">Serial EFIS data</label>
            <select id="id_readEfisData" name="readEfisData">
                <option value="1")#"; if ( g_Config.bReadEfisData) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bReadEfisData) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>
        <div class="form-divs flex-col-6 efistypesetting">
            <label for="id_efisType">EFIS Type</label>
            <select id="id_efisType" name="efisType">
                <option value="DYNOND10")#";  if (g_Config.sEfisType == "DYNOND10")  sPage += " selected"; sPage += R"#(>Dynon D10/D100</option>
                <option value="ADVANCED")#";  if (g_Config.sEfisType == "ADVANCED")  sPage += " selected"; sPage += R"#(>SkyView/Advanced</option>
                <option value="GARMING5")#";  if (g_Config.sEfisType == "GARMING5")  sPage += " selected"; sPage += R"#(>Garmin G5</option>
                <option value="GARMING3X")#"; if (g_Config.sEfisType == "GARMING3X") sPage += " selected"; sPage += R"#(>Garmin G3X</option>
                <option value="VN-300")#";    if (g_Config.sEfisType == "VN-300")    sPage += " selected"; sPage += R"#(>VectorNav VN-300 GNSS/INS</option>
                <option value="VN-100")#";    if (g_Config.sEfisType == "VN-100")    sPage += " selected"; sPage += R"#(>VectorNav VN-100 IMU/AHRS</option>
                <option value="MGL")#";       if (g_Config.sEfisType == "MGL")       sPage += " selected"; sPage += R"#(>MGL iEFIS</option>
            </select>
        </div>)#";

    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_calSource">Calibration Data Source</label>
            <select id="id_calSource" name="calSource">
                <option value="ONSPEED")#"; if (g_Config.sCalSource == "ONSPEED" || g_Config.sCalSource == "") sPage += " selected"; sPage += R"#(>ONSPEED (internal IMU)</option>
                <option value="EFIS")#";    if (g_Config.sCalSource == "EFIS")                                 sPage += " selected"; sPage += R"#(>EFIS (via serial input)</option>
            </select>
        </div>)#";

    // Volume control
    String defaultVolumeVisibility;
    String volumeLevelsVisibility;
    String volumeControlWidth;
    if (!g_Config.bVolumeControl)
        {
        defaultVolumeVisibility = R"#(style="display:block")#";
        volumeLevelsVisibility  = R"#(style="display:none")#";
        volumeControlWidth      = "6";
        }
    else
        {
        defaultVolumeVisibility = R"#(style="display:none")#";
        volumeLevelsVisibility  = R"#(style="display:block")#";
        volumeControlWidth      = "9";
        }

    sPage += R"#(
        <div id="volumeControlDiv" class="form-divs flex-col-)#" + volumeControlWidth + R"#(">
            <label for="id_volumeControl">Volume Potentiometer</label>
            <select id="id_volumeControl" name="volumeControl">
                <option value="1")#"; if ( g_Config.bVolumeControl) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bVolumeControl) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

    sPage += R"#(
        <div class="form-divs flex-col-3 defaultvolumesetting" )#" + String(defaultVolumeVisibility) + R"#(>
            <label for="id_defaultVolume">Volume %</label>
            <input id="id_defaultVolume" name="defaultVolume" type="text" value=")#" + String(g_Config.iDefaultVolume) + R"#("/>
        </div>
        <div class="form-divs flex-col-3">
            <label for="id_volumeTestButton">&nbsp;</label>
            <input id="id_volumeTestButton" name="volumeTestButton" type="button" value="Test Audio" class="greybutton" onclick="ToggleAudioTest(this.id)"/>
        </div>
        <div class="form-divs flex-col-4 volumepossetting" )#" + String(volumeLevelsVisibility) + R"#(>
            <label for="id_volumeLowAnalog">Low Vol. value</label>
            <input id="id_volumeLowAnalog" name="volumeLowAnalog" type="text" value=")#" + String(g_Config.iVolumeLowAnalog) + R"#("/>
        </div>
        <div class="form-divs flex-col-2 volumepossetting" )#" + String(volumeLevelsVisibility) + R"#(>
            <label for="id_volumeLowRead">&nbsp;</label>
            <input id="id_volumeLowRead" name="volumeLowRead" type="button" value="Read" class="greybutton" onclick="FillInValue(this.id,'VOLUME','id_volumeLowAnalog')"/>
        </div>
        <div class="form-divs flex-col-4 volumepossetting" )#" + String(volumeLevelsVisibility) + R"#(>
            <label for="id_volumeHighAnalog">High Vol. value</label>
            <input id="id_volumeHighAnalog" name="volumeHighAnalog" type="text" value=")#" + String(g_Config.iVolumeHighAnalog) + R"#("/>
        </div>
        <div class="form-divs flex-col-2 volumepossetting" )#" + String(volumeLevelsVisibility) + R"#(>
            <label for="id_volumeHighRead">&nbsp;</label>
            <input id="id_volumeHighRead" name="volumeHighRead" type="button" value="Read" class="greybutton" onclick="FillInValue(this.id,'VOLUME','id_volumeHighAnalog')"/>
        </div>
        <div class="form-divs flex-col-6">
            <label for="id_muteAudioUnderIAS">Mute below IAS (kts)</label>
            <input id="id_muteAudioUnderIAS" name="muteAudioUnderIAS" type="text" value=")#" + String(g_Config.iMuteAudioUnderIAS) + R"#("/>
        </div>
        <div class="form-divs flex-col-6">
            <label for="id_slipShiftAudio">3D Audio</label>
            <select id="id_slipShiftAudio" name="audio3D">
                <option value="1")#"; if ( g_Config.bAudio3D) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bAudio3D) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

    // Over G audio warning
    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_overgWarning">OverG audio warning</label>
            <select id="id_overgWarning" name="overgWarning">
                <option value="1")#"; if ( g_Config.bOverGWarning) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bOverGWarning) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

    // Load Limit
    String loadLimitVisibility;
    if (g_Config.bOverGWarning) loadLimitVisibility = R"#(style="display:block")#";
    else                        loadLimitVisibility = R"#(style="display:none")#";

    sPage += R"#(
        <div class="form-divs flex-col-6 loadlimitsetting" )#" + String(loadLimitVisibility) + R"#(>
            <label for="id_loadLimitPositive">Positive G limit</label>
            <input id="id_loadLimitPositive" name="loadLimitPositive" type="text" value=")#" + String(g_Config.fLoadLimitPositive) + R"#("/>
        </div>
        <div class="form-divs flex-col-6 loadlimitsetting" )#" + String(loadLimitVisibility) + R"#(>
            <label for="id_loadLimitNegative">Negative G limit</label>
            <input id="id_loadLimitNegative" name="loadLimitNegative" type="text" value=")#" + String(g_Config.fLoadLimitNegative) + R"#("/>
        </div>)#";

    // Vno chime
    String vnoVisibility;
    if (g_Config.bVnoChimeEnabled) vnoVisibility = R"#(style="display:block")#";
    else                           vnoVisibility = R"#(style="display:none")#";

    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_vnoChimeEnabled">Vno warning chime</label>
            <select id="id_vnoChimeEnabled" name="vnoChimeEnabled">
                <option value="1")#"; if ( g_Config.bVnoChimeEnabled) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bVnoChimeEnabled) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>
        <div class="form-divs flex-col-5 vnochimesetting" )#" + String(vnoVisibility) + R"#(>
            <label for="id_Vno">Aircraft Vno (kts)</label>
            <input id="id_Vno" name="Vno" type="text" value=")#" + String(g_Config.iVno) + R"#("/>
        </div>
        <div class="form-divs flex-col-5 vnochimesetting" )#" + String(vnoVisibility) + R"#(>
            <label for="id_vnoChimeInterval">Chime interval (seconds)</label>
            <input id="id_vnoChimeInterval" name="vnoChimeInterval" type="text" value=")#" + String(g_Config.uVnoChimeInterval) + R"#("/>
        </div>
        <div class="form-divs flex-col-2 vnochimesetting" )#" + String(vnoVisibility) + R"#(>
            <label for="id_chimeTestButton">&nbsp;</label>
            <input id="id_chimeTestButton" name="chimeTestButton" type="button" value="Test" class="greybutton" onclick="FillInValue(this.id,'VNOCHIMETEST','')"/>
        </div>)#";

    sPage += R"#(
        <div class="form-divs flex-col-12">
            <label for="id_sdLogging">SD Card Logging</label>
            <select id="id_sdLogging" name="sdLogging">
                <option value="1")#"; if ( g_Config.bSdLogging) sPage += " selected"; sPage += R"#(>Enabled</option>
                <option value="0")#"; if (!g_Config.bSdLogging) sPage += " selected"; sPage += R"#(>Disabled</option>
            </select>
        </div>)#";

       // Serial output selection
    sPage += R"#(
        <div class="form-divs flex-col-6">
            <label for="id_serialOutFormat">Serial out format</label>
            <select id="id_serialOutFormat" name="serialOutFormat">
                <option value="G3X")#";     if (g_Config.sSerialOutFormat == "G3X")                                        sPage += " selected"; sPage += R"#(>Garmin G3X</option>
                <option value="ONSPEED")#"; if (g_Config.sSerialOutFormat == "ONSPEED" || g_Config.sSerialOutFormat == "") sPage += " selected"; sPage += R"#(>OnSpeed</option>
            </select>
        </div>)#";

#if 0
    sPage += R"#(
        <div class="form-divs flex-col-6">
            <label for="id_serialOutPort">Serial out port</label>
            <select id="id_serialOutPort" name="serialOutPort">
                <option value="NONE")#";    if (g_Config.sSerialOutPort == "NONE")    sPage += " selected"; sPage += R"#(>None</option>
                <option value="Serial1")#"; if (g_Config.sSerialOutPort == "Serial1") sPage += " selected"; sPage += R"#(>Serial 1 (TTL - pin 12, v2 only!)</option>
                <option value="Serial3")#"; if (g_Config.sSerialOutPort == "Serial3") sPage += " selected"; sPage += R"#(>Serial 3 (RS232 - pin 12)</option>
                <option value="Serial5")#"; if (g_Config.sSerialOutPort == "Serial5") sPage += " selected"; sPage += R"#(>Serial 5 (TTL - pin 9)</option>
            </select>
        </div>)#";
#else
    sPage += R"#(
        <div class="form-divs flex-col-6">
        </div>)#";
#endif
      // Load config file
    sPage += R"#(
        <div class="form-divs flex-col-4">
            <div class="upload-btn-wrapper">
                <button class="upload-btn">Upload File</button>
                <input id="id_fileUploadInput" type="file" name="configFileName"/>
            </div>
        </div>)#";

    // Load default config
    sPage += R"#(
        <div class="form-divs flex-col-5">
            <a href="/defaultconfig">
                <input id="id_loadDefaultConfig" name="loadDefaultConfig" type="button" value="Load Defaults" class="greybutton"/>
            </a>
        </div>)#";

    // Save button
    sPage += R"#(
        <div class="form-divs flex-col-3">
            <input class="blackbutton" type="submit" name="saveSettingsButton" value="Save" />
        </div>)#";

    // End of form
    sPage += R"#(
        </div>
    </form>
)#";

    // Fake file upload form
    sPage += R"#(
    <form id="id_realUploadForm">
    </form>
</div>)#";

    // javascript code
    sPage += R"#(

<script>
)#";

    // Hide efisType when efis data is disabled
    sPage += R"#(
document.getElementById('id_readEfisData').onchange = function()
    {
    if (document.getElementById('id_readEfisData').value == 1)
        {
        [].forEach.call(document.querySelectorAll('.efistypesetting'), function (el) {el.style.visibility = 'visible';});
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.efistypesetting'), function (el) {el.style.visibility = 'hidden';});
        }
    };)#";

    // Hide CAS curve when CAS Curve is disabled
    sPage += R"#(

document.getElementById('id_casCurveEnabled').onchange = function()
    {
    if (document.getElementById('id_casCurveEnabled').value == 1)
        {
        [].forEach.call(document.querySelectorAll('.cascurvesetting'), function (el) {el.style.display = 'block';});
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.cascurvesetting'), function (el) {el.style.display = 'none';});
        }
    };)#";

    // Hide volume levels and show default volume when volume control is disabled
    sPage += R"#(

document.getElementById('id_volumeControl').onchange = function()
    {
    if (document.getElementById('id_volumeControl').value == 1)
        {
        [].forEach.call(document.querySelectorAll('.volumepossetting'),     function (el) {el.style.display = 'block';});
        [].forEach.call(document.querySelectorAll('.defaultvolumesetting'), function (el) {el.style.display = 'none';});
        document.getElementById('volumeControlDiv').classList.remove('flex-col-6');
        document.getElementById('volumeControlDiv').classList.add('flex-col-9');
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.volumepossetting'),     function (el) {el.style.display = 'none';});
        [].forEach.call(document.querySelectorAll('.defaultvolumesetting'), function (el) {el.style.display = 'block';});
        document.getElementById('volumeControlDiv').classList.remove('flex-col-9');
        document.getElementById('volumeControlDiv').classList.add('flex-col-6');
        }
    };)#";

    // Hide load limit when overG warning is disabled
    sPage += R"#(

document.getElementById('id_overgWarning').onchange = function()
    {
    if (document.getElementById('id_overgWarning').value == 1)
        {
        [].forEach.call(document.querySelectorAll('.loadlimitsetting'), function (el) {el.style.display = 'block';});
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.loadlimitsetting'), function (el) {el.style.display = 'none';});
        }
    };)#";

    // Hide Vno settings if Vno chime is disabed
    sPage += R"#(

document.getElementById('id_vnoChimeEnabled').onchange = function()
    {
    if (document.getElementById('id_vnoChimeEnabled').value == 1)
        {
        [].forEach.call(document.querySelectorAll('.vnochimesetting'), function (el) {el.style.display = 'block';});
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.vnochimesetting'), function (el) {el.style.display = 'none';});
        }
    };)#";

    // Hide log file when replaylogfile not set
    sPage += R"#(

document.getElementById('id_dataSource').onchange = function()
    {
    if (document.getElementById('id_dataSource').value=='REPLAYLOGFILE')
        {
        [].forEach.call(document.querySelectorAll('.replaylogfilesetting'), function (el) {el.style.display = 'block';});
        }
    else
        {
        [].forEach.call(document.querySelectorAll('.replaylogfilesetting'), function (el) {el.style.display = 'none';});
        }
    };)#";

    // Upload config file
    // Some Javascript trickery here, we're creating a second file upload form (id_realUploadForm) and submitting that with JS.
    sPage += R"#(

document.getElementById('id_fileUploadInput').onchange = function()
    {
    if (document.getElementById('id_fileUploadInput').value.indexOf(".cfg")>0)
        {
        document.getElementById('id_realUploadForm').appendChild(document.getElementById('id_fileUploadInput'));
        document.getElementById('id_realUploadForm').action="/aoaconfigupload";
        document.getElementById('id_realUploadForm').enctype="multipart/form-data";
        document.getElementById('id_realUploadForm').method="POST";
        document.getElementById('id_realUploadForm').submit();
        }
    else
        alert("Please upload a config file with .cfg extension!");
    };)#";

    // Curve type parameter change
    sPage += R"#(

function curveTypeChange(senderId,curveId)
    {
    if (document.getElementById(senderId).value==1)
        {
        document.getElementById('id_aoaCurve'+curveId+'Param'+0).innerHTML=' *X<sup>3</sup>+ ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+1).innerHTML=' *X<sup>2</sup>+ ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+2).innerHTML=' *X<sup></sup>+ ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+3).innerHTML='';
        }
    else if (document.getElementById(senderId).value==2)
        {
        document.getElementById('id_aoaCurve'+curveId+'Param'+0).innerHTML=' * 0 + ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+1).innerHTML=' * 0 + ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+2).innerHTML='*ln(x)+';
        document.getElementById('id_aoaCurve'+curveId+'Param'+3).innerHTML='';
        }
    else if (document.getElementById(senderId).value==3)
        {
        document.getElementById('id_aoaCurve'+curveId+'Param'+0).innerHTML=' * 0 + ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+1).innerHTML=' * 0 + ';
        document.getElementById('id_aoaCurve'+curveId+'Param'+2).innerHTML='* e^ (';
        document.getElementById('id_aoaCurve'+curveId+'Param'+3).innerHTML=' * x)';
        }
    })#";

    // CAS curve type parameter change
    sPage += R"#(

function cascurveTypeChange(senderId)
    {
    if (document.getElementById(senderId).value==1)
        {
        document.getElementById('id_casCurveParam0').innerHTML=' *X<sup>3</sup>+ ';
        document.getElementById('id_casCurveParam1').innerHTML=' *X<sup>2</sup>+ ';
        document.getElementById('id_casCurveParam2').innerHTML=' *X<sup></sup>+ ';
        document.getElementById('id_casCurveParam3').innerHTML='';
        }
    else if (document.getElementById(senderId).value==2)
        {
        document.getElementById('id_casCurveParam0').innerHTML=' * 0 + ';
        document.getElementById('id_casCurveParam1').innerHTML=' * 0 + ';
        document.getElementById('id_casCurveParam2').innerHTML='*ln(x)+';
        document.getElementById('id_casCurveParam3').innerHTML='';
        }
    else if (document.getElementById(senderId).value==3)
        {
        document.getElementById('id_casCurveParam0').innerHTML=' * 0 + ';
        document.getElementById('id_casCurveParam1').innerHTML=' * 0 + ';
        document.getElementById('id_casCurveParam2').innerHTML='* e^ (';
        document.getElementById('id_casCurveParam3').innerHTML=' * x)';
        }
    })#";

    // Disable Delete Flap Position on Enter key
    sPage += R"#(

document.getElementById("id_configForm").onkeypress = function(e)
    {
    var key = e.charCode || e.keyCode || 0;
    if (key == 13)
        {
        e.preventDefault();
        }
    }
</script>)#";

    // add html footer
    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    } // end HandleConfig()


// ----------------------------------------------------------------------------

void HandleConfigSave()
    {
    bool    bDeleteFlap     = false;
    bool    rebootRequired  = false;
    String  errorMessage    = "";

    if (CfgServer.hasArg("aoaSmoothing"))
        {
        if (g_Config.iAoaSmoothing != CfgServer.arg("aoaSmoothing").toInt())
            rebootRequired = true;
        g_Config.iAoaSmoothing = CfgServer.arg("aoaSmoothing").toInt();
        }

    if (CfgServer.hasArg("pressureSmoothing"))
        g_Config.iPressureSmoothing = CfgServer.arg("pressureSmoothing").toInt();

    if (CfgServer.hasArg("dataSource"))
        {
//        if (g_Config.sDataSource != CfgServer.arg("dataSource"))
        if (String(g_Config.suDataSrc.toCStr()) != CfgServer.arg("dataSource"))
            rebootRequired = true;
//        g_Config.sDataSource = CfgServer.arg("dataSource");
        g_Config.suDataSrc.fromStrSet(CfgServer.arg("dataSource"));
        }

    if (CfgServer.hasArg("logFileName"))
        g_Config.sReplayLogFileName = CfgServer.arg("logFileName");

#if 1
    // Save flap setting info to a new set of array elements.
    // I should probably let the config class handle all this. That's
    // what classes are for, right?
    g_Config.aFlaps.clear();
    int iFlapIdx = 0;
    while (CfgServer.hasArg("flapDegrees"+String(iFlapIdx)))
        {
        FOSConfig::SuFlaps  suFlaps;
        if (!CfgServer.hasArg("deleteFlapPos"+String(iFlapIdx)))
            {
            suFlaps.iDegrees     = CfgServer.arg("flapDegrees"+String(iFlapIdx)).toInt();
            suFlaps.iPotPosition = CfgServer.arg("flapPotPositions"+String(iFlapIdx)).toInt();

            suFlaps.fLDMAXAOA       = g_Config.ToFloat(CfgServer.arg("flapLDMAXAOA"+String(iFlapIdx)));
            suFlaps.fONSPEEDFASTAOA = g_Config.ToFloat(CfgServer.arg("flapONSPEEDFASTAOA"+String(iFlapIdx)));
            suFlaps.fONSPEEDSLOWAOA = g_Config.ToFloat(CfgServer.arg("flapONSPEEDSLOWAOA"+String(iFlapIdx)));
            suFlaps.fSTALLWARNAOA   = g_Config.ToFloat(CfgServer.arg("flapSTALLWARNAOA"+String(iFlapIdx)));

            suFlaps.AoaCurve.iCurveType = CfgServer.arg("aoaCurve"+String(iFlapIdx)+"Type").toInt();
            for (int iCoeffIdx=0; iCoeffIdx< MAX_CURVE_COEFF; iCoeffIdx++)
                suFlaps.AoaCurve.afCoeff[iCoeffIdx] = g_Config.ToFloat(CfgServer.arg("aoaCurve"+String(iFlapIdx)+"Coeff"+String(iCoeffIdx)));

            g_Config.aFlaps.push_back(suFlaps);
            }
        else
            bDeleteFlap = true;

        iFlapIdx++;
        }

    std::sort(g_Config.aFlaps.begin(), g_Config.aFlaps.end(),
            [](FOSConfig::SuFlaps a, FOSConfig::SuFlaps b) { return a.iDegrees < b.iDegrees; } );
#endif

    // Read boom enabled/disabled
    if (CfgServer.hasArg("readBoom") && CfgServer.arg("readBoom")=="1") g_Config.bReadBoom = true;
    else                                                                g_Config.bReadBoom = false;

    if (CfgServer.hasArg("casCurveEnabled") && CfgServer.arg("casCurveEnabled")=="1") g_Config.bCasCurveEnabled = true;
    else                                                                              g_Config.bCasCurveEnabled = false;

    if (CfgServer.hasArg("casCurveType")) g_Config.CasCurve.iCurveType=CfgServer.arg("casCurveType").toInt();

    // cas Curve
    for (int curveIndex=0; curveIndex< MAX_CURVE_COEFF; curveIndex++)
        {
        g_Config.CasCurve.afCoeff[curveIndex]=g_Config.ToFloat(CfgServer.arg("casCurveCoeff"+String(curveIndex)));
        }

    // read portsOrientation
    if (CfgServer.hasArg("portsOrientation")) g_Config.sPortsOrientation=CfgServer.arg("portsOrientation");

    // read boxtopOrientation
    if (CfgServer.hasArg("boxtopOrientation")) g_Config.sBoxtopOrientation=CfgServer.arg("boxtopOrientation");

    // read efis enabled/disabled
    if (CfgServer.hasArg("readEfisData") && CfgServer.arg("readEfisData")=="1") g_Config.bReadEfisData=true;
    else                                                                        g_Config.bReadEfisData=false;

    // read efis Type
    if (CfgServer.hasArg("efisType")) g_Config.sEfisType=CfgServer.arg("efisType");

    // read calibration source
    if (CfgServer.hasArg("calSource")) g_Config.sCalSource=CfgServer.arg("calSource");

    // read volume control
    if (CfgServer.hasArg("volumeControl") && CfgServer.arg("volumeControl")=="1") g_Config.bVolumeControl=true;
    else                                                                          g_Config.bVolumeControl=false;

    //read volumePercent
    if (CfgServer.hasArg("defaultVolume")) g_Config.iDefaultVolume=CfgServer.arg("defaultVolume").toInt();

    // read low volume position
    if (CfgServer.hasArg("volumeLowAnalog")) g_Config.iVolumeLowAnalog=CfgServer.arg("volumeLowAnalog").toInt();

    // read high volume position
    if (CfgServer.hasArg("volumeHighAnalog")) g_Config.iVolumeHighAnalog=CfgServer.arg("volumeHighAnalog").toInt();

    // read muteAudioUnderIAS
    if (CfgServer.hasArg("muteAudioUnderIAS")) g_Config.iMuteAudioUnderIAS=CfgServer.arg("muteAudioUnderIAS").toInt();

    // read 3d audio enabled/disabled
    if (CfgServer.hasArg("audio3D") && CfgServer.arg("audio3D")=="1") g_Config.bAudio3D=true;
    else                                                              g_Config.bAudio3D=false;

    //overgWarning
    if (CfgServer.hasArg("overgWarning") && CfgServer.arg("overgWarning")=="1") g_Config.bOverGWarning=true;
    else                                                                        g_Config.bOverGWarning=false;

    // loadLimit
    if (CfgServer.hasArg("loadLimitPositive")) g_Config.fLoadLimitPositive=CfgServer.arg("loadLimitPositive").toFloat();
    if (CfgServer.hasArg("loadLimitNegative")) g_Config.fLoadLimitNegative=CfgServer.arg("loadLimitNegative").toFloat();

    // vnochime
    if (CfgServer.hasArg("vnoChimeEnabled") && (CfgServer.arg("vnoChimeEnabled")=="1")) g_Config.bVnoChimeEnabled=true;
    else                                                                                g_Config.bVnoChimeEnabled=false;

    if (CfgServer.hasArg("Vno")) g_Config.iVno = CfgServer.arg("Vno").toInt();

    if (CfgServer.hasArg("vnoChimeInterval")) g_Config.uVnoChimeInterval=CfgServer.arg("vnoChimeInterval").toInt();

    // serialOutFormat
    if (CfgServer.hasArg("serialOutFormat")) g_Config.sSerialOutFormat=CfgServer.arg("serialOutFormat");

    //serialOutPort
//    if (CfgServer.hasArg("serialOutPort")) g_Config.sSerialOutPort=CfgServer.arg("serialOutPort");

    // sdLogging
    if (CfgServer.hasArg("sdLogging") && CfgServer.arg("sdLogging")=="1") g_Config.bSdLogging=true;
    else                                                                  g_Config.bSdLogging=false;

    // Handle Add Flap
    if (CfgServer.hasArg("addFlapPos"))
        {
        FOSConfig::SuFlaps      suFlaps;

        // Set default values for new flap setpoints and curves
        suFlaps.iDegrees        = 0;
        suFlaps.iPotPosition    = 0;
        suFlaps.fLDMAXAOA       = 0.0;
        suFlaps.fONSPEEDFASTAOA = 0.0;
        suFlaps.fONSPEEDSLOWAOA = 0.0;
        suFlaps.fSTALLWARNAOA   = 0.0;
        suFlaps.fSTALLAOA       = 0.0;
        suFlaps.fMANAOA         = 0.0;
        suFlaps.AoaCurve.iCurveType = 1;    // Default to polynomial curve
        for (int iCoeffIdx = 0; iCoeffIdx < MAX_CURVE_COEFF; iCoeffIdx++)
            suFlaps.AoaCurve.afCoeff[iCoeffIdx] = 0.0;

        // Add new flap curve
        g_Config.aFlaps.push_back(suFlaps);

        CfgServer.sendHeader("Location", "/aoaconfig");
        CfgServer.send(301, "text/html", "");
        } // end if add a new flap position

    // Handle Delete Flap
    else if (bDeleteFlap)
        {
        CfgServer.sendHeader("Location", "/aoaconfig");
        CfgServer.send(301, "text/html", "");
        }

    // Handle regular save
    else
        {
        String sPage;
        String sConfig = "";

        UpdateHeader();
        sPage.reserve(pageHeader.length() + 1024);
        sConfig.reserve(1024);
        sPage += pageHeader;

        // Save configuration. Maybe specify a config file name someday. Default for now.
        g_Config.SaveConfigurationToFile();

        sPage += "<br><br>Configuration saved.";
        if (rebootRequired)
            sPage += R"#(<br><br>Some of the changes require a system reboot to take effect.<br><br><br><a href="reboot?confirm=yes" class="button">Reboot Now</a>)#";
        sPage += pageFooter;

        CfgServer.send(200, "text/html", sPage);
        }

    // Configure anything that needs to be configured based on new config settings

    // Configure accelerometer axes
    g_pIMU->ConfigAxes();
    g_AHRS.Init(IMU_SAMPLE_RATE);

    } // end HandleConfigSave()

// ----------------------------------------------------------------------------

void HandleDefaultConfig()
    {
    String sPage;

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;

    if (CfgServer.hasArg("confirm") && CfgServer.arg("confirm")=="yes")
        {
        g_Config.LoadDefaultConfiguration();

        // Load default config
        CfgServer.sendHeader("Location", "/aoaconfig");
        CfgServer.send(302, "text/plain", "Redirect...");
        return;
        }
    else
        {
        // Display confirmation page
        sPage += R"#(
<br><br>
<p style="color:red">Confirm that you want to load the Default configuration. This will erase all of your current settings.</p>
<br><br><br>
<a href="/defaultconfig?confirm=yes" class="button">Load Default Config</a>
<a href="/aoaconfig">Cancel</a>
)#";
        }

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    } // end HandleDefaultConfig()


// ----------------------------------------------------------------------------

void HandleConfigUpload()
    {
    static String sUploadedConfigString = "";

    // Check if a configfile was uploaded
    HTTPUpload & uploadFile = CfgServer.upload();

    if (uploadFile.status == UPLOAD_FILE_START)
        {
        g_Log.print(MsgLog::EnWebServer, MsgLog::EnDebug, "UPLOAD Start\n");
        if (xSemaphoreTake(uploadMutex, portMAX_DELAY)) {
            sUploadedConfigString = "";
            bUploadedConfigStringGood = false;
            xSemaphoreGive(uploadMutex);
            }
        }

    else if (uploadFile.status == UPLOAD_FILE_WRITE)
        {
        if (xSemaphoreTake(uploadMutex, portMAX_DELAY)) {
            sUploadedConfigString.reserve(sUploadedConfigString.length() + uploadFile.currentSize);
            for (unsigned int i = 0; i < uploadFile.currentSize; i++) {
                sUploadedConfigString += char(uploadFile.buf[i]);
                }
            xSemaphoreGive(uploadMutex);
            }
        }

    else  if (uploadFile.status == UPLOAD_FILE_END)
        {
        g_Log.print(MsgLog::EnWebServer, MsgLog::EnDebug, "UPLOAD End\n");
        if (xSemaphoreTake(uploadMutex, portMAX_DELAY))
            {
            // Done uploading, parse and decode the uploaded configuration
            bUploadedConfigStringGood = g_Config.LoadConfigFromString(sUploadedConfigString);

            if (bUploadedConfigStringGood)
                g_Log.print(MsgLog::EnWebServer, MsgLog::EnDebug, "UPLOAD Good Config String\n");
            else
                g_Log.print(MsgLog::EnWebServer, MsgLog::EnDebug, "UPLOAD Bad Config String\n");

            xSemaphoreGive(uploadMutex);
            } // end if got semaphore
        } // end UPLOAD_FILE_END

    } // end HandleConfigUpload()


// ----------------------------------------------------------------------------

void HandleFinalUpload()
    {
    String sPage;

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 2048);
    sPage += pageHeader;

    if (xSemaphoreTake(uploadMutex, portMAX_DELAY))
        {
        if (bUploadedConfigStringGood == true)
            {
            // Display configuration page
            HandleConfig();
            }

        else
            {
            sPage += "<br><br>The uploaded file does not contain a valid configuration. Try another file.";
            sPage += pageFooter;
            CfgServer.send(200, "text/html", sPage);
            }

        xSemaphoreGive(uploadMutex);
        }

    }


// ----------------------------------------------------------------------------

void HandleSensorConfig()
    {
    String  sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 8192);
    sPage += pageHeader;

    // First time through, no "yes" calibration confirm
    if (CfgServer.arg("confirm").indexOf("yes") < 0)
        {
        String sCurrentConfig = "";
        sCurrentConfig += "\n<table>\n";
        sCurrentConfig += "<tr><td style=\"padding-right: 20px;\">Pfwd Bias:</td><td>"                + String(g_Config.iPFwdBias)                 + "</td><td>Counts</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">P45 Bias:</td><td>"                 + String(g_Config.iP45Bias)                  + "</td><td>Counts</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Static Bias:</td><td>"              + String(g_Config.fPStaticBias)              + "</td><td>millibars</td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gx Bias:</td><td>"                  + String(g_Config.fGxBias)                   + "</td><td></td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gy Bias:</td><td>"                  + String(g_Config.fGyBias)                   + "</td><td></td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gz Bias:</td><td>"                  + String(g_Config.fGzBias)                   + "</td><td></td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">IMU Pitch:</td><td>"                + String(g_pIMU->PitchAC())                  + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Pitch Bias:</td><td>"               + String(g_Config.fPitchBias)                + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Calculated True AC Pitch:</td><td>" + String(g_AHRS.PitchWithBias())             + "</td><td>Degrees</td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">IMU Roll:</td><td>"                 + String(g_pIMU->RollAC())                   + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Roll Bias:</td><td>"                + String(g_Config.fRollBias)                 + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Calculated True AC Roll:</td><td>"  + String(g_AHRS.RollWithBias())              + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" </table>\n";

sPage += R"#(
<br><b>Current sensor calibration:</b><br><br>)#" + sCurrentConfig + R"#(<br>
        <p style="color:black">This procedure will calibrate the system's accelerometers, gyros and pressure sensors.<br><br>
        <b>Requirements:</b><br><br>
        1. Do this configuration in no wind condition. Preferably inside a closed hangar, no moving air<br>
        2. Box orientation is set up properly in <a href="/aoaconfig">System Configuration</a><br>
        3. If the aircraft is not in a level attitude enter the current true pitch and roll angles below.<br>
        4. Enter current pressure altitude in feet. (Set your altimeter to 29.92 inHg and read the altitude)
        <br><br>
        Calibration will take a few seconds.
        </p>
        <br>
        <br>
        <form action="sensorconfig" method="GET">
<table>
        <table>
        <tr><td><label>True Aircraft Pitch (degrees)</label></td>
        <td><input class="inputField" type="text" name="trueAircraftPitch" value=")#" + String(g_AHRS.PitchWithBias()) + R"#("></td></tr>
        <tr><td><label>True Aircraft Roll (degrees)</label></td>
        <td><input class="inputField" type="text" name="trueAircraftRoll" value=")#" + String(g_AHRS.RollWithBias()) + R"#("></td></tr>
        <tr><td><label>True Aircraft Pressure Altitude (feet)</label></td>
        <td><input class="inputField" type="text" name="trueAircraftPalt" value=")#" + String(g_Sensors.Palt) + R"#("></td></tr>
        </table>

        <input type="hidden" name="confirm" value="yes">
        <br><br><br>
        <button type="submit" class="button">Calibrate Sensors</button>
        <a href="/">Cancel</a>
        </form>
)#";
        } // end if no confirm yes


    // Second time through with a confirm yes
    else
        {
        float   fTrueAircraftPitch = 0.0;
        float   fTrueAircraftRoll  = 0.0;
        float   fTrueAircraftPalt  = 0;

        // Configure sensors
        if (CfgServer.arg("trueAircraftPitch") != "" &&
            CfgServer.arg("trueAircraftRoll")  != "" &&
            CfgServer.arg("trueAircraftPalt")  != "")
            {
            fTrueAircraftPitch = CfgServer.arg("trueAircraftPitch").toFloat();
            fTrueAircraftRoll  = CfgServer.arg("trueAircraftRoll").toFloat();
            fTrueAircraftPalt  = CfgServer.arg("trueAircraftPalt").toFloat();
            }
        else
            {
            fTrueAircraftPitch = 0.0;
            fTrueAircraftRoll  = 0.0;
            fTrueAircraftPalt  = 0.0;
            }

        //sdLogging=false;

        // Sample sensors
        long    lP45Total       = 0;        // counts
        long    lPFwdTotal      = 0;        // counts
        float   fPStaticTotal   = 0.0;      // millibars
        float   aVertTotal      = 0.00;
        float   aLatTotal       = 0.00;
        float   aFwdTotal       = 0.00;
        // Gyro biases are applied in raw IMU axes (fGyroX/Y/Z) before any
        // aircraft-orientation remapping/sign is applied. So measure biases in
        // raw axes here as well to avoid sign/mapping errors.
        float   fGyroXTotal      = 0.00;
        float   fGyroYTotal      = 0.00;
        float   fGyroZTotal      = 0.00;
        int     sensorReadCount = 150;

        for (int i=0; i < sensorReadCount; i++)
            {
            xSemaphoreTake(xSensorMutex, portMAX_DELAY);

            lP45Total     += g_pAOA->ReadPressureCounts();       // GetPressureP45();
            lPFwdTotal    += g_pPitot->ReadPressureCounts();     // GetPressurePfwd();
            fPStaticTotal += g_pStatic->ReadPressureMillibars(); // GetStaticPressure();

            g_pIMU->Read();
            aFwdTotal  += g_pIMU->Ax;   // All in aircraft orientation
            aLatTotal  += g_pIMU->Ay;
            aVertTotal += g_pIMU->Az;
            // Raw gyro rates in IMU axes (deg/sec), without bias correction
            fGyroXTotal += g_pIMU->fGyroX;
            fGyroYTotal += g_pIMU->fGyroY;
            fGyroZTotal += g_pIMU->fGyroZ;

            // Avoid a tight busy-wait inside the web handler; yield to keep WiFi/core-0 responsive.
            vTaskDelay(pdMS_TO_TICKS(5));
            xSemaphoreGive(xSensorMutex);
            }

        // Calculate pitch from averaged accelerometer reading, i.e. no bias adjustments
        float fCalcImuPitch = PITCH(aFwdTotal / sensorReadCount, aLatTotal / sensorReadCount, aVertTotal / sensorReadCount);
        float fCalcImuRoll  = ROLL (aFwdTotal / sensorReadCount, aLatTotal / sensorReadCount, aVertTotal / sensorReadCount);

        // Adjust bias, calculate a new bias
        g_Config.fPitchBias = fTrueAircraftPitch - fCalcImuPitch;
        g_Config.fRollBias  = fTrueAircraftRoll  - fCalcImuRoll;
        g_Log.printf(MsgLog::EnWebServer, MsgLog::EnDebug, "aircraftPitch: %.2f,aircraftRoll: %.2f,calcPitch: %.2f,calcRoll: %.2f\n",fTrueAircraftPitch,fTrueAircraftRoll,fCalcImuPitch,fCalcImuRoll);

        g_Config.iPFwdBias  = int((lPFwdTotal/sensorReadCount));
        g_Config.iP45Bias   = int((lP45Total/sensorReadCount));

        float fMeasuredStaticMB   = fPStaticTotal/sensorReadCount;
        float fCalculatedStaticMB = INHG2MB(29.92125535 * pow(((288 - (0.0065 * FT2M(fTrueAircraftPalt))) / 288), 5.2561)); // https://www.weather.gov/media/epz/wxcalc/stationPressure.pdf from inHg to milliBars.
        g_Config.fPStaticBias     = fMeasuredStaticMB - fCalculatedStaticMB;

        g_Log.printf(MsgLog::EnWebServer, MsgLog::EnDebug, "Measured Static %8.3f mb Calculated Static %8.3f mb Bias %6.3f\n", fMeasuredStaticMB, fCalculatedStaticMB, g_Config.fPStaticBias);

        // Biases are stored/applied in raw IMU axes.
        g_Config.fGxBias = -fGyroXTotal / sensorReadCount;
        g_Config.fGyBias = -fGyroYTotal / sensorReadCount;
        g_Config.fGzBias = -fGyroZTotal / sensorReadCount;

        g_Config.SaveConfigurationToFile();

        // Get update IMU
        xSemaphoreTake(xSensorMutex, portMAX_DELAY);
        g_pIMU->ReadAccelGyro(false);
        g_AHRS.Process();
        xSemaphoreGive(xSensorMutex);

        //sdLogging=true;

        sPage += "<br><br><p>Sensors are now calibrated.</p>";
        sPage += "<br>The new parameters are:<br><br>";

        String sCurrentConfig = "";
        sCurrentConfig += "\n<table>\n";
        sCurrentConfig += "<tr><td style=\"padding-right: 20px;\">Pfwd Bias:</td><td style=\"text-align: right;\">"             + String(g_Config.iPFwdBias)     + "</td><td>Counts</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">P45 Bias:</td><td style=\"text-align: right;\">"              + String(g_Config.iP45Bias)      + "</td><td>Counts</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Measured Static:</td><td style=\"text-align: right;\">"       + String(fMeasuredStaticMB)      + "</td><td>millibars</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Calculated Static:</td><td style=\"text-align: right;\">"     + String(fCalculatedStaticMB)    + "</td><td>millibars</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Static Bias:</td><td style=\"text-align: right;\">"           + String(g_Config.fPStaticBias)  + "</td><td>millibars</td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gx Bias:</td><td style=\"text-align: right;\">"               + String(g_Config.fGxBias)       + "</td><td></td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gy Bias:</td><td style=\"text-align: right;\">"               + String(g_Config.fGyBias)       + "</td><td></td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">gz Bias:</td><td style=\"text-align: right;\">"               + String(g_Config.fGzBias)       + "</td><td></td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Current True AC Pitch:</td><td style=\"text-align: right;\">" + String(g_AHRS.PitchWithBias()) + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Pitch Bias:</td><td style=\"text-align: right;\">"            + String(g_Config.fPitchBias)    + "</td><td>Degrees</td></tr>\n";

        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Current True AC Roll:</td><td style=\"text-align: right;\">"  + String(g_AHRS.RollWithBias())  + "</td><td>Degrees</td></tr>\n";
        sCurrentConfig +=" <tr><td style=\"padding-right: 20px;\">Roll Bias:</td><td style=\"text-align: right;\">"             + String(g_Config.fRollBias)     + "</td><td>Degrees</td></tr>\n";

        sCurrentConfig +=" </table>\n";

        sPage += sCurrentConfig;

        } // end else save configure sensor values

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);

    } // end HandleSensorConfig()


// ----------------------------------------------------------------------------
void HandleWifiReflash()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;
    sPage += "<br><br><p>Wifi reflash mode disables the Teensy's serial port to enable reflashing the Wifi chip via its microUSB port.\
    <br><br><span style=\"color:red\">This mode is now activated until reboot.</span></p>";
    sPage += pageFooter;
//    sendSerialCommand("$WIFIREFLASH!");
    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

void HandleUpgrade()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;
    sPage += "<br><br><p>Upgrade Wifi module firmware via binary (.bin) file upload\
    <br><br><br>";
    sPage += R"rawliteral(<form method='POST' action='/upload' enctype='multipart/form-data' id='upload_form'>
    <input type='file' name='update'><br><br>
    <input class='redbutton' type='submit' value='Upload'>
    </form>)rawliteral";
    sPage += pageFooter;

    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

void HandleUpgradeSuccess()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 2048);
    sPage += pageHeader;
    sPage += "<br><br><br><br><span style=\"color:black\">Firmware upgrade complete. Wait a few seconds until OnSpeed reboots.</span></p><br><br><br><br>";
    sPage += "<script>setInterval(function () { document.getElementById('rebootprogress').value+=0.1; document.getElementById('rebootprogress').innerHTML=document.getElementById('rebootprogress').value+'%'}, 10);setTimeout(function () { window.location.href = \"/\";}, 10000);</script>";
    sPage += "<div align=\"center\"><progress id=\"rebootprogress\" max=\"100\" value=\"0\"> 0% </progress></div>";

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

void HandleUpgradeFailure()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 1024);
    sPage += pageHeader;
    sPage += "<br><br><br><br><span style=\"color:red\">Firmware upgrade failed. Power cycle OnSpeed box and try again.</span></p><br><br><br><br>";
    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    }

// ----------------------------------------------------------------------------

/*
/getvalue?name=NAME

Currently supported NAMEs and JSON responses are:
FLAPS
VOLUME
AOA
*/

void HandleGetValue()
    {
//    String          sValueName;
    String          sResponseValue = "";

    if (CfgServer.hasArg("name"))
        {

        if (CfgServer.arg("name") == "FLAPS")
            {
            sResponseValue = String(g_Flaps.uValue);
            CfgServer.send(200, "text/plain", String(g_Flaps.uValue));
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnDebug, "Reqeust FLAPS");
            }

        else if (CfgServer.arg("name") == "AOA")
            {
            sResponseValue = String(g_Sensors.AOA);
            CfgServer.send(200, "text/plain", sResponseValue);
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnDebug, "Reqeust AOA");
            }

        else if (CfgServer.arg("name") == "VOLUME")
            {
            sResponseValue = String(ReadVolume());
            CfgServer.send(200, "text/plain", sResponseValue);
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnDebug, "Reqeust VOLUME");
            }

        else if (CfgServer.arg("name") == "AUDIOTEST")
            {
            bool bStarted = g_AudioPlay.StartAudioTest();
            sResponseValue = bStarted ? "Test Audio" : "Audio Test Busy"; // Not really used
            CfgServer.send(200, "text/plain", sResponseValue);
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnDebug, "Reqeust AUDIOTEST");
            }

        else if (CfgServer.arg("name") == "AUDIOTESTSTOP")
            {
            g_AudioPlay.StopAudioTest();
            sResponseValue = "Stopping";
            CfgServer.send(200, "text/plain", sResponseValue);
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnDebug, "Request AUDIOTESTSTOP");
            }

        else if (CfgServer.arg("name") == "AUDIOTESTSTATUS")
            {
            sResponseValue = g_AudioPlay.IsAudioTestRunning() ? "RUNNING" : "IDLE";
            CfgServer.send(200, "text/plain", sResponseValue);
            // Intentionally no log here; UI may poll frequently.
            }

        else
            {
            CfgServer.send(400);
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Request UNKNOWN");
            }
        }

     else
        {
        CfgServer.send(400);
        g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "Request UNKNOWN");
        }

    }


// ----------------------------------------------------------------------------

void HandleFormat()
    {
    String sPage;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 2048);
    sPage += pageHeader;

    if (CfgServer.arg("confirm").indexOf("yes")<0)
        {
        // display confirmation sPage
        sPage += "<br><br><p style=\"color:red\">Confirm that you want to format the internal SD card. You will lose all the files currently on the card.</p>\
        <br><br><br>\
        <a href=\"format?confirm=yes\" class=\"button\">Format SD Card</a>\
        <a href=\"/\">Cancel</a>";
        }

    else
        {
        bool    bFormatStatus = false;
//        String formatResponse;

        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(1000)))
            {
            bool bOrigSdLogging = g_Config.bSdLogging;

            g_Config.bSdLogging = false; // turn off sdLogging
            if (bOrigSdLogging)
                {
                g_LogSensor.Close();
                }

            // Format the SD card
            bFormatStatus = g_SdFileSys.Format(nullptr);

            if (bOrigSdLogging)
                {
                // Reinitialize card
                g_Config.bSdLogging = true; // if logging was on before FORMAT turn it back on
                g_LogSensor.Open();
                }

            xSemaphoreGive(xWriteMutex);

            // Put the configuration file back onto the card
            // Semaphore is handled in the function call
            g_Config.SaveConfigurationToFile();
            }
        else
            {
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "FORMAT - SD busy (xWriteMutex)");
            }

        // Format OK
        if (bFormatStatus == true)
            {
            sPage += "<br><br><p>SD card has been formatted.</p>";
//            sPage += "<br>New card size is: ";
//            formatResponse=getConfigValue(formatResponse,"FORMATDONE");
//            sPage += formatResponse;
            }

        // Format failed
        else
            {
            sPage += "<br><br>SD card format ERROR";
//            sPage += "<br><br><p>Error: Could not format SD card. Please reboot and try again.</p>";
            }

        } // end else format

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    }


// ----------------------------------------------------------------------------

#if 1
void HandleCalWizard()
    {
    String wizardStep = "";
    if (CfgServer.hasArg("step") && CfgServer.arg("step") != "")
        wizardStep=CfgServer.arg("step");

    String sPage;
    String scripts;
    UpdateHeader();
    sPage.reserve(pageHeader.length() + 16384);
    scripts.reserve(2048);
    sPage += pageHeader;

    if (wizardStep == "")
        {
        sPage += "<br><b>Calibration Wizard</b><br><br>\n";
        sPage += "This wizard will guide you through the AOA calibration process.<br><br>\n";
        sPage += R"#(
Enter the following aircraft parameters:<br><br>
<div class="content-container">
    <form  id="id_configWizStartForm" action="calwiz?step=decel" method="POST">
    <div class="form round-box">)#";

        sPage += R"#(
        <div class="form-divs flex-col-12">
            <label>Aircraft gross weight (lbs)</label>
            <input class="inputField" type="text" name="acGrossWeight" value=")#" + String(iAcGrossWeight) + R"#(">
        </div>
        <div class="form-divs flex-col-12">
            <label>Aircraft current weight (lbs)</label>
            <input class="inputField" type="text" name="acCurrentWeight" value=")#" + String(iAcCurrentWeight) + R"#(">
        </div>
        <div class="form-divs flex-col-12">
            <label>Best glide airspeed at gross weight (KIAS)</label>
            <input class="inputField" type="text" name="acVldmax" value=")#" + String(fAcVldmax) + R"#(">
        </div>
            <div class="form-divs flex-col-12">
            <label>Airframe load factor limit (G)</label>
            <input class="inputField" type="text" name="acGlimit" value=")#" + String(fAcGlimit) + R"#(">
            <br>
            <br>
        Note: The above parameters are needed to calculate your best glide speed and maneuvering speed at the current weight.
        </div>)#";

        sPage += R"#(
        <br><br><br>
        <div class="form-divs flex-col-6">
            <a href="/">Cancel</a>
        </div>
        <div class="form-divs flex-col-6">
            <button type="submit" class="button">Continue</button>
        </div>
    </div>
    </form>
</div>)#";
        } // end if no step specified

    else if  (wizardStep == "decel")
        {
        if (CfgServer.hasArg("acGrossWeight"))   iAcGrossWeight   = CfgServer.arg("acGrossWeight").toInt();
        if (CfgServer.hasArg("acCurrentWeight")) iAcCurrentWeight = CfgServer.arg("acCurrentWeight").toInt();
        if (CfgServer.hasArg("acVldmax"))        fAcVldmax        = CfgServer.arg("acVldmax").toInt();
        if (CfgServer.hasArg("acGlimit"))        fAcGlimit        = CfgServer.arg("acGlimit").toFloat();

        sPage += "<br><b>Calibration Wizard</b><br><br>";
        sPage += "<form  id=\"id_configWizStartForm\" action=\"calwiz?step=flydecel\" method=\"POST\">\
              Get ready to fly a 1kt/sec deceleration.<br><br>\
              <b>Instructions:</b>\
              <br><br>\
              1. Set your flaps now and do not change them until after you saved the calibration.<br\
              2. Fly the entire run at a steady 1kt/sec deceleration (OnSpeed provides feedback).<br>\
              3. Keep the ball in the middle and wings level at all times<br>\
              4. Prioritize pitch smoothness over deceleration rate.<br>\
              5. Do not pull up abruptly into the stall. Stall smoothly.<br>\
              6. After the stall pitch down to a negative pitch angle for recovery. This will end the data recording and start analyzing the data.\
              <br><br>\
              Ready? Hit the Continue button below and then fly max speed (Keep it below Vfe with flaps down!) with the flap setting you want to calibrate.\
              <br><br>\
              <br><br>\
              <a href=\"/\">Cancel</a>\
              <button type=\"submit\" class=\"button\">Continue</button>\
              </form>";
        } // end if decel

    else if (wizardStep == "flydecel")
        {
         sPage += "<script>";
         sPage += "acGrossWeight="     + String(iAcGrossWeight)     + ";";
         sPage += "acCurrentWeight="   + String(iAcCurrentWeight)   + ";";
         sPage += "acVldmax="          + String(fAcVldmax)          + ";";
         sPage += "acGlimit="          + String(fAcGlimit)          + ";";
         sPage += "flapPositionCount=" + String(g_Config.aFlaps.size()) + ";";

         // send flap degrees array to javascript
         sPage += "flapDegrees=[";
         for (int iFlapIdx=0; iFlapIdx < g_Config.aFlaps.size(); iFlapIdx++)
             {
             sPage += String(g_Config.aFlaps[iFlapIdx].iDegrees);
             if (iFlapIdx < g_Config.aFlaps.size()-1)
                sPage += ",";
             }
         sPage += "];";
         sPage += "</script>";

        // send one by one, too long to send it all at once
        String SjsSGfilter      = String(jsSGfilter);
        String SjsRegression    = String(jsRegression);
        String ScssChartist     = String(cssChartist);
        String SjsChartist1     = String(jsChartist1);
        String SjsChartist2     = String(jsChartist2);
        String SjsCalibration   = String(jsCalibration);
        String ShtmlCalibration = String(htmlCalibration);

        int contentLength = sPage.length()          +  SjsSGfilter.length()     + SjsRegression.length() +
                            ScssChartist.length()   + SjsChartist1.length()     + SjsChartist2.length() +
                            SjsCalibration.length() + ShtmlCalibration.length() + pageFooter.length();
        //CfgServer.sendHeader("Content-Length", (String)contentLength);
        CfgServer.setContentLength(CONTENT_LENGTH_UNKNOWN); // send content in chuncks, too large for String
        CfgServer.send(200, "text/html", "");
        CfgServer.sendContent(sPage);
        CfgServer.sendContent(SjsSGfilter);
        CfgServer.sendContent(SjsRegression);
        CfgServer.sendContent(ScssChartist);
        CfgServer.sendContent(SjsChartist1);
        CfgServer.sendContent(SjsChartist2);
        CfgServer.sendContent(SjsCalibration);
        CfgServer.sendContent(ShtmlCalibration);
        CfgServer.sendContent(pageFooter);
        CfgServer.sendContent("");
        return;
        } // end if flydecel

    else if  (wizardStep == "save")
        {
        //String postString="flapsPos="+CfgServer.arg("flapsPos")+"&curve0="+CfgServer.arg("curve0")+"&curve1="+CfgServer.arg("curve1")+"&curve2="+CfgServer.arg("curve2")+"&LDmaxSetpoint="+CfgServer.arg("LDmaxSetpoint")
        //+"&OSFastSetpoint="+CfgServer.arg("OSFastSetpoint")+"&OSSlowSetpoint="+CfgServer.arg("OSSlowSetpoint")+"&StallWarnSetpoint="+CfgServer.arg("StallWarnSetpoint")//
        //+"&ManeuveringSetpoint="+CfgServer.arg("ManeuveringSetpoint")+"&StallSetpoint="+CfgServer.arg("StallSetpoint");
        // find iFlapIdx
        for (int iFlapIdx=0; iFlapIdx< MAX_AOA_CURVES; iFlapIdx++)
            {
            if (g_Config.aFlaps[iFlapIdx].iDegrees == CfgServer.arg("flapsPos").toInt())
                {
                g_Config.aFlaps[iFlapIdx].fLDMAXAOA       = g_Config.ToFloat(CfgServer.arg("LDmaxSetpoint"));
                g_Config.aFlaps[iFlapIdx].fONSPEEDFASTAOA = g_Config.ToFloat(CfgServer.arg("OSFastSetpoint"));
                g_Config.aFlaps[iFlapIdx].fONSPEEDSLOWAOA = g_Config.ToFloat(CfgServer.arg("OSSlowSetpoint"));
                g_Config.aFlaps[iFlapIdx].fSTALLWARNAOA   = g_Config.ToFloat(CfgServer.arg("StallWarnSetpoint"));
                g_Config.aFlaps[iFlapIdx].fSTALLAOA       = g_Config.ToFloat(CfgServer.arg("StallSetpoint"));
                g_Config.aFlaps[iFlapIdx].fMANAOA         = g_Config.ToFloat(CfgServer.arg("ManeuveringSetpoint"));

                g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[0] = 0;
                g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[1] = g_Config.ToFloat(CfgServer.arg("curve0"));
                g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[2] = g_Config.ToFloat(CfgServer.arg("curve1"));
                g_Config.aFlaps[iFlapIdx].AoaCurve.afCoeff[3] = g_Config.ToFloat(CfgServer.arg("curve2"));
                g_Config.aFlaps[iFlapIdx].AoaCurve.iCurveType = 1; // polynomial

                // Save configuration
                g_Config.SaveConfigurationToFile();
                CfgServer.send(200, "text/html", "SUCCESS: Configuration was saved!");
                return;

                String newconfigString;
                } // end if flap position matches
            } // end for flap indexes

        CfgServer.send(200, "text/html", "ERROR saving to config: Could not find the flap position " + CfgServer.arg("flapsPos") +" degrees in the configuration file: "+g_Config.aFlaps.size());
        return;
        } // end save

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    } // end HandleCalWizard()

#endif

// ----------------------------------------------------------------------------

#ifdef SUPPORT_WIFI_CLIENT

void HandleWifiSettings()
    {
    String  sPage = "";
    int     rSSI;
    int     iSignalStrength;

    // Add header before sending page, with updated Wifi Status
    if (CfgServer.arg("disconnect").indexOf("yes")>=0)
        {
        // if connected disconnect
        if (WiFi.status() == WL_CONNECTED)
            {
            WiFi.disconnect(true);
            delay(100);
            }
        } // end disconnect

    if (CfgServer.arg("connect").indexOf("yes")<0)
        {
        // display connection status and available networks
        if (WiFi.status() == WL_CONNECTED)
            {
            sPage += "<br><br>Wifi Status: Connected to <strong>" + sClientWifi_SSID + "</strong>";
            sPage += "<br>";
            sPage += "IP Address: "+ WiFi.localIP().toString()+"<br><br>";
            sPage += "<div align=\"center\"><a href=\"wifi?disconnect=yes\" class=\"button\">Disconnect</a></div><br><br>";
            }
        else
            {
            sPage += "<br><br><span style=\"color:red\">Wifi Status: Disconnected</span>";
            }

        sPage += "<br><br>Tap a Wifi network below to connect to it:<br><br>\n";
        int n = WiFi.scanNetworks();

        if (n == 0)
            {
            sPage += "<br><br>No Wifi networks found.<br><br>Reload this page to scan again.";
            }

        else
            {
            sPage += "<div align=\"center\">\n";
            for (int i = 0; i < n; ++i)
                {
                rSSI = WiFi.RSSI(i);
                if      (rSSI > -50)                 iSignalStrength = 4;
                else if (rSSI <= -50 && rSSI >  -60) iSignalStrength = 3;
                else if (rSSI <= -60 && rSSI >= -70) iSignalStrength = 2;
                else                                 iSignalStrength = 1;
                sPage += "<a href=\"wifi?connect=yes&ssid=" + WiFi.SSID(i);
                if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) sPage += "&auth=yes";
                else                                          sPage += "&auth=no";
                sPage += "\" class=\"wifibutton\">" + WiFi.SSID(i) +
                         "<i class=\"icon__signal-strength signal-" + String(iSignalStrength) + "\">" +
                         "<span class=\"bar-1\"></span>\n" +
                         "<span class=\"bar-2\"></span>\n" +
                         "<span class=\"bar-3\"></span>\n" +
                         "<span class=\"bar-4\"></span></i>";
                if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) sPage += "&#128274";
                else                                          sPage += "&#128275";
                sPage += "</a>";
                sPage += "<br>\n";
                } // end for RSSI values
            sPage += "</div>\n";
            sPage += "<br><br>\n";
            sPage += "&#128274 - password required to connect\n";
            }
        } // end display status

    else
        {
        // connect to network
        if (CfgServer.arg("auth").indexOf("yes") >= 0)
            {
            // require network password
            sPage += "<br><br>Enter password for "+CfgServer.arg("ssid");
            sPage += "<br><br>";
            sPage += "<div align=\"center\"> <form action=\"wifi?connect=yes&ssid="+CfgServer.arg("ssid")+"\" method=\"POST\"> <input type=\"text\" class=\"inputField\" name=\"password\" placeholder=\"Enter password\"><br><br>\
            <button type=\"submit\" class=\"button\">Connect</button> </form> <br></div>";
            }
        else
            {
            // connect to network
            sClientWifi_SSID     = CfgServer.arg("ssid");
            sClientWifi_Password = CfgServer.arg("password");
            // convert string to char*
            char __clientwifi_ssid[sClientWifi_SSID.length()+1];
            char __clientwifi_password[sClientWifi_Password.length()+1];
            sClientWifi_SSID.toCharArray(__clientwifi_ssid,sizeof(__clientwifi_ssid));
            sClientWifi_Password.toCharArray(__clientwifi_password,sizeof(__clientwifi_password));
            //Serial.println("Connecting to Wifi");

            // If connected disconnect
            if (WiFi.status() == WL_CONNECTED)
                {
                unsigned long timeStart=millis();
                WiFi.disconnect(true);
                // wait for disconnect to complete
                while (WiFi.status() == WL_CONNECTED)
                    {
                    delay(100);
                    if (millis()-timeStart>20000) break;
                    }
                } // end if connected disconnect

            // Connect to network
            WiFi.begin(__clientwifi_ssid, __clientwifi_password);
            delay(100);
            unsigned long timeStart=millis();
            while (WiFi.status()!= WL_CONNECTED)
                {
                delay(500);
                if (millis()-timeStart>20000) break;
                }

//                sPage += "password:"+clientwifi_password;
            if (WiFi.status() == WL_CONNECTED)
                {
                //Serial.println("Connected");
                sPage += "<br><br>Connected to network: <strong>" + sClientWifi_SSID + "</strong>";
                sPage += "<br><br>";
                sPage += "IP Address: "+WiFi.localIP().toString();
                }
            else
                {
                sPage += "<br><br><span style=\"color:red\">Error: Could not connect to network: <strong>" + sClientWifi_SSID + "</strong></span>";
                }
            } // end if not handling password
        } // end if not handling connect

    UpdateHeader();
    sPage  = pageHeader + sPage;
    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    } // end HandleWifiSettings()

#endif

// ----------------------------------------------------------------------------

void HandleLogs()
    {
    SdFileSys::SuFileInfoList   suFileList;
    bool        bListStatus = false;
    String      sFileLine = "";
    String      sPage;

    UpdateHeader();
    sPage.reserve(pageHeader.length() + 8192);
    sPage += pageHeader;
//    sPage += "<p>Available log files:</p>\n";
    sPage += "<br>\n";

    // Iterate over log file names here
    {
        // Pause logging while listing files to reduce SD contention.
        struct PauseGuard
            {
            bool bPrevPause;
            PauseGuard() : bPrevPause(g_bPause) { g_bPause = true; }
            ~PauseGuard() { g_bPause = bPrevPause; }
            } pauseGuard;

        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
            {
            bListStatus = g_SdFileSys.FileList(&suFileList);
            xSemaphoreGive(xWriteMutex);
            }
        else
            {
            g_Log.println(MsgLog::EnWebServer, MsgLog::EnWarning, "LOGS - SD busy (xWriteMutex)");
            }
    }

    sPage += "<table>\n";
    if (bListStatus == true)
        for (int iIdx=0; iIdx<suFileList.size(); iIdx++)
            {
            sFileLine  = "<tr><td><a href=\"/download?file=";
            sFileLine += suFileList[iIdx].szFileName;
            sFileLine += "\">";
            sFileLine += suFileList[iIdx].szFileName;
            sFileLine += "</a>";
            sFileLine += "</td>";

            sFileLine += "<td style=\"padding-left: 20px;text-align: right;\">";
            sFileLine += sFormatBytes(suFileList[iIdx].uFileSize);
            sFileLine += "</td>";

            sFileLine += "<td>&nbsp;&nbsp;&nbsp;<a href=\"/delete?file=";
            sFileLine += suFileList[iIdx].szFileName;
            sFileLine += "\">";
            sFileLine += szHtmlTrashcan;
            sFileLine += "</a>";
            sFileLine += "</td>";

            sFileLine += "</tr>\n";

            sPage     += sFileLine;
//            fileCount++;
            } // end for all files in the list
    else
        {
        sPage += "<tr><td><br><br><span style=\"color:red\">SD card busy or not available.</span></td></tr>\n";
        }
    sPage += "</table>\n";

    sPage += pageFooter;
    CfgServer.send(200, "text/html", sPage);
    } // end HandleLogs()


// ----------------------------------------------------------------------------

void HandleDelete()
    {
    String sFilename;

    // Handle case with no file name
    if (!CfgServer.hasArg("file"))
        {
//g_Log.printf("Delete missing file parameter\n");
        CfgServer.send(400, "text/plain", "Missing file parameter");
        return;
        }

    sFilename = CfgServer.arg("file");

    // File delete not yet confirmed
    if (CfgServer.arg("confirm").indexOf("yes") < 0)
        {
        String sPage;
        UpdateHeader();
        sPage.reserve(pageHeader.length() + 1024);
        sPage += pageHeader;

//g_Log.printf("Confirm delete %s\n", sFilename.c_str());

        // Display confirmation page
        sPage += "<br><br><p style=\"color:red\">Confirm that you want to delete <b>" + sFilename + "</b></p>\
        <br><br><br>\
        <a href=\"/delete?confirm=yes\&file=" + sFilename + "\" class=\"button\">Delete</a>\
        <a href=\"/logs\">Cancel</a>";
        sPage += pageFooter;
        CfgServer.send(200, "text/html", sPage);
        }

    // File delete has been confirmed
    else
        {
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
            {
//g_Log.printf("Delete %s\n", sFilename.c_str());
            g_SdFileSys.remove(sFilename.c_str());
            xSemaphoreGive(xWriteMutex);
            }

        CfgServer.sendHeader("Location", "/logs");
        CfgServer.send(301, "text/html", "");
        }

    }

// ----------------------------------------------------------------------------

void HandleDownload()
    {
    FsFile file;

    if (!CfgServer.hasArg("file"))
        {
        CfgServer.send(400, "text/plain", "Missing file parameter");
        return;
        }

    String sFilename = "/" + CfgServer.arg("file");

    // Pause logging during downloads to avoid SD mutex contention and
    // incomplete transfers (and to match the UI guidance on the index page).
    struct PauseGuard
        {
        bool bPrevPause;
        PauseGuard() : bPrevPause(g_bPause) { g_bPause = true; }
        ~PauseGuard() { g_bPause = bPrevPause; }
        } pauseGuard;

    size_t fileSize = 0;
    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
        {
        file = g_SdFileSys.open(sFilename.c_str(), O_READ);
        if (file)
            fileSize = file.size();
        xSemaphoreGive(xWriteMutex);
        }
    else
        {
        CfgServer.send(503, "text/plain", "SD busy");
        return;
        }

    if (!file)
        {
        CfgServer.send(404, "text/plain", "File not found");
        return;
        }

    // Send headers to trigger download
    CfgServer.setContentLength(fileSize);
    CfgServer.sendHeader("Content-Type", "application/octet-stream");
    CfgServer.sendHeader("Content-Disposition", "attachment; filename=" + CfgServer.arg("file"));
    CfgServer.sendHeader("Connection", "close");
    CfgServer.send(200);
//      CfgServer.send(200, "application/octet-stream", "");

    WiFiClient client = CfgServer.client();
    uint8_t achBuffer[1460];
    while (true)
        {
        size_t iLen = 0;
        // Getting and giving the semaphore is a bit slow
        // but wrapping this in one take / give doesn't speed
        // things that much and not having the sepaphore messes up logging.
        if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(2000)))
            {
            iLen = file.read(achBuffer, sizeof(achBuffer));
            xSemaphoreGive(xWriteMutex);
            }
        else
            {
            // SD is busy; wait and retry rather than aborting mid-transfer.
            if (!client.connected())
                break;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
            }

        if (iLen == 0)
            break;

        size_t iWritten = 0;
        while (iWritten < iLen)
            {
            size_t iThisWrite = client.write(achBuffer + iWritten, iLen - iWritten);
            if (iThisWrite == 0)
                {
                if (!client.connected())
                    {
                    iWritten = 0;
                    break;
                    }
                // Yield and retry.
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
                }
            iWritten += iThisWrite;
            // Yield periodically to keep WiFi/core-0 responsive during large transfers.
            delay(0);
            }

        if (iWritten != iLen)
            break;
        }

    if (xSemaphoreTake(xWriteMutex, pdMS_TO_TICKS(100)))
        {
        file.close();
        xSemaphoreGive(xWriteMutex);
        }

    } // end HandleDownload()


// ----------------------------------------------------------------------------

#if 0
bool HandleFileRead(String path) {

  String lengthString=CfgServer.arg("length");
  if (lengthString=="")
      {
      CfgServer.send(500, "text/plain", "Invalid Request");
                  return true;
      }

  String fileName=path.substring(1,path.indexOf("?")-1);
  unsigned long contentLength=atol(lengthString.c_str());
  delay(200);
  sendSerialCommand("$PRINT ");
  sendSerialCommand(fileName); // skip the slash in front of the file
  sendSerialCommand("!"); // finish the print command to grab the file via serial
  CfgServer.setContentLength(contentLength);
  CfgServer.sendHeader("Content-Disposition", "attachment; filename="+fileName);
  if (fileName.indexOf(".csv")>=0) CfgServer.send(200, "text/csv", ""); // send empty string (headers only)
      else CfgServer.send(200, "text/plain", "");

  unsigned long starttime=millis();
  unsigned long bytecount=0;
  //String sendBuffer;
  char sendBuffer[1461];
  int sendBufferIndex=0;
  byte checksum;
  int fullPacketCount=contentLength/1459;
  int endPacketSize=contentLength-fullPacketCount*1459;
  int packetCount=0;

  while (packetCount<fullPacketCount+1) // full packets + 1 partial packet
  {
  if (Serial.available()>0)
      {
       char inChar=Serial.read();
       if (inChar==0xFF && sendBufferIndex<1459) continue; // filter out FF junk (esp32 issue in this version)
       sendBuffer[sendBufferIndex]=inChar;
       sendBufferIndex++;

       if (sendBufferIndex==5 && strstr(sendBuffer, "<404>"))
                  {
                  // file does not exist
                  CfgServer.send(404, "text/plain", "File Not Found");
                  // lower Wifi power after file transfer completed
                  delay(200);
                  Serial.println("404");
                  return true;
                  }

       if (sendBufferIndex==1460 || (sendBufferIndex==endPacketSize+1 && packetCount==fullPacketCount))
        {
        checksum=0;
        for (int i=0;i<sendBufferIndex-1;i++)
           {
           checksum+=sendBuffer[i];
           }
        Serial.write(checksum);
        Serial.flush(true); // flush TX only

        if (checksum==sendBuffer[sendBufferIndex-1])
              {
               packetCount++; // got one packet, correct checksum
               CfgServer.sendContent(sendBuffer,sendBufferIndex-1);
              }
        sendBufferIndex=0;
        }
       starttime=millis();
      }

  if ((millis()-starttime)>5000)
      {
      // SD read timeout
      delay(200);
      Serial.println("Receive Timeout");
      return false;
      }
  }
  // lower Wifi power after file transfer completed
  //if (sendBufferIndex>0) CfgServer.sendContent(sendBuffer,sendBufferIndex-1); // send the remaining characters in buffer
  return true;
}

#endif

// ----------------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------------

#if 0
String getConfigString()
{
// get configstring
sendSerialCommand("$STOPLIVEDATA!");
delay(200);

//Serial.flush();
sendSerialCommand("$SENDCONFIGSTRING!");
String sConfigString="";
unsigned long starttime=millis();
while (true)
  {
  if (Serial.available()>0)
      {
       char inChar=Serial.read();
       sConfigString+=inChar;
       starttime=millis();  // reset timeout
       if (sConfigString.indexOf("</CHECKSUM>")>0)
          {
           String checksumString=getConfigValue(sConfigString,"CHECKSUM");
           String configContent=getConfigValue(sConfigString,"CONFIG");
           // calc CRC
           int16_t calcCRC;
           for (unsigned int i=0;i<configContent.length();i++) calcCRC+=configContent[i];
              if (String(calcCRC,HEX)==checksumString)
                 {
                 // checksum match
                  return sConfigString;
                 } else return "BAD CHECKSUM:"+sConfigString; // checksum error
          }
      }
  // timeout
  if ((millis()-starttime)>3000)
      {
      return "TIMEOUT"; // don't return partial configstring when timeout occurs
      }
  }
return sConfigString;
}

// ----------------------------------------------------------------------------

String getDefaultConfigString()
{
// get configstring
//Serial.flush();
sendSerialCommand("$SENDDEFAULTCONFIGSTRING!");
String sConfigString="";
unsigned long starttime=millis();
while (true)
  {
  if (Serial.available()>0)
      {
       char inChar=Serial.read();
       sConfigString+=inChar;
       starttime=millis();  // reset timeout
       if (sConfigString.indexOf("</CHECKSUM>")>0)
          {
           String checksumString=getConfigValue(sConfigString,"CHECKSUM");
           String configContent=getConfigValue(sConfigString,"CONFIG");
           // calc CRC
           int16_t calcCRC;
           for (unsigned int i=0;i<configContent.length();i++) calcCRC+=configContent[i];
           if (String(calcCRC,HEX)==checksumString)
                 {
                 // checksum match
                 return sConfigString;
                 } else return ""; // checksum error
          }
      }
  // timeout
  if ((millis()-starttime)>2000)
      {
      return ""; // don't return partial configstring when timeout occurs
      }
  }
return sConfigString;
}

// ----------------------------------------------------------------------------

bool setConfigString(String &sConfigString)
{
//Serial.flush();
// add CRC to sConfigString
addCRC(sConfigString);

sendSerialCommand("$SAVECONFIGSTRING"+sConfigString+"!");

unsigned long starttime=millis();
// wait for ACK
String responseString;
while (true)
      {
      //wait 2 seconds to confirm saveConfigSting
      if (Serial.available()>0)
         {
         char inChar=Serial.read();
         responseString+=inChar;
         if (responseString.indexOf("</CONFIGSAVED>")>=0) return true;
         if (responseString.indexOf("</CONFIGERROR>")>=0) return false;
         }

      // timeout
      if ((millis()-starttime)>3000)
          {
          return false; // timeout
          }
      }
return false;
}
#endif

// ----------------------------------------------------------------------------

// format bytes

String sFormatBytes(size_t bytes)
    {
    if (bytes < 1024)
        {
        return String(bytes) + " B";
        }
    else if (bytes < (1024 * 1024))
        {
        return String(bytes / 1024.0) + " KB";
        }
    else if (bytes < (1024 * 1024 * 1024))
        {
        return String(bytes / 1024.0 / 1024.0) + " MB";
        }
    else
        {
        return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
    }
}
