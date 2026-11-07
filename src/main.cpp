/*
Name:		main.cpp
Created:	05/Oct/2022
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Wires the modules together and owns the hardware: relays, sensors and WiFi.

	Settings		settings.txt / compiled-in defaults				include/Settings.h
	door::Door		door state machine (hardware-free, unit tested)	lib/DoorLogic/Door.h
	mqtt_iface		MQTT connection, topics and handlers			include/MqttInterface.h
	display			OLED, screen timeout, wake button				include/Display.h
	ota				over-the-air updates							include/Ota.h
	bootreport		boot report contents							include/BootReport.h

First, go and customise options at the top of Definitions.h, and create include/secrets.h!
*/

#include "Definitions.h"
#include "Log.h"
#include "Settings.h"
#include "Display.h"
#include "MqttInterface.h"
#include "Ota.h"
#include "BootReport.h"
#include "Door.h"
#include "Debouncer.h"

#include <Arduino.h>
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h not found - copy include/secrets.h.example to include/secrets.h and fill in your details"
#endif


// Device parameters
const char* VERSION = "v2.0";

// Customisable settings: compiled-in defaults, overridden by the SD card (see Settings.h)
Settings _settings = settings::defaults();

// The door state machine.  Re-configured from the settings once they've been loaded in setup().
door::Door _door({ USING_TOP_SENSOR, TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED, TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN, MOVEMENT_GRACE_MS });

// Door sensors.  Naturally pulled HIGH; LOW when the magnet is present / the switch is made.
Debouncer _bottomSensor(SENSOR_DEBOUNCE_MS);
Debouncer _topSensor(SENSOR_DEBOUNCE_MS);

// Set by an MQTT reboot request; actioned from loop() once the reply has been sent
bool _rebootRequested = false;


/*
Relays
*/

// Put all three relays into their released (idle) state.
void releaseAllRelays()
{
	digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _settings.relayIdleLevel());
	digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _settings.relayIdleLevel());
	digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _settings.relayIdleLevel());
}

// "Press" one of the Glidermatic's buttons: make sure the other two relays are released, then switch this one on for a
// quarter of a second.
void pulseRelay(uint8_t pin)
{
	const uint8_t relays[] = { PIN_FOR_GARAGE_DOOR_OPEN, PIN_FOR_GARAGE_DOOR_STOP, PIN_FOR_GARAGE_DOOR_CLOSE };
	for (uint8_t other : relays)
	{
		if (other != pin)
		{
			digitalWrite(other, _settings.relayIdleLevel());
			delay(50);
		}
	}
	digitalWrite(pin, _settings.relayActiveLevel);
	delay(250);
	digitalWrite(pin, _settings.relayIdleLevel());
}


/*
Actions for the MQTT interface
*/

void actionOpen()
{
	pulseRelay(PIN_FOR_GARAGE_DOOR_OPEN);
	_door.commandOpen(millis());
}

void actionClose()
{
	pulseRelay(PIN_FOR_GARAGE_DOOR_CLOSE);
	_door.commandClose(millis());
}

void actionStop()
{
	pulseRelay(PIN_FOR_GARAGE_DOOR_STOP);
	_door.commandStop(millis());
}

void actionReboot()
{
	// Don't restart from inside the MQTT callback - the reply would never be sent.  loop() does it next.
	_rebootRequested = true;
}

door::State actionDoorState()
{
	return _door.state();
}

void actionSetRelay(uint8_t pin, bool on)
{
	digitalWrite(pin, on ? _settings.relayActiveLevel : _settings.relayIdleLevel());
}


/*
OTA hooks - called by the Ota module.
*/

void onOtaStart()
{
	// Whatever happens next, the door must not be left with a relay energised.
	releaseAllRelays();

	// Tell home automation we're going offline.  The birth message will set this back to true after the reboot.
	mqtt_iface::publishOffline();

	display::wake();
	display::showLines("OTA", "Updating", "0%");
}

void onOtaProgress(uint8_t percent)
{
	char line[OLED_CHARACTER_WIDTH];

	// Each redraw costs ~25ms of I2C, so only every 5% rather than every 1%
	if (percent % 5 == 0)
	{
		snprintf(line, sizeof(line), "%u%%", (unsigned)percent);
		display::showLines("OTA", "Updating", line);
	}
}

void onOtaEnd(bool success)
{
	if (success)
	{
		display::showLines("OTA", "Done", "Rebooting");
	}
	else
	{
		display::showLines("OTA", "FAILED", "Resuming..");

		// We said we were going offline - we're not, so correct that.
		mqtt_iface::publishOnline();
		delay(2000);
	}
}


/*
WiFi
*/

// Begin connecting to WiFi.  Returns immediately - serviceWifi() tracks the connection from then on.
void startWifi()
{
	LOG("Connecting to %s\r\n", _settings.wifiSsid);

	// Set the hostname for this device.  On the ESP32 this only takes effect if done BEFORE WiFi.mode()/WiFi.begin().
	WiFi.setHostname(_settings.deviceName);

	// Set up in Station Mode - Will be connecting to an access point
	WiFi.mode(WIFI_STA);

	// Keep the radio awake.  By default the ESP32 uses modem-sleep, where the radio dozes between router beacons.
	// That makes it slow to respond and, worse, it routinely misses multicast traffic - which is what mDNS uses, so
	// <device>.local stops resolving and OTA uploads by name fail.  The extra ~80mA is irrelevant on mains/12V power.
	WiFi.setSleep(false);

	WiFi.setAutoReconnect(true);
	WiFi.begin(_settings.wifiSsid, _settings.wifiPassword);
}

// Called each time the WiFi connection comes up.
void onWifiConnected()
{
	static bool firstConnection = true;

	LOG("WiFi connected, IP: %s\r\n", WiFi.localIP().toString().c_str());

	if (firstConnection)
	{
		firstConnection = false;

		// Start listening for over-the-air updates
		ota::Hooks otaHooks;
		otaHooks.onStart = onOtaStart;
		otaHooks.onProgress = onOtaProgress;
		otaHooks.onEnd = onOtaEnd;
		ota::begin(_settings.deviceName, OTA_PASSWORD, OTA_PORT, otaHooks);

		// Start NTP so the boot report can carry a real timestamp.  It keeps itself in sync after this.
		bootreport::startClock();
	}
}

// Call often; never blocks.  Notices the WiFi connection coming up or dropping, and restarts the connection if the
// ESP32's own automatic reconnection appears to be stuck.
void serviceWifi()
{
	static bool wasConnected = false;
	static unsigned long disconnectedSince = 0;

	bool connected = (WiFi.status() == WL_CONNECTED);

	if (connected && !wasConnected)
	{
		onWifiConnected();
	}
	else if (!connected && wasConnected)
	{
		LOG("WiFi connection lost - reconnecting in the background\r\n");
		disconnectedSince = millis();
	}
	else if (!connected && millis() - disconnectedSince >= WIFI_RESTART_AFTER_MS)
	{
		LOG("WiFi still down - restarting the connection\r\n");
		WiFi.disconnect();
		WiFi.begin(_settings.wifiSsid, _settings.wifiPassword);
		disconnectedSince = millis();
	}

	wasConnected = connected;
}

// Like delay(), but keeps WiFi and the OTA listener serviced so the device stays reflashable during long waits.
void delayServicingNetwork(unsigned long ms)
{
	unsigned long start = millis();
	while (millis() - start < ms)
	{
		serviceWifi();
		ota::handle();
		delay(10);
	}
}


/*
Reboot (requested over MQTT)
*/
void rebootNow()
{
	LOG("Rebooting on request\r\n");

	releaseAllRelays();
	display::wake();
	display::showLines("", "Rebooting", "...");

	// A clean disconnect does NOT trigger the last will, so say we're going offline ourselves.
	// The birth message sets this back to true once we're back.
	mqtt_iface::publishOffline();
	mqtt_iface::disconnect();

	// Give the network stack a moment to send the final packets
	delay(250);
	ESP.restart();
}


/*
Door
*/

// Read the sensors into the door state machine and publish whatever it reports.
void serviceDoor()
{
	static char lastPublished[2] = "";

	unsigned long now = millis();
	bool bottomActive = (_bottomSensor.update(digitalRead(PIN_FOR_BOTTOM_SENSOR), now) == LOW);
	bool topActive = _settings.usingTopSensor && (_topSensor.update(digitalRead(PIN_FOR_TOP_SENSOR), now) == LOW);

	_door.update(bottomActive, topActive, now);

	// A movement started by the fob/buttons: let HomeKit know the new target
	char target;
	if (_door.takeTargetChange(target))
	{
		char code[2] = { target, '\0' };
		mqtt_iface::publishTargetState(code);
	}

	// Publish the current state when it changes, or when the door asks (e.g. after a command)
	const char* code = door::homekitCode(_door.state());
	if (_door.takePublishRequest() || strcmp(code, lastPublished) != 0)
	{
		strlcpy(lastPublished, code, sizeof(lastPublished));
		mqtt_iface::publishCurrentState(code);
	}
}

// The normal operating screen: relay pins, sensor pins and the door state.
void updateScreen()
{
	static unsigned long lastRun = 0;

	if (millis() - lastRun < DISPLAY_INTERVAL)
	{
		return;
	}
	lastRun = millis();

	auto level = [](uint8_t pin) { return digitalRead(pin) == HIGH ? 'H' : 'L'; };

	char relaysLine[OLED_CHARACTER_WIDTH];
	char pinsLine[OLED_CHARACTER_WIDTH];

	snprintf(relaysLine, sizeof(relaysLine), "O%c, S%c, C%c", level(PIN_FOR_GARAGE_DOOR_OPEN), level(PIN_FOR_GARAGE_DOOR_STOP), level(PIN_FOR_GARAGE_DOOR_CLOSE));
	if (_settings.usingTopSensor)
	{
		snprintf(pinsLine, sizeof(pinsLine), "T%c, B%c", level(PIN_FOR_TOP_SENSOR), level(PIN_FOR_BOTTOM_SENSOR));
	}
	else
	{
		snprintf(pinsLine, sizeof(pinsLine), "B%c", level(PIN_FOR_BOTTOM_SENSOR));
	}
	display::showLines(relaysLine, pinsLine, door::description(_door.state()));
}


/*
setup()

The setup function runs once when you press reset or power the board
*/
void setup()
{
	// FIRST, before anything else: drive the relay pins to their released state.  Until pinMode() is called the
	// pins float, and what a floating input does to the relay board is undefined.  These use the compiled-in
	// polarity - the SD card hasn't been read yet - and are re-applied below once settings are known.
	releaseAllRelays();
	pinMode(PIN_FOR_GARAGE_DOOR_CLOSE, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_OPEN, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_STOP, OUTPUT);

	// Sensor inputs.  Naturally pulled HIGH; when the sensors are activated they go LOW.
	pinMode(PIN_FOR_BOTTOM_SENSOR, INPUT_PULLUP);
	pinMode(PIN_FOR_TOP_SENSOR, INPUT_PULLUP);

	// Turn on the OLED and show the version
	display::begin();
	display::showLines("", "", VERSION);

	// Serial for debugging
	Serial.begin(115200);

	// Record why we reset and bump the persistent boot counter (published to MQTT once connected)
	bootreport::begin();

	// Load settings from the SD card, if there is one.  Any problem leaves the compiled-in defaults in place.
	switch (settings::loadFromSd(_settings))
	{
	case settings::LoadResult::Loaded:
	case settings::LoadResult::NoFile:
		break;
	case settings::LoadResult::NoCard:
		display::showLines("NO SD CARD", "Using", "Defaults..");
		delay(4000);
		break;
	case settings::LoadResult::TooBig:
		display::showLines("SETTINGS", "TOO BIG", "Defaults..");
		delay(4000);
		break;
	case settings::LoadResult::BadJson:
		display::showLines("SETTINGS", "BAD JSON", "Defaults..");
		delay(4000);
		break;
	}

	// Re-apply the released state now the SD card may have changed the relay polarity
	releaseAllRelays();

	// Configure the door from the (possibly SD card) settings
	_door = door::Door({ _settings.usingTopSensor, _settings.doorOpenTimeMs, _settings.doorCloseTimeMs, MOVEMENT_GRACE_MS });

	// MQTT.  The connection itself is made (and remade) from loop().
	mqtt_iface::Actions actions;
	actions.open = actionOpen;
	actions.close = actionClose;
	actions.stop = actionStop;
	actions.reboot = actionReboot;
	actions.doorState = actionDoorState;
	actions.setRelay = actionSetRelay;
	mqtt_iface::begin(&_settings, VERSION, actions);

	// Start connecting to WiFi.  This doesn't wait: serviceWifi() notices when the connection comes up and starts
	// OTA and NTP, so the device becomes reflashable during the settle wait below.
	startWifi();

	// We will pause here for the longest of the opening/closing durations
	// Because on boot-up, we've no way of determining what is happening
	// So if we wait here until we can be assured the door isn't mid way opening/closing
	// We can reliably go on to determine state going foward.  WiFi and OTA are serviced during the wait.
	display::showLines("", "Determine", "status...");
	LOG("Determining status (waiting %lu ms)\r\n", _settings.longestDoorTimeMs());
	unsigned long settleStart = millis();
	delayServicingNetwork(_settings.longestDoorTimeMs());
	LOG("Determining status complete after %lu ms\r\n", millis() - settleStart);
	(void)settleStart;
	display::showLines("", "", VERSION);
}


/*
loop()

The loop function runs over and over again until power down or reset.  Nothing in here waits on the network,
so the sensors and display stay live through WiFi or broker outages.
*/
void loop()
{
	// Service over-the-air updates first, so a bug further down can't lock us out of remote flashing
	ota::handle();

	// Keep WiFi up
	serviceWifi();

	// Sensors -> door state machine -> MQTT
	serviceDoor();

	// Keep MQTT connected and process any incoming messages (which may action door commands)
	mqtt_iface::service();

	// A reboot requested over MQTT happens here, after its reply has been sent
	if (_rebootRequested)
	{
		rebootNow();
	}

	// Screen
	updateScreen();
	display::service(WiFi.status() == WL_CONNECTED, mqtt_iface::connected(), _door.state());
}
