/*
Name:		MqttInterface.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See MqttInterface.h for usage.
*/

#include "MqttInterface.h"
#include "Definitions.h"
#include "Log.h"
#include "BootReport.h"
#include "CommandGuard.h"
#include "HomeAssistant.h"

#include <WiFi.h>
#include <PubSubClient.h>

namespace mqtt_iface
{
	namespace
	{
		WiFiClient _wifiClient;
		PubSubClient _client(_wifiClient);

		const Settings* _settings = nullptr;
		const char* _version = "";
		Actions _actions;

		unsigned long _subscribedAt = 0;
		bool _bootReportPublished = false;
		bool _bootTargetApplied = false;

		// Last states published (or to be published once connected).  Empty until there is one.
		char _currentState[2] = "";
		char _targetState[2] = "";

		constexpr size_t TOPIC_LENGTH = 128;
		constexpr size_t REPLY_LENGTH = 96;
		constexpr size_t PAYLOAD_LENGTH = 16;		// Incoming payloads are at most a single character

		// Topics we publish (suffixes under <DEVICE_NAME>)
		constexpr const char* TOPIC_CONNECTION_STATUS = "/connection_status";			// Birth and last will
		constexpr const char* TOPIC_DEVICE_BOOT = "/device/boot";						// Boot report, retained
		constexpr const char* TOPIC_DEVICE_STATUS = "/device/status";					// Health, retained, every heartbeat
		constexpr const char* TOPIC_CURRENT_STATE = "/get/current/door/state";			// HomeKit current state, retained
		constexpr const char* TOPIC_TARGET_STATE = "/get/target/door/state";			// HomeKit target state, retained

		// MQTT-Thing / HomeKit target state codes
		constexpr const char* DOOR_STATE_TARGET_OPEN = "O";
		constexpr const char* DOOR_STATE_TARGET_CLOSED = "C";

		// Reply topics
		constexpr const char* RESPONSE_PERFORM_OPEN = "/response/perform/open";
		constexpr const char* RESPONSE_PERFORM_CLOSE = "/response/perform/close";
		constexpr const char* RESPONSE_PERFORM_STOP = "/response/perform/stop";


		// Build "<DEVICE_NAME><suffix>"
		void topicFor(char* buffer, const char* suffix)
		{
			snprintf(buffer, TOPIC_LENGTH, "%s%s", _settings->deviceName, suffix);
		}

		bool publish(const char* suffix, const char* payload, bool retain)
		{
			char topic[TOPIC_LENGTH];
			topicFor(topic, suffix);
			bool ok = _client.publish(topic, payload, retain);
			if (!ok)
			{
				LOG("MQTT publish failed to %s\r\n", topic);
			}
			return ok;
		}

		// Device health: uptime, WiFi signal and memory.  Published on connect and with every heartbeat.
		void publishStatus()
		{
			char status[128];
			snprintf(status, sizeof(status), "{\"uptime_s\":%lu,\"rssi\":%d,\"free_heap\":%u,\"min_free_heap\":%u}",
				millis() / 1000, WiFi.RSSI(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
			publish(TOPIC_DEVICE_STATUS, status, true);
		}

		void performReply(char* reply, const char* statusValue)
		{
			snprintf(reply, REPLY_LENGTH, "{\"statusValue\":\"%s\",\"done\":true}", statusValue);
		}

		void boolReply(char* reply, const char* key, bool value)
		{
			snprintf(reply, REPLY_LENGTH, "{\"%s\":%s}", key, value ? "true" : "false");
		}

		void pinReply(char* reply, uint8_t pin)
		{
			snprintf(reply, REPLY_LENGTH, "{\"value\":\"%s\"}", digitalRead(pin) == HIGH ? "high" : "low");
		}


		// ------------------------------------------------------------------------------------------------ Handlers
		// Each handler acts on a message, writes its JSON reply into `reply`, and returns the topic suffix to send the
		// reply to (or nullptr for no reply).

		const char* onPerformOpen(const char*, char* reply)
		{
			LOG("Performing Open\r\n");
			publishTargetState(DOOR_STATE_TARGET_OPEN);
			_actions.open();
			performReply(reply, "setOpenSuccess");
			return RESPONSE_PERFORM_OPEN;
		}

		const char* onPerformClose(const char*, char* reply)
		{
			LOG("Performing Close\r\n");
			publishTargetState(DOOR_STATE_TARGET_CLOSED);
			_actions.close();
			performReply(reply, "setCloseSuccess");
			return RESPONSE_PERFORM_CLOSE;
		}

		const char* onPerformStop(const char*, char* reply)
		{
			LOG("Performing Stop\r\n");
			_actions.stop();
			performReply(reply, "setStopSuccess");
			return RESPONSE_PERFORM_STOP;
		}

		const char* onPerformReboot(const char*, char* reply)
		{
			LOG("Reboot requested\r\n");
			_actions.reboot();
			performReply(reply, "setRebootSuccess");
			return "/response/perform/reboot";
		}

		// Homebridge MQTT-Thing: "In order to monitor accessory responsiveness automatically, enter the time (in
		// milliseconds) to wait after publishing a 'set' topic to receive a confirmatory 'get' topic update."
		// So echo the requested target straight back on the get topic, then act.
		const char* onSetTargetDoorState(const char* payload, char* reply)
		{
			if (strcmp(payload, "O") == 0)
			{
				publishTargetState("O");
				_actions.open();
				performReply(reply, "setOpenSuccess");
				return RESPONSE_PERFORM_OPEN;
			}
			if (strcmp(payload, "C") == 0)
			{
				publishTargetState("C");
				_actions.close();
				performReply(reply, "setCloseSuccess");
				return RESPONSE_PERFORM_CLOSE;
			}
			// HomeKit doesn't support a target of Stopped, but Home Assistant's Stop sends "S" here.  Stop the door, but
			// leave the target alone - HomeKit only accepts O or C as a target, so echoing "S" would upset MQTT-Thing.
			if (strcmp(payload, "S") == 0)
			{
				_actions.stop();
				performReply(reply, "setStopSuccess");
				return RESPONSE_PERFORM_STOP;
			}
			return nullptr;
		}

		const char* onIsOpen(const char*, char* reply)
		{
			boolReply(reply, "open", _actions.doorState() == door::State::Open);
			return "/response/is/open";
		}

		const char* onIsClosed(const char*, char* reply)
		{
			boolReply(reply, "closed", _actions.doorState() == door::State::Closed);
			return "/response/is/closed";
		}

		const char* onIsStopped(const char*, char* reply)
		{
			boolReply(reply, "partiallyopen", _actions.doorState() == door::State::Stopped);
			return "/response/is/stopped";
		}

		// Development: read the relay and sensor pins
		const char* onValuePinOpen(const char*, char* reply) { pinReply(reply, PIN_FOR_GARAGE_DOOR_OPEN); return "/response/value/pin/open"; }
		const char* onValuePinStop(const char*, char* reply) { pinReply(reply, PIN_FOR_GARAGE_DOOR_STOP); return "/response/value/pin/stop"; }
		const char* onValuePinClose(const char*, char* reply) { pinReply(reply, PIN_FOR_GARAGE_DOOR_CLOSE); return "/response/value/pin/close"; }
		const char* onValuePinTopSensor(const char*, char* reply) { pinReply(reply, PIN_FOR_TOP_SENSOR); return "/response/value/pin/top_sensor"; }
		const char* onValuePinBottomSensor(const char*, char* reply) { pinReply(reply, PIN_FOR_BOTTOM_SENSOR); return "/response/value/pin/bottom_sensor"; }
		const char* onValuePinWakeButton(const char*, char* reply) { pinReply(reply, PIN_FOR_DISPLAY_WAKE_BUTTON); return "/response/value/pin/wake_button"; }

#ifdef ENABLE_DEV_TOPICS
		// Development: force a relay on ("set" - as if its button were held down) or off ("clear").  A set holds the
		// relay until a matching clear arrives.
		const char* doneReply(char* reply) { snprintf(reply, REPLY_LENGTH, "{\"done\":true}"); return reply; }
		const char* onSetPinOpen(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_OPEN, true); doneReply(reply); return "/response/set/value/pin/open"; }
		const char* onSetPinStop(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_STOP, true); doneReply(reply); return "/response/set/value/pin/stop"; }
		const char* onSetPinClose(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_CLOSE, true); doneReply(reply); return "/response/set/value/pin/close"; }
		const char* onClearPinOpen(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_OPEN, false); doneReply(reply); return "/response/clear/value/pin/open"; }
		const char* onClearPinStop(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_STOP, false); doneReply(reply); return "/response/clear/value/pin/stop"; }
		const char* onClearPinClose(const char*, char* reply) { _actions.setRelay(PIN_FOR_GARAGE_DOOR_CLOSE, false); doneReply(reply); return "/response/clear/value/pin/close"; }
#endif


		// ------------------------------------------------------------------------------------------------ Routes
		// Every topic we subscribe to.  This one table drives the subscriptions, the retained-message clearing and
		// the routing of incoming messages, so a topic can't be subscribed to but never handled (or vice versa).
		//
		// isCommand: the topic makes the device do something.  Any retained message on it is deleted before we
		// subscribe, and messages arriving within COMMAND_IGNORE_AFTER_SUBSCRIBE_MS of subscribing are ignored - so a
		// stray retained "open" can't open the door, nor a retained "reboot" cause a reboot loop, on reconnect.
		struct Route
		{
			const char* suffix;
			bool isCommand;
			const char* (*handle)(const char* payload, char* reply);
		};

		const Route ROUTES[] =
		{
			{ "/request/perform/open", true, onPerformOpen },
			{ "/request/perform/close", true, onPerformClose },
			{ "/request/perform/stop", true, onPerformStop },
			{ "/request/perform/reboot", true, onPerformReboot },
			{ "/set/target/door/state", true, onSetTargetDoorState },		// Apple HomeKit (Homebridge + MQTT-Thing)

			{ "/request/is/open", false, onIsOpen },
			{ "/request/is/closed", false, onIsClosed },
			{ "/request/is/stopped", false, onIsStopped },

			{ "/request/value/pin/open", false, onValuePinOpen },
			{ "/request/value/pin/stop", false, onValuePinStop },
			{ "/request/value/pin/close", false, onValuePinClose },
			{ "/request/value/pin/top_sensor", false, onValuePinTopSensor },
			{ "/request/value/pin/bottom_sensor", false, onValuePinBottomSensor },
			{ "/request/value/pin/wake_button", false, onValuePinWakeButton },

#ifdef ENABLE_DEV_TOPICS
			{ "/request/set/value/pin/open", true, onSetPinOpen },
			{ "/request/set/value/pin/stop", true, onSetPinStop },
			{ "/request/set/value/pin/close", true, onSetPinClose },
			{ "/request/clear/value/pin/open", true, onClearPinOpen },
			{ "/request/clear/value/pin/stop", true, onClearPinStop },
			{ "/request/clear/value/pin/close", true, onClearPinClose },
#endif
		};


		// ------------------------------------------------------------------------------------------------ Incoming

		void onMessage(char* topic, byte* message, unsigned int length)
		{
			LOG("Topic: %s\r\n", topic);

			// Find the route: the topic must be <DEVICE_NAME><suffix>
			size_t prefixLength = strlen(_settings->deviceName);
			if (strncmp(topic, _settings->deviceName, prefixLength) != 0)
			{
				return;
			}
			const Route* route = nullptr;
			for (const Route& candidate : ROUTES)
			{
				if (strcmp(topic + prefixLength, candidate.suffix) == 0)
				{
					route = &candidate;
					break;
				}
			}
			if (route == nullptr)
			{
				return;
			}

			// Ignore commands which arrive immediately after (re)subscribing - most likely a retained message being
			// replayed by the broker rather than someone pressing a button right now.
			if (route->isCommand && command_guard::shouldIgnore(millis(), _subscribedAt, COMMAND_IGNORE_AFTER_SUBSCRIBE_MS))
			{
				LOG("Ignoring command received immediately after subscribing (probable retained replay)\r\n");
				return;
			}

			// Copy the payload, never more than the buffer can hold
			char payload[PAYLOAD_LENGTH];
			unsigned int copyLength = min(length, (unsigned int)(sizeof(payload) - 1));
			memcpy(payload, message, copyLength);
			payload[copyLength] = '\0';
			LOG("Payload: %s\r\n", payload);

			char reply[REPLY_LENGTH] = "";
			const char* replySuffix = route->handle(payload, reply);
			if (replySuffix != nullptr && reply[0] != '\0')
			{
				// Replies are never retained
				publish(replySuffix, reply, false);
			}
		}


		// ------------------------------------------------------------------------------------------------ Connecting

		bool connect()
		{
			char topic[TOPIC_LENGTH];

			// Make sure any half-dead previous connection is closed first
			_client.disconnect();

			LOG("Attempting MQTT connection...");

			// Connect with a retained last will of "false" on connection_status, published by the broker if we drop
			topicFor(topic, TOPIC_CONNECTION_STATUS);
			bool ok = _client.connect(_settings->deviceName, _settings->mqttUsername, _settings->mqttPassword, topic, 0, true, "false", true);

			if (ok)
			{
				LOG("Connected MQTT\r\n");

				// Birth: connection status of true, retained
				ok = publish(TOPIC_CONNECTION_STATUS, "true", true);

				for (const Route& route : ROUTES)
				{
					topicFor(topic, route.suffix);
					if (route.isCommand)
					{
						// A zero-length retained publish deletes any retained message on that topic.  The broker handles
						// our packets in order, so the delete is processed before the subscribe that follows it.
						ok = ok && _client.publish(topic, "", true);
					}
					ok = ok && _client.subscribe(topic);
				}

				// Start the window during which commands are ignored (second line of defence against replays)
				_subscribedAt = millis();
			}

			if (!ok)
			{
				LOG("MQTT Failed: RC is %d.  Retrying in %lu seconds.\r\n", _client.state(), MQTT_RETRY_INTERVAL / 1000);
				return false;
			}

			uint8_t mac[6];
			char macHex[13];
			WiFi.macAddress(mac);
			snprintf(macHex, sizeof(macHex), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

			// Home Assistant discovery (retained, so re-sending on every connection is harmless)
			if (HOME_ASSISTANT_DISCOVERY)
			{
				homeassistant::publishDiscovery(_settings->deviceName, _version, macHex,
					[](const char* topic, const char* payload) { return _client.publish(topic, payload, true); });
			}

			// Once per boot: publish when/why/which firmware we started with (retained, so it's always there to read)
			if (!_bootReportPublished)
			{
				char macText[15];
				snprintf(macText, sizeof(macText), "0x%s", macHex);

				char bootReport[320];
				if (bootreport::toJson(bootReport, sizeof(bootReport), _version, WiFi.localIP().toString().c_str(), macText, WiFi.RSSI()))
				{
					_bootReportPublished = publish(TOPIC_DEVICE_BOOT, bootReport, true);
					LOG("%s\r\n", bootReport);
				}
			}

			// Target state.  On the first connection apply BOOT_UP_TARGET_STATE - unless the door has already been used
			// since boot, in which case that is the truth.  On every connection re-send the latest target, in case it
			// changed while we were offline (otherwise HomeKit can be left showing e.g. "Opening..." against a door
			// that is long since shut).
			if (!_bootTargetApplied)
			{
				_bootTargetApplied = true;
				if (_targetState[0] == '\0')
				{
					if (_settings->bootUpTargetState == BOOT_TARGET_STATE_CLOSED)
					{
						strlcpy(_targetState, DOOR_STATE_TARGET_CLOSED, sizeof(_targetState));
					}
					else if (_settings->bootUpTargetState == BOOT_TARGET_STATE_OPEN)
					{
						strlcpy(_targetState, DOOR_STATE_TARGET_OPEN, sizeof(_targetState));
					}
				}
			}
			if (_targetState[0] != '\0')
			{
				publish(TOPIC_TARGET_STATE, _targetState, true);
			}

			// And the current state straight away - the broker may have restarted and lost its retained copy
			if (_currentState[0] != '\0')
			{
				publish(TOPIC_CURRENT_STATE, _currentState, true);
			}

			publishStatus();

			return true;
		}
	}


	void begin(const Settings* settings, const char* version, const Actions& actions)
	{
		_settings = settings;
		_version = version;
		_actions = actions;

		_client.setServer(_settings->mqttServer, _settings->mqttPort);
		_client.setBufferSize(MQTT_BUFFER_SIZE);
		_client.setCallback(onMessage);
		_client.setKeepAlive(MQTT_KEEP_ALIVE);
		_client.setSocketTimeout(MQTT_SOCKET_TIMEOUT);
	}

	void service()
	{
		static unsigned long lastAttempt = 0;
		static bool attempted = false;
		static unsigned long lastHeartbeat = 0;

		if (_client.connected())
		{
			// Process incoming messages (may run handlers, which may pulse relays)
			_client.loop();

			// Heartbeat: re-send the current state periodically as a safety net, along with the device's health
			if (millis() - lastHeartbeat >= MQTT_STATUS_INTERVAL)
			{
				lastHeartbeat = millis();
				if (_currentState[0] != '\0')
				{
					publish(TOPIC_CURRENT_STATE, _currentState, true);
				}
				publishStatus();
			}
			return;
		}

		if (WiFi.status() != WL_CONNECTED)
		{
			return;
		}

		if (attempted && millis() - lastAttempt < MQTT_RETRY_INTERVAL)
		{
			return;
		}

		attempted = true;
		lastAttempt = millis();
		if (connect())
		{
			lastHeartbeat = millis();
		}
	}

	bool connected()
	{
		return _client.connected();
	}

	void publishCurrentState(const char* code)
	{
		strlcpy(_currentState, code, sizeof(_currentState));
		if (_client.connected())
		{
			publish(TOPIC_CURRENT_STATE, _currentState, true);
		}
	}

	void publishTargetState(const char* code)
	{
		strlcpy(_targetState, code, sizeof(_targetState));
		if (_client.connected())
		{
			publish(TOPIC_TARGET_STATE, _targetState, true);
		}
	}

	void publishOffline()
	{
		if (_client.connected())
		{
			publish(TOPIC_CONNECTION_STATUS, "false", true);
		}
	}

	void publishOnline()
	{
		if (_client.connected())
		{
			publish(TOPIC_CONNECTION_STATUS, "true", true);
		}
	}

	void disconnect()
	{
		_client.disconnect();
	}
}
