#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include "secrets.h"   // defines WIFI_SSID and WIFI_PASS (not pushed to GitHub)

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

// ================= LED STRIPS =================
MD_Parola d1(HARDWARE_TYPE, 23, 18, 5, MAX_DEVICES);
MD_Parola d2(HARDWARE_TYPE, 19, 16, 4, MAX_DEVICES);
MD_Parola d3(HARDWARE_TYPE, 27, 26, 25, MAX_DEVICES);

// ================= TIME (NTP) =================
#define TZ_INFO        "IST-5:30"          // India Standard Time
#define NTP_SERVER_1   "pool.ntp.org"
#define NTP_SERVER_2   "time.google.com"
#define NTP_SERVER_3   "time.nist.gov"
#define MIN_VALID_EPOCH 1700000000UL       // anything older means "not synced yet"
#define WIFI_RETRY_MS  30000UL

unsigned long lastWifiTry = 0;
bool timeWasSynced = false;

// ================= PHASE ENGINE =================
unsigned long phaseTimer = 0;
byte phase = 0;

#define T_CODE      8000
#define T_NAME      8000
#define T_STRENGTH  18000

bool strengthHalf = false;
bool fallbackActive = false;

// ================= LAB STRUCT =================
struct Lab {
  const char* code;
  const char* name;
  const char* lecturer;
  const char* designation;
  int sem;
};

// ================= LAB DATA =================
Lab WT   = {"CM406", " WT",  "T NAGA JYOTHI",   "L-CME", 4};
Lab PY   = {"CM507", " PY",  "M SURESH BABU",   "L-CME", 5};
Lab AP   = {"CM506", " AP",  "Y V PRASAD",      "L-CME", 5};
Lab JAVA = {"CM407", "JAVA", "V HARI PRASAD",   "L-CME", 4};
Lab PW   = {"CM509", " PW",  "TNJ KSB",         "SVM",   5};

// ================= STATE =================
int activeDay    = -1;
int activePeriod = -99;

bool labActive = false;
Lab* activeLab = nullptr;

// ================= PERIOD IDS =================
#define P_SLEEP   -1
#define P_GM       0
#define P_BREAK   -2
#define P1         1
#define P2         2
#define P3         3
#define P4         4
#define P5_7       5

const char* dayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// Safe name lookup (the old periodNames[period + 1] went out of range on breaks)
const char* periodName(int p) {
  switch (p) {
    case P_SLEEP: return "SLEEP";
    case P_GM:    return "GM";
    case P_BREAK: return "BREAK";
    case P1:      return "P1";
    case P2:      return "P2";
    case P3:      return "P3";
    case P4:      return "P4";
    case P5_7:    return "P5-7";
    default:      return "NONE";
  }
}

// ================= DISPLAY HELPERS =================
void staticText(MD_Parola &d, const char *t) {
  d.displayClear();
  d.displayText(t, PA_CENTER, 0, 0, PA_PRINT, PA_PRINT);
}

void startScroll(MD_Parola &d, const char *t) {
  d.displayClear();
  d.displayText(t, PA_CENTER, 70, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void fallback() {
  if (!fallbackActive) {
    staticText(d1, "LAB Ic");
    startScroll(d2, "T NAGA JYOTHI");
    staticText(d3, "L-CME");
    fallbackActive = true;
    Serial.println("DEBUG: Fallback display activated - LAB Ic / T NAGA JYOTHI / L-CME");
  }
}

void showDateAndDay(const struct tm &t) {
  // Persistent buffers: MD_Parola keeps a pointer to the text, so these must outlive the call
  static char dateBuf[6];   // "DD MM"
  static char dayBuf[4];    // "MON"

  snprintf(dateBuf, sizeof(dateBuf), "%02d %02d", t.tm_mday, t.tm_mon + 1);
  strcpy(dayBuf, dayNames[t.tm_wday]);

  startScroll(d1, "GOOD MORNING");   // scrolling greeting
  staticText(d2, dateBuf);           // static date
  staticText(d3, dayBuf);            // static day

  Serial.printf("DEBUG: Good Morning display - Date: %s, Day: %s\n", dateBuf, dayBuf);
}

// ================= LAB PHASE MACHINE =================
void showLab(Lab &L) {
  unsigned long now = millis();

  if (phase == 0 && now - phaseTimer >= T_CODE) {
    staticText(d1, L.code);
    staticText(d2, L.name);
    staticText(d3, "LAB");
    phase = 1;
    phaseTimer = now;
    Serial.printf("DEBUG: Lab phase 0->1 - Code: %s, Name: %s\n", L.code, L.name);
  }
  else if (phase == 1 && now - phaseTimer >= T_NAME) {
    staticText(d1, L.code);
    staticText(d3, L.designation);
    startScroll(d2, L.lecturer);
    phase = 2;
    phaseTimer = now;
    Serial.printf("DEBUG: Lab phase 1->2 - Lecturer: %s, Designation: %s\n", L.lecturer, L.designation);
  }
  else if (phase == 2 && now - phaseTimer >= (T_STRENGTH / 2)) {
    staticText(d1, L.sem == 4 ? "4 SEM" : "5 SEM");
    staticText(d2, "Tot:62");

    if (!strengthHalf) {
      staticText(d3, L.sem == 4 ? "B : 41" : "B : 42");
      strengthHalf = true;
      Serial.printf("DEBUG: Lab strength - Boys: %d (sem %d)\n", L.sem == 4 ? 41 : 42, L.sem);
    } else {
      staticText(d3, L.sem == 4 ? "G : 21" : "G : 20");
      strengthHalf = false;
      phase = 0;
      Serial.printf("DEBUG: Lab strength cycle complete - Girls: %d (sem %d), restarting phase\n",
                    L.sem == 4 ? 21 : 20, L.sem);
    }
    phaseTimer = now;
  }
}

// ================= PERIOD FROM TIME =================
int getPeriodFromTime(int mins) {
  if (mins >= 990 || mins < 540) return P_SLEEP;   // 16:30 onwards (was 1000, which left a gap)
  if (mins >= 540 && mins < 570)  return P_GM;
  if ((mins >= 670 && mins < 680) || (mins >= 780 && mins < 840)) return P_BREAK;
  if (mins >= 570 && mins < 620) return P1;
  if (mins >= 620 && mins < 670) return P2;
  if (mins >= 680 && mins < 730) return P3;
  if (mins >= 730 && mins < 780) return P4;
  if (mins >= 840 && mins < 990) return P5_7;
  return -99;
}

// ================= WIFI (non-blocking) =================
void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWifiTry < WIFI_RETRY_MS) return;
  lastWifiTry = millis();
  Serial.println("WIFI: (re)connecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== LAB DISPLAY SYSTEM (WIFI/NTP) STARTED ===");
  Serial.printf("Hardware: ESP32 with %d MAX7219 modules\n", MAX_DEVICES);

  delay(800);
  d1.begin(); d2.begin(); d3.begin();
  d1.displayClear();
  d2.displayClear();
  d3.displayClear();

  delay(800);
  d1.setIntensity(1);
  d2.setIntensity(1);
  d3.setIntensity(1);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWifiTry = millis();

  // Starts background SNTP. Once synced, the ESP32 keeps its own clock running,
  // so the display still works if WiFi drops later.
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  fallback();   // shown until the first successful time sync
}

// ================= LOOP =================
void loop() {
  // ---- ALWAYS ANIMATE ----
  if (d1.displayAnimate()) d1.displayReset();
  if (d2.displayAnimate()) d2.displayReset();
  if (d3.displayAnimate()) d3.displayReset();

  maintainWiFi();

  // ---- WAIT FOR VALID TIME ----
  time_t nowT = time(nullptr);
  if (nowT < MIN_VALID_EPOCH) {
    static unsigned long lastWait = 0;
    if (millis() - lastWait > 5000) {
      Serial.println("TIME: waiting for NTP sync...");
      lastWait = millis();
    }
    return;   // fallback display keeps running
  }

  struct tm t;
  localtime_r(&nowT, &t);
  if (!timeWasSynced) {
    timeWasSynced = true;
    Serial.printf("TIME: synced %02d:%02d:%02d on %s\n", t.tm_hour, t.tm_min, t.tm_sec, dayNames[t.tm_wday]);
  }

  int day  = t.tm_wday;                    // 0 = Sunday
  int mins = t.tm_hour * 60 + t.tm_min;
  int period = getPeriodFromTime(mins);

  // Status every 30 seconds
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    Serial.printf("STATUS: %02d:%02d %s (Period %s, LabActive: %s, WiFi: %s)\n",
                  t.tm_hour, t.tm_min, dayNames[day], periodName(period),
                  labActive ? "YES" : "NO",
                  WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    lastStatus = millis();
  }

  // ---- PERIOD CHANGE DETECTION ----
  if (day != activeDay || period != activePeriod) {
    Serial.printf("EVENT: Day %d->%d, Period %s->%s\n",
                  activeDay, day, periodName(activePeriod), periodName(period));

    activeDay = day;
    activePeriod = period;

    // reset execution state ONCE
    phase = 0;
    phaseTimer = millis();
    strengthHalf = false;
    fallbackActive = false;

    labActive = false;
    activeLab = nullptr;

    Serial.printf("STATE: %s %s - Resetting displays\n", dayNames[day], periodName(period));

    switch (day) {
      case 0: // Sunday
        Serial.println("SUNDAY: Fallback mode");
        fallback();
        break;

      case 1: // Monday
        if (period >= P2 && period <= P4) {      // P2-P4 (as in the original code)
          activeLab = &PY;
          labActive = true;
          Serial.println("MONDAY P2-P4: PY lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      case 2: // Tuesday
        if (period >= P1 && period <= P4) {
          activeLab = &WT;
          labActive = true;
          Serial.println("TUESDAY P1-P4: WT lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      case 3: // Wednesday
        if (period >= P1 && period <= P4) {
          activeLab = &AP;
          labActive = true;
          Serial.println("WEDNESDAY P1-P4: AP lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      case 4: // Thursday
        if (period == P5_7) {
          activeLab = &PW;
          labActive = true;
          Serial.println("THURSDAY P5-7: PW lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      case 5: // Friday
        if (period >= P1 && period <= P4) {
          activeLab = &JAVA;
          labActive = true;
          Serial.println("FRIDAY P1-P4: JAVA lab activated");
        }
        else if (period == P5_7) {
          activeLab = &PW;
          labActive = true;
          Serial.println("FRIDAY P5-7: PW lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      case 6: // Saturday
        if (period == P5_7) {
          activeLab = &PW;
          labActive = true;
          Serial.println("SATURDAY P5-7: PW lab activated");
        }
        else if (period == P_GM) showDateAndDay(t);
        else fallback();
        break;

      default:
        Serial.println("UNKNOWN DAY: Fallback");
        fallback();
    }
  }

  // ---- CONTINUOUS LAB EXECUTION ----
  if (labActive && activeLab != nullptr) {
    showLab(*activeLab);
  }
}
