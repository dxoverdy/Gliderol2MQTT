/*
Name:		Definitions.h
Created:	05/Oct/2022
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Compiled-in settings and hardware definitions.  Credentials are in include/secrets.h (git-ignored).
Most of the Friendly Settings can also be overridden at boot from settings.txt on the SD card - see Settings.h.
*/

#pragma once

#include <Arduino.h>


// **************** Friendly Settings ****************
// The device name is used as the MQTT base topic, the WiFi hostname and the OTA name (<DEVICE_NAME>.local).
// If you need more than one Gliderol2MQTT on your network, give them unique names.
constexpr const char* DEVICE_NAME = "Gliderol2MQTT";

// If there is a top sensor in use, set this to true.  This will enable 'stop' functionality to be reported correctly
// as 'stopped' is somewhere between top and bottom after the duration to open or close has elapsed.  Closing from a fob
// is also supported when using the top sensor.  Otherwise only open/closed can be reported.
constexpr bool USING_TOP_SENSOR = true;

// How long the door takes to open from fully closed, and vice versa (milliseconds).  Used for timing the 'Opening' and
// 'Closing' statuses.  Time your door to the moment the sensor clicks and add a second or two.  A movement is reported
// as finished as soon as the destination sensor confirms arrival, so a generous value costs nothing.
constexpr unsigned long TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED = 10000;
constexpr unsigned long TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN = 10000;

// If the travel time above passes with the door mid-way (neither sensor active), keep reporting Opening/Closing for up
// to this much longer (milliseconds) before reporting Stopped - in case the door is just slow today.  Top sensor only.
constexpr unsigned long MOVEMENT_GRACE_MS = 5000;

// The GPIO level which switches a relay ON (energised - the equivalent of pressing the Glidermatic button).
// Relay boards come in two kinds:
//   Active-high: the relay switches on when the ESP32 pin is HIGH (3.3V).  My current board is this kind.
//   Active-low:  the relay switches on when the ESP32 pin is LOW (GND).  Many cheap optocoupler boards are this kind.
// The idle (off) level is always the opposite, so there is only one setting.
// SD card key: "RELAY_ACTIVE_LEVEL":"HIGH" or "LOW".  (The old "NORMALLY_OPEN" key is still accepted and means the same.)
constexpr int RELAY_ACTIVE_LEVEL = HIGH;

// On boot up you have a choice to either
// Leave the door target state as last known (leaves the target state as is - the current retained value in the MQTT broker) = 0
// Set to Closed = 1
// Set to Open = 2
constexpr int BOOT_TARGET_STATE_LAST_KNOWN = 0;		// Don't change these three
constexpr int BOOT_TARGET_STATE_CLOSED = 1;
constexpr int BOOT_TARGET_STATE_OPEN = 2;
constexpr int BOOT_UP_TARGET_STATE = BOOT_TARGET_STATE_LAST_KNOWN;		// Change this


// **************** More Advanced Settings ****************
// The current state is published (retained) immediately on every change and on every (re)connect, so this periodic
// re-send is only a heartbeat/safety net - e.g. for a broker restarted without persistence.  Once a minute is plenty.
constexpr unsigned long MQTT_STATUS_INTERVAL = 60000;

// "Sets the keep alive interval used by the client." (seconds)
// The broker declares us offline (and publishes the last will and testament) after 1.5 x this with no contact, so
// 15 means ~22 seconds.  Shorter values make brief WiFi blips show as "No Response" in HomeKit and force a full
// reconnect, without making the door any safer - offline is offline.  15 is the library default.
constexpr uint16_t MQTT_KEEP_ALIVE = 15;

// "Sets the socket timeout used by the client. This determines how long the client will wait for incoming data when it
// expects data to arrive - for example, whilst it is in the middle of reading an MQTT packet." (seconds)
constexpr uint16_t MQTT_SOCKET_TIMEOUT = 5;

// While disconnected from the broker, how often to try reconnecting (milliseconds).  The main loop keeps running in
// between - sensors are tracked and the display updated - so this only paces the attempts.
constexpr unsigned long MQTT_RETRY_INTERVAL = 5000;

// The ESP32 reconnects to WiFi by itself, but occasionally gets stuck.  If WiFi has been down this long
// (milliseconds), start the connection afresh.
constexpr unsigned long WIFI_RESTART_AFTER_MS = 30000;

// MQTT packet buffer (bytes).  The largest message is the boot report at around 300 bytes.
constexpr uint16_t MQTT_BUFFER_SIZE = 512;

// How often to refresh the relay/sensor lines on the screen (milliseconds)
constexpr unsigned long DISPLAY_INTERVAL = 250;

// How often to flash the activity indicator on the display (milliseconds)
constexpr unsigned long UPDATE_STATUS_BAR_INTERVAL = 500;

// OLEDs wear (dim, burn in) in proportion to how long their pixels are lit, so the screen switches itself off after
// this long with nothing happening (milliseconds).  It wakes on boot, any door state change, WiFi/MQTT connecting or
// dropping, an OTA update or reboot, and the wake button.  It stays on whenever WiFi or MQTT is down, so a problem is
// always visible.  Set to 0 to keep the screen on permanently.
constexpr unsigned long DISPLAY_TIMEOUT_MS = 120000;

// A sensor reading must hold steady for this many milliseconds before it is believed.
// This filters contact bounce and electrical noise on long sensor cables, which would otherwise
// be reported as the door starting to open/close.
constexpr unsigned long SENSOR_DEBOUNCE_MS = 100;

// A briefly-pressed button must be held this long (milliseconds) to count - shorter than SENSOR_DEBOUNCE_MS so a quick
// press is never missed, but long enough to reject contact bounce and noise.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 30;

// After (re)subscribing, commands arriving within this window are ignored.
// A broker re-delivers retained messages on subscribe, and a stray retained "open" (or "reboot") command must
// never act just because the device reconnected.  Retained commands are also cleared from the broker before
// subscribing; this is a second line of defence.
constexpr unsigned long COMMAND_IGNORE_AFTER_SUBSCRIBE_MS = 1500;

// The same door command (open/close/stop) arriving again within this window (milliseconds) is treated as a repeat -
// e.g. HomeKit/MQTT-Thing sending the same target twice - and the Glidermatic button is not pressed a second time.
// Kept short so a deliberate second press (say, after the door was stopped by the fob) still works.
constexpr unsigned long REPEAT_COMMAND_WINDOW_MS = 2000;

// Over-the-air updates.  The OTA password is OTA_PASSWORD in include/secrets.h, and the device is reachable for uploads
// at <DEVICE_NAME>.local - see the [env:nodemcu-32s-ota] section of platformio.ini.
constexpr uint16_t OTA_PORT = 3232;

// Development-only MQTT topics which directly set/clear the relay pins (request/set/value/pin/* and
// request/clear/value/pin/*).  These can hold a relay energised indefinitely, so they are compiled out
// unless you uncomment this.  The read-only request/value/pin/* topics are always available.
//#define ENABLE_DEV_TOPICS

// For general purpose messaging to the serial port, define DEBUG
#define DEBUG
// For verbose messaging to the serial port, define DEBUG_LEVEL2
//#define DEBUG_LEVEL2

// **************** End of Settings ****************


constexpr size_t SETTING_MAX_WIDTH = 64;			// Longest string setting + 1 (a WPA2 password can be 63 characters)
constexpr size_t SETTINGS_FILE_MAX_BYTES = 4096;	// settings.txt files larger than this are rejected
constexpr size_t OLED_CHARACTER_WIDTH = 11;			// Characters per line on the display (10) + 1


/*
CAT5e Colour		RJ 45 Pin		Function				ESP32 Pin
Orange White		1				12V
Green				6				Relay 1 (Up (Open)		GPIO32
Green White			3				Relay 2 (Stop)			GPIO33
Blue				4				Relay 3 (Down (Close))	GPIO25
Brown				8				GND
*/

// Up Pin (Relay 1) Open
constexpr uint8_t PIN_FOR_GARAGE_DOOR_OPEN = 32;
// Stop Pin (Relay 2) Stop
constexpr uint8_t PIN_FOR_GARAGE_DOOR_STOP = 33;
// Down Pin (Relay 3) Close
constexpr uint8_t PIN_FOR_GARAGE_DOOR_CLOSE = 25;

// Opened Sensor
constexpr uint8_t PIN_FOR_TOP_SENSOR = 26;
// Closed Sensor
constexpr uint8_t PIN_FOR_BOTTOM_SENSOR = 27;
// Chip Select (VSPI for Micro-SD Card Reader)
constexpr uint8_t PIN_FOR_SDCARD_VSPI_CS = 5;
// Optional display wake button: a normally-open push button between this pin and GND.  Uses the internal pull-up,
// so nothing else is needed, and with no button fitted it simply never triggers.
// GPIO4 was chosen as it has no boot-strapping role, no JTAG role, isn't used for flash/PSRAM on WROOM or WROVER
// modules, and (unlike GPIO34-39) has an internal pull-up.
constexpr uint8_t PIN_FOR_DISPLAY_WAKE_BUTTON = 4;
