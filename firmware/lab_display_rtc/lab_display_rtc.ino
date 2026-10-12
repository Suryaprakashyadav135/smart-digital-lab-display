#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>
#include <RTClib.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

// ================= LED STRIPS =================
MD_Parola d1(HARDWARE_TYPE, 23, 18, 5, MAX_DEVICES);
MD_Parola d2(HARDWARE_TYPE, 19, 16, 4, MAX_DEVICES);
MD_Parola d3(HARDWARE_TYPE, 27, 26, 25, MAX_DEVICES);

RTC_DS3231 rtc;

// ================= PHASE ENGINE =================
unsigned long phaseTimer = 0;
byte phase = 0;

#define T_CODE      8000
#define T_NAME      8000
#define T_STRENGTH  18000

bool strengthHalf = false;
bool fallbackActive = false;
bool goodMorningScrollStarted = false;

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
Lab PY   = {"CM507"," PY","M SURESH BABU","L-CME",5};
Lab AP   = {"CM506"," AP","Y V PRASAD","L-CME",5};
Lab JAVA = {"CM407","JAVA","V HARI PRASAD","L-CME",4};
Lab PW   = {"CM509"," PW","TNJ KSB","SVM",5};

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

void showDateAndDay(const DateTime &now) {
  static bool initialized = false;

  // 🔒 PERSISTENT BUFFERS (IMPORTANT)
  static char dateBuf[6];   // "DD MM"
  static char dayBuf[4];    // "MON"

  static const char* days[] = {
    "SUN","MON","TUE","WED","THU","FRI","SAT"
  };

  // Update content (safe even if already initialized)
  sprintf(dateBuf, "%02d %02d", now.day(), now.month());
  strcpy(dayBuf, days[now.dayOfTheWeek()]);

  if (!initialized) {
    // -------- STRIP 1 : CONTINUOUS SCROLL --------
    d1.displayText(
      "GOOD MORNING",
      PA_CENTER,
      70,
      0,
      PA_SCROLL_LEFT,
      PA_SCROLL_LEFT
    );

    // -------- STRIP 2 : STATIC DATE --------
    d2.displayText(
      dateBuf,
      PA_CENTER,
      0,
      0,
      PA_PRINT,
      PA_PRINT
    );

    // -------- STRIP 3 : STATIC DAY --------
    d3.displayText(
      dayBuf,
      PA_CENTER,
      0,
      0,
      PA_PRINT,
      PA_PRINT
    );

    Serial.printf("DEBUG: Good Morning display - Date: %s, Day: %s\n", dateBuf, dayBuf);
    initialized = true;
  }
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
      Serial.printf("DEBUG: Lab strength cycle complete - Girls: %d (sem %d), restarting phase\n", L.sem == 4 ? 21 : 20, L.sem);
    }
    phaseTimer = now;
  }
}

// ================= PERIOD FROM TIME =================
int getPeriodFromTime(int mins) {
  if (mins >= 1000 || mins < 540) return P_SLEEP;
  if (mins >= 540 && mins < 570)  return P_GM;
  if ((mins >= 670 && mins < 680) || (mins >= 780 && mins < 840)) return P_BREAK;
  if (mins >= 570 && mins < 620) return P1;
  if (mins >= 620 && mins < 670) return P2;
  if (mins >= 680 && mins < 730) return P3;
  if (mins >= 730 && mins < 780) return P4;
  if (mins >= 840 && mins < 990) return P5_7;
  return -99;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize
  Serial.println("=== LAB DISPLAY SYSTEM STARTED ===");
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
  
  if (!rtc.begin()) {
    Serial.println("ERROR: RTC not found!");
    while (1);
  }
  Serial.println("INFO: RTC initialized successfully");
  
  fallback();
}

// ================= LOOP =================
void loop() {
  DateTime now = rtc.now();
  int day  = now.dayOfTheWeek();
  int mins = now.hour() * 60 + now.minute();

  // Print status every 30 seconds for monitoring
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    Serial.printf("STATUS: %02d:%02d %s (Day %d, Period %d, LabActive: %s)\n", 
                  now.hour(), now.minute(), 
                  now.dayOfTheWeek() == 0 ? "SUN" : now.dayOfTheWeek() == 1 ? "MON" : 
                  now.dayOfTheWeek() == 2 ? "TUE" : now.dayOfTheWeek() == 3 ? "WED" :
                  now.dayOfTheWeek() == 4 ? "THU" : now.dayOfTheWeek() == 5 ? "FRI" : "SAT",
                  day, getPeriodFromTime(mins), labActive ? "YES" : "NO");
    lastStatus = millis();
  }

  // ---- ALWAYS ANIMATE ----
  if (d1.displayAnimate()) d1.displayReset();
  if (d2.displayAnimate()) d2.displayReset();
  if (d3.displayAnimate()) d3.displayReset();

  int period = getPeriodFromTime(mins);

  // ---- PERIOD CHANGE DETECTION ----
  if (day != activeDay || period != activePeriod) {
    Serial.printf("EVENT: Day change %d->%d or Period %d->%d\n", activeDay, day, activePeriod, period);
    
    activeDay = day;
    activePeriod = period;

    // reset execution state ONCE
    phase = 0;
    phaseTimer = millis();
    strengthHalf = false;
    fallbackActive = false;
    goodMorningScrollStarted = false;

    labActive = false;
    activeLab = nullptr;

    const char* dayNames[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const char* periodNames[] = {"SLEEP", "GM", "BREAK", "P1", "P2", "P3", "P4", "P5-7"};
    Serial.printf("STATE: %s P%s - Resetting displays\n", dayNames[day], periodNames[period + 1]);

    switch (day) {
      case 0: // Sunday
        Serial.println("SUNDAY: Fallback mode");
        fallback();
        break;

      case 1: // Monday
        if (period >= P2 && period <= P4) { 
          activeLab = &PY; 
          labActive = true; 
          Serial.println("MONDAY P1-P4: PY lab activated");
        }
        else if (period == P_GM) {
          showDateAndDay(now);
        }
        else {
          fallback();
        }
        break;

      case 2: // Tuesday
        // Tues P1, P2, P3, P4 -> WT
        if (period >= P1 && period <= P4) { 
          activeLab = &WT; 
          labActive = true; 
          Serial.println("TuesDAY P1-4: WT lab activated");
          
          }
        else if (period == P_GM) showDateAndDay(now);
        else {
          Serial.println("TUESDAY: Fallback mode");
          fallback();
        }
        break;
        
      case 3: // Wednesday
        if (period >= P1 && period <= P4) { 
          activeLab = &AP; 
          labActive = true; 
          Serial.println("WEDNESDAY P1-P4: AP lab activated");
        }
        else if (period == P_GM) showDateAndDay(now);
        else fallback();
        break;

      case 4: // Thursday
        if (period == P5_7) { 
          activeLab = &PW; 
          labActive = true; 
          Serial.println("THURSDAY P5-7: PW lab activated");
        }
        else if (period == P_GM) showDateAndDay(now);
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
        else if (period == P_GM) showDateAndDay(now);
        else fallback();
        break;

      case 6: // Saturday
        if (period == P5_7) { 
          activeLab = &PW; 
          labActive = true; 
          Serial.println("SATURDAY P5-7: PW lab activated");
        }
        else if (period == P_GM) showDateAndDay(now);
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
