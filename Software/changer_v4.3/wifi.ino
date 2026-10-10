#include <DNSServer.h>

// Captive portal: while we're our own access point, answer every DNS lookup with our
// own address. Phones and laptops check for internet by fetching a known page right
// after joining a network; with every name pointing here, that check lands on our web
// server, gets redirected to the join page (see notFound() in webserver.ino), and the
// device pops it up on its own - no address to type.
DNSServer dnsServer;
bool dnsRunning = false;

// Non-blocking WiFi connect state - see setup_wifi().
unsigned long wifiBeginAt = 0;  // when WiFi.begin() was last issued; 0 = not yet in this window
#define WIFI_RETRY_MS 10000      // re-issue WiFi.begin() this often while it's failing

// True while we're running our own hotspot. Tests the AP bit rather than comparing
// with WIFI_AP: the hotspot runs as WIFI_AP_STA (see setup_AP()), so a "== WIFI_AP"
// test would say we weren't - which is what sent a scan's next page load to the
// status page instead of the join page.
bool hotspot_mode() {
  return (WiFi.getMode() & WIFI_AP) != 0;
}

// Called every loop(). Starts/stops the DNS responder to follow the radio, so it's
// right however hotspot mode was entered or left.
void captive_portal_loop() {
  bool apActive = hotspot_mode();
  if(apActive && !dnsRunning) {
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsRunning = dnsServer.start(53, "*", WiFi.softAPIP());
    if(!dnsRunning)
      weblog.println(F("Captive portal DNS FAILED to start.")); // always - it's an error
    else if(DEBUG)
      weblog.println(F("Captive portal DNS started."));
  } else if(!apActive && dnsRunning) {
    dnsServer.stop();
    dnsRunning = false;
    if(DEBUG)
      weblog.println(F("Captive portal DNS stopped."));
  }
  if(dnsRunning)
    dnsServer.processNextRequest();
}

// Sets new WiFi credentials, saves them, and (if WiFi is enabled) forces a fresh
// connection attempt rather than waiting on whatever was already happening.
// Shared by the serial WIFI command and the web /join page, so both behave
// identically and there's only one place that does this.
void applyWifiCredentials(String newSsid, String newPassword) {
  ssid = newSsid;
  wifi_password = newPassword;
  save_config(LittleFS, "/config.ini");

  if(wifi_enabled) {
    // Force a reconnect with the new credentials rather than waiting on the old one.
    // WiFi.disconnect() also drops the AP if one is currently running - setup_wifi()
    // will switch the radio to WIFI_STA on its next 1-second pass.
    WiFi.disconnect(true);
    wifi_connected = false;
    wifi_counter = millis(); // fresh 30s window to connect before falling back to AP mode
    wifiBeginAt = 0;         // ...starting with a fresh WiFi.begin()
  }
}

// Turns WiFi on or off, saves the setting, and handles the radio state either way -
// used by the serial WIFI_ENABLE command, the OLED's WiFi page, and the web UI's
// Disable WiFi button, so all three agree on exactly what "off" means rather than
// each reimplementing it slightly differently.
void setWifiEnabled(boolean enabled) {
  wifi_enabled = enabled;
  if(wifi_enabled) {
    wifi_counter = millis(); // fresh 30s window to connect before falling back to AP mode
    wifiBeginAt = 0;
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_connected = false;
  }
  save_config(LittleFS, "/config.ini");
}

// Non-blocking WiFi connect. Called once a second from loop() during the 30s window
// after boot (or a credentials change): the first call starts the connection, later
// calls just check on it. It used to wait inside WiFi.waitForConnectResult(), which
// froze loop() - buttons, display, carriage sensor - for up to a minute per attempt
// when the network wasn't there.

void setup_wifi()
{
  if(ssid == PROJECT) {
    // We're not configured to connect anywhere else - scan, then start the hotspot.
    start_hotspot();
    return;
  }

  if(WiFi.status() == WL_CONNECTED) {
    weblog.print(F("WiFi connected. IP address: "));
    weblog.println(WiFi.localIP());
    start_network_services();
    wifi_connected = true;
    return;
  }

  if(wifiBeginAt == 0 || millis() - wifiBeginAt >= WIFI_RETRY_MS) {
    if(wifiBeginAt == 0) {
      weblog.print(F("Connecting to WiFi - "));
      weblog.println(ssid);
    } else if(DEBUG) {
      weblog.println(F("WiFi not connected yet - retrying."));
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, wifi_password);
    wifiBeginAt = millis();
    if(wifiBeginAt == 0)
      wifiBeginAt = 1; // 0 means "not started"
  }
}

// OTA, mDNS and the web server - set up once, the first time we're on a network
// (ours or someone else's). Running setupAsyncServer() again on every reconnect used
// to register every route a second time.
void start_network_services() {
  static bool started = false;
  if(started)
    return;
  started = true;

  ArduinoOTA.setHostname(PROJECT);

  // Same password as the web interface (http_password, from config.ini), so the
  // Arduino IDE asks for it before a network upload. /password updates it too.
  ArduinoOTA.setPassword(http_password.c_str());

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS - the constant keeps its old name for any filesystem image
        type = "filesystem";

      // NOTE: if updating the filesystem this would be the place to unmount it using LittleFS.end()
      weblog.print(F("Start updating "));
      weblog.println(type);
    })
    .onEnd([]() {
      weblog.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) weblog.println(F("Auth Failed"));
      else if (error == OTA_BEGIN_ERROR) weblog.println(F("Begin Failed"));
      else if (error == OTA_CONNECT_ERROR) weblog.println(F("Connect Failed"));
      else if (error == OTA_RECEIVE_ERROR) weblog.println(F("Receive Failed"));
      else if (error == OTA_END_ERROR) weblog.println(F("End Failed"));
    });

  ArduinoOTA.begin();

  MDNS.begin(PROJECT);
  weblog.println(F("Host: http://" PROJECT ".local/"));

  setupAsyncServer();
}

// Scans for networks first, then starts the hotspot, so the join page can list them
// the moment it opens (see prescanJson in webserver.ino). The scan runs in station
// mode, before the hotspot exists, so it can't disturb anyone connected to it. Called
// once a second from loop() until the hotspot is up; never blocks.
bool prescanRunning = false;
unsigned long prescanStart = 0;
#define PRESCAN_TIMEOUT_MS 15000

void start_hotspot() {
  if(!prescanRunning) {
    weblog.println(F("Scanning for networks before starting the hotspot..."));
    WiFi.disconnect();   // stop any connection attempt from setup_wifi()
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
    prescanRunning = true;
    prescanStart = millis();
    return;
  }

  int n = WiFi.scanComplete();
  unsigned long elapsed = millis() - prescanStart;
  // still going (it can briefly read FAILED just after starting - see wifiScanStarted)
  if((n == WIFI_SCAN_RUNNING || (n == WIFI_SCAN_FAILED && elapsed < 3000)) && elapsed < PRESCAN_TIMEOUT_MS)
    return;

  if(n >= 0) {
    prescanJson = build_scan_json(n);
    weblog.print(F("Found "));
    weblog.print(n);
    weblog.println(F(" networks."));
  } else {
    WiFi.scanDelete();
  }
  prescanRunning = false;
  setup_AP();
}

void setup_AP() {
  weblog.print(F("Configuring access point..."));

  // Hotspot address 4.3.2.1 rather than the usual 192.168.4.1: Android only treats a
  // network as a sign-in (captive portal) network if its connectivity check resolves
  // to a public-looking address - with a private one it just reports "no internet"
  // and never opens the join page. Nothing else on the hotspot cares which it is.
  // AP+STA rather than AP alone, with the station side idle: the join page's "Scan for
  // networks" needs the station side, and WiFi.scanNetworks() switches it on itself if
  // it's off - that mode change restarted the hotspot and dropped whoever was on it.
  WiFi.disconnect();       // stop any connection attempt still running from setup_wifi()
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(4, 3, 2, 1), IPAddress(4, 3, 2, 1), IPAddress(255, 255, 255, 0));

  // Open network (no password) - it only ever serves the join page.
  if (!WiFi.softAP(PROJECT)) {
    weblog.println(F("Soft AP creation failed."));
    while(1);
  }

  weblog.println(F("Success!"));
  weblog.print(F("IP address: "));
  weblog.println(WiFi.softAPIP());

  start_network_services();

  wifi_connected = true;
}

void ota_loop() {
  ArduinoOTA.handle();
}