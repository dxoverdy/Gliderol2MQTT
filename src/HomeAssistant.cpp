/*
Name:		HomeAssistant.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See HomeAssistant.h for usage.  Config format: https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery
*/

#include "HomeAssistant.h"
#include "Definitions.h"
#include "Log.h"

#include <ArduinoJson.h>

namespace homeassistant
{
	namespace
	{
		constexpr size_t TOPIC_LENGTH = 160;
		constexpr size_t PAYLOAD_LENGTH = 1024;

		// Everything shared by each entity's config: the device it belongs to, and availability via G2M's
		// birth/last-will topic.
		void addCommon(JsonDocument& doc, const char* deviceName, const char* version, const char* macHex, const char* entity)
		{
			char text[TOPIC_LENGTH];

			snprintf(text, sizeof(text), "g2m_%s_%s", macHex, entity);
			doc["unique_id"] = text;

			snprintf(text, sizeof(text), "%s/connection_status", deviceName);
			doc["availability_topic"] = text;
			doc["payload_available"] = "true";
			doc["payload_not_available"] = "false";

			JsonObject device = doc["device"].to<JsonObject>();
			snprintf(text, sizeof(text), "g2m_%s", macHex);
			device["identifiers"].to<JsonArray>().add(text);
			device["name"] = deviceName;
			device["manufacturer"] = "Gliderol2MQTT";
			device["model"] = "Glidermatic garage door interface";
			device["sw_version"] = version;
		}

		// Serialise and publish to <prefix>/<component>/g2m_<mac>/<entity>/config
		bool send(JsonDocument& doc, const char* component, const char* macHex, const char* entity, PublishFn publish)
		{
			char topic[TOPIC_LENGTH];
			char payload[PAYLOAD_LENGTH];

			snprintf(topic, sizeof(topic), "%s/%s/g2m_%s/%s/config", HOME_ASSISTANT_DISCOVERY_PREFIX, component, macHex, entity);

			size_t length = serializeJson(doc, payload, sizeof(payload));
			if (length == 0 || length >= sizeof(payload) - 1)
			{
				LOG("Home Assistant: config for %s is too large\r\n", entity);
				return false;
			}
			return publish(topic, payload);
		}

		// A topic under the device, e.g. "Gliderol2MQTT/device/status"
		const char* topic(char* buffer, const char* deviceName, const char* suffix)
		{
			snprintf(buffer, TOPIC_LENGTH, "%s%s", deviceName, suffix);
			return buffer;
		}

		// A diagnostic sensor reading a field from one of G2M's JSON topics
		bool sendSensor(const char* deviceName, const char* version, const char* macHex, PublishFn publish,
			const char* entity, const char* name, const char* stateSuffix, const char* valueTemplate,
			const char* deviceClass, const char* unit, bool measurement)
		{
			char buffer[TOPIC_LENGTH];
			JsonDocument doc;
			doc["name"] = name;
			doc["state_topic"] = topic(buffer, deviceName, stateSuffix);
			doc["value_template"] = valueTemplate;
			doc["entity_category"] = "diagnostic";
			if (deviceClass != nullptr)
			{
				doc["device_class"] = deviceClass;
			}
			if (unit != nullptr)
			{
				doc["unit_of_measurement"] = unit;
			}
			if (measurement)
			{
				doc["state_class"] = "measurement";
			}
			addCommon(doc, deviceName, version, macHex, entity);
			return send(doc, "sensor", macHex, entity, publish);
		}
	}


	bool publishDiscovery(const char* deviceName, const char* version, const char* macHex, PublishFn publish)
	{
		char buffer[TOPIC_LENGTH];
		bool ok = true;

		// The garage door.  Commands go to set/target/door/state (the same topic HomeKit uses), state comes from
		// get/current/door/state.
		{
			JsonDocument doc;
			doc["name"] = "Door";
			doc["device_class"] = "garage";
			doc["command_topic"] = topic(buffer, deviceName, "/set/target/door/state");
			doc["payload_open"] = "O";
			doc["payload_close"] = "C";
			doc["payload_stop"] = "S";
			doc["state_topic"] = topic(buffer, deviceName, "/get/current/door/state");
			doc["state_open"] = "O";
			doc["state_opening"] = "o";
			doc["state_closed"] = "C";
			doc["state_closing"] = "c";
			doc["state_stopped"] = "S";
			addCommon(doc, deviceName, version, macHex, "door");
			ok = send(doc, "cover", macHex, "door", publish) && ok;
		}

		// Reboot button
		{
			JsonDocument doc;
			doc["name"] = "Reboot";
			doc["device_class"] = "restart";
			doc["entity_category"] = "config";
			doc["command_topic"] = topic(buffer, deviceName, "/request/perform/reboot");
			addCommon(doc, deviceName, version, macHex, "reboot");
			ok = send(doc, "button", macHex, "reboot", publish) && ok;
		}

		// Diagnostics
		ok = sendSensor(deviceName, version, macHex, publish, "rssi", "WiFi signal", "/device/status",
			"{{ value_json.rssi }}", "signal_strength", "dBm", true) && ok;
		ok = sendSensor(deviceName, version, macHex, publish, "free_heap", "Free memory", "/device/status",
			"{{ value_json.free_heap }}", "data_size", "B", true) && ok;
		ok = sendSensor(deviceName, version, macHex, publish, "last_boot", "Last boot", "/device/boot",
			"{{ value_json.time }}", "timestamp", nullptr, false) && ok;
		ok = sendSensor(deviceName, version, macHex, publish, "reset_reason", "Last reset reason", "/device/boot",
			"{{ value_json.reset_reason }}", nullptr, nullptr, false) && ok;

		LOG("Home Assistant discovery %s\r\n", ok ? "published" : "FAILED");
		return ok;
	}
}
