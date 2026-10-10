// Whole-filesystem backup and restore, as a standard .tar archive.
//
// GET  /backup  - streams every file on the filesystem as one .tar download. Nothing is
//                 buffered: the archive is generated on the fly as the browser reads
//                 it, so it works however full the filesystem is.
// POST /restore - takes a .tar upload (multipart, field "restore"). Every file is
//                 written to a temporary name first; only if the whole archive is
//                 valid are they renamed into place, then the unit restarts so the
//                 config, patterns and servo positions all reload cleanly. A bad or
//                 truncated archive changes nothing.
//
// Restore replaces the files in the archive and leaves any others alone. Directory
// paths inside the archive are stripped to the bare filename (everything lives
// in the root), so re-tarring an extracted backup folder works too.
//
// The archive is plain ustar - extract it with `tar xf`, 7-Zip, or Windows' built-in
// tar. Note it includes config.ini, so it holds the web password and WiFi settings.
//
// Everything here is wrapped in classes so the Arduino IDE's automatic prototype
// generation never sees a function signature using a type it hasn't met yet.

#include <vector>
#include <memory>

#define TAR_BLOCK 512
#define RESTORE_MAX_FILES 64
#define RESTORE_TEMP_PREFIX "~r" // temp files during a restore, e.g. /~r03

class Tar {
public:
  // width-1 zero-padded octal digits, then a NUL - the standard numeric field format
  static void writeOctal(uint8_t *field, size_t width, uint32_t value) {
    field[width-1] = '\0';
    for(int i = (int)width-2; i >= 0; i--) {
      field[i] = '0' + (value & 7);
      value >>= 3;
    }
  }

  // Leading spaces, octal digits, then space/NUL (or the end of the field).
  // Rejects base-256 sizes (files over 8GB) and anything else unexpected.
  static bool readOctal(const uint8_t *field, size_t width, uint32_t &out) {
    out = 0;
    size_t i = 0;
    bool any = false;
    while(i < width && field[i] == ' ')
      i++;
    for(; i < width; i++) {
      uint8_t c = field[i];
      if(c == ' ' || c == '\0')
        break;
      if(c < '0' || c > '7' || out > (0xFFFFFFFFUL >> 3))
        return false;
      out = (out << 3) | (c - '0');
      any = true;
    }
    return any;
  }

  // Sum of all header bytes with the checksum field itself counted as spaces.
  // Some old tars summed signed bytes, so a restore accepts either.
  static uint32_t checksum(const uint8_t *h, bool asSigned) {
    uint32_t sum = 0;
    for(int i = 0; i < TAR_BLOCK; i++) {
      uint8_t c = (i >= 148 && i < 156) ? ' ' : h[i];
      sum += asSigned ? (uint32_t)(int32_t)(int8_t)c : c;
    }
    return sum;
  }

  static uint32_t padded(uint32_t n) {
    return (n + TAR_BLOCK - 1) & ~(uint32_t)(TAR_BLOCK - 1);
  }

  static String htmlEscape(const String &in) {
    String out;
    out.reserve(in.length());
    for(unsigned int i = 0; i < in.length(); i++) {
      char c = in[i];
      if(c == '<') out += F("&lt;");
      else if(c == '>') out += F("&gt;");
      else if(c == '&') out += F("&amp;");
      else if(c == '"') out += F("&quot;");
      else out += c;
    }
    return out;
  }
};


// ---------------------------------------------------------------- backup

// Generates the archive a piece at a time for AsyncWebServer's response filler.
// The file list (and so the Content-Length) is fixed when the download starts; if a
// file changes size mid-download it's padded/truncated to the size promised in its
// header so the archive stays valid.
class TarBackupStream {
  struct Entry {
    String name;
    uint32_t size;
  };
  std::vector<Entry> entries;
  size_t fileIdx = 0;
  enum Phase { HEADER, DATA, PAD, TRAILER, FINISHED } phase = HEADER;
  uint32_t pos = 0; // position within the current phase
  uint8_t header[TAR_BLOCK];
  File file;

  void buildHeader() {
    const Entry &e = entries[fileIdx];
    memset(header, 0, TAR_BLOCK);
    strncpy((char *)header, e.name.c_str(), 99);   // name
    Tar::writeOctal(header + 100, 8, 0644);         // mode
    Tar::writeOctal(header + 108, 8, 0);            // uid
    Tar::writeOctal(header + 116, 8, 0);            // gid
    Tar::writeOctal(header + 124, 12, e.size);      // size
    Tar::writeOctal(header + 136, 12, 0);           // mtime - no RTC, so 1970
    header[156] = '0';                              // regular file
    memcpy(header + 257, "ustar", 6);               // magic, NUL included
    header[263] = '0';                              // version "00"
    header[264] = '0';
    Tar::writeOctal(header + 148, 7, Tar::checksum(header, false));
    header[155] = ' ';                              // checksum is "6 digits, NUL, space"
  }

public:
  size_t totalSize = 0;

  TarBackupStream() {
    File root = LittleFS.open("/");
    if(root) {
      File f = root.openNextFile();
      while(f) {
        if(!f.isDirectory()) {
          String n = f.name();
          if(n.startsWith("/"))
            n = n.substring(1);
          entries.push_back({n, (uint32_t)f.size()});
          totalSize += TAR_BLOCK + Tar::padded(f.size());
        }
        f.close();
        f = root.openNextFile();
      }
      root.close();
    }
    totalSize += 2 * TAR_BLOCK; // end-of-archive marker
    if(entries.empty())
      phase = TRAILER;
    else
      buildHeader();
  }

  ~TarBackupStream() {
    if(file)
      file.close();
  }

  size_t fileCount() const {
    return entries.size();
  }

  size_t fill(uint8_t *buf, size_t maxLen) {
    size_t out = 0;
    while(out < maxLen && phase != FINISHED) {
      size_t room = maxLen - out;
      switch(phase) {
        case HEADER: {
          size_t n = min(room, (size_t)(TAR_BLOCK - pos));
          memcpy(buf + out, header + pos, n);
          out += n;
          pos += n;
          if(pos == TAR_BLOCK) {
            pos = 0;
            phase = DATA;
            file = LittleFS.open("/" + entries[fileIdx].name, "r");
          }
          break;
        }
        case DATA: {
          uint32_t size = entries[fileIdx].size;
          size_t n = min(room, (size_t)(size - pos));
          size_t got = (n && file) ? file.read(buf + out, n) : 0;
          if(got < n)
            memset(buf + out + got, 0, n - got); // file shrank or vanished
          out += n;
          pos += n;
          if(pos >= size) {
            if(file)
              file.close();
            pos = 0;
            phase = PAD;
          }
          break;
        }
        case PAD: {
          uint32_t size = entries[fileIdx].size;
          uint32_t padLen = Tar::padded(size) - size;
          size_t n = min(room, (size_t)(padLen - pos));
          memset(buf + out, 0, n);
          out += n;
          pos += n;
          if(pos >= padLen) {
            pos = 0;
            fileIdx++;
            if(fileIdx < entries.size()) {
              buildHeader();
              phase = HEADER;
            } else {
              phase = TRAILER;
            }
          }
          break;
        }
        case TRAILER: {
          size_t n = min(room, (size_t)(2 * TAR_BLOCK - pos));
          memset(buf + out, 0, n);
          out += n;
          pos += n;
          if(pos >= 2 * TAR_BLOCK)
            phase = FINISHED;
          break;
        }
        default:
          break;
      }
    }
    return out;
  }
};


// ---------------------------------------------------------------- restore

// Streaming tar parser fed straight from the upload callback - the archive is never
// held in RAM. One restore at a time; a second one starting cancels the first.
class TarRestore {
  struct Pending {
    String temp;
    String final;
  };
  std::vector<Pending> pending;
  enum State { HDR, DATA, SKIP, DONE, FAILED } state = HDR;
  uint8_t hdr[TAR_BLOCK];
  size_t hdrFill = 0;
  uint32_t remaining = 0; // bytes left in the current DATA/SKIP run
  uint32_t padAfter = 0;  // padding to skip after the current file's data
  byte zeroBlocks = 0;
  File out;

  void fail(const String &why) {
    if(state != FAILED) {
      error = why;
      state = FAILED;
    }
    if(out)
      out.close();
  }

  static bool isZeroBlock(const uint8_t *h) {
    for(int i = 0; i < TAR_BLOCK; i++)
      if(h[i])
        return false;
    return true;
  }

  // Base name of the entry, or "" if it should be skipped (directories, macOS "._"
  // metadata files, our own temp names).
  String entryName() {
    char raw[101];
    memcpy(raw, hdr, 100);
    raw[100] = '\0';
    String n = raw;
    while(n.endsWith("/"))
      n.remove(n.length() - 1);
    int slash = n.lastIndexOf('/');
    if(slash >= 0)
      n = n.substring(slash + 1);
    if(n == "." || n == ".." || n.startsWith("._") || n.startsWith(RESTORE_TEMP_PREFIX))
      return "";
    return n;
  }

  void handleHeader() {
    if(isZeroBlock(hdr)) {
      if(++zeroBlocks >= 2)
        state = DONE;
      return;
    }
    zeroBlocks = 0;

    uint32_t stored;
    if(!Tar::readOctal(hdr + 148, 8, stored) ||
       (stored != Tar::checksum(hdr, false) && stored != Tar::checksum(hdr, true))) {
      fail("not a valid .tar file (bad header checksum)");
      return;
    }

    uint32_t size;
    if(!Tar::readOctal(hdr + 124, 12, size))
      size = 0; // some tars leave size blank on directories
    uint32_t pad = Tar::padded(size) - size;
    char type = hdr[156];

    String name = (type == '0' || type == '\0' || type == '7') ? entryName() : "";
    if(name.length() == 0) {
      // directory, pax header, long-name record, link, metadata... skip its data
      remaining = size + pad;
      state = remaining ? SKIP : HDR;
      return;
    }

    // 31 chars including the "/": SPIFFS's limit before 4.3. LittleFS allows more, but
    // keeping it means a backup restores onto either.
    if(name.length() + 1 > 31) {
      fail("filename too long for the device: " + name);
      return;
    }
    for(unsigned int i = 0; i < name.length(); i++) {
      if(name[i] < 0x20 || name[i] > 0x7E || name[i] == '\\') {
        fail("invalid character in filename: " + name);
        return;
      }
    }

    // a later copy of the same file in the archive replaces the earlier one
    String temp = "";
    for(auto &p : pending)
      if(p.final == "/" + name)
        temp = p.temp;
    if(temp.length() == 0) {
      if(pending.size() >= RESTORE_MAX_FILES) {
        fail("too many files in the archive");
        return;
      }
      char t[12];
      snprintf(t, sizeof(t), "/" RESTORE_TEMP_PREFIX "%02u", (unsigned)pending.size());
      temp = t;
      pending.push_back({temp, "/" + name});
    }

    out = LittleFS.open(temp, FILE_WRITE);
    if(!out) {
      fail("couldn't create a temporary file (filesystem full?)");
      return;
    }
    remaining = size;
    padAfter = pad;
    state = DATA;
    if(remaining == 0)
      endOfData();
  }

  void endOfData() {
    out.close();
    remaining = padAfter;
    state = remaining ? SKIP : HDR;
  }

public:
  AsyncWebServerRequest *owner = nullptr;
  String error;

  // Removes temp files left behind by an interrupted restore.
  static void cleanupTemps() {
    std::vector<String> stale;
    File root = LittleFS.open("/");
    if(!root)
      return;
    File f = root.openNextFile();
    while(f) {
      String n = f.name();
      if(n.startsWith("/"))
        n = n.substring(1);
      if(n.startsWith(RESTORE_TEMP_PREFIX))
        stale.push_back("/" + n);
      f.close();
      f = root.openNextFile();
    }
    root.close();
    for(auto &s : stale)
      LittleFS.remove(s);
  }

  void begin(AsyncWebServerRequest *req) {
    abort();
    cleanupTemps();
    owner = req;
    state = HDR;
    hdrFill = 0;
    remaining = 0;
    padAfter = 0;
    zeroBlocks = 0;
    error = "";
    pending.clear();
    weblog.println(F("Restore: receiving backup..."));
  }

  void feed(const uint8_t *data, size_t len) {
    while(len && state != DONE && state != FAILED) {
      size_t n;
      switch(state) {
        case HDR:
          n = min(len, TAR_BLOCK - hdrFill);
          memcpy(hdr + hdrFill, data, n);
          hdrFill += n;
          if(hdrFill == TAR_BLOCK) {
            hdrFill = 0;
            handleHeader();
          }
          break;
        case DATA:
          n = min(len, (size_t)remaining);
          if(out.write(data, n) != n) {
            fail("write failed (filesystem full?)");
            return;
          }
          remaining -= n;
          if(remaining == 0)
            endOfData();
          break;
        case SKIP:
          n = min(len, (size_t)remaining);
          remaining -= n;
          if(remaining == 0)
            state = HDR;
          break;
        default:
          n = len;
          break;
      }
      data += n;
      len -= n;
    }
  }

  // True if a complete, valid archive with at least one file was received.
  bool finish() {
    if(state == FAILED)
      return false;
    // tolerate archives missing the end-of-archive blocks, as long as they stop
    // cleanly on a header boundary rather than mid-file
    if(!(state == DONE || (state == HDR && hdrFill == 0))) {
      fail("the archive is incomplete or truncated");
      return false;
    }
    if(pending.empty()) {
      fail("the archive doesn't contain any files");
      return false;
    }
    return true;
  }

  // Moves every temp file into place. Returns how many were restored.
  int commit() {
    int count = 0;
    SafeFile::Lock lock; // don't interleave with a settings save
    for(auto &p : pending) {
      bool moved = LittleFS.rename(p.temp, p.final); // replaces in one step on LittleFS
      if(!moved) {
        LittleFS.remove(p.final);
        moved = LittleFS.rename(p.temp, p.final);
      }
      if(moved) {
        weblog.print(F("Restore: "));
        weblog.println(p.final);
        count++;
      } else {
        weblog.print(F("Restore: FAILED to restore "));
        weblog.println(p.final);
        LittleFS.remove(p.temp);
      }
    }
    pending.clear();
    owner = nullptr;
    return count;
  }

  // Throws away anything received so far. Safe to call at any time.
  void abort() {
    if(out)
      out.close();
    for(auto &p : pending)
      LittleFS.remove(p.temp);
    pending.clear();
    owner = nullptr;
  }

  static String resultPage(const String &msg, bool ok) {
    String s = F("<!DOCTYPE HTML><html><head><meta charset=\"UTF-8\">"
                 "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    if(ok)
      s += F("<meta http-equiv=\"refresh\" content=\"15;url=/#status\">");
    s += F("<title>" PROJECT " - Restore</title>"
           "<style>body{background:#f7f7f7;font-family:Arial,sans-serif;padding:20px}</style>"
           "</head><body><h2>Restore</h2><p>");
    s += Tar::htmlEscape(msg);
    s += F("</p>");
    if(ok)
      s += F("<p>This page will reload in 15 seconds.</p>");
    else
      s += F("<p><a href=\"/#status\">Back</a></p>");
    s += F("</body></html>");
    return s;
  }
};

TarRestore tarRestore;


void setupBackupRoutes() {
  server.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    auto stream = std::make_shared<TarBackupStream>();
    weblog.print(F("Backup: sending "));
    weblog.print(stream->fileCount());
    weblog.println(F(" files"));
    AsyncWebServerResponse *response = request->beginResponse("application/x-tar", stream->totalSize,
      [stream](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        return stream->fill(buffer, maxLen);
      });
    response->addHeader("Content-Disposition",
      "attachment; filename=\"" + String(PROJECT) + "-" + String(VERSION) + "-backup.tar\"");
    request->send(response);
  });

  server.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }
    bool ok = false;
    String msg;
    if(tarRestore.owner != request) {
      msg = F("Restore failed: no backup file was received. Nothing was changed.");
    } else if(!tarRestore.finish()) {
      msg = "Restore failed: " + tarRestore.error + ". Nothing was changed.";
      weblog.println(msg);
      tarRestore.abort();
    } else {
      int count = tarRestore.commit();
      msg = "Restored " + String(count) + " files. Restarting...";
      weblog.println(msg);
      ok = true;
    }
    AsyncWebServerResponse *response =
      request->beginResponse(ok ? 200 : 400, "text/html", TarRestore::resultPage(msg, ok));
    response->addHeader("Connection", "close");
    request->send(response);
    if(ok)
      rebooting = true; // loop() restarts, reloading config, patterns and servos
  },
  [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
    // No response from here - an unauthenticated upload is just ignored, and the
    // request handler above sends the 401.
    if(!request->authenticate(http_username.c_str(), http_password.c_str()))
      return;
    if(!index) {
      tarRestore.begin(request);
      // client gave up mid-upload: throw away the partial restore
      request->onDisconnect([request]() {
        if(tarRestore.owner == request)
          tarRestore.abort();
      });
    }
    if(tarRestore.owner == request)
      tarRestore.feed(data, len);
  });
}