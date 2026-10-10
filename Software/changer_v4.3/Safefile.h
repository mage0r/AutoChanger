// Crash-safe file writes.
//
// Every settings file is written to "<name>.tmp" first and then renamed over the real
// one. LittleFS renames atomically, so after a power cut the file is either the old
// version or the new one - never empty or half-written (which used to wipe the arm
// calibration, or reset the login and WiFi settings to defaults).
//
// Writes are also serialised with a mutex, since saves can come from loop() and from
// web handlers on the async_tcp task at the same time, and two writers sharing one
// .tmp file would corrupt it.
//
// A .tmp left behind by a power cut mid-write is just a stale copy - removeTemps()
// clears them at boot.

#pragma once

class SafeFile {
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
  }

public:
  // Holds the write lock for its lifetime.
  class Lock {
  public:
    Lock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(mutex()); }
  };

  static bool write(fs::FS &fs, const char *path, const char *data, size_t len) {
    Lock lock;
    String tmp = String(path) + ".tmp";
    File f = fs.open(tmp, FILE_WRITE);
    if(!f)
      return false;
    size_t written = len ? f.write((const uint8_t *)data, len) : 0;
    f.close();
    if(written != len) {
      fs.remove(tmp);
      return false;
    }
    if(fs.rename(tmp, path)) // replaces the old file in one step on LittleFS
      return true;
    fs.remove(path);           // fallback for a filesystem whose rename won't replace
    return fs.rename(tmp, path);
  }

  static bool write(fs::FS &fs, const char *path, const String &content) {
    return write(fs, path, content.c_str(), content.length());
  }

  static void removeTemps(fs::FS &fs) {
    Lock lock;
    String stale[16];
    int count = 0;
    File root = fs.open("/");
    if(!root)
      return;
    File f = root.openNextFile();
    while(f && count < 16) {
      String n = f.name();
      if(n.endsWith(".tmp"))
        stale[count++] = n.startsWith("/") ? n : "/" + n;
      f.close();
      f = root.openNextFile();
    }
    root.close();
    for(int i = 0; i < count; i++)
      fs.remove(stale[i]);
  }
};