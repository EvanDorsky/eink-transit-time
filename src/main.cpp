#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <GxEPD2_BW.h>
#include <ArduinoJson.h>
#include "render.h"
#include <pb_decode.h>
#include "gtfs_realtime.pb.h"
#include "secrets.h"

// CrowPanel 5.79" pinout (dual-SSD1683 panel, GDEY0579T93)
#define EPD_POWER 7
#define EPD_MOSI 11
#define EPD_SCK 12
#define EPD_CS 45
#define EPD_DC 46
#define EPD_RST 47
#define EPD_BUSY 48

static const char *FEED_URL =
    "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
static const char *ROUTE_ID = "C";
static const char *STOP_NORTH = "HOME_N"; // Manhattan-bound
static const char *STOP_SOUTH = "HOME_S"; // Euclid Av-bound

// The home bus stop, both directions; each stop serves B25 and B26
static const char *BUS_URL_WEST = // downtown-bound
    "https://bustime.mta.info/api/siri/stop-monitoring.json?key=" BUSTIME_API_KEY
    "&MonitoringRef=BUS_STOP_W&MaximumStopVisits=8";
static const char *BUS_URL_EAST =
    "https://bustime.mta.info/api/siri/stop-monitoring.json?key=" BUSTIME_API_KEY
    "&MonitoringRef=BUS_STOP_E&MaximumStopVisits=8";
static const uint32_t REFRESH_MS = 15 * 1000;
static const size_t FEED_BUF_CAP = 1024 * 1024;

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT>
    display(GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ---------------- feed decode ----------------

static const size_t MAX_ARRIVALS = 8;

struct Arrivals {
  time_t north[MAX_ARRIVALS];
  size_t northCount;
  time_t south[MAX_ARRIVALS];
  size_t southCount;
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
  if (strcmp(ctx->tripUpdate->trip.route_id, ROUTE_ID) != 0) return true;

  time_t t = 0;
  if (stu.has_arrival && stu.arrival.has_time) t = stu.arrival.time;
  else if (stu.has_departure && stu.departure.has_time) t = stu.departure.time;
  if (t == 0) return true;

  if (strcmp(stu.stop_id, STOP_NORTH) == 0)
    addArrival(ctx->arrivals->north, &ctx->arrivals->northCount, t);
  else if (strcmp(stu.stop_id, STOP_SOUTH) == 0)
    addArrival(ctx->arrivals->south, &ctx->arrivals->southCount, t);
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
    Serial.printf("pb_decode failed: %s\n", PB_GET_ERROR(&stream));
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
  DeserializationError err = deserializeJson(doc, buf, len);
  if (err) {
    Serial.printf("bus json error: %s\n", err.c_str());
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

static bool fetchUrl(const char *url) {
  if (!feedSink.begin()) {
    Serial.println("psram alloc failed");
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure(); // public read-only data; skip CA validation
  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    ok = http.writeToStream(&feedSink) > 0;
  } else {
    Serial.printf("http error: %d\n", code);
  }
  http.end();
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

static void drawArrivals(const Arrivals &arrivals) {
  char northRow[48], southRow[48], busWestRow[48], busEastRow[48];
  formatRow(northRow, sizeof(northRow), arrivals.north, arrivals.northCount);
  formatRow(southRow, sizeof(southRow), arrivals.south, arrivals.southCount);
  formatRow(busWestRow, sizeof(busWestRow), arrivals.busWest,
            arrivals.busWestCount);
  formatRow(busEastRow, sizeof(busEastRow), arrivals.busEast,
            arrivals.busEastCount);

  if (!firstDraw && strcmp(northRow, lastNorthRow) == 0 &&
      strcmp(southRow, lastSouthRow) == 0 &&
      strcmp(busWestRow, lastBusWestRow) == 0 &&
      strcmp(busEastRow, lastBusEastRow) == 0)
    return; // nothing changed, don't flash the panel

  strcpy(lastNorthRow, northRow);
  strcpy(lastSouthRow, southRow);
  strcpy(lastBusWestRow, busWestRow);
  strcpy(lastBusEastRow, busEastRow);

  if (firstDraw) display.setFullWindow();
  else display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    renderArrivals(display, northRow, southRow, busWestRow, busEastRow);
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("connected: %s\n", WiFi.localIP().toString().c_str());

  // NTP so we can turn absolute arrival timestamps into minutes-away
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  drawMessage("setting clock...");
  while (time(nullptr) < 1600000000) delay(200);
}

void loop() {
  static uint32_t lastFetch = 0;
  static int failures = 0;

  if (lastFetch != 0 && millis() - lastFetch < REFRESH_MS) {
    delay(250);
    return;
  }
  lastFetch = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("wifi dropped, reconnecting");
    WiFi.reconnect();
    return;
  }

  Arrivals arrivals;
  memset(&arrivals, 0, sizeof(arrivals));
  bool subwayOk =
      fetchUrl(FEED_URL) && decodeFeed(feedSink.buf, feedSink.len, &arrivals);
  bool busWestOk = fetchUrl(BUS_URL_WEST) &&
                   parseBusJson(feedSink.buf, feedSink.len, arrivals.busWest,
                                &arrivals.busWestCount);
  bool busEastOk = fetchUrl(BUS_URL_EAST) &&
                   parseBusJson(feedSink.buf, feedSink.len, arrivals.busEast,
                                &arrivals.busEastCount);

  if (subwayOk || busWestOk || busEastOk) {
    failures = 0;
    drawArrivals(arrivals);
  } else if (++failures >= 3) {
    drawMessage("feed unavailable");
  }
}
