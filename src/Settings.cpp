/*
Name:		Settings.cpp
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

See Settings.h for usage.

settings.txt format (every key optional - anything missing keeps its compiled-in default):
{
	"WIFI_SSID":"MyNetwork",
	"WIFI_PASSWORD":"My password!",
	"MQTT_SERVER":"192.168.1.135",
	"MQTT_PORT":1883,
	"MQTT_USERNAME":"Garage",
	"MQTT_PASSWORD":"DoorOpener1",
	"DEVICE_NAME":"Gliderol2MQTT",
	"USING_TOP_SENSOR":"Yes",
	"TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED":10000,
	"TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN":10000,
	"RELAY_ACTIVE_LEVEL":"HIGH",
	"BOOT_UP_TARGET_STATE":0
}
For compatibility with older settings files, numbers may also be written as strings ("1883"), USING_TOP_SENSOR
may be true/false as well as "Yes"/"No", and the old NORMALLY_OPEN key is accepted in place of RELAY_ACTIVE_LEVEL.
*/

#include "Settings.h"
#include "Log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h not found - copy include/secrets.h.example to include/secrets.h and fill in your details"
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

namespace settings
{
	namespace
	{
		// Copy a string setting if present and valid.  Returns true if applied.
		bool readString(JsonDocument& doc, const char* key, char* destination, size_t length, bool secret = false)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (!value.is<const char*>())
			{
				LOG("Settings: %s must be a string - ignored\r\n", key);
				return false;
			}

			const char* text = value.as<const char*>();
			if (strlen(text) >= length)
			{
				LOG("Settings: %s is longer than %u characters - ignored\r\n", key, (unsigned)(length - 1));
				return false;
			}

			strlcpy(destination, text, length);
			if (secret)
			{
				LOG("Settings: %s set (value hidden)\r\n", key);
			}
			else
			{
				LOG("Settings: %s = \"%s\"\r\n", key, destination);
			}
			return true;
		}

		// Read a whole number, accepting either a JSON number or a numeric string (older files quoted numbers).
		bool readNumber(JsonDocument& doc, const char* key, long minimum, long maximum, long& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}

			long number;
			if (value.is<long>())
			{
				number = value.as<long>();
			}
			else if (value.is<const char*>())
			{
				char* end;
				const char* text = value.as<const char*>();
				number = strtol(text, &end, 10);
				if (end == text || *end != '\0')
				{
					LOG("Settings: %s is not a whole number - ignored\r\n", key);
					return false;
				}
			}
			else
			{
				LOG("Settings: %s is not a whole number - ignored\r\n", key);
				return false;
			}

			if (number < minimum || number > maximum)
			{
				LOG("Settings: %s = %ld is outside %ld..%ld - ignored\r\n", key, number, minimum, maximum);
				return false;
			}

			result = number;
			LOG("Settings: %s = %ld\r\n", key, number);
			return true;
		}

		// Read a yes/no setting, accepting true/false or "Yes"/"No" (any case).
		bool readYesNo(JsonDocument& doc, const char* key, bool& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (value.is<bool>())
			{
				result = value.as<bool>();
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "Yes") == 0)
			{
				result = true;
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "No") == 0)
			{
				result = false;
			}
			else
			{
				LOG("Settings: %s must be \"Yes\" or \"No\" - ignored\r\n", key);
				return false;
			}
			LOG("Settings: %s = %s\r\n", key, result ? "Yes" : "No");
			return true;
		}

		// Read "HIGH"/"LOW" (any case).
		bool readLevel(JsonDocument& doc, const char* key, int& result)
		{
			JsonVariant value = doc[key];
			if (value.isNull())
			{
				return false;
			}
			if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "HIGH") == 0)
			{
				result = HIGH;
			}
			else if (value.is<const char*>() && strcasecmp(value.as<const char*>(), "LOW") == 0)
			{
				result = LOW;
			}
			else
			{
				LOG("Settings: %s must be \"HIGH\" or \"LOW\" - ignored\r\n", key);
				return false;
			}
			LOG("Settings: %s = %s\r\n", key, result == HIGH ? "HIGH" : "LOW");
			return true;
		}
	}


	Settings defaults()
	{
		Settings s;
		strlcpy(s.wifiSsid, WIFI_SSID, sizeof(s.wifiSsid));
		strlcpy(s.wifiPassword, WIFI_PASSWORD, sizeof(s.wifiPassword));
		strlcpy(s.mqttServer, MQTT_BROKER, sizeof(s.mqttServer));
		s.mqttPort = MQTT_PORT;
		strlcpy(s.mqttUsername, MQTT_USERNAME, sizeof(s.mqttUsername));
		strlcpy(s.mqttPassword, MQTT_PASSWORD, sizeof(s.mqttPassword));
		strlcpy(s.deviceName, DEVICE_NAME, sizeof(s.deviceName));
		s.usingTopSensor = USING_TOP_SENSOR;
		s.doorOpenTimeMs = TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED;
		s.doorCloseTimeMs = TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN;
		s.relayActiveLevel = RELAY_ACTIVE_LEVEL;
		s.bootUpTargetState = BOOT_UP_TARGET_STATE;
		return s;
	}


	LoadResult loadFromSd(Settings& settings)
	{
		LOG("Initialising SD card...");
		if (!SD.begin(PIN_FOR_SDCARD_VSPI_CS))
		{
			LOG("Initialisation failed!\r\n");
			return LoadResult::NoCard;
		}
		LOG("Initialisation complete!\r\n");

		// Open settings.txt in the root.  Note the forward slash.  It is required.
		File file = SD.open("/settings.txt");
		if (!file)
		{
			LOG("settings.txt was not found.\r\n");
			SD.end();
			return LoadResult::NoFile;
		}

		if (file.size() > SETTINGS_FILE_MAX_BYTES)
		{
			LOG("settings.txt is %u bytes, maximum is %d.  Using defaults.\r\n", (unsigned)file.size(), (int)SETTINGS_FILE_MAX_BYTES);
			file.close();
			SD.end();
			return LoadResult::TooBig;
		}

		// Parse straight from the file - no intermediate buffer needed.
		JsonDocument doc;
		DeserializationError error = deserializeJson(doc, file);
		file.close();
		SD.end();

		if (error)
		{
			LOG("settings.txt is not valid JSON (%s).  Using defaults.\r\n", error.c_str());
			return LoadResult::BadJson;
		}

		LOG("settings.txt was found and parsed.\r\n");

		// Apply each setting that is present and valid.  Invalid ones are logged and keep their default.
		long number;
		readString(doc, "WIFI_SSID", settings.wifiSsid, sizeof(settings.wifiSsid));
		readString(doc, "WIFI_PASSWORD", settings.wifiPassword, sizeof(settings.wifiPassword), true);
		readString(doc, "MQTT_SERVER", settings.mqttServer, sizeof(settings.mqttServer));
		if (readNumber(doc, "MQTT_PORT", 1, 65535, number))
		{
			settings.mqttPort = (uint16_t)number;
		}
		readString(doc, "MQTT_USERNAME", settings.mqttUsername, sizeof(settings.mqttUsername));
		readString(doc, "MQTT_PASSWORD", settings.mqttPassword, sizeof(settings.mqttPassword), true);
		readString(doc, "DEVICE_NAME", settings.deviceName, sizeof(settings.deviceName));
		readYesNo(doc, "USING_TOP_SENSOR", settings.usingTopSensor);
		if (readNumber(doc, "TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED", 1000, 120000, number))
		{
			settings.doorOpenTimeMs = (unsigned long)number;
		}
		if (readNumber(doc, "TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN", 1000, 120000, number))
		{
			settings.doorCloseTimeMs = (unsigned long)number;
		}
		// RELAY_ACTIVE_LEVEL wins if both it and the old NORMALLY_OPEN name are present.  (NORMALLY_CLOSED is ignored -
		// the idle level is always the opposite of the active level.)
		if (!readLevel(doc, "RELAY_ACTIVE_LEVEL", settings.relayActiveLevel))
		{
			readLevel(doc, "NORMALLY_OPEN", settings.relayActiveLevel);
		}
		if (readNumber(doc, "BOOT_UP_TARGET_STATE", BOOT_TARGET_STATE_LAST_KNOWN, BOOT_TARGET_STATE_OPEN, number))
		{
			settings.bootUpTargetState = (int)number;
		}

		return LoadResult::Loaded;
	}
}
