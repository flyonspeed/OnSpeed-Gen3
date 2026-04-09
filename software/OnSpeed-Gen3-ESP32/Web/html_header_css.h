
// Main CSS extracted from html_header.h for external serving with ETag caching.

const char szHtmlHeaderCss[] PROGMEM = R"=====(
*, *::before, *::after {
    box-sizing: border-box;
    }
html, body {
    margin: 0;
    padding: 0;
    }
body {
    background-color: #cccccc;
    font-family: Arial, Helvetica, Sans-Serif; Color: #000088;
    }
body > ul {
    list-style-type: none;
    margin: 0;
    padding: 0;
    overflow: hidden;
    background-color: #333;
    }
body > ul > li {
    float: left;
    }
body > ul > li a, .dropbtn {
    display: inline-block;
    color: white;
    text-align: center;
    padding: 14px 16px;
    text-decoration: none;
    background: none;
    border: none;
    font-family: inherit;
    font-size: inherit;
    cursor: pointer;
    }
body > ul > li a:hover, body > ul > li.dropdown:hover .dropbtn, body > ul > li.dropdown.open .dropbtn {
    background-color: red;
    }
body > ul > li.dropdown {
    display: inline-block;
    position: relative;
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
.dropdown:hover .dropdown-content,
.dropdown.open .dropdown-content {
    display: block;
    }
.nav-toggle {
    display: none;
    background: #333;
    color: white;
    border: none;
    padding: 12px 16px;
    font-size: 18px;
    cursor: pointer;
    width: 100%;
    text-align: left;
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
    width: 100%;
    max-width: 245px;
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
.icon__signal-strength .bar-4{
    height:100%;
    animation-duration:0.15s;
    animation-delay:0.4s
    }
.signal-0 .bar-1,.signal-0 .bar-2,.signal-0 .bar-3,.signal-0 .bar-4{
    opacity:.2
    }
.signal-1 .bar-2,.signal-1 .bar-3,.signal-1 .bar-4{
    opacity:.2
    }
.signal-2 .bar-3,.signal-2 .bar-4{
    opacity:.2
    }
.signal-3 .bar-4{
    opacity:.2
    }

.page-container {
    max-width: 960px;
    margin: 0 auto;
    padding: 16px;
    background: #fff;
    }
@media (min-width: 1024px) {
    .page-container {
        max-width: 1100px;
        padding: 24px;
        }
    }

.form-grid,
.form-option-box,
.round-box {
    display: flex;
    flex-wrap: wrap;
    flex-direction: row;
    background: #000;
    align-content: stretch;
    justify-content: flex-start;
    width: 100%;
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
    appearance: menulist;
    }

.form-divs input[type="radio"] {
    width: auto;
    height: auto;
    -webkit-appearance: radio;
    display: inline;
    margin: 0 4px 0 0;
    border: none;
    }
.radio-group {
    display: flex;
    flex-wrap: wrap;
    gap: 10px 18px;
    }
.radio-group label {
    display: inline-flex;
    align-items: center;
    font-size: 14px;
    color: #333;
    cursor: pointer;
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

@media screen and (max-width: 1023px) {
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
    border: 1px solid #d0d0d0;
    display: flex;
    flex-wrap: wrap;
    flex-direction: row;
    background: #f0f0f5;
    align-content: stretch;
    padding: 34px 12px 10px 12px;
    margin: 24px 0;
    border-radius: 6px;
    align-items: center;
    position: relative;
    width: 100%;
    }

section h2 {
    position: absolute;
    top: 10px;
    left: 16px;
    margin: 0;
    padding: 0;
    float: none;
    background: transparent;
    border: none;
    height: auto;
    font-weight: 600;
    font-size: 14px;
    letter-spacing: 0.02em;
    text-transform: uppercase;
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
.sp-row {
    width: 100%;
    padding: 8px 5px;
    border-bottom: 1px solid #e0e0e0;
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px 16px;
    }
.sp-row label {
    display: block;
    color: #737373;
    font-size: 13px;
    margin-bottom: 0;
    flex: 0 0 auto;
    min-width: 110px;
    }
.sp-controls {
    display: flex;
    flex-wrap: nowrap;
    align-items: center;
    gap: 8px;
    flex: 0 0 auto;
    white-space: nowrap;
    }
.sp-mult, .sp-ias {
    white-space: nowrap;
    }
.sp-btn {
    width: 36px;
    height: 36px;
    font-size: 20px;
    font-weight: bold;
    border: 1px solid #ccc;
    border-radius: 4px;
    background: #f0f0f0;
    cursor: pointer;
    line-height: 1;
    padding: 0 !important;
    }
.sp-btn:active {
    background: #ddd;
    }
.sp-mult {
    font-size: 18px;
    font-weight: bold;
    color: #000088;
    min-width: 80px;
    }
.sp-ias {
    font-size: 14px;
    color: #666;
    }
.sp-secondary {
    display: flex;
    flex-wrap: nowrap;
    align-items: center;
    gap: 6px;
    font-size: 12px;
    color: #888;
    white-space: nowrap;
    }
.sp-aoa-input {
    width: 60px !important;
    height: 24px !important;
    font-size: 12px !important;
    padding: 0 4px !important;
    }
.sp-live-btn {
    font-size: 11px !important;
    padding: 4px 8px !important;
    height: auto !important;
    width: auto !important;
    }
.sp-info-row {
    padding: 4px 5px;
    font-size: 12px;
    color: #666;
    }
.sp-info {
    margin-right: 16px;
    }
.sp-vs-info {
    font-size: 13px;
    color: #000088;
    font-weight: bold;
    padding: 2px 5px;
    }

/* Wrapped tables for raw-table pages (sensor calibration, logs) */
.sensor-table {
    width: 100%;
    border-collapse: collapse;
    }
.sensor-table td {
    padding: 4px 20px 4px 0;
    font-size: 14px;
    }
.sensor-table td.num {
    text-align: right;
    font-variant-numeric: tabular-nums;
    }
.logs-table {
    width: 100%;
    border-collapse: collapse;
    }
.logs-table td {
    padding: 6px 4px;
    font-size: 14px;
    }
.logs-table td.size {
    padding-left: 20px;
    text-align: right;
    white-space: nowrap;
    }
.logs-table td a {
    color: #000088;
    }

/* Phone-size nav: hamburger + vertical menu */
@media (max-width: 599px) {
    .nav-toggle {
        display: block;
        }
    body > ul {
        display: none;
        flex-direction: column;
        overflow: visible;
        }
    body > ul.open {
        display: flex;
        }
    body > ul > li {
        float: none;
        width: 100%;
        }
    body > ul > li a, .dropbtn {
        text-align: left;
        width: 100%;
        padding: 14px 20px;
        }
    body > ul > li.dropdown {
        display: block;
        position: static;
        }
    .dropdown-content {
        position: static;
        box-shadow: none;
        min-width: 0;
        background-color: #555;
        }
    .dropdown-content a {
        color: #eee;
        padding-left: 40px;
        }
    .dropdown-content a:hover {
        background-color: #666;
        }
    .dropdown:hover .dropdown-content {
        display: none;
        }
    .dropdown.open .dropdown-content {
        display: block;
        }
    .page-container {
        padding: 10px;
        }
    }
)=====";
