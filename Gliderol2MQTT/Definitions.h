// Definitions.h

#ifndef _DEFINITIONS_h
#define _DEFINITIONS_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif


// Update with your Wifi details
#define WIFI_SSID	"Stardust"
#define WIFI_PASSWORD	"Sniegulinka1983"

// Update with your MQTT Broker details
#define MQTT_SERVER	"192.168.1.40"
#define MQTT_PORT	1883
#define MQTT_USERNAME	"Alpha"			// Empty string for none.
#define MQTT_PASSWORD	"Inverter1"

// The device name is used as the MQTT base topic and presence on the network.
// If you need more than one Alpha2MQTT on your network, give them unique names.
#define DEVICE_NAME "Gliderol2MQTT"

// Comment out if SD Card not used, it will simply bypass the attempts to load from SD Card
#define SDCARD
#define USING_TOP_SENSOR false


// These timers are used in the main loop.
// How often to update the status on the screen
#define DISPLAY_INTERVAL 250

// How often to flash the top indicators
#define UPDATE_STATUS_BAR_INTERVAL 500

// How often the status of the door is sent out
#define MQTT_STATUS_INTERVAL 3000

#define TIME_TO_FULLY_OPEN_FROM_FULLY_CLOSED 10000
#define TIME_TO_FULLY_CLOSED_FROM_FULLY_OPEN 10000


// The ESP8266 has limited memory and so reserving lots of RAM to build a payload and MQTT buffer causes out of memory exceptions.
// 4096 works well given the RAM requirements of Alpha2MQTT.
// If you aren't using an ESP8266 you may be able to increase this.
// At 4096 (4095 usable) you can request up to around 70 to 80 registers on a schedule or by request.
// Alpha2MQTT on boot will request a buffer size of (MAX_MQTT_PAYLOAD_SIZE + MQTT_HEADER_SIZE) for MQTT, and
// MAX_MQTT_PAYLOAD_SIZE for building payloads.  If these fail and your device doesn't boot, you can assume you've set this too high.
#define MAX_MQTT_PAYLOAD_SIZE 4096
#define MIN_MQTT_PAYLOAD_SIZE 512
#define MQTT_HEADER_SIZE 512





// MQTT Subscriptions
enum mqttSubscriptions
{
	requestPerformClose,
	requestPerformOpen,
	requestPerformStop,

	requestValuePinClose,
	requestValuePinOpen,
	requestValuePinStop,
	requestValuePinRelayPower,

	requestIsOpen,
	requestIsClosed,
	requestIsStopped,

	requestSetValuePinClose,
	requestSetValuePinOpen,
	requestSetValuePinStop,
	requestSetValuePinRelayPower,

	requestClearValuePinClose,
	requestClearValuePinOpen,
	requestClearValuePinStop,
	requestClearValuePinRelayPower,

	setTargetDoorState,
	getTargetDoorState,
	getCurrentDoorState,

	subscriptionUnknown
};

#define MQTT_HOMEKIT_SET_TARGET_DOOR_STATE "/set/target/door/state"
#define MQTT_HOMEKIT_GET_TARGET_DOOR_STATE "/get/target/door/state"
#define MQTT_HOMEKIT_GET_CURRENT_DOOR_STATE "/get/current/door/state"

#define MQTT_SUB_REQUEST_PERFORM_CLOSE "/request/perform/close"
#define MQTT_SUB_REQUEST_PERFORM_OPEN "/request/perform/open"
#define MQTT_SUB_REQUEST_PERFORM_STOP "/request/perform/stop"
#define MQTT_SUB_REQUEST_IS_OPEN "/request/is/open"
#define MQTT_SUB_REQUEST_IS_CLOSED "/request/is/closed"
#define MQTT_SUB_REQUEST_IS_STOPPED "/request/is/stopped"

#define MQTT_SUB_REQUEST_VALUE_PIN_CLOSE "/request/value/pin/close"
#define MQTT_SUB_REQUEST_VALUE_PIN_STOP "/request/value/pin/stop"
#define MQTT_SUB_REQUEST_VALUE_PIN_OPEN "/request/value/pin/open"
#define MQTT_SUB_REQUEST_VALUE_PIN_RELAYPOWER "/request/value/pin/relaypower"


#define MQTT_SUB_REQUEST_SET_VALUE_PIN_CLOSE "/request/set/value/pin/close"
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_STOP "/request/set/value/pin/stop"
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_OPEN "/request/set/value/pin/open"
#define MQTT_SUB_REQUEST_SET_VALUE_PIN_RELAYPOWER "/request/set/value/pin/relaypower"

#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_CLOSE "/request/clear/value/pin/close"
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_STOP "/request/clear/value/pin/stop"
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_OPEN "/request/clear/value/pin/open"
#define MQTT_SUB_REQUEST_CLEAR_VALUE_PIN_RELAYPOWER "/request/clear/value/pin/relaypower"


// MQTT Responses
#define MQTT_SUB_RESPONSE_PERFORM_CLOSE "/response/perform/close"
#define MQTT_SUB_RESPONSE_PERFORM_OPEN "/response/perform/open"
#define MQTT_SUB_RESPONSE_PERFORM_STOP "/response/perform/stop"
#define MQTT_SUB_RESPONSE_IS_OPEN "/response/is/open"
#define MQTT_SUB_RESPONSE_IS_CLOSED "/response/is/closed"
#define MQTT_SUB_RESPONSE_IS_STOPPED "/response/is/stopped"

#define MQTT_SUB_RESPONSE_VALUE_PIN_CLOSE "/response/value/pin/close"
#define MQTT_SUB_RESPONSE_VALUE_PIN_STOP "/response/value/pin/stop"
#define MQTT_SUB_RESPONSE_VALUE_PIN_OPEN "/response/value/pin/open"
#define MQTT_SUB_RESPONSE_VALUE_PIN_RELAYPOWER "/response/value/pin/relaypower"

#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_CLOSE "/response/set/value/pin/close"
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_STOP "/response/set/value/pin/stop"
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_OPEN "/response/set/value/pin/open"
#define MQTT_SUB_RESPONSE_SET_VALUE_PIN_RELAYPOWER "/response/set/value/pin/relaypower"

#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_CLOSE "/response/clear/value/pin/close"
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_STOP "/response/clear/value/pin/stop"
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_OPEN "/response/clear/value/pin/open"
#define MQTT_SUB_RESPONSE_CLEAR_VALUE_PIN_RELAYPOWER "/response/clear/value/pin/relaypower"


#define MAX_MQTT_NAME_LENGTH 81
#define MAX_MQTT_STATUS_LENGTH 51
#define OLED_CHARACTER_WIDTH 11
#define SETTING_MAX_WIDTH 32
#define SETTINGS_FILE_BUFFER 512
#define DOOR_STATE_MAX_LENGTH 10
#define DEBUG_MAX_LENGTH 100
#define MQTT_TOPIC_MAX_LENGTH 100
#define MQTT_PAYLOAD_LINE_MAX_LENGTH




/*
RJ45 Colour			RJ 45 Pin		Function				ESP8266 Pin
Orange White		1				12V
Green White			3				Relay 3 (Stop)			D7
Blue				4				Relay 2 (Down (Close))	D6
Green				6				Relay 1 (Up (Open)		D5
Brown				8				GND
*/

#define PIN_FOR_GARAGE_DOOR_STOP D5		// Stop Pin (Relay 3) 
#define PIN_FOR_GARAGE_DOOR_CLOSE D3	// Down Pin (Relay 2) Close
#define PIN_FOR_GARAGE_DOOR_OPEN D4		// Up Pin (Relay 1) Open


#define PIN_FOR_RELAY_POWER D8			// Power Safeguard Relay Power
#define PIN_FOR_BOTTOM_SENSOR D6 // Closed Sensor
#define PIN_FOR_TOP_SENSOR D7 // Open Sensor

//D8 orig sensor

//#define PIN_FOR_GARAGE_DOOR_OPEN D0 // Transmission set pin
//#define PIN_FOR_GARAGE_DOOR_STOP D3 // Serial Receive pin
//#define PIN_FOR_GARAGE_DOOR_CLOSE D10 // Serial Transmit pin
//#define PIN_FOR_SENSOR D4 // Open or Closed Sensor pin


enum doorState
{
	opening,
	closing,
	open,
	closed,
	stopped,
	stateUnknown
};

#define DOOR_STATE_OPENING_DESC "Opening"
#define DOOR_STATE_CLOSING_DESC "Closing"
#define DOOR_STATE_OPEN_DESC "Open"
#define DOOR_STATE_CLOSED_DESC "Closed"
#define DOOR_STATE_STOPPED_DESC "Stopped"
#define DOOR_STATE_UNKNOWN_DESC "Unknown"


#define DOOR_STATE_HOMEKIT_OPENING "o"
#define DOOR_STATE_HOMEKIT_CLOSING "c"
#define DOOR_STATE_HOMEKIT_OPEN "O"
#define DOOR_STATE_HOMEKIT_CLOSED "C"
#define DOOR_STATE_HOMEKIT_STOPPED "S"
#define DOOR_STATE_HOMEKIT_UNKNOWN "U"

// Ensure we stick to fixed values by forcing from a selection of values for a Modbus request & response
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

#define STATUS_PREPROCESSING_MQTT_DESC "preProcessing"
#define STATUS_NO_MQTT_PAYLOAD_MQTT_DESC "noMQTTPayload"
#define STATUS_INVALID_MQTT_PAYLOAD_MQTT_DESC "invalidMQTTPayload"
#define STATUS_SET_OPEN_SUCCESS_MQTT_DESC "setOpenSuccess"
#define STATUS_SET_STOP_SUCCESS_MQTT_DESC "setStopSuccess"
#define STATUS_SET_CLOSE_SUCCESS_MQTT_DESC "setCloseSuccess"
#define STATUS_PAYLOAD_EXCEEDED_CAPACITY_MQTT_DESC "payloadExceededCapacity"
#define STATUS_ADDED_TO_PAYLOAD_MQTT_DESC "addedToPayload"
#define STATUS_NOT_VALID_INCOMING_TOPIC_DESC "notValidIncomingTopic"


#define DEBUG
//#define DEBUG_LEVEL2 // For serial flooding action

#endif

