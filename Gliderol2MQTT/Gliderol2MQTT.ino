/*
Name:		Gliderol2MQTT.ino
Created:	05/Oct/2022
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Notes

First, go and customise options at the top of Definitions.h!
*/

// Supporting files
#include "Definitions.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>

#include <SD.h>

// Device parameters
char _version[6] = "v1.0";

// WiFi parameters
WiFiClient _wifi;

// MQTT parameters
PubSubClient _mqtt(_wifi);


// Buffer Size (and therefore payload size calc)
int _bufferSize;
int _maxPayloadSize;


// I want to declare this once at a modular level, keep the heap somewhere in check.
//char _mqttPayload[MAX_MQTT_PAYLOAD_SIZE] = "";
char* _mqttPayload;

// OLED variables
char _oledOperatingIndicator = '*';
char _oledLine2[OLED_CHARACTER_WIDTH] = "";
char _oledLine3[OLED_CHARACTER_WIDTH] = "";
char _oledLine4[OLED_CHARACTER_WIDTH] = "";


// Customisable settings
char _wifiSSID[SETTING_MAX_WIDTH] = WIFI_SSID;
char _wifiPassword[SETTING_MAX_WIDTH] = WIFI_PASSWORD;
char _mqttServer[SETTING_MAX_WIDTH] = MQTT_SERVER;
int _mqttPort = MQTT_PORT;
char _mqttUsername[SETTING_MAX_WIDTH] = MQTT_USERNAME;
char _mqttPassword[SETTING_MAX_WIDTH] = MQTT_PASSWORD;
char _deviceName[SETTING_MAX_WIDTH] = DEVICE_NAME;
bool _usingTopSensor = USING_TOP_SENSOR;
int _doorCloseTime = TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN;
int _doorOpenTime = TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED;
int _normallyOpen = NORMALLY_OPEN;
int _normallyClosed = NORMALLY_CLOSED;
int _bootUpTargetState = BOOT_UP_TARGET_STATE;

// When _forceMqttOnce = true, will force a one time MQTT publish of current state outside of the typical timer
bool _forceMqttOnce = false;

// Fixed char array for messages to the serial port
char _debugOutput[DEBUG_MAX_LENGTH];

// Door State
bool _firstBoot = true;

unsigned long _mqttOpeningClosingManagementTimer = 0;
doorState _mqttOpeningClosingManagement = doorState::doorStateUnknown;

// Previously known state of the door
bool _previousIsOpen;
bool _previousIsStopped;
bool _previousIsClosed;

// Current door state for sending over MQTT/display
doorState _doorState = doorState::doorStateUnknown;
char _doorStateDesc[DOOR_STATE_MAX_LENGTH] = "";
char _doorStateHomeKit[2] = DOOR_STATE_HOMEKIT_UNKNOWN;

char _macAddress[15] = "0xFFFFFFFFFFFF"; // 0xFFAAEEFFVVAA



#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO
#define I2C_ADDRESS 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
Adafruit_SH1106G _display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);








/*
setup()

The setup function runs once when you press reset or power the board
*/
void setup()
{
	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = "";

	// Turn on the OLED
	_display.begin(I2C_ADDRESS, true); 
	_display.display();
	_display.clearDisplay();
	
	// Display the version on screen
	updateOLED(false, "", "", _version);

	// Set up serial for debugging using an appropriate baud rate
	// This is for communication with the development environment, NOT the Alpha system
	// See Definitions.h for this.
	Serial.begin(115200);




	// Micro-SD Card stuff from here
	char fileContents[SETTINGS_FILE_BUFFER];
	unsigned int fileContentsLength;

	// Variables for new JSON parser
	int iSegNameCounter;
	int iSegValueCounter;
	int iPairNameCounter;
	int iPairValueCounter;
	int iCleanCounter;
	int iCounter;

	// All are emptied on creation as new arrays will just tend to have garbage in which would be recognised as actual content.
	char pairNameRaw[SETTING_MAX_WIDTH] = "";
	char pairNameClean[SETTING_MAX_WIDTH] = "";
	char pairValueRaw[SETTING_MAX_WIDTH] = "";
	char pairValueClean[SETTING_MAX_WIDTH] = "";

	File myFile;
#ifdef DEBUG
	Serial.print("Initialising SD card...");
#endif
	if (!SD.begin(PIN_FOR_SDCARD_VSPI_CS))
	{
#ifdef DEBUG
		Serial.println("Initialisation failed!");
#endif
		// No SD Card or other failure, display a warning for four seconds on the screen
		updateOLED(false, "NO SD CARD", "Using", "Defaults..");
		delay(4000);

	}
	else
	{
#ifdef DEBUG
		Serial.println("Initialisation complete!");
#endif

		// SD Card is a go, open settings.txt in the root.  Note the forward slash.  It is required.
		myFile = SD.open("/settings.txt");
		if (myFile)
		{
#ifdef DEBUG
			Serial.println("settings.txt was found.");
#endif


			/*
File Format:
{
	"WIFI_SSID":"",
	"WIFI_PASSWORD":"",
	"MQTT_SERVER":"192.168.1.40",
	"MQTT_PORT":1883,
	"MQTT_USERNAME":"Alpha",
	"MQTT_PASSWORD":"Inverter1",
	"DEVICE_NAME":"Gliderol2MQTT",
	"USING_TOP_SENSOR":"Yes",
	"TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED":10000,
	"TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN":10000,
	"NORMALLY_OPEN": "HIGH",
	"NORMALLY_CLOSED": "LOW",
	"BOOT_UP_TARGET_STATE" : 0
}
			*/

			// Get the file size
			fileContentsLength = myFile.size();
			// Read the file into the buffer
			myFile.readBytes(fileContents, fileContentsLength);
			// Add the terminating null char
			fileContents[fileContentsLength] = '\0';
#ifdef DEBUG_LEVEL2
			// Print the file to the serial monitor.
			Serial.println(fileContentsLength);
			Serial.println(fileContents);
#endif
			// Close the file
			myFile.close();


			// Rudimentary JSON parser here, saves on using a library
			// Go through character by character
			for (iCounter = 0; iCounter < fileContentsLength; iCounter++)
			{
				// Find a colon
				if (fileContents[iCounter] == ':')
				{
					// Everything to left is name until reached the start, a comma or a left brace.
					for (iSegNameCounter = iCounter - 1; iSegNameCounter >= 0; iSegNameCounter--)
					{
						if (fileContents[iSegNameCounter] == ',' || fileContents[iSegNameCounter] == '{')
						{
							iSegNameCounter++;
							break;
						}
					}
					if (iSegNameCounter < 0)
					{
						// If went beyond the start, correct
						iSegNameCounter = 0;
					}
					// Segment name is now from the following character until before the colon
					iPairNameCounter = 0;
					for (int x = iSegNameCounter; x < iCounter; x++)
					{
						pairNameRaw[iPairNameCounter] = fileContents[x];
						iPairNameCounter++;
					}
					pairNameRaw[iPairNameCounter] = '\0';

					// Everything to right is value until reached the end, a comma or a right brace.
					for (iSegValueCounter = iCounter + 1; iSegValueCounter < fileContentsLength; iSegValueCounter++)
					{
						if (fileContents[iSegValueCounter] == ',' || fileContents[iSegValueCounter] == '}')
						{
							iSegValueCounter--;
							break;
						}
					}
					// Correct if went beyond the end
					if (iSegValueCounter >= fileContentsLength)
					{
						// If went beyond end, correct
						iSegValueCounter = fileContentsLength - 1;
					}
					// Segment value is now from the after the colon until the found character
					iPairValueCounter = 0;
					for (int x = iCounter + 1; x <= iSegValueCounter; x++)
					{
						pairValueRaw[iPairValueCounter] = fileContents[x];
						iPairValueCounter++;
					}
					pairValueRaw[iPairValueCounter] = '\0';


					iPairNameCounter = 0;
					iCleanCounter = 0;
					while (pairNameRaw[iCleanCounter] != 0)
					{
						// Allow alpha numeric, upper case and lower case and underscore
						if ((pairNameRaw[iCleanCounter] >= 'a' && pairNameRaw[iCleanCounter] <= 'z') || (pairNameRaw[iCleanCounter] >= 'A' && pairNameRaw[iCleanCounter] <= 'Z') || (pairNameRaw[iCleanCounter] >= '0' && pairNameRaw[iCleanCounter] <= '9') || pairNameRaw[iCleanCounter] == '_')
						{
							// Transfer Over
							pairNameClean[iPairNameCounter] = pairNameRaw[iCleanCounter];
							iPairNameCounter++;
						}
						iCleanCounter++;
					}
					pairNameClean[iPairNameCounter] = '\0';



					iPairValueCounter = 0;
					iCleanCounter = 0;
					while (pairValueRaw[iCleanCounter] != 0)
					{
						// Allow alpha numeric, upper case and lower case and underscore, full stop, and @ 
						if ((pairValueRaw[iCleanCounter] >= 'a' && pairValueRaw[iCleanCounter] <= 'z') || (pairValueRaw[iCleanCounter] >= 'A' && pairValueRaw[iCleanCounter] <= 'Z') || (pairValueRaw[iCleanCounter] >= '0' && pairValueRaw[iCleanCounter] <= '9') || pairValueRaw[iCleanCounter] == '_' || pairValueRaw[iCleanCounter] == '.' || pairValueRaw[iCleanCounter] == '@')
						{
							// Transfer Over
							pairValueClean[iPairValueCounter] = pairValueRaw[iCleanCounter];
							iPairValueCounter++;
						}
						iCleanCounter++;
					}
					pairValueClean[iPairValueCounter] = '\0';

#ifdef DEBUG_LEVEL2
					sprintf(_debugOutput, "Clean Name: \"%s\", Clean Value: \"%s\"", pairNameClean, pairValueClean);
					Serial.println(_debugOutput);
#endif

					if (strncmp(pairNameClean, "WIFI_SSID", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got WIFI_SSIS, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_wifiSSID, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "WIFI_PASSWORD", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got WIFI_PASSWORD, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_wifiPassword, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "MQTT_SERVER", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_SERVER, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_mqttServer, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "MQTT_PORT", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_PORT, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_mqttPort = atoi(pairValueClean);
#ifdef DEBUG_LEVEL2
						Serial.println(_mqttPort);
#endif
					}
					else if (strncmp(pairNameClean, "MQTT_USERNAME", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_USERNAME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_mqttUsername, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "MQTT_PASSWORD", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_PASSWORD, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_mqttPassword, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "DEVICE_NAME", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got DEVICE_NAME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strncpy(_deviceName, pairValueClean, SETTING_MAX_WIDTH);
					}
					else if (strncmp(pairNameClean, "USING_TOP_SENSOR", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got USING_TOP_SENSOR, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_usingTopSensor = strncmp(pairValueClean, "Yes", SETTING_MAX_WIDTH) == 0 ? true : false;
#ifdef DEBUG_LEVEL2
						Serial.println(_usingTopSensor);
#endif
					}
					else if (strncmp(pairNameClean, "TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_doorCloseTime = atoi(pairValueClean);
#ifdef DEBUG_LEVEL2
						Serial.println(_doorCloseTime);
#endif
					}
					else if (strncmp(pairNameClean, "TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_doorOpenTime = atoi(pairValueClean);
#ifdef DEBUG_LEVEL2
						Serial.println(_doorOpenTime);
#endif
					}
					else if (strncmp(pairNameClean, "NORMALLY_OPEN", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got NORMALLY_OPEN, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_normallyOpen = strncmp(pairValueClean, "HIGH", SETTING_MAX_WIDTH) == 0 ? HIGH : LOW;
#ifdef DEBUG_LEVEL2
						Serial.println(_normallyOpen);
#endif
					}
					else if (strncmp(pairNameClean, "NORMALLY_CLOSED", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got NORMALLY_CLOSED, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_normallyClosed = strncmp(pairValueClean, "HIGH", SETTING_MAX_WIDTH) == 0 ? HIGH : LOW;
#ifdef DEBUG_LEVEL2
						Serial.println(_normallyClosed);
#endif
					}
					else if (strncmp(pairNameClean, "BOOT_UP_TARGET_STATE", SETTING_MAX_WIDTH) == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got BOOT_UP_TARGET_STATE, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_bootUpTargetState = atoi(pairValueClean);
#ifdef DEBUG_LEVEL2
						Serial.println(_bootUpTargetState);
#endif
					}
				}
			}

		}
		else
		{
			// if the file didn't open, print an error:
#ifdef DEBUG
			Serial.println("settings.txt was not found.");
#endif
		}

	}
	
	SD.end();





	// We will pause here for the longest of the opening/closing durations
	// Because on boot-up, we've no way of determining what is happening
	// So if we wait here until we can be assured the door isn't mid way opening/closing
	// We can reliably go on to determine state going foward
	updateOLED(false, "", "Determine", "status...");
#ifdef DEBUG
	Serial.println("Determining status");
#endif
	delay(_doorCloseTime > _doorOpenTime ? _doorCloseTime : _doorOpenTime);
	updateOLED(false, "", "", _version);
#ifdef DEBUG
	Serial.println("Determining status complete");
#endif


	// Set appropriate pins for output
	digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _normallyClosed);
	digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _normallyClosed);
	digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _normallyClosed);
	pinMode(PIN_FOR_GARAGE_DOOR_CLOSE, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_OPEN, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_STOP, OUTPUT);

	// And for input from the sensors.  Naturally pulled to HIGH, when the sensors are activated they will be go LOW.
	pinMode(PIN_FOR_BOTTOM_SENSOR, INPUT_PULLUP);
	pinMode(PIN_FOR_TOP_SENSOR, INPUT_PULLUP);

	// Configure WIFI
	setupWifi();


	// One time MQTT configuration on boot
	// Configure MQTT to the address and port specified in the config
	_mqtt.setServer(_mqttServer, _mqttPort);


#ifdef DEBUG_LEVEL2
	sprintf(_debugOutput, "About to request buffer");
	Serial.println(_debugOutput);
#endif
	_bufferSize = (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE);
#ifdef DEBUG_LEVEL2
		sprintf(_debugOutput, "Requesting a buffer of : %d bytes", _bufferSize);
		Serial.println(_debugOutput);
#endif
	_mqtt.setBufferSize(_bufferSize);
	_maxPayloadSize = _bufferSize - MQTT_HEADER_SIZE;
#ifdef DEBUG_LEVEL2
			sprintf(_debugOutput, "_bufferSize: %d,\r\n\r\n_maxPayload (Including null terminator): %d", _bufferSize, _maxPayloadSize);
			Serial.println(_debugOutput);
#endif
	// Example, 2048, if declared as 2048 is positions 0 to 2047, and position 2047 needs to be zero.  2047 usable chars in payload.
	_mqttPayload = new char[_maxPayloadSize];
	emptyPayload();
	

	// And any messages we are subscribed to will be pushed to the mqttCallback function for processing
	_mqtt.setCallback(mqttCallback);

	// Set custom timeouts
	_mqtt.setKeepAlive(MQTT_KEEP_ALIVE);
	_mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT);

	// Connect to MQTT
	mqttReconnect();


	// Boot up target state?
	if (_bootUpTargetState == BOOT_TARGET_STATE_CLOSED)
	{
#ifdef DEBUG
	sprintf(_debugOutput, "Setting boot target state Closed");
	Serial.println(_debugOutput);
#endif
		// Send out target closed
		// Let HomeKit know of the updated target state
		emptyPayload();
		addToPayload(DOOR_STATE_HOMEKIT_CLOSED);
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse, true);
		emptyPayload();

	}
	else if (_bootUpTargetState == BOOT_TARGET_STATE_OPEN)
	{
#ifdef DEBUG
	sprintf(_debugOutput, "Setting boot target state Open");
	Serial.println(_debugOutput);
#endif
		// Send out target open
		// Let HomeKit know of the updated target state
		emptyPayload();
		addToPayload(DOOR_STATE_HOMEKIT_OPEN);
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse, true);
		emptyPayload();
	}


	updateOLED(false, "", "", _version);
}





/*
setupWifi()

Connect to WiFi
*/
void setupWifi()
{
	// We start by connecting to a WiFi network
#ifdef DEBUG
	sprintf(_debugOutput, "Connecting to %s", _wifiSSID);
	Serial.println(_debugOutput);
#endif

	// Set up in Station Mode - Will be connecting to an access point
	WiFi.mode(WIFI_STA);

	// And connect to the details defined at the top
	WiFi.begin(_wifiSSID, _wifiPassword);

	// And continually try to connect to WiFi.  If it doesn't, the device will just wait here before continuing
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(250);
		updateOLED(false, "Connecting", "WiFi...", _version);
	}

	// Set the hostname for this Arduino
	WiFi.hostname(_deviceName);

	// Get the mac to use a unique device identifier in Home Assistant in potential future developments
	uint8_t macAddress[WL_MAC_ADDR_LENGTH];
	WiFi.macAddress(macAddress);
	sprintf(_macAddress, "0x%02x%02x%02x%02x%02x%02x", macAddress[0], macAddress[1], macAddress[2], macAddress[3], macAddress[4], macAddress[5]);


	// Output some debug information
#ifdef DEBUG
	sprintf(_debugOutput, "WiFi connected, IP: %s, MAC: %s", WiFi.localIP().toString(), _macAddress);
	Serial.println(_debugOutput);
#endif

	// Connected, so ditch out with blank screen
	updateOLED(false, "", "", _version);
}



/*
mqttReconnect()

This function disconnects and reconnects the ESP32 to the MQTT broker, as such,
it doesn't discern whether an existing connection is active.  That needs to be done by the
calling function first to prevent a needless disconnect and reconnect.
*/
void mqttReconnect()
{
	bool connectedAndSetupSuccessfully = false;
	char topic[MQTT_TOPIC_MAX_LENGTH];

	// Loop until we're reconnected
	while (true)
	{
		_mqtt.disconnect();
		delay(200);

#ifdef DEBUG
		Serial.print("Attempting MQTT connection...");
#endif

		updateOLED(false, "Connecting", "MQTT...", _version);
		delay(100);

/*
clientID const char[] - the client ID to use when connecting to the server
Credentials - (optional)
username const char[] - the username to use. If NULL, no username or password is used
password const char[] - the password to use. If NULL, no password is used
Will - (optional)
willTopic const char[] - the topic to be used by the will message
willQoS int: 0,1 or 2 - the quality of service to be used by the will message
willRetain boolean - whether the will should be published with the retain flag
willMessage const char[] - the payload of the will message
cleanSession boolean (optional) - whether to connect clean-session or not
*/

		// Create a will topic, i.e. Gliderol2MQTT/connection_status
		sprintf(topic, "%s%s", _deviceName, MQTT_CONNECTION_STATUS);

		// Attempt to connect, with a will which will be retained as Disconnected in the event of drop.
		if (_mqtt.connect(_deviceName, _mqttUsername, _mqttPassword, topic, 0, true, "false", true))
		{
#ifdef DEBUG
			Serial.println("Connected MQTT");
#endif

			// Connected
			
			// Send out a birth showing connection status of Connected, retained
			connectedAndSetupSuccessfully = _mqtt.publish(topic, "true", true);


			// Listen to the following topics
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_CLOSE);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_OPEN);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_STOP);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);

			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_OPEN);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_CLOSED);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_STOPPED);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);


			// Development topics only
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_CLOSE);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_STOP);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_OPEN);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_TOP_SENSOR);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_BOTTOM_SENSOR);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);

			// Development topics only
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);

			// Development topics only
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);
			sprintf(topic, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);


			// Apple HomeKit (Homebridge + MQTT-Thing) topic
			sprintf(topic, "%s%s", _deviceName, MQTT_HOMEKIT_SET_TARGET_DOOR_STATE);
			connectedAndSetupSuccessfully = connectedAndSetupSuccessfully && _mqtt.subscribe(topic);


			if (connectedAndSetupSuccessfully)
			{
				// Connected, so ditch out with runstate on the screen
				updateRunstate();
				break;
			}

		}

		if (!connectedAndSetupSuccessfully)
#ifdef DEBUG
			sprintf(_debugOutput, "MQTT Failed: RC is %d\r\nTrying again in five seconds...", _mqtt.state());
			Serial.println(_debugOutput);
#endif

		// Wait 3 seconds before retrying
		delay(3000);
	}
}



/*
loop()

The loop function runs over and over again until power down or reset
*/
void loop()
{
	// Refresh LED Screen, will cause the status asterisk to flicker
	updateOLED(true, "", "", "");

	// Make sure WiFi is good
	if (WiFi.status() != WL_CONNECTED)
	{
		setupWifi();
	}

	// Make sure mqtt is still connected and process any messages (_mqtt.loop())
	if ((!_mqtt.connected()) || !_mqtt.loop())
	{
		mqttReconnect();
	}

	// Update runstate.  By this point any MQTT messages instructing to open/close have been actioned
	updateRunstate();
}












































/*
checkTimer()

Check to see if the elapsed interval has passed since the passed in millis() value. If it has, return true and update the lastRun.
Note that millis() overflows after 50 days, so we need to deal with that too... in our case we just zero the last run, which means the timer
could be shorter but it's not critical... not worth the extra effort of doing it properly for once in 50 days.
*/
bool checkTimer(unsigned long* lastRun, unsigned long interval)
{
	unsigned long now = millis();

	if (*lastRun > now)
		*lastRun = 0;

	if (now >= *lastRun + interval)
	{
		*lastRun = now;
		return true;
	}

	return false;
}


/*
updateOLED()

justStatus = true will ignore anything in the line parameters, false will update status bar and detail lines.
Update the OLED. Use "" for an empty line.
Three parameters representing each of the three lines available for status indication - Top line functionality fixed
*/
void updateOLED(bool justStatus, const char* line2, const char* line3, const char* line4)
{
	static unsigned long updateStatusBar = 0;


	_display.clearDisplay();
	_display.setTextSize(2);
	//_display.setTextColor(WHITE);
	_display.setTextColor(SH110X_WHITE);
	_display.setCursor(0, 0);

	char line1Contents[OLED_CHARACTER_WIDTH];
	char line2Contents[OLED_CHARACTER_WIDTH];
	char line3Contents[OLED_CHARACTER_WIDTH];
	char line4Contents[OLED_CHARACTER_WIDTH];

	// Ensure only dealing with 10 chars passed in, and null terminate.
	if (strlen(line2) > OLED_CHARACTER_WIDTH - 1)
	{
		strncpy(line2Contents, line2, OLED_CHARACTER_WIDTH - 1);
		line2Contents[OLED_CHARACTER_WIDTH - 1] = 0;
	}
	else
	{
		strcpy(line2Contents, line2);
	}


	if (strlen(line3) > OLED_CHARACTER_WIDTH - 1)
	{
		strncpy(line3Contents, line3, OLED_CHARACTER_WIDTH - 1);
		line3Contents[OLED_CHARACTER_WIDTH - 1] = 0;
	}
	else
	{
		strcpy(line3Contents, line3);
	}


	if (strlen(line4) > OLED_CHARACTER_WIDTH - 1)
	{
		strncpy(line4Contents, line4, OLED_CHARACTER_WIDTH - 1);
		line4Contents[OLED_CHARACTER_WIDTH - 1] = 0;
	}
	else
	{
		strcpy(line4Contents, line4);
	}

	// Only update the operating indicator once per half second.
	if (checkTimer(&updateStatusBar, UPDATE_STATUS_BAR_INTERVAL))
	{
		// Simply swap between space and asterisk every time we come here to give some indication of activity
		_oledOperatingIndicator = (_oledOperatingIndicator == '*') ? ' ' : '*';
	}

	// There's ten characters we can play with, width wise.
	sprintf(line1Contents, "%s%c%c%c", "G2M    ", _oledOperatingIndicator, (WiFi.status() == WL_CONNECTED ? 'W' : ' '), (_mqtt.connected() && _mqtt.loop() ? 'M' : ' '));
	_display.println(line1Contents);




	// Next line
	_display.setCursor(0, 16);
	if (!justStatus)
	{
		_display.println(line2Contents);
		strcpy(_oledLine2, line2Contents);
	}
	else
	{
		_display.println(_oledLine2);
	}



	_display.setCursor(0, 32);
	if (!justStatus)
	{
		_display.println(line3Contents);
		strcpy(_oledLine3, line3Contents);
	}
	else
	{
		_display.println(_oledLine3);
	}

	_display.setCursor(0, 48);
	if (!justStatus)
	{
		_display.println(line4Contents);
		strcpy(_oledLine4, line4Contents);
	}
	else
	{
		_display.println(_oledLine4);
	}
	// Refresh the display
	_display.display();
}









/*
updateRunstate()

Determines a few things about the sytem and updates the display
*/
void updateRunstate()
{
	static unsigned long lastRunDisplay = 0;
	static unsigned long lastRunMqttStatusInterval = 0;

	char relaysLine[OLED_CHARACTER_WIDTH] = "";
	char pinsLine[OLED_CHARACTER_WIDTH] = "";
	char doorStatusLine[OLED_CHARACTER_WIDTH] = "";

	int pinOpenValue;
	int pinStopValue;
	int pinCloseValue;
	int pinTopSensorValue;
	int pinBottomSensorValue;

	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = "";
	statusValues resultAddedToPayload = statusValues::addedToPayload;
	char stateAddition[MQTT_PAYLOAD_STATE_ADDITION] = "";



	// Do the main work determining door state
	getDoorState();


	if (checkTimer(&lastRunDisplay, DISPLAY_INTERVAL))
	{
		//Flash the LED
		//digitalWrite(LED_BUILTIN, LOW);
		//delay(4);
		//digitalWrite(LED_BUILTIN, HIGH);

		// Just for displaying on the screen
		pinOpenValue = digitalRead(PIN_FOR_GARAGE_DOOR_OPEN);
		pinStopValue = digitalRead(PIN_FOR_GARAGE_DOOR_STOP);
		pinCloseValue = digitalRead(PIN_FOR_GARAGE_DOOR_CLOSE);
		pinTopSensorValue = digitalRead(PIN_FOR_TOP_SENSOR);
		pinBottomSensorValue = digitalRead(PIN_FOR_BOTTOM_SENSOR);
		
		sprintf(relaysLine, "O%s, S%s, C%s", pinOpenValue == 1 ? "H" : "L", pinStopValue == 1 ? "H" : "L", pinCloseValue == 1 ? "H" : "L");
		if (_usingTopSensor)
		{
			sprintf(pinsLine, "T%s, B%s", pinTopSensorValue == 1 ? "H" : "L", pinBottomSensorValue == 1 ? "H" : "L");
		}
		else
		{
			sprintf(pinsLine, "B%s", pinBottomSensorValue == 1 ? "H" : "L");
		}
		sprintf(doorStatusLine, "%s", _doorStateDesc);
		updateOLED(false, relaysLine, pinsLine, doorStatusLine);
	}

	

	// Periodically send out the state or unless forced once by another process
	if (checkTimer(&lastRunMqttStatusInterval, MQTT_STATUS_INTERVAL) || _forceMqttOnce)
	{
		_forceMqttOnce = false;

		// getCurrentDoorState for HomeKit
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_CURRENT_DOOR_STATE);
		strcpy(stateAddition, _doorStateHomeKit);

		resultAddedToPayload = addToPayload(stateAddition);
		if (resultAddedToPayload == statusValues::addedToPayload)
		{
			sendMqtt(topicResponse, true);
		}
	}
}




/*
getDoorState()

Compares the absolute latest state of the sensor(s) to the previously known states in order to determine change of door state
either via the FOB/on-device button or from an MQTT message.
*/
void getDoorState()
{
	int pinTopSensorValue;
	int pinBottomSensorValue;

	bool isOpen;
	bool isClosed;
	bool isStopped;
	bool stillWithinTimer = false;
	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = "";
	bool skipDoorCloseTimerDueToFobAndNoTopSensor = false;

#ifdef DEBUG_LEVEL2
	static long debugCounter = 0;
#endif

	// Determine bottom sensor state from what the pin is saying
	pinBottomSensorValue = digitalRead(PIN_FOR_BOTTOM_SENSOR);


	// Sensors are naturally high with pullup resistors
	// When the sensors are pulled low it means the magnet is touching the bottom sensor or the spring switch has switched
	// Both low shouldn't be possible, we will report such instances as stopped to prompt the garage door owner there's somethin awry
	isClosed = (pinBottomSensorValue == LOW);

	if (_usingTopSensor)
	{
		// Determine top sensor state from what the pin is saying
		pinTopSensorValue = digitalRead(PIN_FOR_TOP_SENSOR);
		isOpen = (pinTopSensorValue == LOW);
		// Stopped is when not open and not closed.
		// And as a safeguard for both open and closed at the same time (in theory not possible), report stopped in this instance too
		isStopped = (!isOpen && !isClosed) || (isOpen && isClosed);
	}
	else
	{
		// Not utilising a top sensor, so can only presume open is opposite of closed, and Stopped is indeterminate.
		isOpen = !isClosed;
		isStopped = false;
	}


	if (_firstBoot)
	{
		// First boot, create reliable values to compare against
		_firstBoot = false;
		_previousIsOpen = isOpen;
		_previousIsStopped = isStopped;
		_previousIsClosed = isClosed;
	}


	// Lets determine if opening or closing via a genuine fob or on-pcb button
	// We do this by comparing the previous door sensor readings to the current.  If the previous bottom sensor reading indicated door closed and now open, we can kick off an Opening, etc.

	// If opening/closing/stopped triggered by MQTT then _mqttOpeningClosingManagement will be known and target updated.
	// If change is due to sensor state change, send target updated and start the timer
	// If not using the top sensor, the closing time should be ignored as we only known when the door has shut.

#ifdef DEBUG_LEVEL2
	debugCounter++;
	if (debugCounter % 10 == 0)
	{
		sprintf(_debugOutput, "debugCounter: %d, isOpen: %d, isStopped: %d, isClosed: %d, _previousIsOpen: %d, _previousIsStopped: %d, _previousIsClosed: %d", debugCounter, isOpen, isStopped, isClosed, _previousIsOpen, _previousIsStopped, _previousIsClosed);
		Serial.println(_debugOutput);
	}
#endif


	// First thing to check
	// Scenario: if using a top sensor and we know the door is stopped mid-way, then then previously known open and closed values will both be false
	// So, if coming out of stopped by way of a fob/on device button, we won't have an idea of this until it reaches a fully open or fully closed position.
	// With that in mind, we don't want to trigger a full Opening/Closing timer when the state of the Open or Closed sensor changes.
	// So, set the previous open and previous closed the same as what they are now so we don't trigger the timer.
	// Further, let HomeKit know of the updated target
	if (_usingTopSensor && _previousIsStopped && !isStopped && _mqttOpeningClosingManagement == doorState::doorStateUnknown)
	{
#ifdef DEBUG_LEVEL2
		sprintf(_debugOutput, "Setting the same - debugCounter: %d, isOpen: %d, isStopped: %d, isClosed: %d, _previousIsOpen: %d, _previousIsStopped: %d, _previousIsClosed: %d", debugCounter, isOpen, isStopped, isClosed, _previousIsOpen, _previousIsStopped, _previousIsClosed);
		Serial.println(_debugOutput);
#endif

		_previousIsOpen = isOpen;
		_previousIsClosed = isClosed;

		if (isOpen)
		{
			// Let HomeKit know of the updated target state
			emptyPayload();
			addToPayload(DOOR_STATE_HOMEKIT_OPEN);
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
			sendMqtt(topicResponse, true);
			emptyPayload();
		}
		else if (isClosed)
		{
			// Let HomeKit know of the updated target state
			emptyPayload();
			addToPayload(DOOR_STATE_HOMEKIT_CLOSED);
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
			sendMqtt(topicResponse, true);
			emptyPayload();
		}

		// Force a Runstate Update immediately
		_forceMqttOnce = true;
	}


	// Now the fob/on-device button workaround for coming out of stopped has been taken care of
	// Now do typical checks for routine usage outside of MQTT.  I.e. was open, fob pressed, sensor now says not open, etc
	// Outside of MQTT - _mqttOpeningClosingManagement == doorState::doorStateUnknown
	if (_previousIsOpen && !isOpen && _mqttOpeningClosingManagement == doorState::doorStateUnknown)
	{
		// Start the closing timer
		// So send out an MQTT saying target is CLOSED, and start the timer and set the status
		emptyPayload();
		addToPayload(DOOR_STATE_HOMEKIT_CLOSED);
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse, true);

		emptyPayload();

		_mqttOpeningClosingManagement = doorState::doorClosing;
		_mqttOpeningClosingManagementTimer = millis();

		// Skip the timer for closing when no top sensor and fob activated as the first we will know that the door is closed
		// is when it is closed.  Further down this will enable the process to go straight in.
		skipDoorCloseTimerDueToFobAndNoTopSensor = !_usingTopSensor;

		// Force a Runstate Update immediately
		_forceMqttOnce = true;
#ifdef DEBUG_LEVEL2
		sprintf(_debugOutput, "Want to close - debugCounter: %d, isOpen: %d, isStopped: %d, isClosed: %d, _previousIsOpen: %d, _previousIsStopped: %d, _previousIsClosed: %d", debugCounter, isOpen, isStopped, isClosed, _previousIsOpen, _previousIsStopped, _previousIsClosed);
		Serial.println(_debugOutput);
#endif
	}
	// And likewise the opposite, compare previous door state to current.  If previous closed and now not, then opening...
	else if (_previousIsClosed && !isClosed && _mqttOpeningClosingManagement == doorState::doorStateUnknown)
	{
		// Start the opening timer
		// So send out an MQTT saying target is OPEN, and start the timer and set the status
		emptyPayload();
		addToPayload(DOOR_STATE_HOMEKIT_OPEN);
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse, true);

		emptyPayload();
		_mqttOpeningClosingManagement = doorState::doorOpening;
		_mqttOpeningClosingManagementTimer = millis();

		// Force a Runstate Update immediately
		_forceMqttOnce = true;
#ifdef DEBUG_LEVEL2
		sprintf(_debugOutput, "Want to open - debugCounter: %d, isOpen: %d, isStopped: %d, isClosed: %d, _previousIsOpen: %d, _previousIsStopped: %d, _previousIsClosed: %d", debugCounter, isOpen, isStopped, isClosed, _previousIsOpen, _previousIsStopped, _previousIsClosed);
		Serial.println(_debugOutput);
#endif
	}







	// So, now we have determined either via FOB or via MQTT that we are opening or closing or stopped....
	if (_mqttOpeningClosingManagement == doorState::doorOpening)
	{
		// There was an MQTT to control or sensor change, which will have set the time of instigation
		if (checkTimer(&_mqttOpeningClosingManagementTimer, _doorOpenTime))
		{
			// Dealt with this now as the full time for the door to open has passed, so get rid so isn't processed next time
			_mqttOpeningClosingManagement = doorState::doorStateUnknown;

			// Timer elapsed to handle an open, so it is presumably stopped, but we will verify that later with sensors
			// Force a current state MQTT message
			_forceMqttOnce = true;
		}
		else
		{
			// Still opening
			_doorState = doorState::doorOpening;
			strcpy(_doorStateDesc, DOOR_STATE_OPENING_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_OPENING);

			stillWithinTimer = true;
		}
	}
	else if (_mqttOpeningClosingManagement == doorState::doorClosing)
	{
		// There was an MQTT to control or sensor change, which will have set the time of instigation
		if (checkTimer(&_mqttOpeningClosingManagementTimer, _doorCloseTime) || skipDoorCloseTimerDueToFobAndNoTopSensor)
		{
			// Dealt with this now as the full time for the door to close has passed, so get rid so isn't processed next time
			_mqttOpeningClosingManagement = doorState::doorStateUnknown;

			// Timer elapsed to handle a close, so it is presumably stopped, but we will verify that later with sensors
			// Force a current state MQTT message
			_forceMqttOnce = true;
		}
		else
		{
			// Still closing
			_doorState = doorState::doorClosing;
			strcpy(_doorStateDesc, DOOR_STATE_CLOSING_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_CLOSING);

			stillWithinTimer = true;
		}
	}
	else if (_mqttOpeningClosingManagement == doorState::doorStopped)
	{
		// Dealt with this now as no time needed for this to fulfil, so get rid so isn't processed next time
		_mqttOpeningClosingManagement = doorState::doorStateUnknown;

		_doorState = doorState::doorStopped;
		strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
		strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);

		// Force a current state MQTT message
		_forceMqttOnce = true;
	}


	// If we are still within a movement timer then we skip this step as we want to report 'closing' or 'opening' until we know it should have definitely happened.
	// Now we need to overwrite if current sensor values tell us we are at a destination
	if (!stillWithinTimer)
	{
		if (isStopped)
		{
			_doorState = doorState::doorStopped;
			strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);
		}
		else if (isOpen)
		{
			_doorState = doorState::doorOpen;
			strcpy(_doorStateDesc, DOOR_STATE_OPEN_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);
		}
		else if (isClosed)
		{
			_doorState = doorState::doorClosed;
			strcpy(_doorStateDesc, DOOR_STATE_CLOSED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_CLOSED);
		}
	}

	// So save what the current state is, can compare to it next loop.
	_previousIsOpen = isOpen;
	_previousIsStopped = isStopped;
	_previousIsClosed = isClosed;

	// I found the sensor values would bounce in testing (more likely me not quick enough shorting the sensor pins manually), but we can afford a little delay here anyway.
	delay(100);
}











/*
mqttCallback()

This function is executed when an MQTT message arrives on a topic that we are subscribed to.
*/
void mqttCallback(char* topic, byte* message, unsigned int length)
{
	statusValues result = statusValues::preProcessing;
	statusValues resultAddToPayload = statusValues::addedToPayload;

	char stateAddition[MQTT_PAYLOAD_STATE_ADDITION] = "";
	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = "";
	char topicIncomingCheck[MQTT_TOPIC_MAX_LENGTH] = "";
	char mqttIncomingPayload[MIN_MQTT_PAYLOAD_SIZE] = "";

	mqttSubscriptions subscription = mqttSubscriptions::subscriptionUnknown;

	char statusMqttMessage[MAX_MQTT_STATUS_LENGTH] = STATUS_PREPROCESSING_MQTT_DESC;

	int pinBottomSensorValue;
	int pinTopSensorValue;
	int pinCloseValue;
	int pinOpenValue;
	int pinStopValue;

	bool gotTopic = false;

	// Some MQTT messages should be retained, others not.
	bool retain = false;


	// Get the most up to date state
	getDoorState();


	// Start by clearing out the payload
	emptyPayload();

#ifdef DEBUG
	sprintf(_debugOutput, "Topic: %s", topic);
	Serial.println(_debugOutput);
#endif

	// Get the payload
	for (int i = 0; i < length; i++)
	{
		mqttIncomingPayload[i] = message[i];
	}

#ifdef DEBUG
	Serial.println("Payload:");
	Serial.println(mqttIncomingPayload);
#endif


	// Get an easy to use subscription type for later
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;

			// Let HomeKit know of the updated target state
			emptyPayload();
			addToPayload(DOOR_STATE_HOMEKIT_CLOSED);
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
			sendMqtt(topicResponse, true);

			subscription = mqttSubscriptions::requestPerformClose;

			// Don't retain a response to a request to close
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_CLOSE);
			// Force a Runstate Update immediately
			_forceMqttOnce = true;
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;

			// Let HomeKit know of the updated target state
			emptyPayload();
			addToPayload(DOOR_STATE_HOMEKIT_OPEN);
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
			sendMqtt(topicResponse, true);

			subscription = mqttSubscriptions::requestPerformOpen;
			// Don't retain a response to a request to open
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_OPEN);

			// Force a Runstate Update immediately
			_forceMqttOnce = true;
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestPerformStop;
			// Don't retain a response to a request to stop
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_STOP);

			// Force a Runstate Update immediately
			_forceMqttOnce = true;
		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestIsOpen;
			// Don't retain a response to a request of "Is Door Open"
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_OPEN);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_CLOSED);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestIsClosed;
			// Don't retain a response to a request of "Is Door Closed"
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_CLOSED);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_STOPPED);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestIsStopped;
			// Don't retain a response to a request of "Is Door Stopped"
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_STOPPED);
		}
	}



	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestValuePinClose;
			// Don't retain a response to a request of a pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_CLOSE);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestValuePinOpen;
			// Don't retain a response to a request of a pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_OPEN);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestValuePinStop;
			// Don't retain a response to a request of a pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_STOP);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_TOP_SENSOR);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestValuePinTopSensor;
			// Don't retain a response to a request of a pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_TOP_SENSOR);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_BOTTOM_SENSOR);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestValuePinBottomSensor;
			// Don't retain a response to a request of a pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_BOTTOM_SENSOR);
		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestSetValuePinClose;
			// Don't retain a response to a request of a set pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_CLOSE);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestSetValuePinStop;
			// Don't retain a response to a request of a set pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_STOP);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestSetValuePinOpen;
			// Don't retain a response to a request of a set pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_OPEN);
		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestClearValuePinClose;
			// Don't retain a response to a request of a clear pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_CLOSE);
		}
	}

	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestClearValuePinStop;
			// Don't retain a response to a request of a clear pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_STOP);
		}
	}

	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subscription = mqttSubscriptions::requestClearValuePinOpen;
			// Don't retain a response to a request of a clear pin value
			retain = false;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_OPEN);
		}
	}


	// Homebridge MQTT-Thing requests
	// "In order to monitor accessory responsiveness automatically, enter the time (in milliseconds) to wait after publishing a 'set' topic to receive a confirmatary 'get' topic update."
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_HOMEKIT_SET_TARGET_DOOR_STATE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;

			if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_OPEN) == 0)
			{
				// MQTTThing wants an response to the set via an equivalent get within 1S, send it now.
				// Send the original payload back, retained
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse, true);
				emptyPayload();

				// Now in the onward processes ensure it is done
				subscription = mqttSubscriptions::requestPerformOpen;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_OPEN);

				// Force a Runstate Update immediately
				_forceMqttOnce = true;
				
			}
			else if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_CLOSED) == 0)
			{
				// MQTTThing wants an response to the set via an equivalent get within 1S, send it now.
				// Send the original payload back, retained
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse, true);
				emptyPayload();

				// Now in the onward processes ensure it is done
				subscription = mqttSubscriptions::requestPerformClose;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_CLOSE);

				// Force a Runstate Update immediately
				_forceMqttOnce = true;
			}

			// Stopped has been implemented below using best guess, however,
			// MQTTThing (or perhaps, more Homekit) doesn't support a target of STOPPED
			else if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_STOPPED) == 0)
			{
				// MQTTThing wants an response to the set via an equivalent get within 1S, send it now.
				// Send the original payload back, retained
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse, true);
				emptyPayload();


				// Now in the onward processes ensure it is done
				subscription = mqttSubscriptions::requestPerformStop;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_STOP);

				// Force a Runstate Update immediately
				_forceMqttOnce = true;
			}
		}
	}



	
	if (!gotTopic)
	{
		mqttSubscriptions::subscriptionUnknown;
		result = statusValues::notValidIncomingTopic;
	}


	// So, by this point we know if we got a valid topic, and in the case of MQTT-Thing, have sent back an acknowledgement
	// But, that said, we now need to action the requests
	// Carry on?
	if (result == statusValues::preProcessing)
	{
		if (subscription == mqttSubscriptions::requestPerformClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Close");
			Serial.println(_debugOutput);
#endif

			// Ensure OPEN/STOP relays are closed
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _normallyClosed);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _normallyClosed);
			delay(50);
			// And for quarter of a second, open the CLOSE relay
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _normallyOpen);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _normallyClosed);

			result = statusValues::setCloseSuccess;
			strcpy(statusMqttMessage, STATUS_SET_CLOSE_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    ,\"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			// Useful to do this here and not just rely on change of sensors because the top sensor may not be used
			_mqttOpeningClosingManagement = doorState::doorClosing;
			_mqttOpeningClosingManagementTimer = millis();

		}
		else if (subscription == mqttSubscriptions::requestPerformOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Open");
			Serial.println(_debugOutput);
#endif

			// Ensure CLOSE/STOP relays are closed
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _normallyClosed);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _normallyClosed);
			delay(50);
			// And for quarter of a second, open the OPEN relay
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _normallyOpen);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _normallyClosed);


			result = statusValues::setCloseSuccess;

			strcpy(statusMqttMessage, STATUS_SET_OPEN_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    ,\"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			// Useful to do this here and not just rely on change of sensors because the top sensor may not be used
			_mqttOpeningClosingManagement = doorState::doorOpening;
			_mqttOpeningClosingManagementTimer = millis();

		}
		else if (subscription == mqttSubscriptions::requestPerformStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Stop");
			Serial.println(_debugOutput);
#endif

			// Ensure CLOSE/OPEN relays are closed
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, _normallyClosed);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, _normallyClosed);
			delay(50);
			// And for quarter of a second, open the STOP relay
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _normallyOpen);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, _normallyClosed);

			result = statusValues::setStopSuccess;
			strcpy(statusMqttMessage, STATUS_SET_STOP_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    ,\"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			// Useful to do this here and not just rely on change of sensors because the top sensor may not be used
			_mqttOpeningClosingManagement = doorState::doorStopped;
			_mqttOpeningClosingManagementTimer = millis();
		}





		else if (subscription == mqttSubscriptions::requestIsClosed)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Closed");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"closed\": %s\r\n}", _doorState == doorState::doorClosed ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestIsOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Open");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"open\": %s\r\n}", _doorState == doorState::doorOpen ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestIsStopped)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Stopped");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"partiallyopen\": %s\r\n}", _doorState == doorState::doorStopped ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}






		// Debug outputs
		else if (subscription == mqttSubscriptions::requestValuePinClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Close Pin");
			Serial.println(_debugOutput);
#endif
			pinCloseValue = digitalRead(PIN_FOR_GARAGE_DOOR_CLOSE);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinCloseValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestValuePinStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Stop Pin");
			Serial.println(_debugOutput);
#endif
			pinStopValue = digitalRead(PIN_FOR_GARAGE_DOOR_STOP);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinStopValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestValuePinOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Open Pin");
			Serial.println(_debugOutput);
#endif
			pinOpenValue = digitalRead(PIN_FOR_GARAGE_DOOR_OPEN);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinOpenValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestValuePinTopSensor)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Top Sensor Pin");
			Serial.println(_debugOutput);
#endif
			pinTopSensorValue = digitalRead(PIN_FOR_TOP_SENSOR);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinTopSensorValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestValuePinBottomSensor)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Bottom Sensor Pin");
			Serial.println(_debugOutput);
#endif
			pinBottomSensorValue = digitalRead(PIN_FOR_BOTTOM_SENSOR);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinBottomSensorValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}

		// Debug inputs
		else if (subscription == mqttSubscriptions::requestClearValuePinClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Clearing Close Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestClearValuePinStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Clearing Stop Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestClearValuePinOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Clearing Open Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}

		else if (subscription == mqttSubscriptions::requestSetValuePinClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Setting Close Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestSetValuePinStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Setting Stop Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subscription == mqttSubscriptions::requestSetValuePinOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Setting Open Pin");
			Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
	}



	if (subscription != mqttSubscriptions::subscriptionUnknown && result != statusValues::notValidIncomingTopic)
	{
		sendMqtt(topicResponse, retain);
	}

	// Ensure an empty payload for next use.
	emptyPayload();

	return;
}



/*
sendMqtt()

Sends whatever is in the modular level payload to the specified topic.
*/
void sendMqtt(char* topic, bool retain)
{
	// Attempt a send
	if (!_mqtt.publish(topic, _mqttPayload, retain))
	{
#ifdef DEBUG
		sprintf(_debugOutput, "MQTT publish failed to %s", topic);
		Serial.println(_debugOutput);
		Serial.println(_mqttPayload);
#endif
	}
	else
	{
#ifdef DEBUG_LEVEL2
		sprintf(_debugOutput, "MQTT publish success");
		Serial.println(_debugOutput);
		Serial.println(_mqttPayload);
#endif
	}

	// Empty payload for next use.
	emptyPayload();
	return;
}





/*
emptyPayload()

Clears every char so end of string can be easily found
*/
void emptyPayload()
{
	for (int i = 0; i < _maxPayloadSize; i++)
	{
		_mqttPayload[i] = '\0';
	}
}



/*
addToPayload()

Safely adds some contents to the payload buffer.
*/
statusValues addToPayload(char* addition)
{
	int targetRequestedSize = strlen(_mqttPayload) + strlen(addition);

	// If max payload size is 2048 it is stored as (0-2047), however character 2048  (position 2047) is null terminator so 2047 chars usable usable
	if (targetRequestedSize > _maxPayloadSize - 1)
	{
		// Safely print using snprintf
		snprintf(_mqttPayload, _maxPayloadSize, "{\r\n    \"mqttError\": \"Length of payload exceeds %d bytes.  Length would be %d bytes.\"\r\n}", _maxPayloadSize - 1, targetRequestedSize);

		return statusValues::payloadExceededCapacity;
	}
	else
	{
		// Add to the payload by sprintf back on itself with the addition
		sprintf(_mqttPayload, "%s%s", _mqttPayload, addition);

		return statusValues::addedToPayload;
	}
}

