/*
Name:		Definitions.h
Created:	05/Oct/2022
Author:		Daniel Young

This file is part of Gliderol2MQTT (G2M) which is released under GNU GENERAL PUBLIC LICENSE.
See file LICENSE or go to https://choosealicense.com/licenses/gpl-3.0/ for full license details.

Notes


*/

#ifndef _DEFINITIONS_h
#define _DEFINITIONS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

// **************** Friendly Settings ****************
// Update with your Wifi details
#define WIFI_SSID "Stardust_EXT"
#define WIFI_PASSWORD ""

// Update with your MQTT Broker details
#define MQTT_SERVER	"192.168.1.135"
#define MQTT_PORT 1883
#define MQTT_USERNAME "Garage"
#define MQTT_PASSWORD "DoorOpener1"

// The device name is used as the MQTT base topic and presence on the network.
// If you need more than one Gliderol2MQTT on your network, give them unique names.
#define DEVICE_NAME "Gliderol2MQTT"

// If there is a top sensor in use, change this to true.  This will enable 'stop' functionality to be reported correctly
// as 'stopped' is somewhere between top and bottom after the duration to open or close has elapsed.  Closing from a fob
// is also supported when using the top sensor.  Otherwise only open/closed can be reported.
#define USING_TOP_SENSOR true

// How long the door takes to open from fully closed, and vice versa.  Used for timing the 'Opening' and 'Closing' statuses.
#define TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED 10000
#define TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN 10000

// My relay board flips relays from normally closed to normally open when a 3.3V signal (HIGH) is received from the ESP32.
// But I have historically used relays which are normally closed when HIGH and flip to normally open when LOW.
// Depending on the type you have, swap HIGH and LOW here.
#define NORMALLY_OPEN HIGH
#define NORMALLY_CLOSED LOW


// On boot up you have a choice to either
// Leave the door target state as last known (leaves the target state as is - the current retained value in the MQTT broker) = 0
// Set to Closed = 1
// Set to Open = 2

// Don't change these:
#define BOOT_TARGET_STATE_LAST_KNOWN 0
#define BOOT_TARGET_STATE_CLOSED 1
#define BOOT_TARGET_STATE_OPEN 2
// Change this:
#define BOOT_UP_TARGET_STATE 0






// **************** More Advanced Settings ****************
// How often the status of the door is sent out
// 5 seconds is probably overkill given statuses are updated on demand and there is the birth and last will and testament
// But... given the relatively tiny demand on both the ESP32 and home network I am happy with it.
#define MQTT_STATUS_INTERVAL 5000

// "Sets the keep alive interval used by the client." (seconds)
// The library default is 15 however with this project carrying the security of a door I am using 5 seconds
// As in, if offline for 5 seconds, the MQTT broker will indicate the device is offline and send out the last will and testament.
#define MQTT_KEEP_ALIVE 5

// "Sets the socket timeout used by the client. This determines how long the client will wait for incoming data when it expects data to arrive - for example, whilst it is in the middle of reading an MQTT packet." (seconds)
// Library default is 15 however with this project carrying the security of a door I am using 5
#define MQTT_SOCKET_TIMEOUT 5

// These timers are used in the main loop.
// How often to update the status on the screen
#define DISPLAY_INTERVAL 250

// How often to flash the top indicator on the display.
#define UPDATE_STATUS_BAR_INTERVAL 500


// The ESP8266 on which Gliderol2MQTT was originally developed has limited memory and so reserving lots of RAM to build a
// payload and MQTT buffer caused out of memory exceptions.  4096 worked well given the RAM requirements of Gliderol2MQTT.
// This could be increased given the project is now ESP32 based however none of the payloads come even close to 4096.
// Gliderol2MQTT on boot will request a buffer size of (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE) for MQTT, and
// MAX_MQTT_PAYLOAD_SIZE for building payloads.  If these fail and your device doesn't boot, you can assume you've set this too high.
#define MAX_MQTT_PAYLOAD_SIZE 4096
#define MIN_MQTT_PAYLOAD_SIZE 512
#define MQTT_HEADER_SIZE 512



// For general purpose messaging to the serial port, define DEBUG
#define DEBUG
// For verbose messaging to the serial port, define DEBUG_LEVEL2
//#define DEBUG_LEVEL2


// **************** End of Settings ****************



// MQTT Subscriptions
enum mqttSubscriptions
{
	requestPerformClose,
	requestPerformOpen,
	requestPerformStop,

	requestValuePinClose,
	requestValuePinOpen,
	requestValuePinStop,
	requestValuePinTopSensor,
	requestValuePinBottomSensor,

	requestIsOpen,
	requestIsClosed,
	requestIsStopped,

	requestSetValuePinClose,
	requestSetValuePinOpen,
	requestSetValuePinStop,

	requestClearValuePinClose,
	requestClearValuePinOpen,
	requestClearValuePinStop,

	setTargetDoorState,
	getTargetDoorState,
	getCurrentDoorState,

	subscriptionUnknown
};

// Birth and Last Will and Testament Topic
#define MQTT_CONNECTION_STATUS "/connection_status"

// Current and Target Request and Response Topics
#define MQTT_HOMEKIT_SET_TARGET_DOOR_STATE "/set/target/door/state"
#define MQTT_HOMEKIT_GET_TARGET_DOOR_STATE "/get/target/door/state"
#define MQTT_HOMEKIT_GET_CURRENT_DOOR_STATE "/get/current/door/state"

// Request Topics
#define MQTT_SUB_REQUEST_PERFORM_CLOSE "/request/perform/close"
#define MQTT_SUB_REQUEST_PERFORM_OPEN "/request/perform/open"
#define MQTT_SUB_REQUEST_PERFORM_STOP "/request/perform/stop"
#define MQTT_SUB_REQUEST_IS_OPEN "/request/is/open"
#define MQTT_SUB_REQUEST_IS_CLOSED "/request/is/closed"
#define MQTT_SUB_REQUEST_IS_STOPPED "/request/is/stopped"

// Response Topics
#define MQTT_SUB_RESPONSE_PERFORM_CLOSE "/response/perform/close"
#define MQTT_SUB_RESPONSE_PERFORM_OPEN "/response/perform/open"
#define MQTT_SUB_RESPONSE_PERFORM_STOP "/response/perform/stop"
#define MQTT_SUB_RESPONSE_IS_OPEN "/response/is/open"
#define MQTT_SUB_RESPONSE_IS_CLOSED "/response/is/closed"
#define MQTT_SUB_RESPONSE_IS_STOPPED "/response/is/stopped"

// For development only
#define MQTT_SUB_REQUEST_VALUE_PIN_CLOSE "/request/value/pin/close"
#define MQTT_SUB_REQUEST_VALUE_PIN_STOP "/request/value/pin/stop"
#define MQTT_SUB_REQUEST_VALUE_PIN_OPEN "/request/value/pin/open"
#define MQTT_SUB_REQUEST_VALUE_PIN_TOP_SENSOR "/request/value/pin/top_sensor"
#define MQTT_SUB_REQUEST_VALUE_PIN_BOTTOM_SENSOR "/request/value/pin/bottom_sensor"

// For development only
#define MQTT_SUB_RESPONSE_VALUE_PIN_CLOSE "/response/value/pin/close"
#define MQTT_SUB_RESPONSE_VALUE_PIN_STOP "/response/value/pin/stop"
#define MQTT_SUB_RESPONSE_VALUE_PIN_OPEN "/response/value/pin/open"
#define MQTT_SUB_RESPONSE_VALUE_PIN_TOP_SENSOR "/response/value/pin/top_sensor"
#define MQTT_SUB_RESPONSE_VALUE_PIN_BOTTOM_SENSOR "/response/value/pin/bottom_sensor"

// For development only
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE "/request/set/value/pin/close"
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP "/request/set/value/pin/stop"
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN "/request/set/value/pin/open"

// For development only
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_CLOSE "/response/set/value/pin/close"
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_STOP "/response/set/value/pin/stop"
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_OPEN "/response/set/value/pin/open"

// For development only
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE "/request/clear/value/pin/close"
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP "/request/clear/value/pin/stop"
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN "/request/clear/value/pin/open"

// For development only
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_CLOSE "/response/clear/value/pin/close"
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_STOP "/response/clear/value/pin/stop"
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_OPEN "/response/clear/value/pin/open"



#define MAX_MQTT_NAME_LENGTH 81
#define MAX_MQTT_STATUS_LENGTH 51
#define OLED_CHARACTER_WIDTH 11				// Number of chars which can be displayed per line on the display
#define SETTING_MAX_WIDTH 64				// Number of chars a JSON setting name can be
#define SETTINGS_FILE_BUFFER 512			// Total length of JSON settings buffer
#define DOOR_STATE_MAX_LENGTH 10
#define DEBUG_MAX_LENGTH 256
#define MQTT_TOPIC_MAX_LENGTH 256
#define MQTT_PAYLOAD_STATE_ADDITION 256




/*
CAT5e Colour		RJ 45 Pin		Function				ESP32 Pin
Orange White		1				12V
Green				6				Relay 1 (Up (Open)		GPIO32
Green White			3				Relay 2 (Stop)			GPIO33
Blue				4				Relay 3 (Down (Close))	GPIO25
Brown				8				GND
*/

// Up Pin (Relay 1) Open
#define PIN_FOR_GARAGE_DOOR_OPEN 32
// Stop Pin (Relay 2) Stop
#define PIN_FOR_GARAGE_DOOR_STOP 33
// Down Pin (Relay 3) Close
#define PIN_FOR_GARAGE_DOOR_CLOSE 25

// Opened Sensor
#define PIN_FOR_TOP_SENSOR 26
// Closed Sensor
#define PIN_FOR_BOTTOM_SENSOR 27
// Chip Select (VSPI for Micro-SD Card Reader)
#define PIN_FOR_SDCARD_VSPI_CS 5

// Door States
enum doorState
{
	doorOpening,
	doorClosing,
	doorOpen,
	doorClosed,
	doorStopped,
	doorStateUnknown
};

// Door State Descriptions (Display)
#define DOOR_STATE_OPENING_DESC "Opening"
#define DOOR_STATE_CLOSING_DESC "Closing"
#define DOOR_STATE_OPEN_DESC "Open"
#define DOOR_STATE_CLOSED_DESC "Closed"
#define DOOR_STATE_STOPPED_DESC "Stopped"
#define DOOR_STATE_UNKNOWN_DESC "Unknown"

// MQTT-Thing default statuses, can be changed in conjunction with Homebridge MQTT-Thing for example.
#define DOOR_STATE_HOMEKIT_OPENING "o"
#define DOOR_STATE_HOMEKIT_CLOSING "c"
#define DOOR_STATE_HOMEKIT_OPEN "O"
#define DOOR_STATE_HOMEKIT_CLOSED "C"
#define DOOR_STATE_HOMEKIT_STOPPED "S"
#define DOOR_STATE_HOMEKIT_UNKNOWN "U"

// Ensure we stick to fixed values by forcing from a selection of values
enum statusValues
{
	preProcessing,
	noMQTTPayload,
	invalidMQTTPayload,
	setOpenSuccess,
	setStopSuccess,
	setCloseSuccess,
	payloadExceededCapacity,
	addedToPayload,
	notValidIncomingTopic
};

// MQTT JSON responses for several topics
#define STATUS_PREPROCESSING_MQTT_DESC "preProcessing"
#define STATUS_NO_MQTT_PAYLOAD_MQTT_DESC "noMQTTPayload"
#define STATUS_INVALID_MQTT_PAYLOAD_MQTT_DESC "invalidMQTTPayload"
#define STATUS_SET_OPEN_SUCCESS_MQTT_DESC "setOpenSuccess"
#define STATUS_SET_STOP_SUCCESS_MQTT_DESC "setStopSuccess"
#define STATUS_SET_CLOSE_SUCCESS_MQTT_DESC "setCloseSuccess"
#define STATUS_PAYLOAD_EXCEEDED_CAPACITY_MQTT_DESC "payloadExceededCapacity"
#define STATUS_ADDED_TO_PAYLOAD_MQTT_DESC "addedToPayload"
#define STATUS_NOT_VALID_INCOMING_TOPIC_DESC "notValidIncomingTopic"

// Definitions not found within libraries
#define WL_MAC_ADDR_LENGTH 6

#endif

