/*
 * Online firmware update - the unit fetches a small JSON file describing the latest
 * release, and on request downloads and installs that firmware itself. The existing
 * /update page (upload a .bin from the browser) and ArduinoOTA still work as before.
 *
 * The info file lives at update_url (config.ini; default UPDATE_URL_DEFAULT in
 * webserver.h), e.g.
 *
 *   {
 *     "version": "V.4.4.1",
 *     "url": "changer_v4.4.1.bin",
 *     "md5": "0123456789abcdef0123456789abcdef",
 *     "notes": "Fixes hotspot fallback after a power cut."
 *   }
 *
 *   version - compared with VERSION as three numbers (V.4.4.1 > V.4.4.0); only a
 *             higher version is offered for install.
 *   url     - the .bin (Sketch > Export Compiled Binary). Absolute, or relative to the
 *             info file's own address ("changer_v4.4.1.bin", "/firmware/x.bin").
 *   size    - optional: the .bin's size in bytes (a number, not a string). Only used to
 *             show a percentage when the server sends no Content-Length (chunked).
 *   md5     - optional but recommended: lowercase hex MD5 of the .bin, checked before
 *             the new firmware is switched to. (The ESP32 also checks the image's own
 *             checksum, so a truncated or corrupt download is refused either way.)
 *   notes   - optional, shown on the web page.
 *
 * Plain http:// only. HTTPClient + TLS would add ~180KB and take the sketch from 81% to
 * 94% of the app partition, which can't be enlarged on units already out there. The
 * trade-off: the MD5 and image checks catch a damaged download, but not a deliberately
 * spoofed server on the unit's own network. Both files must be reachable over plain
 * http - redirects are followed, but not to https:// (e.g. Cloudflare's "Always Use
 * HTTPS" needs an exception for them).
 *
 * With update_auto on (the default) the unit also checks by itself - a minute after
 * joining a network, then daily - and the main page shows a banner when there's
 * something newer. Installing only ever happens when someone asks.
 *
 * Checking and installing both run from loop() - queued by the web UI through
 * WebActions, or run directly by the serial UPDATE_CHECK / UPDATE commands. While an
 * install downloads (around a minute) loop() is blocked: buttons, screen and the
 * carriage sensor stop until the unit restarts on the new firmware. The web server
 * keeps answering, so the page can show progress. After a successful install the
 * built-in web pages are rewritten from the new firmware on the next boot (see
 * update_boot()), so nobody has to press the "out of date" Fix button.
 */

#define UPDATE_PAGES_MARKER "/update_pages" // written after an install, handled at boot
#define UPDATE_INFO_MAX 4096                // the info file is tiny - refuse anything silly
#define UPDATE_MAX_REDIRECTS 3

// What the web page shows. Written from loop() (and the Update progress callback,
// also on loop()), read by /update_status on the async_tcp task - hence the lock.
struct UpdateInfo {
  String state = "idle"; // idle, checking, uptodate, available, downloading, done, error
  String latest;         // version from the info file
  String notes;
  String error;
  String url;            // resolved .bin address
  String md5;
  int progress = 0;      // 0-100 while downloading, -1 if the size isn't known
  size_t received = 0;   // bytes downloaded so far
  long size = 0;         // "size" from the info file, 0 if not given
};
UpdateInfo updInfo;

class UpdLock {
  static SemaphoreHandle_t m() {
    static SemaphoreHandle_t h = xSemaphoreCreateMutex();
    return h;
  }
public:
  UpdLock() { xSemaphoreTake(m(), portMAX_DELAY); }
  ~UpdLock() { xSemaphoreGive(m()); }
};

// "V.4.4.1" / "4.4.1" -> 4004001, for comparing. Missing parts count as 0.
uint32_t update_version_number(const String &v) {
  uint32_t parts[3] = {0, 0, 0};
  int idx = 0;
  bool inNum = false;
  for(unsigned int i = 0; i < v.length() && idx < 3; i++) {
    char c = v[i];
    if(isDigit(c)) {
      if(parts[idx] < 1000)
        parts[idx] = parts[idx] * 10 + (c - '0');
      inNum = true;
    } else if(inNum) {
      idx++;
      inNum = false;
    }
  }
  return parts[0] * 1000000UL + parts[1] * 1000UL + parts[2];
}

// The string value of "key" in a flat JSON object - enough for the info file, without
// pulling in a JSON library. Returns "" if it's missing or not a string.
String update_json_field(const String &json, const char *key) {
  String pat = String("\"") + key + "\"";
  int k = json.indexOf(pat);
  if(k < 0)
    return "";
  int i = k + pat.length();
  while(i < (int)json.length() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
    i++;
  if(i >= (int)json.length() || json[i] != ':')
    return "";
  i++;
  while(i < (int)json.length() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
    i++;
  if(i >= (int)json.length() || json[i] != '"')
    return "";
  String out;
  for(i++; i < (int)json.length(); i++) {
    char c = json[i];
    if(c == '\\' && i + 1 < (int)json.length()) {
      c = json[++i];
      if(c == 'n')
        c = '\n';
    } else if(c == '"') {
      return out;
    }
    out += c;
  }
  return ""; // unterminated
}

// The number value of "key" (unquoted, e.g. "size": 1088393), or 0 if missing.
long update_json_number(const String &json, const char *key) {
  String pat = String("\"") + key + "\"";
  int k = json.indexOf(pat);
  if(k < 0)
    return 0;
  int colon = json.indexOf(':', k + pat.length());
  if(colon < 0)
    return 0;
  long v = atol(json.c_str() + colon + 1); // skips leading spaces, stops at , or }
  return v > 0 ? v : 0;
}

// Resolves the info file's "url" against the info file's own address.
String update_resolve_url(const String &base, const String &rel) {
  if(rel.indexOf("://") > 0)
    return rel;
  int schemeEnd = base.indexOf("://");
  int hostEnd = base.indexOf('/', schemeEnd < 0 ? 0 : schemeEnd + 3);
  if(hostEnd < 0)
    hostEnd = base.length();
  if(rel.startsWith("/"))
    return base.substring(0, hostEnd) + rel;
  int lastSlash = base.lastIndexOf('/');
  if(lastSlash < hostEnd)
    return base.substring(0, hostEnd) + "/" + rel;
  return base.substring(0, lastSlash + 1) + rel;
}

void update_fail(const String &why) {
  {
    UpdLock lock;
    updInfo.state = "error";
    updInfo.error = why;
    updInfo.progress = 0;
  }
  weblog.print(F("Update failed: "));
  weblog.println(why);
}

// update_fail(), unless quiet: then just back to whatever was known before the check.
void update_fail_q(bool quiet, const String &why) {
  if(!quiet) {
    update_fail(why);
    return;
  }
  {
    UpdLock lock;
    bool known = updInfo.url.length() && update_version_number(updInfo.latest) > update_version_number(VERSION);
    updInfo.state = known ? "available" : "idle";
  }
  if(DEBUG) {
    weblog.print(F("Update: automatic check failed - "));
    weblog.println(why);
  }
}

// The version on offer and its notes, if the last check found something newer.
// Used for the banner at the top of the main page (see sendIndexPage() in pages.ino).
bool update_available(String &latest, String &notes) {
  UpdLock lock;
  if(updInfo.state != "available")
    return false;
  latest = updInfo.latest;
  notes = updInfo.notes;
  return true;
}

// Called by the web handlers before queueing a check/install, so the page sees
// "checking"/"downloading" straight away. Returns "" if OK, else why not.
String update_begin(bool install) {
  UpdLock lock;
  if(updInfo.state == "checking" || updInfo.state == "downloading" || updInfo.state == "done")
    return "an update check or install is already running";
  if(install && updInfo.state != "available")
    return "no update to install - check for one first";
  updInfo.state = install ? "downloading" : "checking";
  updInfo.error = "";
  updInfo.progress = 0;
  return "";
}

String update_status_json() {
  UpdLock lock;
  return "{\"state\":\"" + updInfo.state + "\",\"current\":\"" VERSION "\",\"latest\":\"" +
         jsonEscape(updInfo.latest) + "\",\"notes\":\"" + jsonEscape(updInfo.notes) +
         "\",\"error\":\"" + jsonEscape(updInfo.error) + "\",\"progress\":" +
         String(updInfo.progress) + ",\"received\":" + String(updInfo.received) + "}";
}

// One line for the serial UPDATE_CHECK command.
String update_summary() {
  UpdLock lock;
  String s = "Installed: " VERSION ", latest: " + (updInfo.latest.length() ? updInfo.latest : String("unknown"));
  if(updInfo.state == "available")
    s += " - update available, send UPDATE to install";
  else if(updInfo.state == "uptodate")
    s += " - up to date";
  else if(updInfo.state == "error")
    s += " - " + updInfo.error;
  return s;
}

bool update_state_is(const char *state) {
  UpdLock lock;
  return updInfo.state == state;
}

// Splits http://host[:port]/path. Returns false (with err) for anything else.
bool update_parse_url(const String &url, String &host, uint16_t &port, String &path, String &err) {
  if(url.startsWith("https://")) {
    err = "https:// isn't supported - use an http:// address";
    return false;
  }
  if(!url.startsWith("http://")) {
    err = "address must start with http://";
    return false;
  }
  String rest = url.substring(7);
  int slash = rest.indexOf('/');
  String hostport = (slash < 0) ? rest : rest.substring(0, slash);
  path = (slash < 0) ? String("/") : rest.substring(slash);
  int colon = hostport.indexOf(':');
  port = 80;
  host = hostport;
  if(colon >= 0) {
    host = hostport.substring(0, colon);
    long p = hostport.substring(colon + 1).toInt();
    if(p < 1 || p > 65535) {
      err = "invalid port in " + url;
      return false;
    }
    port = p;
  }
  if(host.length() == 0) {
    err = "invalid address: " + url;
    return false;
  }
  return true;
}

// GETs url, following redirects, and reads the response headers. On success the
// client is positioned at the start of a 200 response's body; length is its
// Content-Length (-1 if none given) and chunked says whether it's chunked. On failure
// err says why and the client is closed.
bool update_get(WiFiClient &client, String url, long &length, bool &chunked, String &err) {
  for(int hop = 0; hop <= UPDATE_MAX_REDIRECTS; hop++) {
    String host, path;
    uint16_t port;
    if(!update_parse_url(url, host, port, path, err))
      return false;

    client.stop();
    if(!client.connect(host.c_str(), port, 10000)) {
      err = "couldn't connect to " + host;
      return false;
    }
    client.setTimeout(15); // seconds on WiFiClient - also covers readBytes()/readStringUntil()
    client.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
                 "\r\nUser-Agent: " PROJECT "/" VERSION "\r\nConnection: close\r\n\r\n");

    String status = client.readStringUntil('\n');
    status.trim();
    int sp = status.indexOf(' ');
    int code = (status.startsWith("HTTP/") && sp > 0) ? status.substring(sp + 1).toInt() : 0;
    if(code == 0) {
      client.stop();
      err = "no valid answer from " + host;
      return false;
    }

    length = -1;
    chunked = false;
    String location;
    while(true) {
      String line = client.readStringUntil('\n');
      line.trim(); // drops the \r too
      if(line.length() == 0)
        break; // end of headers (or the connection went quiet)
      int c = line.indexOf(':');
      if(c < 0)
        continue;
      String name = line.substring(0, c);
      name.trim();
      name.toLowerCase();
      String value = line.substring(c + 1);
      value.trim();
      if(name == "content-length") {
        length = value.toInt();
      } else if(name == "transfer-encoding") {
        value.toLowerCase();
        chunked = value.indexOf("chunked") >= 0;
      } else if(name == "location") {
        location = value;
      }
    }

    if(code == 200)
      return true;
    client.stop();

    if((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && location.length()) {
      if(location.startsWith("https://")) {
        err = host + " redirects to https:// - the update files must be served over plain http";
        return false;
      }
      url = update_resolve_url(url, location);
      continue;
    }
    err = "server answered HTTP " + String(code) + " for " + url;
    return false;
  }
  err = "too many redirects";
  return false;
}

// Reads a small response body (the info file) into a String, whichever way it's sent.
bool update_read_body(WiFiClient &client, long length, bool chunked, String &body, String &err) {
  char buf[128];
  body = "";
  if(chunked) {
    while(true) {
      String sizeLine = client.readStringUntil('\n');
      sizeLine.trim();
      long n = strtol(sizeLine.c_str(), NULL, 16);
      if(n <= 0)
        return true; // last chunk
      if((long)body.length() + n > UPDATE_INFO_MAX) {
        err = "update info file is too big";
        return false;
      }
      while(n > 0) {
        size_t got = client.readBytes(buf, min((long)sizeof(buf), n));
        if(got == 0) {
          err = "timed out reading the update info file";
          return false;
        }
        for(size_t i = 0; i < got; i++)
          body += buf[i];
        n -= got;
      }
      client.readStringUntil('\n'); // the CRLF after each chunk
    }
  }

  if(length > UPDATE_INFO_MAX) {
    err = "update info file is too big";
    return false;
  }
  while(length < 0 || (long)body.length() < length) {
    size_t want = sizeof(buf);
    if(length >= 0 && (long)want > length - (long)body.length())
      want = length - body.length();
    size_t got = client.readBytes(buf, want);
    if(got == 0) {
      if(length < 0)
        return true; // no Content-Length: the body ends when the server closes
      err = "timed out reading the update info file";
      return false;
    }
    for(size_t i = 0; i < got; i++)
      body += buf[i];
    if((long)body.length() > UPDATE_INFO_MAX) {
      err = "update info file is too big";
      return false;
    }
  }
  return true;
}

// Fetches the info file and decides whether there's anything newer. quiet (the
// automatic daily check) keeps failures out of the web page and the log - nobody
// asked, so "couldn't connect" would only confuse. Runs on loop(), or on its own task
// for the automatic check (see update_auto_loop()).
void update_check_run(bool quiet) {
  if(WiFi.status() != WL_CONNECTED) {
    update_fail_q(quiet, "not connected to a WiFi network with internet access");
    return;
  }
  {
    UpdLock lock;
    updInfo.state = "checking";
  }
  if(!quiet || DEBUG) {
    weblog.print(F("Update: checking "));
    weblog.println(update_url);
  }

  WiFiClient client;
  long length;
  bool chunked;
  String err, body;
  if(!update_get(client, update_url, length, chunked, err)) {
    update_fail_q(quiet, err);
    return;
  }
  bool ok = update_read_body(client, length, chunked, body, err);
  client.stop();
  if(!ok) {
    update_fail_q(quiet, err);
    return;
  }

  String ver = update_json_field(body, "version");
  String bin = update_json_field(body, "url");
  String md5 = update_json_field(body, "md5");
  String notes = update_json_field(body, "notes");
  long size = update_json_number(body, "size");
  md5.trim();
  md5.toLowerCase();
  if(!ver.length() || !bin.length()) {
    update_fail_q(quiet, "update info file has no \"version\" or \"url\"");
    return;
  }
  if(md5.length() && md5.length() != 32) {
    update_fail_q(quiet, "\"md5\" in the update info file must be 32 hex characters");
    return;
  }

  bool newer = update_version_number(ver) > update_version_number(VERSION);
  {
    UpdLock lock;
    updInfo.latest = ver;
    updInfo.notes = notes;
    updInfo.url = update_resolve_url(update_url, bin);
    updInfo.md5 = md5;
    updInfo.size = size;
    updInfo.state = newer ? "available" : "uptodate";
  }
  if(!quiet || DEBUG || newer) {
    weblog.print(F("Update: latest is "));
    weblog.print(ver);
    weblog.println(newer ? F(" - available to install") : F(" - up to date"));
  }
}

void update_check() {
  update_check_run(false);
}



// Reads the next piece of a response body into buf, whichever way the server sends
// it: Content-Length, chunked (what Cloudflare often does), or neither (ends when the
// server closes). left/finished carry the state between calls - start with left = 0
// for chunked, the Content-Length if known, else -1. Returns the bytes read, 0 at the
// end of the body, or -1 if it stalled or broke off.
int update_read_block(WiFiClient &client, bool chunked, long &left, bool &finished, uint8_t *buf, size_t size) {
  if(finished)
    return 0;
  if(chunked && left == 0) {
    String line = client.readStringUntil('\n');
    line.trim();
    if(line.length() == 0) { // the CRLF that ends the previous chunk
      line = client.readStringUntil('\n');
      line.trim();
    }
    if(line.length() == 0)
      return -1; // timed out
    long n = strtol(line.c_str(), NULL, 16); // stops at any ";extension"
    if(n <= 0) {
      finished = true; // the zero-length last chunk
      return 0;
    }
    left = n;
  }
  size_t want = size;
  if(left >= 0 && (long)want > left)
    want = left;
  size_t got = client.readBytes(buf, want);
  if(got == 0) {
    if(!chunked && left < 0 && !client.connected()) {
      finished = true; // no length given: the server closing is the end
      return 0;
    }
    return -1;
  }
  if(left >= 0)
    left -= got;
  if(!chunked && left == 0)
    finished = true;
  return got;
}

// Downloads url and installs it, then restarts. md5 may be "". sizeHint (0 if
// unknown) is the "size" from the info file - only used for the progress figure when
// the server doesn't send a Content-Length. Runs on loop().
void update_flash(const String &url, const String &md5, long sizeHint) {
  if(WiFi.status() != WL_CONNECTED) {
    update_fail("not connected to a WiFi network with internet access");
    return;
  }
  {
    UpdLock lock;
    updInfo.state = "downloading";
    updInfo.progress = 0;
    updInfo.received = 0;
    updInfo.error = "";
  }
  weblog.print(F("Update: downloading "));
  weblog.println(url);
  display_message("UPDATING"); // loop() - and so display_page() - is blocked until we're done

  // loop() won't run detachServo() during the download - let the arms go limp now
  // rather than holding whichever one just moved for the whole install.
  for(int x = 0; x < maxServo; x++)
    pwm.setPWM(x, 0, 0);
  servoTimeout = 0;

  WiFiClient client;
  long len;
  bool chunked;
  String err;
  if(!update_get(client, url, len, chunked, err)) {
    update_fail(err);
    return;
  }

  bool known = !chunked && len > 0;
  long expected = known ? len : sizeHint; // for the progress figure only
  if(known && (size_t)len > ESP.getFreeSketchSpace()) {
    client.stop();
    update_fail("firmware is " + String(len) + " bytes - too big for this unit");
    return;
  }
  // Without a Content-Length, Update writes until the partition is full and end(true)
  // takes whatever arrived - the image checksum (and MD5) still catch a short download.
  if(!Update.begin(known ? (size_t)len : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    client.stop();
    update_fail(String("couldn't start: ") + Update.errorString());
    return;
  }
  if(md5.length() && !Update.setMD5(md5.c_str())) {
    Update.abort();
    client.stop();
    update_fail("invalid md5");
    return;
  }

  const size_t BUF_SIZE = 4096;
  uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
  if(!buf) {
    Update.abort();
    client.stop();
    update_fail("out of memory");
    return;
  }

  long left = chunked ? 0 : (len > 0 ? len : -1);
  bool finished = false;
  size_t total = 0;
  while(true) {
    int n = update_read_block(client, chunked, left, finished, buf, BUF_SIZE);
    if(n == 0)
      break;
    if(n < 0) {
      err = "download stopped after " + String(total) + " bytes";
      break;
    }
    if(Update.write(buf, n) != (size_t)n) {
      err = (total == 0) ? String("not a valid firmware file (") + Update.errorString() + ")"
                         : String("couldn't write: ") + Update.errorString();
      break;
    }
    total += n;
    UpdLock lock;
    updInfo.received = total;
    updInfo.progress = (expected > 0) ? (int)min((uint64_t)99, (uint64_t)total * 100 / expected) : -1;
  }
  free(buf);
  client.stop();

  if(!err.length() && known && total != (size_t)len)
    err = "download stopped after " + String(total) + " of " + String(len) + " bytes";
  if(!err.length() && total == 0)
    err = "the firmware file was empty";
  if(err.length()) {
    Update.abort();
    update_fail(err);
    return;
  }
  if(!Update.end(true)) {
    update_fail(String("couldn't finish: ") + Update.errorString()); // includes an MD5 mismatch
    return;
  }

  SafeFile::write(LittleFS, UPDATE_PAGES_MARKER, String("1")); // refresh pages on next boot
  {
    UpdLock lock;
    updInfo.state = "done";
    updInfo.progress = 100;
  }
  weblog.print(F("Update: installed "));
  weblog.print(total);
  weblog.println(F(" bytes - restarting."));
  delay(2000); // give the web page a poll or two to see "done"
  rebooting = true; // loop() restarts
}

// Installs whatever the last check found. Runs on loop().
void update_install_latest() {
  String url, md5;
  long size;
  {
    UpdLock lock;
    if(updInfo.url.length() == 0) {
      updInfo.state = "error";
      updInfo.error = "no update to install - check for one first";
      return;
    }
    url = updInfo.url;
    md5 = updInfo.md5;
    size = updInfo.size;
  }
  update_flash(url, md5, size);
}

// ---------------------------------------------------------------- automatic check

#define UPDATE_AUTO_FIRST_MS 60000UL     // first check a minute after joining a network
#define UPDATE_AUTO_EVERY_MS 86400000UL  // then once a day

void update_auto_task(void *param) {
  update_check_run(true);
  vTaskDelete(NULL);
}

// Called once a second from loop(). When update_auto is on (config.ini, or the
// UPDATE_AUTO command) and we're on a real network, checks for new firmware a minute
// after connecting and then once a day, so the main page can show a banner. The check
// runs on its own task: it can take several seconds (longer if the server is
// unreachable), and blocking loop() that long mid-knit would leave the arms late.
void update_auto_loop() {
  static unsigned long connectedAt = 0;
  static unsigned long lastCheck = 0;
  static bool checkedOnce = false;

  if(!update_auto || WiFi.status() != WL_CONNECTED || hotspot_mode()) {
    connectedAt = 0;
    return;
  }
  unsigned long now = millis();
  if(connectedAt == 0)
    connectedAt = now ? now : 1;

  bool due = checkedOnce ? (now - lastCheck >= UPDATE_AUTO_EVERY_MS)
                         : (now - connectedAt >= UPDATE_AUTO_FIRST_MS);
  if(!due)
    return;
  checkedOnce = true;
  lastCheck = now;

  if(update_begin(false).length())
    return; // a check or install is already running
  if(xTaskCreate(update_auto_task, "update_check", 8192, NULL, 1, NULL) != pdPASS) {
    UpdLock lock;
    updInfo.state = "idle";
  }
}

// Called from setup() once LittleFS is mounted: after an online update, rewrite the
// built-in web pages from the new firmware (see pages.ino).
void update_boot() {
  if(!LittleFS.exists(UPDATE_PAGES_MARKER))
    return;
  weblog.println(F("Update: first boot after an online update - refreshing web pages."));
  update_default_pages();
  LittleFS.remove(UPDATE_PAGES_MARKER);
}
