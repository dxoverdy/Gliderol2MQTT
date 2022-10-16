/*
Name:		Gliderol2MQTT.ino
Created:	10/05/2022
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is private software.

Notes

First, go and customise options at the top of Definitions.h!
*/

// Supporting files

#include "Definitions.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SDFS.h>
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
bool _forceOnce = false;

// Fixed char array for messages to the serial port
char _debugOutput[DEBUG_MAX_LENGTH];

// Door State
static unsigned long _mqttOpeningClosingManagementTimer = 0;
static doorState _doorState = doorState::stateUnknown;
static doorState _doorTargetState = doorState::stateUnknown;
static doorState _mqttOpeningClosingManagement = doorState::stateUnknown;

char _doorStateDesc[DOOR_STATE_MAX_LENGTH] = "";
char _doorTargetStateHomeKit[2] = DOOR_STATE_HOMEKIT_UNKNOWN;
char _doorStateHomeKit[2] = DOOR_STATE_HOMEKIT_UNKNOWN;


// Wemos OLED Shield set up. 64x48, pins D1 and D2
#define OLED_RESET 0
// GPIO0
Adafruit_SSD1306 _display(OLED_RESET);




void getDoorState()
{
	int pinTopSensorValue;
	int pinBottomSensorValue;
	bool isOpen;
	bool isClosed;
	bool isStopped;
	bool stillWithinTimer = false;
	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = ""; // 100 should cover a topic name



	// Determine state from what the pins are saying
	pinBottomSensorValue = digitalRead(PIN_FOR_BOTTOM_SENSOR);
	isClosed = (pinBottomSensorValue == 0);

	if (_usingTopSensor)
	{
		pinTopSensorValue = digitalRead(PIN_FOR_TOP_SENSOR);
		isOpen = (pinTopSensorValue == 0);
		isStopped = (!isOpen && !isClosed);
	}
	else
	{
		// Not utilising a top sensor, so can only presume open is opposite of closed, and Stopped is indeterminate.
		isOpen = !isClosed;
		isStopped = false;
	}




	// Lets determine if opening or closing via a genuine fob or on-pcb button
	// We do this by comparing the previous door state to current.  If previous open and now not, then closing...
	if (_doorState == doorState::open && !isOpen)
	{
		// Start the closing timer
		// So send out an MQTT saying target is CLOSED, and start the timer and set the status
		emptyPayload();
		addToPayload("C");
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse);

		emptyPayload();
		_mqttOpeningClosingManagement = doorState::closing;
		_mqttOpeningClosingManagementTimer = millis();

		// Force a Runstate Update immediately
		_forceOnce = true;
	}
	// And likewise the opposite, compare previous door state to current.  If previous closed and now not, then opening...
	else if (_doorState == doorState::closed && !isClosed)
	{
		// Start the opening timer
		// So send out an MQTT saying target is OPEN, and start the timer and set the status
		emptyPayload();
		addToPayload("O");
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		sendMqtt(topicResponse);

		emptyPayload();
		_mqttOpeningClosingManagement = doorState::opening;
		_mqttOpeningClosingManagementTimer = millis();

		// Force a Runstate Update immediately
		_forceOnce = true;
	}




	// So, if we have determined either via FOB or via MQTT that we are opening or closing....
	if (_mqttOpeningClosingManagement == doorState::opening)
	{
		// There was an MQTT to control, which will have set the time of instigation
		if (checkTimer(&_mqttOpeningClosingManagementTimer, _doorOpenTime))
		{
			// Dealt with this now as the full time for the door to open has passed, so get rid so isn't processed next time
			_mqttOpeningClosingManagement = doorState::stateUnknown;

			// Timer elapsed to handle an open, so it is presumably stopped, but we will verify that later with sensors
			_doorState = doorState::stopped;
			strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);

			_doorTargetState = doorState::open;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);
		}
		else
		{
			// Still opening
			_doorState = doorState::opening;
			strcpy(_doorStateDesc, DOOR_STATE_OPENING_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_OPENING);

			_doorTargetState = doorState::open;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);
			stillWithinTimer = true;
		}
	}
	else if (_mqttOpeningClosingManagement == doorState::closing)
	{
		// There was an MQTT to control, which will have set the time of instigation
		if (checkTimer(&_mqttOpeningClosingManagementTimer, _doorCloseTime))
		{
			// Dealt with this now as the full time for the door to close has passed, so get rid so isn't processed next time
			_mqttOpeningClosingManagement = doorState::stateUnknown;

			// Timer elapsed to handle a close, so it is presumably stopped, but we will verify that later with sensors
			_doorState = doorState::stopped;
			strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);

			_doorTargetState = doorState::closed;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_CLOSED);
		}
		else
		{
			// Still closing
			_doorState = doorState::closing;
			strcpy(_doorStateDesc, DOOR_STATE_CLOSING_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_CLOSING);

			_doorTargetState = doorState::closed;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_CLOSED);
			stillWithinTimer = true;
		}
	}
	else if (_mqttOpeningClosingManagement == doorState::stopped)
	{
		// Dealt with this now as no time needed for this to fulfil, so get rid so isn't processed next time
		_mqttOpeningClosingManagement = doorState::stateUnknown;

		_doorState = doorState::stopped;
		strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
		strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);

		// Target cannot be stopped, so inform HomeKit that Open is probably the target
		_doorTargetState = doorState::open;
		strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);
	}


	// OK so from an MQTT perspective we have a presumed door state.


	// If we are still within a movement timer then we skip this step as we want to report 'closing' or 'opening' until we know it should have definitely happened.
	// Now we need to overwrite if sensors tell us we are at a destination
	// Pins are naturally high, so partially open (stopped) is when top sensor high signal and bottom sensor high

	if (!stillWithinTimer)
	{
		if (isStopped)
		{
			// If stopped, and verified as outside timer, send HomeKit that we are probably after a target of open, so that the next HomeKit interaction is a close.
			_doorState = doorState::stopped;
			strcpy(_doorStateDesc, DOOR_STATE_STOPPED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_STOPPED);

			_doorTargetState = doorState::open;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);
		}
		else if (isOpen)
		{
			_doorState = doorState::open;
			strcpy(_doorStateDesc, DOOR_STATE_OPEN_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);

			// Open after the timer, so presume target was indeed open
			_doorTargetState = doorState::open;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_OPEN);

		}
		else if (isClosed)
		{
			_doorState = doorState::closed;
			strcpy(_doorStateDesc, DOOR_STATE_CLOSED_DESC);
			strcpy(_doorStateHomeKit, DOOR_STATE_HOMEKIT_CLOSED);

			// Closed after the timer, so presume target was indeed closed
			_doorTargetState = doorState::closed;
			strcpy(_doorTargetStateHomeKit, DOOR_STATE_HOMEKIT_CLOSED);
		}
	}
}


/*
setup

The setup function runs once when you press reset or power the board
*/
void setup()
{
	// Turn on the OLED
	_display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize OLED with the I2C addr 0x3C (for the 64x48)
	_display.clearDisplay();
	_display.display();
	updateOLED(false, "", "", _version);

	// Set up serial for debugging using an appropriate baud rate
	// This is for communication with the development environment, NOT the Alpha system
	// See Definitions.h for this.
	Serial.begin(9600);


#ifdef SDCARD
	// SD Card
	const int chipSelect = D4;

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

	/*
	char wifiSSID[SETTING_MAX_WIDTH] = "";
	char wifiPassword[SETTING_MAX_WIDTH] = "";
	char mqttServer[SETTING_MAX_WIDTH] = "";
	char mqttPort[SETTING_MAX_WIDTH] = "";
	char mqttUsername[SETTING_MAX_WIDTH] = "";
	char mqttPassword[SETTING_MAX_WIDTH] = "";
	char usingTopSensor[SETTING_MAX_WIDTH] = "";
	char garageCloseTime[SETTING_MAX_WIDTH] = "";
	char garageOpenTime[SETTING_MAX_WIDTH] = "";
	*/


	File myFile;
#ifdef DEBUG
	Serial.print("Initialising SD card...");
#endif
	if (!SD.begin(chipSelect))
	{
#ifdef DEBUG
		Serial.println("initialization failed!");
#endif
		// No SD Card or other failure, display a warning for four seconds on the screen
		updateOLED(false, "NO SD CARD", "Using", "Defaults..");
		delay(4000);

	}
	else
	{
#ifdef DEBUG
		Serial.println("initialisation done.");
#endif

		// SD Card is a go
		myFile = SD.open("settings.txt");
		if (myFile)
		{
#ifdef DEBUG
			Serial.println("settings.txt was found.");
#endif


			/*
File Format:
{
	"WIFI_SSID":"Stardust",
	"WIFI_PASSWORD":"Sniegulinka1983",
	"MQTT_SERVER":"192.168.1.40",
	"MQTT_PORT":1883,
	"MQTT_USERNAME":"Alpha",
	"MQTT_PASSWORD":"Inverter1",
	"DEVICE_NAME":"Gliderol2MQTT",
	"USING_TOP_SENSOR":"Yes"
}
			*/

			fileContentsLength = myFile.size();  // Get the file size.
			//pBuffer = (char*)malloc(fileSize + 1);  // Allocate memory for the file and a terminating null char.
			myFile.readBytes(fileContents, fileContentsLength);         // Read the file into the buffer.
			fileContents[fileContentsLength] = '\0';               // Add the terminating null char.
#ifdef DEBUG
			//Serial.println(fileContentsLength);                // Print the file to the serial monitor.
			//Serial.println(fileContents);
#endif
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

					//sprintf(_debugOutput, "Clean Name: \"%s\", Clean Value: \"%s\"", pairNameClean, pairValueClean);
					//Serial.println(_debugOutput);
					
					if (strcmp(pairNameClean, "WIFI_SSID") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got WIFI_SSIS, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_wifiSSID, pairValueClean);
					}
					else if (strcmp(pairNameClean, "WIFI_PASSWORD") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got WIFI_PASSWORD, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_wifiPassword, pairValueClean);
					}
					else if (strcmp(pairNameClean, "MQTT_SERVER") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_SERVER, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_mqttServer, pairValueClean);
					}
					else if (strcmp(pairNameClean, "MQTT_PORT") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_PORT, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_mqttPort = atoi(pairValueClean);
						Serial.println(_mqttPort);

					}
					else if (strcmp(pairNameClean, "MQTT_USERNAME") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_USERNAME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_mqttUsername, pairValueClean);
					}
					else if (strcmp(pairNameClean, "MQTT_PASSWORD") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got MQTT_PASSWORD, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_mqttPassword, pairValueClean);
					}
					else if (strcmp(pairNameClean, "DEVICE_NAME") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got DEVICE_NAME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						strcpy(_deviceName, pairValueClean);
					}
					else if (strcmp(pairNameClean, "USING_TOP_SENSOR") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got USING_TOP_SENSOR, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_usingTopSensor = strcmp(pairValueClean, "Yes") == 0 ? true : false;
					}
					else if (strcmp(pairNameClean, "DOOR_CLOSE_TIME") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got DOOR_CLOSE_TIME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_doorCloseTime = atoi(pairValueClean);
					}
					else if (strcmp(pairNameClean, "DOOR_OPEN_TIME") == 0)
					{
#ifdef DEBUG
						sprintf(_debugOutput, "Got DOOR_OPEN_TIME, setting to \"%s\"", pairValueClean);
						Serial.println(_debugOutput);
#endif
						_doorOpenTime = atoi(pairValueClean);
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
	

	/*
	// open the file. note that only one file can be open at a time,
	// so you have to close this one before opening another.
	myFile = SD.open("test.txt", FILE_WRITE);

	// if the file opened okay, write to it:
	if (myFile) {
		Serial.print("Writing to test.txt...");
		myFile.println("testing 1, 2, 3.");
		// close the file:
		myFile.close();
		Serial.println("done.");
	}
	else {
		// if the file didn't open, print an error:
		Serial.println("error opening test.txt");
	}
	*/


/*
	// re-open the file for reading:
	myFile = SD.open("test.txt");
	if (myFile) {
		Serial.println("test.txt:");

		// read from the file until there's nothing else in it:
		while (myFile.available()) {
			Serial.write(myFile.read());
		}
		// close the file:
		myFile.close();
	}
	else {
		// if the file didn't open, print an error:
		Serial.println("error opening test.txt");
	}
	*/

	// Free up for relay usage
	SD.end();
#endif




	// We will pause here for the longest of the opening/closing durations
	// Because on boot-up, we've no way of determining what is happening
	// So if we wait here until we can be assued the door isn't opening/closing
	// We can reliably go on to determine state going foward
	updateOLED(false, "", "Determine", "status...");
	delay(_doorCloseTime > _doorOpenTime ? _doorCloseTime : _doorOpenTime);
	updateOLED(false, "", "", _version);





	// Set appropriate pins for output
	digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
	digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
	digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
	pinMode(PIN_FOR_GARAGE_DOOR_CLOSE, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_OPEN, OUTPUT);
	pinMode(PIN_FOR_GARAGE_DOOR_STOP, OUTPUT);
	//pinMode(LED_BUILTIN, OUTPUT);
	// 
	// Use the appropriate pin to drive a separate relay to provide the door switching relay with constant power
	// Power supplied by the appropriate pin isn't enough.
	digitalWrite(PIN_FOR_RELAY_POWER, LOW);
	pinMode(PIN_FOR_RELAY_POWER, OUTPUT);

	//digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, LOW);
	//digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, LOW);
	//digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, LOW);

	


	// And for input
	pinMode(PIN_FOR_BOTTOM_SENSOR, INPUT_PULLUP);
	pinMode(PIN_FOR_TOP_SENSOR, INPUT_PULLUP);
	digitalWrite(PIN_FOR_BOTTOM_SENSOR, LOW);
	digitalWrite(PIN_FOR_TOP_SENSOR, LOW);



	// Configure WIFI
	setupWifi();

	// Configure MQTT to the address and port specified above
	_mqtt.setServer(_mqttServer, _mqttPort);
#ifdef DEBUG
	sprintf(_debugOutput, "About to request buffer");
	Serial.println(_debugOutput);
#endif
	for (_bufferSize = (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE); _bufferSize >= MIN_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE; _bufferSize = _bufferSize - 1024)
	{
#ifdef DEBUG
		sprintf(_debugOutput, "Requesting a buffer of : %d bytes", _bufferSize);
		Serial.println(_debugOutput);
#endif

		if (_mqtt.setBufferSize(_bufferSize))
		{

			_maxPayloadSize = _bufferSize - MQTT_HEADER_SIZE;
#ifdef DEBUG
			sprintf(_debugOutput, "_bufferSize: %d,\r\n\r\n_maxPayload (Including null terminator): %d", _bufferSize, _maxPayloadSize);
			Serial.println(_debugOutput);
#endif

			// Example, 2048, if declared as 2048 is positions 0 to 2047, and position 2047 needs to be zero.  2047 usable chars in payload.
			_mqttPayload = new char[_maxPayloadSize];
			emptyPayload();

			break;
		}
		else
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Coudln't allocate buffer of %d bytes", _bufferSize);
			Serial.println(_debugOutput);
#endif
		}
	}


	// And any messages we are subscribed to will be pushed to the mqttCallback function for processing
	_mqtt.setCallback(mqttCallback);

	// Connect to MQTT
	mqttReconnect();

	updateOLED(false, "", "", _version);
}







/*
loop

The loop function runs overand over again until power down or reset
*/
void loop()
{
#ifdef DEBUG
	int pC, pO, pS, pControl, pBottomSensor, pTopSensor;

	pC = digitalRead(PIN_FOR_GARAGE_DOOR_CLOSE);
	pS = digitalRead(PIN_FOR_GARAGE_DOOR_STOP);
	pO = digitalRead(PIN_FOR_GARAGE_DOOR_OPEN);
	pControl = digitalRead(PIN_FOR_RELAY_POWER);
	pBottomSensor = digitalRead(PIN_FOR_BOTTOM_SENSOR);
	pTopSensor = digitalRead(PIN_FOR_TOP_SENSOR);

	// Just output raw pin signals
	sprintf(_debugOutput, "", "Loop -- Close:%d, Open:%d, Stop:%d, Relay Power:%d, Bottom Sensor:%d, Top Sensor:%d", pC, pO, pS, pControl, pBottomSensor, pTopSensor);
#endif

	// Refresh LED Screen, will cause the status asterisk to flicker
	updateOLED(true, "", "", "");

	// Make sure WiFi is good
	if (WiFi.status() != WL_CONNECTED)
	{
		setupWifi();
	}

	// make sure mqtt is still connected
	if ((!_mqtt.connected()) || !_mqtt.loop())
	{
		mqttReconnect();
	}

	// Check and display the runstate on the display
	updateRunstate();
}



/*
setupWifi

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

	// Output some debug information
#ifdef DEBUG
	Serial.print("WiFi connected, IP is");
	Serial.print(WiFi.localIP());
#endif

	// Connected, so ditch out with blank screen
	updateOLED(false, "", "", _version);
}




/*
checkTimer

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
updateOLED

Update the OLED. Use "NULL" for no change to a line or "" for an empty line.
Three parameters representing each of the three lines available for status indication - Top line functionality fixed
*/
void updateOLED(bool justStatus, const char* line2, const char* line3, const char* line4)
{
	static unsigned long updateStatusBar = 0;


	_display.clearDisplay();
	_display.setTextSize(1);
	_display.setTextColor(WHITE);
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
	_display.setCursor(0, 12);
	if (!justStatus)
	{
		_display.println(line2Contents);
		strcpy(_oledLine2, line2Contents);
	}
	else
	{
		_display.println(_oledLine2);
	}



	_display.setCursor(0, 24);
	if (!justStatus)
	{
		_display.println(line3Contents);
		strcpy(_oledLine3, line3Contents);
	}
	else
	{
		_display.println(_oledLine3);
	}

	_display.setCursor(0, 36);
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
updateRunstate

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
	int pinRelayPowerValue;
	int pinTopSensorValue;
	int pinBottomSensorValue;

	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = "";
	statusValues resultAddedToPayload = statusValues::addedToPayload;
	char payloadLine[MQTT_PAYLOAD_LINE_MAX_LENGTH] = "";



	pinOpenValue = digitalRead(PIN_FOR_GARAGE_DOOR_OPEN);
	pinStopValue = digitalRead(PIN_FOR_GARAGE_DOOR_STOP);
	pinCloseValue = digitalRead(PIN_FOR_GARAGE_DOOR_CLOSE);
	pinRelayPowerValue = digitalRead(PIN_FOR_RELAY_POWER);

	pinTopSensorValue = digitalRead(PIN_FOR_TOP_SENSOR);
	pinBottomSensorValue = digitalRead(PIN_FOR_BOTTOM_SENSOR);




	getDoorState();


	if (checkTimer(&lastRunDisplay, DISPLAY_INTERVAL))
	{
		//Flash the LED
		//digitalWrite(LED_BUILTIN, LOW);
		//delay(4);
		//digitalWrite(LED_BUILTIN, HIGH);

		sprintf(relaysLine, "O%s, S%s, C%s", pinOpenValue == 1 ? "H" : "L", pinStopValue == 1 ? "H" : "L", pinCloseValue == 1 ? "H" : "L");
		if (_usingTopSensor)
		{
			sprintf(pinsLine, "R%s, T%s, B%s", pinRelayPowerValue == 1 ? "H" : "L", pinTopSensorValue == 1 ? "H" : "L", pinBottomSensorValue == 1 ? "H" : "L");
		}
		else
		{
			sprintf(pinsLine, "R%s, B%s", pinRelayPowerValue == 1 ? "H" : "L", pinBottomSensorValue == 1 ? "H" : "L");
		}
		sprintf(doorStatusLine, "%s", _doorStateDesc);
		//Serial.print(_usingTopSensor);
		updateOLED(false, relaysLine, pinsLine, doorStatusLine);
	}

	

	// Periodically send out the state or unless forced once by another process
	if (checkTimer(&lastRunMqttStatusInterval, MQTT_STATUS_INTERVAL) || _forceOnce)
	{
		_forceOnce = false;

		// getCurrentDoorState for HomeKit
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_CURRENT_DOOR_STATE);
		strcpy(payloadLine, _doorStateHomeKit);

		resultAddedToPayload = addToPayload(payloadLine);
		if (resultAddedToPayload == statusValues::addedToPayload)
		{
			sendMqtt(topicResponse);
		}


		// getCurrentDoorState for HomeKit
		sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
		strcpy(payloadLine, _doorTargetStateHomeKit);

		resultAddedToPayload = addToPayload(payloadLine);
		if (resultAddedToPayload == statusValues::addedToPayload)
		{
			sendMqtt(topicResponse);
		}
	}
}




/*
mqttReconnect

This function reconnects the ESP8266 to the MQTT broker
*/
void mqttReconnect()
{
	bool subscribed = false;
	char subscriptionDef[100];

	// Loop until we're reconnected
	while (true)
	{

		_mqtt.disconnect();		// Just in case.
		delay(200);

#ifdef DEBUG
		Serial.print("Attempting MQTT connection...");
#endif

		updateOLED(false, "Connecting", "MQTT...", _version);
		delay(100);

		// Attempt to connect
		if (_mqtt.connect(_deviceName, _mqttUsername, _mqttPassword))
		{
			Serial.println("Connected MQTT");

			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_CLOSE);
			subscribed = _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_OPEN);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_STOP);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_OPEN);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_CLOSED);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_STOPPED);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);

			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_CLOSE);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_STOP);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_OPEN);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_RELAYPOWER);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);

			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_RELAYPOWER);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);

			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_RELAYPOWER);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);

			// Apple Homekit subscriptions
			sprintf(subscriptionDef, "%s%s", _deviceName, MQTT_HOMEKIT_SET_TARGET_DOOR_STATE);
			subscribed = subscribed && _mqtt.subscribe(subscriptionDef);


			// Subscribe or resubscribe to topics.
			if (subscribed)
			{
				// Connected, so ditch out with runstate on the screen
				updateRunstate();
				break;
			}

		}

		if (!subscribed)
#ifdef DEBUG
			sprintf(_debugOutput, "MQTT Failed: RC is %d\r\nTrying again in five seconds...", _mqtt.state());
		Serial.println(_debugOutput);
#endif

		// Wait 5 seconds before retrying
		delay(5000);
	}
}








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



/*
mqttCallback()

// This function is executed when an MQTT message arrives on a topic that we are subscribed to.
*/
void mqttCallback(char* topic, byte* message, unsigned int length)
{
	statusValues result = statusValues::preProcessing;
	statusValues resultAddToPayload = statusValues::addedToPayload;

	char stateAddition[MQTT_PAYLOAD_STATE_ADDITION] = ""; // 256 should cover individual additions to be added to the payload.
	char topicResponse[MQTT_TOPIC_MAX_LENGTH] = ""; // 100 should cover a topic name
	char topicIncomingCheck[MQTT_TOPIC_MAX_LENGTH] = ""; // 100 should cover a topic name

	// Variables for new JSON parser
	int iSegNameCounter;
	int iSegValueCounter;
	int iPairNameCounter;
	int iPairValueCounter;
	int iCleanCounter;
	int iCounter;

	// All are emptied on creation as new arrays will just tend to have garbage in which would be recognised as actual content.
	char pairNameRaw[32] = "";
	char pairNameClean[32] = "";
	char pairValueRaw[32] = "";
	char pairValueClean[32] = "";

	/*
	char registerAddress[32] = "";
	char dataBytes[32] = "";
	char value[32] = "";
	char watts[32] = "";
	char duration[32] = "";
	char socPercent[32] = "";
	char startPos[32] = "";
	char endPos[32] = "";
	*/

	// Bytes are received back as base ten, 0-255, so four chars to account for null terminator
	//char rawByteForPayload[4] = "";
	//char rawDataForPayload[100] = "";
	char mqttIncomingPayload[MIN_MQTT_PAYLOAD_SIZE] = ""; // Should be enough to cover request JSON.

	mqttSubscriptions subScription = mqttSubscriptions::subscriptionUnknown;



	char statusMqttMessage[MAX_MQTT_STATUS_LENGTH] = STATUS_PREPROCESSING_MQTT_DESC;


	//int pinBottomSensorValue;
	//int pinTopSensorValue;
	int pinCloseValue;
	int pinOpenValue;
	int pinStopValue;

	bool gotTopic = false;



	// Get the state
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


	// Get an easy to use subScription type for later
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestPerformClose;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_CLOSE);

			// Force a Runstate Update immediately
			_forceOnce = true;
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestPerformOpen;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_OPEN);

			// Force a Runstate Update immediately
			_forceOnce = true;
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_PERFORM_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestPerformStop;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_STOP);

			// Force a Runstate Update immediately
			_forceOnce = true;
		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestIsOpen;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_OPEN);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_CLOSED);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestIsClosed;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_CLOSED);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_IS_STOPPED);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestIsStopped;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_IS_STOPPED);
		}
	}



	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestValuePinClose;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_CLOSE);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestValuePinOpen;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_OPEN);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestValuePinStop;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_STOP);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_VALUE_PIN_RELAYPOWER);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestValuePinRelayPower;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_VALUE_PIN_RELAYPOWER);
		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestSetValuePinClose;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_CLOSE);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestSetValuePinStop;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_STOP);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestSetValuePinOpen;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_OPEN);
		}
	}
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_SET_VALUE_PIN_RELAYPOWER);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestSetValuePinRelayPower;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_SET_VALUE_PIN_RELAYPOWER);
			Serial.println(topicResponse);

		}
	}


	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestClearValuePinClose;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_CLOSE);
		}
	}

	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestClearValuePinStop;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_STOP);
		}
	}

	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestClearValuePinOpen;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_OPEN);
		}
	}

	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_RELAYPOWER);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;
			subScription = mqttSubscriptions::requestClearValuePinRelayPower;
			sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_RELAYPOWER);
		}
	}


	// HomeKit requests
	if (!gotTopic)
	{
		sprintf(topicIncomingCheck, "%s%s", _deviceName, MQTT_HOMEKIT_SET_TARGET_DOOR_STATE);
		if (strcmp(topic, topicIncomingCheck) == 0)
		{
			gotTopic = true;

			if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_OPEN) == 0)
			{
				// HomeKit wants an immediate response to the set via an equivalent get within 1S, send it now.
				// Presume an empty payload is good for HomeKit, so don't worry about payload success or fail, for one char.
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse);

				emptyPayload();

				// Now in the onward processes ensure it is done
				subScription = mqttSubscriptions::requestPerformOpen;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_OPEN);

				// Force a Runstate Update immediately
				_forceOnce = true;
				
			}
			else if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_CLOSED) == 0)
			{
				// HomeKit wants an immediate response to the set via an equivalent get within 1S, send it now.
				// Presume an empty payload is good for HomeKit, so don't worry about payload success or fail, for one char.
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse);
				emptyPayload();

				// Now in the onward processes ensure it is done
				subScription = mqttSubscriptions::requestPerformClose;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_CLOSE);

				// Force a Runstate Update immediately
				_forceOnce = true;
			}

			// Jury is out as to whether this is needed
			else if (strcmp(mqttIncomingPayload, DOOR_STATE_HOMEKIT_STOPPED) == 0)
			{
				// HomeKit wants an immediate response to the set via an equivalent get within 1S, send it now.
				// Presume an empty payload is good for HomeKit, so don't worry about payload success or fail, for one char.
				emptyPayload();
				addToPayload(mqttIncomingPayload);
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_HOMEKIT_GET_TARGET_DOOR_STATE);
				sendMqtt(topicResponse);
				emptyPayload();

				// Now in the onward processes ensure it is done
				subScription = mqttSubscriptions::requestPerformStop;
				sprintf(topicResponse, "%s%s", _deviceName, MQTT_SUB_RESPONSE_PERFORM_STOP);

				// Force a Runstate Update immediately
				_forceOnce = true;
			}
		}
	}



	
	if (!gotTopic)
	{
		mqttSubscriptions::subscriptionUnknown;
		result = statusValues::notValidIncomingTopic;
	}



	if (result == statusValues::preProcessing)
	{
		/*
		if (length == 0)
		{
			// We won't be doing anything if no payload
			result = statusValues::noMQTTPayload;

			strcpy(statusMqttMessage, STATUS_NO_MQTT_PAYLOAD_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);
		}
		*/
	}

	if (result == statusValues::preProcessing)
	{
		// Rudimentary JSON parser here, saves on using a library
		// Go through character by character
		for (iCounter = 0; iCounter < length; iCounter++)
		{
			// Find a colon
			if (mqttIncomingPayload[iCounter] == ':')
			{
				// Everything to left is name until reached the start, a comma or a left brace.
				for (iSegNameCounter = iCounter - 1; iSegNameCounter >= 0; iSegNameCounter--)
				{
					if (mqttIncomingPayload[iSegNameCounter] == ',' || mqttIncomingPayload[iSegNameCounter] == '{')
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
					pairNameRaw[iPairNameCounter] = mqttIncomingPayload[x];
					iPairNameCounter++;
				}
				pairNameRaw[iPairNameCounter] = '\0';


				// Everything to right is value until reached the end, a comma or a right brace.
				for (iSegValueCounter = iCounter + 1; iSegValueCounter < length; iSegValueCounter++)
				{
					if (mqttIncomingPayload[iSegValueCounter] == ',' || mqttIncomingPayload[iSegValueCounter] == '}')
					{
						iSegValueCounter--;
						break;
					}
				}
				// Correct if went beyond the end
				if (iSegValueCounter >= length)
				{
					// If went beyond end, correct
					iSegValueCounter = length - 1;
				}
				// Segment value is now from the after the colon until the found character
				iPairValueCounter = 0;
				for (int x = iCounter + 1; x <= iSegValueCounter; x++)
				{
					pairValueRaw[iPairValueCounter] = mqttIncomingPayload[x];
					iPairValueCounter++;
				}
				pairValueRaw[iPairValueCounter] = '\0';


				// Transfer to a cleansed copy, without unwanted chars

				iPairNameCounter = 0;
				iCleanCounter = 0;
				while (pairNameRaw[iCleanCounter] != 0)
				{
					// Allow alpha numeric, upper case and lower case
					if ((pairNameRaw[iCleanCounter] >= 'a' && pairNameRaw[iCleanCounter] <= 'z') || (pairNameRaw[iCleanCounter] >= 'A' && pairNameRaw[iCleanCounter] <= 'Z') || (pairNameRaw[iCleanCounter] >= '0' && pairNameRaw[iCleanCounter] <= '9'))
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
					// Allow a minus, x (for hex), and 0-9, and a-f A-F for hex
					if ((pairValueRaw[iCleanCounter] == '-' || pairValueRaw[iCleanCounter] == 'x' || (pairValueRaw[iCleanCounter] >= '0' && pairValueRaw[iCleanCounter] <= '9') || (pairValueRaw[iCleanCounter] >= 'A' && pairValueRaw[iCleanCounter] <= 'F')) || (pairValueRaw[iCleanCounter] >= 'a' && pairValueRaw[iCleanCounter] <= 'f'))
					{
						// Transfer Over
						pairValueClean[iPairValueCounter] = pairValueRaw[iCleanCounter];
						iPairValueCounter++;
					}
					iCleanCounter++;
				}
				pairValueClean[iPairValueCounter] = '\0';


				/*
				if (strcmp(pairNameClean, "registerAddress") == 0)
				{
					//	Serial.println("Got registerAddress");
					strcpy(registerAddress, pairValueClean);
				}
				else if (strcmp(pairNameClean, "dataBytes") == 0)
				{
					//	Serial.println("Got dataBytes");
					strcpy(dataBytes, pairValueClean);
				}
				else if (strcmp(pairNameClean, "value") == 0)
				{
					//	Serial.println("Got value");
					strcpy(value, pairValueClean);
				}
				else if (strcmp(pairNameClean, "watts") == 0)
				{
					//	Serial.println("Got watts");
					strcpy(watts, pairValueClean);
				}
				else if (strcmp(pairNameClean, "duration") == 0)
				{
					//	Serial.println("Got duration");
					strcpy(duration, pairValueClean);
				}
				else if (strcmp(pairNameClean, "socPercent") == 0)
				{
					//	Serial.println("Got socPercent");
					strcpy(socPercent, pairValueClean);
				}
				else if (strcmp(pairNameClean, "start") == 0)
				{
					//	Serial.println("Got start");
					strcpy(startPos, pairValueClean);
				}
				else if (strcmp(pairNameClean, "end") == 0)
				{
					//	Serial.println("Got end");
					strcpy(endPos, pairValueClean);
				}
				*/

			}
		}
	}


	// Carry on?
	if (result == statusValues::preProcessing)
	{
		if (subScription == mqttSubscriptions::requestPerformClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Close");
			Serial.println(_debugOutput);
#endif

			// If using a separate power driver as a result of SD Card usage
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, HIGH);
			delay(100);
#endif
			// The relays are off at +12V, so set pin low to create voltage difference and kick them in
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, LOW);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, LOW);
#endif	

			result = statusValues::setCloseSuccess;
			strcpy(statusMqttMessage, STATUS_SET_CLOSE_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    \"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			_mqttOpeningClosingManagement = doorState::closing;
			_mqttOpeningClosingManagementTimer = millis();

		}
		else if (subScription == mqttSubscriptions::requestPerformOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Open");
			Serial.println(_debugOutput);
#endif

			// If using a separate power driver as a result of SD Card usage
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, HIGH);
			delay(100);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, LOW);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, LOW);
#endif

			result = statusValues::setCloseSuccess;
			strcpy(statusMqttMessage, STATUS_SET_OPEN_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    \"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			_mqttOpeningClosingManagement = doorState::opening;
			_mqttOpeningClosingManagementTimer = millis();

		}
		else if (subScription == mqttSubscriptions::requestPerformStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Performing Stop");
			Serial.println(_debugOutput);
#endif

			// If using a separate power driver as a result of SD Card usage
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, HIGH);
			delay(100);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
			delay(50);
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, LOW);
			delay(250);
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
#ifdef SDCARD
			digitalWrite(PIN_FOR_RELAY_POWER, LOW);
#endif	
			result = statusValues::setStopSuccess;
			strcpy(statusMqttMessage, STATUS_SET_STOP_SUCCESS_MQTT_DESC);
			sprintf(stateAddition, "{\r\n    \"statusValue\": \"%s\"\r\n    \"done\": true\r\n}", statusMqttMessage);

			resultAddToPayload = addToPayload(stateAddition);

			_mqttOpeningClosingManagement = doorState::stopped;
			_mqttOpeningClosingManagementTimer = millis();
		}





		else if (subScription == mqttSubscriptions::requestIsClosed)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Closed");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"closed\": %s\r\n}", _doorState == doorState::closed ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestIsOpen)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Open");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"open\": %s\r\n}", _doorState == doorState::open ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestIsStopped)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Is Stopped");
			Serial.println(_debugOutput);
#endif
			sprintf(stateAddition, "{\r\n    \"partiallyopen\": %s\r\n}", _doorState == doorState::stopped ? "true" : "false");
			resultAddToPayload = addToPayload(stateAddition);
		}





		else if (subScription == mqttSubscriptions::requestValuePinClose)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Close Pin");
			Serial.println(_debugOutput);
#endif
			pinCloseValue = digitalRead(PIN_FOR_GARAGE_DOOR_CLOSE);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinCloseValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestValuePinStop)
		{
#ifdef DEBUG
			sprintf(_debugOutput, "Requesting Value Of Stop Pin");
			Serial.println(_debugOutput);
#endif
			pinStopValue = digitalRead(PIN_FOR_GARAGE_DOOR_STOP);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinStopValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestValuePinOpen)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Requesting Value Of Open Pin");
		Serial.println(_debugOutput);
#endif
			pinOpenValue = digitalRead(PIN_FOR_GARAGE_DOOR_OPEN);
			sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinOpenValue == 1 ? "high" : "low");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestValuePinRelayPower)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Requesting Value Of Relay Power Pin");
		Serial.println(_debugOutput);
#endif
		pinOpenValue = digitalRead(PIN_FOR_RELAY_POWER);
		sprintf(stateAddition, "{\r\n    \"value\": %s\r\n}", pinOpenValue == 1 ? "high" : "low");
		resultAddToPayload = addToPayload(stateAddition);
		}





		else if (subScription == mqttSubscriptions::requestClearValuePinClose)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Clearing Close Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestClearValuePinStop)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Clearing Stop Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestClearValuePinOpen)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Clearing Open Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, LOW);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestClearValuePinRelayPower)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Clearing Relay Power Pin");
		Serial.println(_debugOutput);
#endif
		digitalWrite(PIN_FOR_RELAY_POWER, LOW);
		sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
		resultAddToPayload = addToPayload(stateAddition);
		}

		else if (subScription == mqttSubscriptions::requestSetValuePinClose)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Setting Close Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_CLOSE, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestSetValuePinStop)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Setting Stop Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_STOP, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestSetValuePinOpen)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Setting Open Pin");
		Serial.println(_debugOutput);
#endif
			digitalWrite(PIN_FOR_GARAGE_DOOR_OPEN, HIGH);
			sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
			resultAddToPayload = addToPayload(stateAddition);
		}
		else if (subScription == mqttSubscriptions::requestSetValuePinRelayPower)
		{
#ifdef DEBUG
		sprintf(_debugOutput, "Setting Relay Power Pin");
		Serial.println(_debugOutput);
#endif
		digitalWrite(PIN_FOR_RELAY_POWER, HIGH);
		sprintf(stateAddition, "{\r\n    \"done\": true\r\n}");
		resultAddToPayload = addToPayload(stateAddition);
		}




	}



	if (subScription != mqttSubscriptions::subscriptionUnknown && result != statusValues::notValidIncomingTopic)
	{
		sendMqtt(topicResponse);
	}

	emptyPayload();

	return;
}


/*
sendMqtt

Sends whatever is in the modular level payload to the specified topic.
*/
void sendMqtt(char* topic)
{
	// Attempt a send
	if (!_mqtt.publish(topic, _mqttPayload))
	{
#ifdef DEBUG
		sprintf(_debugOutput, "MQTT publish failed to %s", topic);
		Serial.println(_debugOutput);
		Serial.println(_mqttPayload);
#endif
	}
	else
	{
#ifdef DEBUG
		//sprintf(_debugOutput, "MQTT publish success");
		//Serial.println(_debugOutput);
#endif
	}

	// Empty payload for next use.
	emptyPayload();
	return;
}

/*
emptyPayload

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
uint32_t freeMemory()
{

	return ESP.getFreeHeap();
}
*/