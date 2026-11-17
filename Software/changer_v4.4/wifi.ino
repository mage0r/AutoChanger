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

// Retrying the configured network while in hotspot mode - see hotspot_retry_loop().
#define HOTSPOT_RETRY_MS 60000   // try the configured network this often
#define HOTSPOT_ATTEMPT_MS 15000 // give each try this long before going back to waiting
unsigned long hotspotRetryAt = 0; // when the last try started (or the hotspot came up)
bool hotspotAttempting = false;

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
    apply_sta_ip_config();
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

  ArduinoOTA.setHostname(hostname.c_str());

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

  MDNS.begin(hostname.c_str());
  weblog.print(F("Host: http://"));
  weblog.print(hostname);
  weblog.println(F(".local/"));

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
  if (!WiFi.softAP(hostname.c_str())) { // the unit name - "AutoChanger" unless changed
    // This used to hang here forever with a frozen screen - restart and try again.
    weblog.println(F("Soft AP creation failed - restarting."));
    delay(1000);
    ESP.restart();
  }

  weblog.println(F("Success!"));
  weblog.print(F("IP address: "));
  weblog.println(WiFi.softAPIP());

  start_network_services();

  wifi_connected = true;
  hotspotRetryAt = millis(); // first retry of the configured network a minute from now
  hotspotAttempting = false;
}

// Called once a second from loop(). If we fell back to the hotspot even though a
// network is configured - typically a power cut, where the router takes longer than
// our 30 seconds to come back - keep trying that network instead of staying a hotspot
// until someone reboots the unit. Tries only while nobody is connected to the hotspot:
// the station side joining a network can move the radio to another channel, which
// would drop anyone using the join page. Once it connects, the hotspot is shut down;
// the web server, OTA and mDNS were started on all interfaces and carry on as is.
void hotspot_retry_loop() {
  if(!wifi_enabled || !hotspot_mode() || ssid == PROJECT || prescanRunning) {
    hotspotAttempting = false;
    return;
  }

  if(WiFi.status() == WL_CONNECTED) {
    weblog.print(F("WiFi connected from hotspot mode. IP address: "));
    weblog.println(WiFi.localIP());
    hotspotAttempting = false;
    WiFi.softAPdisconnect(true); // AP off, station stays - captive_portal_loop() stops DNS
    WiFi.mode(WIFI_STA);
    return;
  }

  if(hotspotAttempting) {
    // someone joined the hotspot, or this try has had long enough
    if(WiFi.softAPgetStationNum() > 0 || millis() - hotspotRetryAt >= HOTSPOT_ATTEMPT_MS) {
      WiFi.disconnect(false); // station side only - the hotspot stays up
      hotspotAttempting = false;
      if(DEBUG)
        weblog.println(F("Hotspot mode: configured WiFi not reachable yet."));
    }
    return;
  }

  if(WiFi.softAPgetStationNum() == 0 && millis() - hotspotRetryAt >= HOTSPOT_RETRY_MS) {
    if(DEBUG) {
      weblog.print(F("Hotspot mode: retrying WiFi - "));
      weblog.println(ssid);
    }
    apply_sta_ip_config();
    WiFi.begin(ssid, wifi_password);
    hotspotRetryAt = millis();
    hotspotAttempting = true;
  }
}

// ---------------------------------------------------------------- unit name / static IP

// 1-32 letters, digits or hyphens, not starting or ending with a hyphen - a valid DNS
// label, so it works as name.local, as a DHCP hostname and as an SSID.
bool valid_hostname(const String &h) {
  if(h.length() < 1 || h.length() > 32 || h[0] == '-' || h[h.length() - 1] == '-')
    return false;
  for(unsigned int i = 0; i < h.length(); i++) {
    if(!isAlphaNumeric(h[i]) && h[i] != '-')
      return false;
  }
  return true;
}

// A mask is valid if, as a 32-bit number, it's all ones followed by all zeros.
bool valid_subnet_mask(const IPAddress &m) {
  uint32_t v = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
  if(v == 0)
    return false;
  uint32_t inv = ~v;
  return (inv & (inv + 1)) == 0;
}

// Validates and saves the WiFi tab's Network Settings (also the serial HOSTNAME and
// NET commands). Returns "" on success, else what's wrong - nothing is saved then.
// The caller restarts the unit (web) or asks for REBOOT (serial) to apply it.
String apply_network_settings(String name, bool useStatic, String ip, String gateway,
                              String subnet, String dns1, String dns2) {
  name.trim();
  ip.trim();
  gateway.trim();
  subnet.trim();
  dns1.trim();
  dns2.trim();
  if(!valid_hostname(name))
    return "The unit name must be 1-32 letters, numbers or hyphens, not starting or ending with a hyphen.";

  if(useStatic) {
    IPAddress a, g, m, d;
    if(!a.fromString(ip) || (uint32_t)a == 0)
      return "Invalid IP address.";
    if(!g.fromString(gateway) || (uint32_t)g == 0)
      return "Invalid gateway.";
    if(!m.fromString(subnet) || !valid_subnet_mask(m))
      return "Invalid subnet mask (e.g. 255.255.255.0).";
    if(((uint32_t)a & (uint32_t)m) != ((uint32_t)g & (uint32_t)m))
      return "The IP address and gateway aren't on the same subnet.";
    if(a == g)
      return "The IP address can't be the same as the gateway.";
    if(dns1.length() && !d.fromString(dns1))
      return "Invalid DNS server.";
    if(dns2.length() && !d.fromString(dns2))
      return "Invalid second DNS server.";
    static_addr = ip;
    static_gateway = gateway;
    static_subnet = subnet;
    static_dns1 = dns1;
    static_dns2 = dns2;
  }
  hostname = name;
  static_ip = useStatic;
  save_config(LittleFS, "/config.ini");
  weblog.print(F("Network settings saved: "));
  weblog.print(hostname);
  weblog.println(static_ip ? (" - static " + static_addr) : String(" - DHCP"));
  return "";
}

// Static address or DHCP for the station side - call before every WiFi.begin().
void apply_sta_ip_config() {
  IPAddress a, g, m, d1, d2;
  if(static_ip && a.fromString(static_addr) && g.fromString(static_gateway) &&
     m.fromString(static_subnet)) {
    if(!d1.fromString(static_dns1))
      d1 = g; // most home routers answer DNS themselves
    if(!d2.fromString(static_dns2))
      d2 = IPAddress(0, 0, 0, 0);
    WiFi.config(a, g, m, d1, d2);
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // DHCP
  }
}

// For the WiFi tab's fields: 0 ip, 1 gateway, 2 subnet, 3 dns1, 4 dns2. The saved
// static values if there are any, otherwise what DHCP gave us right now - so
// switching to static starts from addresses that are known to work.
String net_field(int which) {
  const String *saved[] = {&static_addr, &static_gateway, &static_subnet, &static_dns1, &static_dns2};
  if(which < 0 || which > 4)
    return "";
  if(static_ip || saved[0]->length() || WiFi.status() != WL_CONNECTED || hotspot_mode())
    return Tar::htmlEscape(*saved[which]);
  IPAddress v;
  switch(which) {
    case 0: v = WiFi.localIP(); break;
    case 1: v = WiFi.gatewayIP(); break;
    case 2: v = WiFi.subnetMask(); break;
    case 3: v = WiFi.dnsIP(0); break;
    default: v = WiFi.dnsIP(1); break;
  }
  return ((uint32_t)v == 0) ? String() : v.toString();
}

void ota_loop() {
  ArduinoOTA.handle();
}