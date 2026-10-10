// Mounting the filesystem, and the one-off move from SPIFFS (up to 4.2.x) to LittleFS
// (4.3 onwards).
//
// Both use the same "spiffs" flash partition, so the first boot of 4.3 on a unit that
// ran older firmware finds a SPIFFS filesystem LittleFS can't mount. Rather than just
// formatting it (losing WiFi settings, patterns and arm calibration, and dropping the
// unit into hotspot mode), we mount it as SPIFFS, copy the files into RAM, format the
// partition as LittleFS and write them back.
//
// The built-in web pages aren't copied - create_missing_pages() writes fresh ones from
// the firmware straight after. Everything else (config.ini, patterns.txt, servos.txt
// and anything uploaded by hand) is, up to MIGRATE_MAX_BYTES in total.
//
// There's a short window between the format and the files being written back where
// a power cut would lose them - the same outcome as a plain format, so no worse than
// not migrating at all.

#include <SPIFFS.h>
#include <vector>

#define MIGRATE_MAX_BYTES 65536 // RAM used to hold the old files during the move

class FsMigration {
  struct Carried {
    String path;
    std::vector<uint8_t> data;
  };

  static bool isDefaultPage(const String &path) {
    for(byte i = 0; i < DEFAULT_PAGE_COUNT; i++)
      if(path == defaultPagePaths[i])
        return true;
    return false;
  }

public:
  // Called with LittleFS unmountable. Returns true once LittleFS is mounted.
  static bool run() {
    std::vector<Carried> carried;
    size_t total = 0;

    if(SPIFFS.begin(false)) {
      weblog.println(F("Found a SPIFFS filesystem from older firmware - moving it to LittleFS."));
      File root = SPIFFS.open("/");
      File f = root ? root.openNextFile() : File();
      while(f) {
        String path = f.name();
        if(!path.startsWith("/"))
          path = "/" + path;
        size_t size = f.size();

        if(f.isDirectory() || isDefaultPage(path)) {
          // built-in pages are recreated from the firmware
        } else if(total + size > MIGRATE_MAX_BYTES) {
          weblog.print(F("  not enough RAM to carry over "));
          weblog.println(path);
        } else {
          Carried c;
          c.path = path;
          c.data.resize(size);
          if(size == 0 || f.read(c.data.data(), size) == size) {
            total += size;
            carried.push_back(std::move(c));
          } else {
            weblog.print(F("  couldn't read "));
            weblog.println(path);
          }
        }
        f.close();
        f = root.openNextFile();
      }
      if(root)
        root.close();
      SPIFFS.end();
    } else {
      weblog.println(F("No usable filesystem found - formatting as LittleFS."));
    }

    if(!LittleFS.begin(true)) // formats, then mounts
      return false;

    for(auto &c : carried) {
      File out = LittleFS.open(c.path, FILE_WRITE);
      size_t written = out ? out.write(c.data.data(), c.data.size()) : 0;
      if(out)
        out.close();
      weblog.print(written == c.data.size() ? F("  moved ") : F("  FAILED to write "));
      weblog.println(c.path);
    }
    if(!carried.empty()) {
      weblog.print(F("Moved "));
      weblog.print(carried.size());
      weblog.println(F(" files to LittleFS."));
    }
    return true;
  }
};

// Mounts LittleFS, migrating from SPIFFS or formatting if it won't mount as-is.
bool mount_filesystem() {
  bool mounted = LittleFS.begin(false);
  if(!mounted) {
    weblog.println(F("LittleFS didn't mount."));
    mounted = FsMigration::run();
  }
  if(mounted)
    SafeFile::removeTemps(LittleFS); // leftovers from a save cut short by a power cut
  return mounted;
}