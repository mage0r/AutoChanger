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

#define FORMAT_SPIFFS_IF_FAILED true

String ssid;
String wifi_password;
String http_username;
String http_password;
String host;
String allowedExtensionsForEdit;
String jquery;

String textareaContent = "";
String savePath = "";
String savePathInput = "";

AsyncWebServer server(80);

bool rebooting = false;

// Wifi enabled.
bool wifi_enabled = false;
unsigned long wifi_counter = 0; // keep trying the wifi for 2 minutes

// These are the pages we need created.  They are stored in program memory as they are static.

const char manager_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>ESP32 SPIFFS Manager</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background-color: #f7f7f7;
      }
      #submit {
        width:120px;
      }
      #spacer_50 {
        height: 50px;
      }
      #spacer_20 {
        height: 20px;
      }
      table {
        background-color: #dddddd;
        border-collapse: collapse;
        width:650px;
      }
      td, th {
        border: 1px solid #dddddd;
        text-align: left;
        padding: 8px;
      }
      #first_td_th {
        width:350px;
      }
      #center_td {
        text-align: center;
      }
      tr:nth-child(even) {
        background-color: #ffffff;
      }
      fieldset {
        width:700px;
        background-color: #f7f7f7;
      }
      #format_notice {
        color: #ff0000;
      }
    </style>
    <script>
      function validateFormUpdate() {
        var inputElement = document.getElementById('update');
        var files = inputElement.files;
        if(files.length==0) {
          alert("You have not chosen a file!");
          return false;
        }
        var value = inputElement.value;
        var dotIndex = value.lastIndexOf(".")+1;
        var valueExtension = value.substring(dotIndex);
        if(valueExtension != "bin") {
          alert("Incorrect file type!");
          return false;
        }
      }
      function validateFormUpload() {
        var inputElement = document.getElementById('upload_data');
        var files = inputElement.files;
        if(files.length==0) {
          alert("You have not chosen a file!");
          return false;
        }
      }
      function validateFormEdit()
      {
        var allowedExtensions = "%ALLOWED_EXTENSIONS_EDIT%";
        var editSelectValue = document.getElementById('edit_path').value;
        var dotIndex = editSelectValue.lastIndexOf(".")+1;
        var editSelectValueExtension = editSelectValue.substring(dotIndex);
        var extIndex = allowedExtensions.indexOf(editSelectValueExtension);
        
        if(editSelectValue == "new") {
          return true;
        }
        if(editSelectValue == "choose") {
          alert("You have not chosen a file!");
          return false;
        }
        if(extIndex == -1) {
          alert("Editing of this file type is not supported!");
          return false;
        }
      }
      function confirmFormat() {
        var text = "Pressing the \"OK\" button immediately deletes all data from SPIFFS and restarts ESP32!";
        if (confirm(text) == true) {
          return true;
        } else {
          return false;
        }
      }
    </script>
  </head>
  <body>
    <center>
           
      <h2>ESP32 Manager</h2>

      <div id="spacer_20"></div>

      <fieldset>
        <legend>System</legend>
          <div id="spacer_20"></div>
          <table>
            <tr>
              <th>SPIFFS Use</th>
              <th>ESP32 Status</th>
            </tr>
            <tr>
              <td>
                Total: %SPIFFS_TOTAL_BYTES%<br />
                Used: %SPIFFS_USED_BYTES%<br />
                Free: %SPIFFS_FREE_BYTES%
              </td>
              <td>
                Project: %PROJECT% - %VERSION%<br />
                Build Date: %BUILDDATE% %BUILDTIME%<br />
                Free RAM: %GETFREEHEAP% / %GETTOTALHEAP%<br />
                Free PSRAM: %GETFREEPSRAM% / %GETTOTALPSRAM%<br />
              </td>
            </tr>
          </table>
          
        <div id="spacer_20"></div>
      </fieldset>

      <div id="spacer_20"></div>

      <fieldset>
        <legend>File list</legend>
          <div id="spacer_20"></div>
          <table>
            <tr>
              <th id='first_td_th'></th>
              <th></th>
              <th></th>
              <th id='center_td'><input type='button' onclick='window.location.href="/edit?edit_path=new";' value='New File' /></th>
            </tr>
            %LISTEN_FILES%
          </table>
          <div id="spacer_20"></div>
      </fieldset>

      <div id="spacer_20"></div>

      <fieldset>
        <legend>File upload</legend>
          <div id="spacer_20"></div>
          <form method="POST" action="/upload" enctype="multipart/form-data">
            <table><tr><td>
              <input type="file" id="upload_data" name="upload_data">
              </td><td style="width: 50px">
              <input type="submit" id="submit" value="File upload!" onclick="return validateFormUpload()">
            </td></tr></table>
          </form>
          <div id="spacer_20"></div>
      </fieldset>
      
      <div id="spacer_20"></div>

      <fieldset>
        <legend>Format SPIFFS</legend>
          <div id="spacer_20"></div>
          <form method="POST" action="/format" target="self_page">
            <table><tr><td>
              <div id="format_notice">Pressing the 'Format' button will immediately delete all data from SPIFFS!</div>
            </td><td style="width: 50px">
              <input type="submit" id="submit" value="Format" onclick="return confirmFormat()">
            </td></tr></table>
          </form>
          <div id="spacer_20"></div>
      </fieldset>

      <div id="spacer_20"></div>
      
      <fieldset>
        <legend>Firmware Update</legend>
          <div id="spacer_20"></div>
          <form method="POST" action="/update" enctype="multipart/form-data">
            <table><tr><td>
            <input type="file" id="update" name="update">
            </td><td style="width: 50px">
            <input type="submit" id="submit" value="Update!" onclick="return validateFormUpdate()">
            </td></tr></table>
          </form>
          <div id="spacer_20"></div>
      </fieldset>

      <div id="spacer_20"></div>

      <iframe style="display:none" name="self_page"></iframe>
    </center>
  </body>
</html> )rawliteral";

const char failed_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>Update unsuccessful</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background-color: #f7f7f7;
      }
      #spacer_50 {
        height: 50px;
      }
    </style>
  </head>
  <body>
    <center>
      <h2>The update has failed.</h2>
      <div id="spacer_50"></div>
      <button onclick="window.location.href='/manager';">to homepage</button>
    </center>
  </body>
</html> )rawliteral";

const char edit_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>Edit file</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background-color: #f7f7f7;
      }
      #submit {
        width:120px;
      }
      #spacer_50 {
        height: 50px;
      }
      #spacer_20 {
        height: 20px;
      }
      fieldset {
        width:800px;
        background-color: #f7f7f7;
      }
      table {
        background-color: #dddddd;
      }
      td, th {
        text-align: center;
        padding: 15px;
      }
      textarea {
        width: 700px;
        height: 400px;
        padding: 12px 20px;
        box-sizing: border-box;
        border: 2px solid #ccc;
        border-radius: 4px;
        resize: none;
      }
    </style>
    <script>
      function validateForm()
      {
        var allowedExtensions = "%ALLOWED_EXTENSIONS_EDIT%";
        var inputMessage = document.getElementById('save_path').value;
        var dotIndex = inputMessage.lastIndexOf(".")+1;
        var inputMessageExtension = inputMessage.substring(dotIndex);
        var extIndex = allowedExtensions.indexOf(inputMessageExtension);
        var isSlash = inputMessage.substring(0,1);

        if(inputMessage == "")
        {
          alert("Enter the file name! \ne.g.: new.txt");
          return false;
        }
        if(dotIndex == 0)
        {
          alert("The extension is missing at the end of the file!");
          return false;
        }
        if(inputMessageExtension == "")
        {
          alert("The extension is missing at the end of the file!");
          return false;
        }
        if(extIndex == -1)
        {
          alert("Extension not supported!");
          return false;
        }
      }
    </script>
  </head>
  <body>
    <center>
      <h2>Edit file</h2>
      <div id="spacer_20"></div>
      
      <fieldset>
        <legend>Edit text file</legend>
        <div id="spacer_20"></div>
        <table><tr><td colspan="2">
        <form name="edit_file" action="/save" onsubmit="return validateForm()">
          <textarea name="edit_textarea">%TEXTAREA_CONTENT%</textarea>
          <div id="spacer_20"></div>
        </td></tr><tr><td>
          %SAVE_PATH_INPUT%
          <button type "submit" id="submit" >Save</button>
        </form>
        </td><td>
        <button id="submit" onclick="window.location.href='/manager';">Cancel</button>
        </td></tr></table>
        <div id="spacer_50"></div>
      </fieldset>
      
      <iframe style="display:none" name="self_page"></iframe>
    </center>
  </body>
</html> )rawliteral";

const char ok_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>Update successful</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        background-color: #f7f7f7;
      }
      #spacer_50 {
        height: 50px;
      }
    </style>
  </head>
  <body>
    <center>
      <h2>The update was successful.</h2>
      <div id="spacer_50"></div>
      <button onclick="window.location.href='/manager';">to homepage</button>
    </center>
  </body>
</html> )rawliteral";