# ESP32 Lab Schedule Display

Automated LED matrix signage built for lab digitalization at GPREC.
It shows the current lab, lecturer, designation and class strength
based on the day and period, and improves the look and usability
of the lab notice area.

## Features
- Timetable-driven: content changes automatically by day and period
- 3 independent MAX7219 LED strips (4 modules each) with scrolling and static text
- "Good Morning" screen showing date and day
- Fallback display when no lab is scheduled
- WiFi-based internet time sync (no RTC module needed)
- DS3231 RTC version for offline use
- Easily modifiable schedule data (labs, lecturers, strengths, timings)
- Non-blocking design using millis() instead of delay()
- Serial debug logging for monitoring

## Hardware
- ESP32 DevKit
- 3 x MAX7219 FC16 4-in-1 LED matrix modules
- DS3231 RTC (RTC version only)

## Wiring
| Strip | DIN | CLK | CS |
|---|---|---|---|
| d1 | 23 | 18 | 5 |
| d2 | 19 | 16 | 4 |
| d3 | 27 | 26 | 25 |

## Libraries
MD_Parola, MD_MAX72XX, RTClib

## Versions
- `firmware/lab_display_rtc`: DS3231 RTC timekeeping
- `firmware/lab_display_wifi`: WiFi internet time (coming from the lab system)

## How it works
Every loop the device reads the time and maps it to a period. When the
day or period changes, it resets its state and picks the right lab.
A 3-phase state machine then cycles through code, name, lecturer and
strength on the strips.

## Modifying the schedule
Lab details live in the `Lab` structs, and day/period mapping is in the
`switch` block in `loop()`. Edit those to match any timetable.
