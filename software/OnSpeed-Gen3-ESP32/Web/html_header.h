
// Base64 image encoding
// https://www.base64-image.de/

const char szHtmlHeader[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>

<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="utf-8"/>
<meta http-equiv="Cache-Control" content="no-cache">
<title>OnSpeed Gen2 Configuration</title>

<style>
body {
    background-color: #cccccc;
    font-family: Arial, Helvetica, Sans-Serif; Color: #000088;
    }
ul {
    list-style-type: none;
    margin: 0;
    padding: 0;
    overflow: hidden;
    background-color: #333;
    }
li {
    float: left;
    }
li a, .dropbtn {
    display: inline-block;
    color: white;
    text-align: center;
    padding: 14px 16px;
    text-decoration: none;
    }
li a:hover, .dropdown:hover .dropbtn {
    background-color: red;
    }
li.dropdown {
    display: inline-block;
    }
.dropdown-content {
    display: none;
    position: absolute;
    background-color: #f9f9f9;
    min-width: 160px;
    box-shadow: 0px 8px 16px 0px rgba(0,0,0,0.2);
    z-index: 1;
    }
.dropdown-content a {
    color: black;
    padding: 12px 16px;
    text-decoration: none;
    display: block;
    text-align: left;
    }
.dropdown-content a:hover {background-color: #f1f1f1}
.dropdown:hover .dropdown-content {
    display: block;
    }
.button {
    background-color: red;
    border: none;
    color: white;
    padding: 15px 32px;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    margin: 4px 25px;
    cursor: pointer;
    border-radius:3px;
    }
.inputField {
    width: 245px;
    height: 40px;
    margin: 0 .25rem;
    min-width: 125px;
    border: 1px solid #eee;
    border-radius: 5px;
    transition: border-color .5s ease-out;
    font-size: 16px;
    padding-left:10px;
    }
.wifi {
    padding: 2px;
    margin-left: auto;
    }
.wifi, .wifi:before {
    display: inline-block;
    border: 8px double transparent;
    border-top-color: #42a7f5;
    border-radius: 25px;
    }
.wifi:before {
    content: '';
    width: 0; height: 0;
    }
.offline,.offline:before{
    border-top-color:#999999;
    }
.header-container{
    display: flex;
    align-items: flex-end;  
    margin: 0px;
    }
.firmware{
    font-size:9px;
    margin-bottom: 6px;
    margin-left: 10px;
    }
.logo{
    font-size:36px;
    font-weight:bold;
    font-family:helvetica;
    color:black;
    }
.wifibutton{
    background-color:#42a7f5;
    border:none;
    color:white;
    padding:12px 20px;
    text-align:center;
    text-decoration:none;
    display:inline-block;
    font-size:16px;
    margin:10px 25px;
    cursor:pointer;
    min-width:220px
    }
.icon__signal-strength{
    display:inline-flex;
    align-items:flex-end;
    justify-content:flex-end;
    width:auto;
    height:15px;
    padding:10px;
    }
.icon__signal-strength span{
    display:inline-block;
    width:4px;
    margin-left:2px;
    transform-origin:100% 100%;
    background-color:#fff;
    border-radius:2px;
    animation-iteration-count:1;
    animation-timing-function:cubic-bezier(.17,.67,.42,1.3);
    animation-fill-mode:both;
    animation-play-state:paused
    }
.icon__signal-strength .bar-1{
    height:25%;
    animation-duration:0.3s;
    animation-delay:0.1s
    }
.icon__signal-strength .bar-2{
    height:50%;
    animation-duration:0.25s;
    animation-delay:0.2s
    }
.icon__signal-strength .bar-3{
    height:75%;
    animation-duration:0.2s;
    animation-delay:0.3s
    }
.icon__signal-strength.bar-4{
    height:100%;
    animation-duration:0.15s;
    animation-delay:0.4s
    }
.signal-0 .bar-1,.signal-0 .bar-2,.signal-0 .bar-3,.signal-0 .bar-4{
    opacity:.2
    }
.signal-1 .bar-2,.signal-1 .bar-3,.signal-1.bar-4{
    opacity:.2
    }
.signal-2 .bar-3,.signal-2 .bar-4{
    opacity:.2
    }
.signal-3 .bar-4{
    opacity:.2
    } 
    
.form-grid,
.form-option-box,
.round-box {
    display: flex;
    flex-wrap: wrap;
    flex-direction: row;
    background: #000;
    align-content: stretch;
    justify-content: flex-end;
    max-width:500px;
    align-items: center;
    }

.round-box {
    background: #fff;
    padding: 15px 10px;
    -webkit-appearance: none;
    -webkit-border-radius: 3px;
    -moz-border-radius: 3px;
    -ms-border-radius: 3px;
    -o-border-radius: 3px;
    border-radius: 3px;
    }

.form-divs {
    margin-bottom: 26px;
    padding:0px 5px;
    font-size:12px;
    }

* {
    box-sizing: border-box;
    }

.form-divs label {
    display: block;
    color: #737373;
    margin-bottom: 8px;
    line-height: 15px;
    font-size:15px;
    }

.curvelabel {
    margin-bottom: 0px;
    }

.border,
.form-divs input,
.form-divs select,
.form-divs textarea {
    border: 1px solid #cccccc;
    }

.form-divs input,
.form-divs select,
.form-divs textarea {
    font-size: 14px;
    width: 100%;
    height: 33px;
    padding: 0 8px;
    -webkit-appearance: none; 
    -webkit-border-radius: 3px;
    -moz-border-radius: 3px;
    -ms-border-radius: 3px;
    -o-border-radius: 3px;
    border-radius: 3px;
    }

.form-divs select {
    -webkit-appearance: menulist;
    }

.form-divs.inline-formfield.top-label-gap {
    margin-top: 26px;
    }

.flex-col-1 {
    width: 8.33%;
    }

.flex-col-2 {
    width: 16.66%;
    }

.flex-col-3 {
    width: 24.99%;
    }

.flex-col-4 {
    width: 33.32%;
    }

.flex-col-5 {
    width: 41.65%;
    }

.flex-col-6 {
    width: 49.98%;
    }

.flex-col-7 {
    width: 58.31%;
    }

.flex-col-8 {
    width: 66.64%;
    }

.flex-col-9 {
    width: 74.97%;
    }

.flex-col-10 {
    width: 83.3%;
    }

.flex-col-11 {
    width: 91.63%;
    }

.flex-col-12 {
    width: 99.96%;
    }

.flex-col--1 {
    width: 8.33% !important;
    }

.flex-col--2 {
    width: 16.66% !important;
    }

.flex-col--3 {
    width: 24.99% !important;
    }

.flex-col--4 {
    width: 33.32% !important;
    }

.flex-col--5 {
    width: 41.65% !important;
    }

.flex-col--6 {
    width: 49.98% !important;
    }

.flex-col--7 {
    width: 58.31% !important;
    }

.flex-col--8 {
    width: 66.64% !important;
    }

.flex-col--9 {
    width: 74.97% !important;
    }

.flex-col--10 {
    width: 83.3% !important;
    }

.flex-col--11 {
    width: 91.63% !important;
    }

.flex-col--12 {
    width: 99.96% !important;
    }

.quick-help {
    position: relative;
    display: inline-block;
    padding: 0px 8px;
    }
.quick-help span {
    display: none;
    min-width: 200px;
    width: auto;
    height: auto;
    -webkit-border-radius: 3px;
    -moz-border-radius: 3px;
    -ms-border-radius: 3px;
    -o-border-radius: 3px;
    border-radius: 3px;
    background-color: #fff;
    box-shadow: 0px 0px 12px 0px rgba(0, 0, 0, 0.2);
    position: absolute;
    top: 14px;
    left: 14px;
    padding: 12px;
    }
.quick-help:hover span {
    display: block;
    }

.form-divs input.error-field {
    border-color: #c32929;
    }

.error {
    border-bottom: solid 1px red;
    color: red;
    }

@media screen and (max-width: 768) {
    .flex-col-1 {
        width: 100%;
        }

    .flex-col-2 {
        width: 100%;
        }

    .flex-col-3 {
        width: 100%;
        }

    .flex-col-4 {
        width: 100%;
        }

    .flex-col-5 {
        width: 100%;
        }

    .flex-col-6 {
        width: 100%;
        }

    .flex-col-7 {
        width: 100%;
        }

    .flex-col-8 {
        width: 100%;
        }

    .flex-col-9 {
        width: 100%;
        }

    .flex-col-10 {
        width: 100%;
        }

    .flex-col-11 {
        width: 100%;
        }

    .flex-col-12 {
        width: 100%;
        }
    }

.redbutton {
    background-color: red;
    border: none;
    color: black;
    padding: 15px 32px;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    cursor: pointer;
    border-radius:3px;
    }

.greybutton {
    background-color: #ddd;
    border: none;
    color: black;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    cursor: pointer;
    }

.blackbutton {
    background-color: black;
    border: none;
    color: white;
    padding: 15px 32px;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    cursor: pointer;
    border-radius:3px;
    }

section {
    border: 1px groove threedface;
    padding: 0px;
    display: flex;
    flex-wrap: wrap;
    flex-direction: row;
    background: #f0f0f5;
    align-content: stretch;
    padding-top:10px;
    margin-top:15px;
    margin-bottom:15px;
    border-radius: 3px;
    align-items: center;  
    }

section h2 {
    float: left;
    margin: -22px 0 0;
    padding-left:20px;
    padding-right:20px;
    margin-left:5px;  
    background: #fff;
    font-weight: bold;
    height:22px;
    border-radius: 3px;
    border:1px groove threedface;
    font-size: 15px;
    color: #737373;
    }   

.content-container{
    display: flex;
    align-items: flex-end;  
    margin: 0px;
    background:#fff;
    }
.upload-btn-wrapper {
    position: relative;
    overflow: hidden;
    display: inline-block;
    }
                    
.upload-btn {
    border: none;
    color: black;
    background-color: #ddd;
    border-radius: 3px;
    font-size: 14px;
    text-align: center;
    text-decoration: none;
    height: 33px;
    width: 100%;
    padding: 0 8px;
    }
                    
.upload-btn-wrapper input[type=file] {
    font-size: 100px;
    position: absolute;
    left: 0;
    top: 0;
    opacity: 0;
    }
.switch-field {
    display: flex;
    /*margin-bottom: 5px;*/
    overflow: hidden;
    }
.switch-field input {
    position: absolute !important;
    clip: rect(0, 0, 0, 0);
    height: 1px;
    width: 1px;
    border: 0;
    overflow: hidden;
    }
.switch-field label {
    background-color: #e4e4e4;
    color: rgba(0, 0, 0, 0.6);
    font-size: 14px;
    line-height: 1;
    text-align: center;
    padding: 8px 16px;
    margin-right: -1px;
    border: 1px solid rgba(0, 0, 0, 0.2);
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.3), 0 1px rgba(255, 255, 255, 0.1);
    transition: all 0.1s ease-in-out;
    }
.switch-field label:hover {
    cursor: pointer;
    }
.switch-field input:checked + label {
    background-color: #a5dc86;
    box-shadow: none;
    }
.switch-field label:first-of-type {
    border-radius: 4px 0 0 4px;
    }
.switch-field label:last-of-type {
    border-radius: 0 4px 4px 0;  
    }   
</style>
</head>

<body>
<div class="header-container">
<img src="data:image/png;base64,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" alt="OnSpeed">
<div class="firmware">wifi_fw</div>
<!-- <div class="wifi wifi_status" title="wifi_network"></div> -->
</div>

<ul>
    <li><a href="/">Home</a></li>
    <li class="dropdown"> 
        <a href="javascript:void(0)" class="dropbtn">Tools</a>
        <div class="dropdown-content">
            <a href="logs">Log Files</a>
            <a href="format">Format SD Card</a>
            <a href="upgrade">Upgrade Wifi module</a>
            <a href="reboot">Reboot System</a>
        </div>    
        </li>
    <li class="dropdown"> 
        <a href="javascript:void(0)" class="dropbtn">Settings</a>
        <div class="dropdown-content">
            <!-- <a href="wifi">Wifi Connection</a> -->
            <a href="aoaconfig">System Configuration</a>
            <a href="sensorconfig">Sensor Calibration</a>    
            <a href="calwiz">AOA Calibration Wizard</a>
        </div>     
        </li>
    <li>
        <a href="live">LiveView</a></li>   
</ul>
)=====";
