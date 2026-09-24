// All the HTML related to the manager page is here.
// Also, all the variables we need created

#include <WiFi.h>
#include <ESPAsyncWebSrv.h>
#include <AsyncTCP.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "FS.h"
#include "SPIFFS.h"
#include <WiFiAP.h>
#include <ArduinoOTA.h>
#include <SPIFFSEditor.h>
#include "base64.hpp"

String ssid;
String wifi_password;
String http_username;
String http_password;
String allowedExtensionsForEdit;
String jquery;

String textareaContent = "";
String savePath = "";
String savePathInput = "";

AsyncWebServer server(80);

bool rebooting = false;
bool save = false;

// Simple XOR key – change this to whatever you like
static const uint8_t XOR_KEY = 0x5A;   // arbitrary; same for enc/dec
static const char ENC_PREFIX = '!';    // leading char to mark encrypted

// Wifi enabled.
bool wifi_enabled = false;
bool wifi_connected = false;
unsigned long wifi_counter = 0; // keep trying the wifi for 2 minutes

// These are the pages we need created.  They are stored in program memory as they are static.

// Optimized HTML templates with better compression
const char manager_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>ESP32 SPIFFS Manager</title>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{background:#f7f7f7;font-family:Arial,sans-serif}
table{background:#ddd;border-collapse:collapse;width:650px;margin:10px 0}
td,th{border:1px solid #ddd;text-align:left;padding:8px}
tr:nth-child(even){background:#fff}
fieldset{width:700px;background:#f7f7f7;margin:10px 0}
.btn{width:120px;padding:5px;margin:2px}
.center{text-align:center}
.warn{color:#f00}
</style>
<script>
function validateUpdate(){
var f=document.getElementById('update').files;
if(!f.length){alert("Choose a file!");return false}
if(!f[0].name.endsWith('.bin')){alert("Wrong file type!");return false}
}
function validateUpload(){
if(!document.getElementById('upload_data').files.length){alert("Choose a file!");return false}
}
function confirmFormat(){return confirm("Delete all SPIFFS data and restart?")}
</script></head><body><center>
<h2>ESP32 Manager</h2>
<a href="https://github.com/mage0r/Esp32-Web-Update-and-SPIFFS-manager">GitHub</a>
<fieldset><legend>System</legend>
<table><tr><th>SPIFFS</th><th>ESP32 Status</th></tr><tr>
<td>Total: %SPIFFS_TOTAL_BYTES%<br>Used: %SPIFFS_USED_BYTES%<br>Free: %SPIFFS_FREE_BYTES%</td>
<td>Project: %PROJECT% - %VERSION%<br>Build: %BUILDDATE% %BUILDTIME%<br>
RAM: %GETFREEHEAP% / %GETTOTALHEAP%<br>PSRAM: %GETFREEPSRAM% / %GETTOTALPSRAM%</td>
</tr></table></fieldset>
<fieldset><legend>Files</legend>
<table><tr><th></th><th></th><th></th><th></th><th class="center">
<input type="button" onclick="location.href='/edit?edit_path=new'" value="New File" class="btn"></th></tr>
%LISTEN_FILES%</table></fieldset>
<fieldset><legend>Upload</legend>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" id="upload_data" name="upload_data">
<input type="submit" value="Upload" onclick="return validateUpload()" class="btn">
</form></fieldset>
<fieldset><legend>Format SPIFFS</legend>
<form method="POST" action="/format">
<span class="warn">Deletes all data!</span>
<input type="submit" value="Format" onclick="return confirmFormat()" class="btn">
</form></fieldset>
<fieldset><legend>Firmware Update</legend>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" id="update" name="update">
<input type="submit" value="Update" onclick="return validateUpdate()" class="btn">
</form></fieldset>
</center></body></html>)rawliteral";

const char edit_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Edit File</title>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{background:#f7f7f7;font-family:Arial,sans-serif}
fieldset{width:800px;background:#f7f7f7}
textarea{width:700px;height:200px;padding:12px;border:2px solid #ccc;border-radius:4px;resize:vertical}
.btn{width:120px;padding:8px;margin:5px}
</style>
<script>
function validateForm(){
var ext='%ALLOWED_EXTENSIONS_EDIT%';
var path=document.getElementById('save_path').value;
var dotIndex=path.lastIndexOf('.')+1;
if(!path){alert('Enter filename!');return false}
if(!dotIndex){alert('Missing extension!');return false}
if(ext.indexOf(path.substring(dotIndex))==-1){alert('Extension not supported!');return false}
}
</script></head><body><center>
<h2>Edit File</h2>
<fieldset><legend>Edit Text File</legend>
<form name="edit_file" action="/save" onsubmit="return validateForm()">
<textarea name="edit_textarea">%TEXTAREA_CONTENT%</textarea><br>
%SAVE_PATH_INPUT%
<input type="submit" value="Save" class="btn">
</form>
<button onclick="location.href='/manage'" class="btn">Cancel</button>
</fieldset></center></body></html>)rawliteral";

const char ok_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Success</title>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#f7f7f7;text-align:center;font-family:Arial,sans-serif}</style>
</head><body><h2>Update successful!</h2>
<button onclick="location.href='/manage'">Return</button></body></html>)rawliteral";

const char failed_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Failed</title>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#f7f7f7;text-align:center;font-family:Arial,sans-serif}</style>
</head><body><h2>Update failed!</h2>
<button onclick="location.href='/manage'">Return</button></body></html>)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>ESP32 SPIFFS Manager</title>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{background:#f7f7f7;font-family:Arial,sans-serif;padding:20px}
table{background:#ddd;border-collapse:collapse;width:650px}
td,th{border:1px solid #ddd;text-align:left;padding:8px}
tr:nth-child(even){background:#fff}
fieldset{width:700px;background:#f7f7f7;margin:10px 0}
</style></head><body>
<h2>ESP32 Default Index Page</h2>
<p>Replace with your project content!<br>Auto-generated when no config exists.</p>
<a href="https://github.com/mage0r/Esp32-Web-Update-and-SPIFFS-manager">GitHub Project</a>
<fieldset><legend>System Status</legend>
<table><tr><th>SPIFFS</th><th>ESP32 Status</th></tr><tr>
<td>Total: %SPIFFS_TOTAL_BYTES%<br>Used: %SPIFFS_USED_BYTES%<br>Free: %SPIFFS_FREE_BYTES%</td>
<td>Project: %PROJECT% - %VERSION%<br>Build: %BUILDDATE% %BUILDTIME%<br>
RAM: %GETFREEHEAP% / %GETTOTALHEAP%<br>PSRAM: %GETFREEPSRAM% / %GETTOTALPSRAM%</td>
</tr></table></fieldset>
</body></html>)rawliteral";