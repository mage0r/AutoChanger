// Keeps the web pages stored on SPIFFS in step with the ones built into the firmware.
//
// The pages are written to SPIFFS once and served from there, so a firmware update
// alone never changes them. When /index.html on SPIFFS differs from the firmware's
// copy, "/" is served with a banner injected at the top of <body>, offering a Fix
// button (POST /fix_pages). The banner is added by the server as the page streams
// out rather than being part of the page itself, so it still appears on an old
// index.html that has never heard of it.
//
// Fix rewrites every built-in page (defaultPagePaths[] in webserver.h) from the
// firmware. It's done from loop() via pagesFixPending rather than in the web
// handler, since writing ~100KB to SPIFFS on the async_tcp task risks its watchdog.
//
// Note a page edited by hand in /manage also counts as "different" - Fix replaces
// it, and the confirm dialog says so.

// True if /index.html is missing or differs from the firmware's built-in copy.
bool indexOutdated() {
  File f = SPIFFS.open("/index.html", "r");
  if(!f || f.isDirectory())
    return true;

  size_t len = strlen(index_html);
  if(f.size() != len) {
    f.close();
    return true;
  }

  uint8_t buf[512];
  size_t pos = 0;
  bool differs = false;
  while(pos < len) {
    size_t n = f.read(buf, min(sizeof(buf), len - pos));
    if(n == 0 || memcmp(buf, index_html + pos, n) != 0) {
      differs = true;
      break;
    }
    pos += n;
  }
  f.close();
  return differs;
}

// Streams /index.html with the banner spliced in just after the opening <body> tag.
// Wrapped in a class so the Arduino IDE's prototype generation leaves it alone.
class BannerPageStream {
  File file;
  String banner;
  size_t insertAt = 0; // file offset just after "<body ...>"
  size_t filePos = 0;
  size_t bannerPos = 0;

  // Finds the end of the opening <body> tag, case-insensitively. Only looks after
  // </head>, since the page's own scripts mention "<body" in comments. Falls back to
  // the very start of the file if there isn't one.
  size_t findBodyEnd() {
    const char *targets[] = {"</head>", "<body"};
    byte stage = 0;   // 0: looking for </head>, 1: for <body, 2: for the tag's '>'
    byte matched = 0;
    size_t offset = 0;
    uint8_t buf[256];
    size_t n;
    while((n = file.read(buf, sizeof(buf))) > 0) {
      for(size_t i = 0; i < n; i++, offset++) {
        char c = tolower(buf[i]);
        if(stage == 2) {
          if(c == '>')
            return offset + 1;
          continue;
        }
        const char *t = targets[stage];
        if(c == t[matched]) {
          if(t[++matched] == '\0') {
            stage++;
            matched = 0;
          }
        } else {
          matched = (c == '<') ? 1 : 0;
        }
      }
    }
    return 0;
  }

public:
  BannerPageStream(const String &bannerHtml) : banner(bannerHtml) {
    file = SPIFFS.open("/index.html", "r");
    if(file) {
      insertAt = findBodyEnd();
      file.seek(0);
    }
  }

  ~BannerPageStream() {
    if(file)
      file.close();
  }

  size_t fill(uint8_t *buf, size_t maxLen) {
    size_t out = 0;
    while(out < maxLen) {
      if(filePos < insertAt) {
        // the part of the page before the banner
        size_t n = file.read(buf + out, min(maxLen - out, insertAt - filePos));
        if(n == 0)
          break;
        filePos += n;
        out += n;
      } else if(bannerPos < banner.length()) {
        size_t n = min(maxLen - out, banner.length() - bannerPos);
        memcpy(buf + out, banner.c_str() + bannerPos, n);
        bannerPos += n;
        out += n;
      } else {
        // the rest of the page
        size_t n = file ? file.read(buf + out, maxLen - out) : 0;
        if(n == 0)
          break;
        filePos += n;
        out += n;
      }
    }
    return out; // 0 ends the (chunked) response
  }
};

// Must not contain a percent sign - the page still goes through the template processor.
String pageUpdateBanner() {
  String s = F("<div id=\"pageUpdateBanner\" style=\"background:#fff3cd;border:1px solid #e0b84f;"
               "color:#5c4400;padding:10px 14px;margin:0 0 10px;max-width:500px;box-sizing:border-box;"
               "font-family:Arial,sans-serif;font-size:14px\">"
               "<b>The web pages on this device are out of date.</b> Firmware ");
  s += VERSION;
  s += F(" includes newer versions.<form method=\"POST\" action=\"/fix_pages\" style=\"margin:8px 0 0\" "
         "onsubmit=\"return confirm('Replace the web pages on the device with the versions built into "
         "the firmware? Any changes you have made to them will be lost. Your settings, patterns and "
         "arm positions are not affected.')\">"
         "<input type=\"submit\" value=\"Fix\" style=\"padding:6px 16px;cursor:pointer\"></form></div>");
  return s;
}

// Serves the main page (for both "/" and "/index.html"), with the update banner when
// the stored page is out of date. no-store stops the browser showing a cached copy
// from before a firmware update - the page is tiny to regenerate anyway.
void sendIndexPage(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response;
  if(!indexOutdated()) {
    response = request->beginResponse(SPIFFS, "/index.html", "text/html", false, processor);
  } else {
    auto stream = std::make_shared<BannerPageStream>(pageUpdateBanner());
    // length 0 + a template processor = chunked response, ended when fill() returns 0
    response = request->beginResponse("text/html", 0,
      [stream](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        return stream->fill(buffer, maxLen);
      }, processor);
  }
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

// Small standalone result page (password change etc). msg is escaped.
String messagePage(const String &title, const String &msg, bool ok) {
  String s = F("<!DOCTYPE HTML><html><head><meta charset=\"UTF-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<title>" PROJECT "</title>"
               "<style>body{background:#f7f7f7;font-family:Arial,sans-serif;padding:20px}</style>"
               "</head><body><h2>");
  s += Tar::htmlEscape(title);
  s += F("</h2><p>");
  s += Tar::htmlEscape(msg);
  s += F("</p><p><a href=\"/#status\">");
  s += ok ? F("Continue") : F("Back");
  s += F("</a></p></body></html>");
  return s;
}

// One line in the log at boot saying whether the stored pages match the firmware.
void log_page_status() {
  weblog.println(indexOutdated()
    ? F("Web pages: differ from firmware - use Fix on the main page to update.")
    : F("Web pages: match firmware."));
}

// Rewrites every built-in page from the firmware. Each is written to a temp file and
// renamed into place, so a power cut mid-way leaves the old page rather than half a
// new one. Called from loop().
void update_default_pages() {
  byte updated = 0;
  for(byte i = 0; i < DEFAULT_PAGE_COUNT; i++) {
    const char *path = defaultPagePaths[i];
    const char *html = defaultPageContent[i];
    size_t len = strlen(html);

    File f = SPIFFS.open("/~page.tmp", FILE_WRITE);
    size_t written = f ? f.write((const uint8_t *)html, len) : 0;
    if(f)
      f.close();
    if(written != len) {
      SPIFFS.remove("/~page.tmp");
      weblog.print(F("Page update FAILED (filesystem full?): "));
      weblog.println(path);
      continue;
    }
    SPIFFS.remove(path);
    if(SPIFFS.rename("/~page.tmp", path)) {
      updated++;
    } else {
      weblog.print(F("Page update FAILED: "));
      weblog.println(path);
    }
  }
  weblog.print(F("Web pages updated from firmware: "));
  weblog.print(updated);
  weblog.print(F("/"));
  weblog.println(DEFAULT_PAGE_COUNT);
}