#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include "HomeSpan.h"
#include <WiFiUdp.h>
#include <mutex>
#include <mdns.h>
#include <vector>
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"
#include "render.h"
#include <pb_decode.h>
#include "gtfs_realtime.pb.h"
#include "secrets.h"

// Log to USB serial, HomeSpan's web log (http://HomeSpan-110FDD4CD8EE.local/log),
// and a live UDP broadcast on port 5555 — listen with `make udplog`.
// fmt string without trailing \n.
static const uint16_t LOG_UDP_PORT = 5555;

// Safe to call from any task with WiFi up (loop task + HomeSpan poll task);
// the mutex serializes use of the shared UDP socket
static void udpLogLine(const char *line) {
  static WiFiUDP udp;
  static std::mutex udpMutex;
  if (WiFi.status() != WL_CONNECTED) return;
  std::lock_guard<std::mutex> lock(udpMutex);
  udp.beginPacket(WiFi.broadcastIP(), LOG_UDP_PORT);
  udp.write((const uint8_t *)line, strlen(line));
  udp.write((const uint8_t *)"\n", 1);
  udp.endPacket();
}

__attribute__((format(printf, 1, 2))) static void logLine(const char *fmt,
                                                          ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  WEBLOG("%s", buf);
  udpLogLine(buf);
}

#define LOGB(fmt, ...) logLine(fmt, ##__VA_ARGS__)

// CrowPanel 5.79" pinout (dual-SSD1683 panel, GDEY0579T93)
#define HOME_KEY 2
#define EPD_POWER 7
#define EPD_MOSI 11
#define EPD_SCK 12
#define EPD_CS 45
#define EPD_DC 46
#define EPD_RST 47
#define EPD_BUSY 48

static const char *FEED_URL =
    "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
// Late nights the C stops running and the A runs local past this stop, so
// A trains only appear at HOME_STOP when they're actually stopping here
static const char *ROUTE_PRIMARY = "C";
static const char *ROUTE_FALLBACK = "A";
static const char *STOP_NORTH = "HOME_N"; // Manhattan-bound
static const char *STOP_SOUTH = "HOME_S"; // Euclid Av-bound

// The home bus stop, both directions; each stop serves B25 and B26
static const char *BUS_URL_WEST = // downtown-bound
    "https://bustime.mta.info/api/siri/stop-monitoring.json?key=" BUSTIME_API_KEY
    "&MonitoringRef=BUS_STOP_W&MaximumStopVisits=8";
static const char *BUS_URL_EAST =
    "https://bustime.mta.info/api/siri/stop-monitoring.json?key=" BUSTIME_API_KEY
    "&MonitoringRef=BUS_STOP_E&MaximumStopVisits=8";
static const uint32_t REFRESH_MS = 20 * 1000;
static const size_t FEED_BUF_CAP = 1024 * 1024;

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT>
    display(GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------------- feed decode ----------------

static const size_t MAX_ARRIVALS = 8;

struct Arrivals {
  time_t north[MAX_ARRIVALS]; // C train
  size_t northCount;
  time_t south[MAX_ARRIVALS];
  size_t southCount;
  time_t aNorth[MAX_ARRIVALS]; // A train (late-night local fallback)
  size_t aNorthCount;
  time_t aSouth[MAX_ARRIVALS];
  size_t aSouthCount;
  time_t busWest[MAX_ARRIVALS];
  size_t busWestCount;
  time_t busEast[MAX_ARRIVALS];
  size_t busEastCount;
};

struct EntityCtx {
  transit_realtime_TripUpdate *tripUpdate;
  Arrivals *arrivals;
};

static void addArrival(time_t *arr, size_t *count, time_t t) {
  if (*count < MAX_ARRIVALS) {
    arr[(*count)++] = t;
    return;
  }
  size_t maxIdx = 0;
  for (size_t i = 1; i < MAX_ARRIVALS; i++)
    if (arr[i] > arr[maxIdx]) maxIdx = i;
  if (t < arr[maxIdx]) arr[maxIdx] = t;
}

// TripUpdate.stop_time_update (repeated) — one call per StopTimeUpdate
static bool stopTimeUpdateCb(pb_istream_t *stream, const pb_field_t *field,
                             void **arg) {
  EntityCtx *ctx = (EntityCtx *)*arg;
  transit_realtime_TripUpdate_StopTimeUpdate stu =
      transit_realtime_TripUpdate_StopTimeUpdate_init_default;
  if (!pb_decode(stream, transit_realtime_TripUpdate_StopTimeUpdate_fields,
                 &stu))
    return false;

  // trip (field 1) is serialized before stop_time_update (field 2),
  // so route_id is already populated when we get here
  const char *route = ctx->tripUpdate->trip.route_id;
  bool isPrimary = strcmp(route, ROUTE_PRIMARY) == 0;
  bool isFallback = strcmp(route, ROUTE_FALLBACK) == 0;
  if (!isPrimary && !isFallback) return true;

  time_t t = 0;
  if (stu.has_arrival && stu.arrival.has_time) t = stu.arrival.time;
  else if (stu.has_departure && stu.departure.has_time) t = stu.departure.time;
  if (t == 0) return true;

  Arrivals *a = ctx->arrivals;
  if (strcmp(stu.stop_id, STOP_NORTH) == 0) {
    if (isPrimary) addArrival(a->north, &a->northCount, t);
    else addArrival(a->aNorth, &a->aNorthCount, t);
  } else if (strcmp(stu.stop_id, STOP_SOUTH) == 0) {
    if (isPrimary) addArrival(a->south, &a->southCount, t);
    else addArrival(a->aSouth, &a->aSouthCount, t);
  }
  return true;
}

// FeedMessage.entity (repeated) — one call per FeedEntity
static bool entityCb(pb_istream_t *stream, const pb_field_t *field,
                     void **arg) {
  Arrivals *arrivals = (Arrivals *)*arg;
  transit_realtime_FeedEntity entity = transit_realtime_FeedEntity_init_default;
  EntityCtx ctx = {&entity.trip_update, arrivals};
  entity.trip_update.stop_time_update.funcs.decode = &stopTimeUpdateCb;
  entity.trip_update.stop_time_update.arg = &ctx;
  return pb_decode(stream, transit_realtime_FeedEntity_fields, &entity);
}

static bool decodeFeed(const uint8_t *buf, size_t len, Arrivals *arrivals) {
  transit_realtime_FeedMessage msg = transit_realtime_FeedMessage_init_default;
  msg.entity.funcs.decode = &entityCb;
  msg.entity.arg = arrivals;
  pb_istream_t stream = pb_istream_from_buffer(buf, len);
  if (!pb_decode(&stream, transit_realtime_FeedMessage_fields, &msg)) {
    LOGB("pb_decode failed: %s", PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}

// ---------------- bus (SIRI StopMonitoring, JSON) ----------------

// "2026-08-05T23:21:07.528-04:00" -> epoch seconds. Device TZ is UTC
// (configTime offset 0), so mktime() interprets struct tm as UTC.
static time_t parseIso8601(const char *s) {
  int Y, M, D, h, m;
  float sec;
  if (sscanf(s, "%d-%d-%dT%d:%d:%f", &Y, &M, &D, &h, &m, &sec) != 6) return 0;
  struct tm tmv = {};
  tmv.tm_year = Y - 1900;
  tmv.tm_mon = M - 1;
  tmv.tm_mday = D;
  tmv.tm_hour = h;
  tmv.tm_min = m;
  tmv.tm_sec = (int)sec;
  time_t t = mktime(&tmv);

  const char *tpos = strchr(s, 'T');
  const char *plus = strrchr(s, '+');
  const char *minus = strrchr(s, '-');
  const char *tz = nullptr;
  if (plus && plus > tpos) tz = plus;
  if (minus && minus > tpos && (!tz || minus > tz)) tz = minus;
  if (tz) {
    int oh, om;
    if (sscanf(tz + 1, "%d:%d", &oh, &om) == 2) {
      int off = oh * 3600 + om * 60;
      t += (*tz == '-') ? off : -off;
    }
  }
  return t;
}

static bool parseBusJson(const uint8_t *buf, size_t len, time_t *times,
                         size_t *count) {
  JsonDocument doc;
  // SIRI nests ~11 levels deep; ArduinoJson's default limit is 10
  DeserializationError err =
      deserializeJson(doc, buf, len, DeserializationOption::NestingLimit(16));
  if (err) {
    LOGB("bus json error: %s", err.c_str());
    return false;
  }
  JsonArray visits = doc["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"]
                        [0]["MonitoredStopVisit"];
  for (JsonObject v : visits) {
    JsonObject call = v["MonitoredVehicleJourney"]["MonitoredCall"];
    const char *eta = call["ExpectedArrivalTime"] | call["ExpectedDepartureTime"]
                          | (const char *)nullptr;
    if (!eta) continue; // scheduled-only trip, no realtime estimate
    time_t t = parseIso8601(eta);
    if (t) addArrival(times, count, t);
  }
  return true;
}

// ---------------- feed fetch ----------------

// Stream sink writing into a PSRAM buffer; lets HTTPClient handle
// content-length and chunked transfer uniformly
class PsramSink : public Stream {
 public:
  uint8_t *buf = nullptr;
  size_t len = 0;

  bool begin() {
    if (!buf) buf = (uint8_t *)ps_malloc(FEED_BUF_CAP);
    len = 0;
    return buf != nullptr;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    size_t room = FEED_BUF_CAP - len;
    if (n > room) n = room;
    memcpy(buf + len, data, n);
    len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

static PsramSink feedSink;

// Persistent TLS sessions, one per host, so refresh cycles reuse the open
// connection instead of paying a ~2s handshake per fetch. A reused socket the
// server closed while we were idle fails fast and gets one clean
// reconnect+retry. Loop-task only; no locking needed.
static WiFiClientSecure subwayTls, busTls;
static HTTPClient subwayHttp, busHttp;

static bool fetchUrl(const char *url) {
  if (!feedSink.begin()) {
    LOGB("psram alloc failed");
    return false;
  }
  bool isBus = strstr(url, "bustime") != nullptr;
  WiFiClientSecure &client = isBus ? busTls : subwayTls;
  HTTPClient &http = isBus ? busHttp : subwayHttp;

  static bool tlsInited = false;
  if (!tlsInited) {
    subwayTls.setInsecure(); // public read-only data; skip CA validation
    busTls.setInsecure();
    subwayHttp.setReuse(true);
    busHttp.setReuse(true);
    tlsInited = true;
  }

  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code < 0) { // stale keep-alive socket; reconnect once
    LOGB("%s socket stale (%d), reconnecting", isBus ? "bus" : "subway", code);
    http.end();
    client.stop();
    if (!http.begin(client, url)) return false;
    code = http.GET();
  }
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    ok = http.writeToStream(&feedSink) > 0;
  } else {
    LOGB("http error: %d", code);
  }
  http.end(); // setReuse(true): connection stays open for the next cycle
  return ok;
}

// ---------------- display ----------------

static int timesToMinutes(const time_t *arr, size_t count, int *mins,
                          int maxOut) {
  time_t sorted[MAX_ARRIVALS];
  memcpy(sorted, arr, count * sizeof(time_t));
  std::sort(sorted, sorted + count);
  time_t now = time(nullptr);
  int n = 0;
  for (size_t i = 0; i < count && n < maxOut; i++) {
    if (sorted[i] < now - 30) continue; // already left
    mins[n++] = (int)((sorted[i] - now + 30) / 60);
  }
  return n;
}

static void formatRow(char *out, size_t outLen, const time_t *arr,
                      size_t count) {
  int mins[4];
  int n = timesToMinutes(arr, count, mins, 4);
  if (n == 0) {
    snprintf(out, outLen, "");
    return;
  }
  size_t pos = 0;
  for (int i = 0; i < n; i++)
    pos += snprintf(out + pos, outLen - pos, "%s%d", i ? "," : "", mins[i]);
}

static char lastNorthRow[48] = "";
static char lastSouthRow[48] = "";
static bool firstDraw = true;

static char lastBusWestRow[48] = "";
static char lastBusEastRow[48] = "";
static const char *lastRoute = "";

static void drawArrivals(const Arrivals &arrivals) {
  // Show the C as long as any C trains are coming; when none are (late
  // nights) fall back to the A, which only appears at this stop when it
  // runs local
  const char *route = ROUTE_PRIMARY;
  const time_t *north = arrivals.north, *south = arrivals.south;
  size_t northCount = arrivals.northCount, southCount = arrivals.southCount;
  if (northCount + southCount == 0 &&
      arrivals.aNorthCount + arrivals.aSouthCount > 0) {
    route = ROUTE_FALLBACK;
    north = arrivals.aNorth;
    northCount = arrivals.aNorthCount;
    south = arrivals.aSouth;
    southCount = arrivals.aSouthCount;
  }

  char northRow[48], southRow[48], busWestRow[48], busEastRow[48];
  formatRow(northRow, sizeof(northRow), north, northCount);
  formatRow(southRow, sizeof(southRow), south, southCount);
  formatRow(busWestRow, sizeof(busWestRow), arrivals.busWest,
            arrivals.busWestCount);
  formatRow(busEastRow, sizeof(busEastRow), arrivals.busEast,
            arrivals.busEastCount);

  if (!firstDraw && strcmp(route, lastRoute) == 0 &&
      strcmp(northRow, lastNorthRow) == 0 &&
      strcmp(southRow, lastSouthRow) == 0 &&
      strcmp(busWestRow, lastBusWestRow) == 0 &&
      strcmp(busEastRow, lastBusEastRow) == 0)
    return; // nothing changed, don't flash the panel

  strcpy(lastNorthRow, northRow);
  strcpy(lastSouthRow, southRow);
  strcpy(lastBusWestRow, busWestRow);
  strcpy(lastBusEastRow, busEastRow);
  lastRoute = route;

  if (firstDraw) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    renderArrivals(display, route, northRow, southRow, busWestRow, busEastRow);
  } while (display.nextPage());

  firstDraw = false;
}

static void drawMessage(const char *msg) {
  display.setFullWindow();
  display.firstPage();
  do {
    renderMessage(display, msg);
  } while (display.nextPage());
  // firstDraw = true; // next data draw does a clean full refresh
}

// ---------------- HomeKit (HomeSpan) ----------------

// Stateless programmable switch on the panel's menu key: publishes
// single/double/long press events for HomeKit automations to bind to
struct HomeButton : Service::StatelessProgrammableSwitch {
  SpanCharacteristic *switchEvent;

  HomeButton(int pin) : Service::StatelessProgrammableSwitch() {
    switchEvent = new Characteristic::ProgrammableSwitchEvent();
    new SpanButton(pin);
  }

  void button(int pin, int pressType) override {
    LOGB("button event: press type %d", pressType);
    // SpanButton press types match HAP event values (0/1/2)
    switchEvent->setVal(pressType);
  }
};


static void setupHomeKit() {
  homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASS);
  homeSpan.enableOTA(); // espota, serviced by the poll task; default password
  // ring buffer of LOGB/WEBLOG entries at http://HomeSpan-110FDD4CD8EE.local/log
  homeSpan.enableWebLog(200, "pool.ntp.org", "UTC", "log");
  homeSpan.begin(Category::ProgrammableSwitches, "Transit Display");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Transit Button");
  new HomeButton(HOME_KEY);

  // HAP gets serviced on its own FreeRTOS task, so the blocking
  // fetch/render loop below never makes HomeKit "Not Responding"
  homeSpan.autoPoll();
}

// ---------------- HAP session visibility ----------------

// HomeSpan doesn't weblog controller connections, so we watch lwip's TCP
// table ourselves: a client that holds a connection to the HAP port for 30s+
// is a hub (or an open Home app) — browsers hitting the weblog churn through
// short-lived connections and get filtered out by the threshold.

static const int MAX_HAP_CONN = 12;
static struct {
  uint32_t ip[MAX_HAP_CONN];
  uint16_t port[MAX_HAP_CONN];
  int n;
  volatile bool ready;
} hapSnap;

// Runs on the lwip thread via tcpip_callback: the pcb list may only be
// safely walked there
static void hapSnapshot(void *) {
  hapSnap.n = 0;
  for (struct tcp_pcb *p = tcp_active_pcbs; p && hapSnap.n < MAX_HAP_CONN;
       p = p->next) {
    if (p->local_port == 80 && p->state == ESTABLISHED) {
      hapSnap.ip[hapSnap.n] = ip4_addr_get_u32(ip_2_ip4(&p->remote_ip));
      hapSnap.port[hapSnap.n] = p->remote_port;
      hapSnap.n++;
    }
  }
  hapSnap.ready = true;
}

static void pollHapSessions() {
  struct Known {
    uint32_t ip;
    uint16_t port;
    uint32_t firstSeen;
    bool logged, present;
  };
  static std::vector<Known> known;
  static uint32_t lastPoll = 0;

  if (millis() - lastPoll < 5000) return;
  lastPoll = millis();

  if (hapSnap.ready) {
    hapSnap.ready = false;
    for (auto &k : known) k.present = false;
    for (int i = 0; i < hapSnap.n; i++) {
      bool found = false;
      for (auto &k : known)
        if (k.ip == hapSnap.ip[i] && k.port == hapSnap.port[i])
          k.present = found = true;
      if (!found) known.push_back({hapSnap.ip[i], hapSnap.port[i], millis(),
                                   false, true});
    }
    for (auto it = known.begin(); it != known.end();) {
      if (!it->present) {
        if (it->logged)
          LOGB("hap session closed: %s", IPAddress(it->ip).toString().c_str());
        it = known.erase(it);
      } else {
        if (!it->logged && millis() - it->firstSeen > 30000) {
          it->logged = true;
          LOGB("hap session established: %s (hub or Home app)",
               IPAddress(it->ip).toString().c_str());
        }
        ++it;
      }
    }
  }
  tcpip_callback(hapSnapshot, nullptr);
}

// ---------------- setup / loop ----------------

void setup() {
  Serial.begin(115200);

  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);
  delay(100);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);
  display.setRotation(0); // landscape, 792x272

  drawMessage("connecting...");

  // HomeSpan owns WiFi (credentials, connection, reconnection)
  setupHomeKit();


  while (WiFi.status() != WL_CONNECTED) delay(500);

  // With BLE gone there's no coex constraint: disable modem sleep for
  // reliable multicast RX and snappier HomeKit/fetch latency
  WiFi.setSleep(false);
  LOGB("connected: %s", WiFi.localIP().toString().c_str());

  // NTP so we can turn absolute arrival timestamps into minutes-away
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  drawMessage("setting clock...");
  while (time(nullptr) < 1600000000) delay(200);
}

void loop() {
  static uint32_t lastFetch = 0;
  static int failures = 0;

  pollHapSessions();

  // Re-announce our HAP mDNS service every 30s. Kept from the BLE-coex era
  // (multicast RX is reliable now that modem sleep is off), but it stays as
  // cheap insurance against lost announcements after reboots. Re-setting a
  // constant TXT item is HomeSpan's own broadcast mechanism (see HAP.cpp).
  static uint32_t lastAnnounce = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - lastAnnounce > 30000) {
    lastAnnounce = millis();
    mdns_service_txt_item_set("_hap", "_tcp", "md", "HomeSpan-ESP32");
  }

  if (lastFetch != 0 && millis() - lastFetch < REFRESH_MS) {
    delay(250);
    return;
  }
  lastFetch = millis();

  if (WiFi.status() != WL_CONNECTED) return; // HomeSpan handles reconnection

  Arrivals arrivals;
  memset(&arrivals, 0, sizeof(arrivals));
  bool subwayOk =
      fetchUrl(FEED_URL) && decodeFeed(feedSink.buf, feedSink.len, &arrivals);
  const bool busEnabled = true;
  bool busWestOk = busEnabled && fetchUrl(BUS_URL_WEST) &&
                   parseBusJson(feedSink.buf, feedSink.len, arrivals.busWest,
                                &arrivals.busWestCount);
  bool busEastOk = busEnabled && fetchUrl(BUS_URL_EAST) &&
                   parseBusJson(feedSink.buf, feedSink.len, arrivals.busEast,
                                &arrivals.busEastCount);

  if (subwayOk || busWestOk || busEastOk) {
    failures = 0;
    drawArrivals(arrivals);
  } else if (++failures >= 3) {
    LOGB("feed unavailable (3 consecutive failures)");
    drawMessage("feed unavailable");
  }
}
