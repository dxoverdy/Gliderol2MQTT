/*
Name:		MqttInterface.h
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Everything MQTT: connecting (and reconnecting, without blocking), birth/last will, the boot report, subscriptions,
routing incoming messages to handlers, and publishing the door's current and target states.  All topics live under
<DEVICE_NAME>/ - see the README for the full list.

The module doesn't drive hardware itself; the application supplies Actions for that.
*/

#pragma once

#include <stdint.h>
#include "Door.h"
#include "Settings.h"

namespace mqtt_iface
{
	struct Actions
	{
		void (*open)();								// Pulse the open relay and tell the door state machine
		void (*close)();
		void (*stop)();
		void (*reboot)();							// Request a reboot (carried out from loop(), after the reply is sent)
		door::State (*doorState)();					// Current door state, for the request/is/* topics
		void (*setRelay)(uint8_t pin, bool on);		// For the development set/clear topics
	};

	// Configure.  Nothing connects until service() is called with WiFi up.
	void begin(const Settings* settings, const char* version, const Actions& actions);

	// Call every loop.  Connects/reconnects (one attempt every MQTT_RETRY_INTERVAL), processes incoming messages and
	// sends the periodic heartbeat.  Never blocks for long.
	void service();

	bool connected();

	// Publish (retained) the door's current state code ("O", "c", ...) now, and remember it so it is re-sent on
	// reconnect and by the heartbeat.  If offline it is just remembered.
	void publishCurrentState(const char* code);

	// Publish (retained) the target state ("O"/"C") now, and remember it so it is re-sent on reconnect.
	void publishTargetState(const char* code);

	// Publish connection_status false/true ourselves - e.g. around an OTA update or a requested reboot, where the
	// last will (which only fires on an unclean disconnect) won't.
	void publishOffline();
	void publishOnline();

	// Cleanly disconnect from the broker.
	void disconnect();
}
