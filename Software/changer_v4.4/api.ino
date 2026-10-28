/*
 * HTTP API - runs one command through the same interpreter as Serial and I2C (see
 * commands.ino) and returns exactly what it printed, ending with the usual
 * "<command>:OK" or "<command>:ERR <reason>" status line. Needs the admin login.
 *
 *   curl -u admin:PASSWORD "http://autochanger.local/api?cmd=INFO"
 *   curl -u admin:PASSWORD --data-urlencode "cmd=P0 1234" http://autochanger.local/api
 *
 * GET or POST, parameter "cmd". Responses:
 *   200 - the command ran; the body is its output. Always 200 once a command is
 *         accepted (the headers go out before it runs), so check the last line.
 *   400 - missing, empty or over-long cmd.   401 - login needed.
 *   503 - too many API commands already waiting.
 *
 * Commands run on loop(), one at a time, like every other action from the web UI;
 * the response is held open until the command has finished, so a long one (UPDATE)
 * can take a minute or more. PUT isn't available here (it needs a raw byte stream -
 * use Serial, or /upload). REBOOT answers first, then restarts about a second later.
 * Output beyond API_MAX_OUTPUT is cut, but the final status line is always kept.
 *
 * Everything is inside a class so the IDE's prototype generation never sees ApiJob
 * in a function signature before the class exists.
 */

#include <atomic>
#include <new>

#define API_MAX_CMD 512        // UPDATE <url> can be long
#define API_MAX_OUTPUT 32768   // CAT of a big file gets cut - use /download for those
#define API_QUEUE_LEN 4

// Defined in commands.ino, which the IDE joins after this file.
extern Print *cmdOut;
extern bool processingViaSingleShot;

class ApiServer {
  // One command and its output. Shared by loop() (which runs it) and the HTTP
  // response (which sends the output) - whichever finishes last frees it.
  class Job : public Print {
  public:
    String cmd;
    String out;
    std::atomic<bool> done{false};
    std::atomic<int> refs{1};

    size_t write(uint8_t c) override {
      if(out.length() < API_MAX_OUTPUT) {
        out += (char)c;
      } else {
        truncated = true;
        // past the cap, keep only the latest complete line - the status line is
        // printed last, so it survives
        if(c == '\n') {
          lastLine = curLine;
          curLine = "";
        } else {
          curLine += (char)c;
        }
      }
      return 1;
    }
    size_t write(const uint8_t *data, size_t size) override {
      for(size_t i = 0; i < size; i++)
        write(data[i]);
      return size;
    }

    void finish() {
      if(truncated)
        out += "\n[output cut at " + String(API_MAX_OUTPUT) + " bytes]\n" + lastLine + "\n";
    }
    void release() {
      if(--refs == 0)
        delete this;
    }

  private:
    bool truncated = false;
    String curLine, lastLine;
  };

  static QueueHandle_t queue() {
    static QueueHandle_t q = xQueueCreate(API_QUEUE_LEN, sizeof(Job *));
    return q;
  }

  static void handle(AsyncWebServerRequest *request) {
    if(!request->authenticate(http_username.c_str(), http_password.c_str())) {
      return request->requestAuthentication();
    }

    String cmd;
    if(request->hasParam("cmd", true))
      cmd = request->getParam("cmd", true)->value(); // POST form field
    else if(request->hasParam("cmd"))
      cmd = request->getParam("cmd")->value();       // query string
    cmd.trim();
    if(cmd.length() == 0)
      return request->send(400, "text/plain", "missing cmd");
    if(cmd.length() > API_MAX_CMD)
      return request->send(400, "text/plain", "cmd too long");

    Job *job = new (std::nothrow) Job();
    if(!job)
      return request->send(503, "text/plain", "out of memory");
    job->cmd = cmd;
    job->refs = 2; // one for loop(), one for the response
    if(xQueueSend(queue(), &job, 0) != pdTRUE) {
      delete job;
      return request->send(503, "text/plain", "busy - too many API commands waiting");
    }

    // The response's reference, dropped when the response is destroyed - whether it
    // finished or the client gave up waiting.
    std::shared_ptr<Job> hold(job, [](Job *j) { j->release(); });

    // Chunked, so it can say "nothing yet" (RESPONSE_TRY_AGAIN) until loop() has run
    // the command; the server keeps polling this until then.
    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain",
      [hold](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if(!hold->done)
          return RESPONSE_TRY_AGAIN;
        size_t left = hold->out.length() - index;
        size_t n = (left < maxLen) ? left : maxLen;
        memcpy(buffer, hold->out.c_str() + index, n);
        return n; // 0 ends the response
      });
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  }

public:
  static void setupRoutes() {
    server.on("/api", HTTP_GET | HTTP_POST, handle);
  }

  // Called every loop() - runs whatever's waiting, in order.
  static void runPending() {
    Job *job;
    while(xQueueReceive(queue(), &job, 0) == pdTRUE) {
      if(DEBUG) {
        weblog.print(F("API: "));
        weblog.println(job->cmd);
      }
      Print *previousOut = cmdOut;
      cmdOut = job;
      processingViaSingleShot = true; // refuses PUT, makes REBOOT answer first
      processCommand(job->cmd);
      processingViaSingleShot = false;
      cmdOut = previousOut;

      job->finish();
      job->done = true;
      job->release();
    }
  }
};

// Plain wrappers for webserver.ino and the main .ino.
void setup_api_routes() {
  ApiServer::setupRoutes();
}

void api_loop() {
  ApiServer::runPending();
}
