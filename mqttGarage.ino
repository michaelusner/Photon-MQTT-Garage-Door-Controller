// MQTT garage door controller for use with Home Assistant
// Copyright 2020 - Michael Usner
//
// If you're fortunate enough to have 2 garage doors, you'll need to modify this code.
// TODO - configure via HTTP form

// This #include statement was automatically added by the Particle IDE.
#include <MQTT.h>

#define LOGGING

#define MQTT_CLIENT_NAME  "garage_controller"
#define MQTT_BROKER 192,168,1,9		// your broker IP here
#define MQTT_PORT 1883				// your broker port here

#define MQTT_USER "<user>"			// your broker user here
#define MQTT_PASSWORD "<pass>"		// your broker password here

#define MQTT_HASSIO_ONLINE_PAYLOAD  "online"
#define MQTT_HASSIO_STATUS_TOPIC    "homeassistant/status"
#define MQTT_DISCOVERY_TOPIC        "homeassistant/cover/garage/rollerdoor/config"
#define MQTT_AVAILABILITY_TOPIC     "homeassistant/cover/garage/rollerdoor/availability"
#define MQTT_COMMAND_TOPIC          "homeassistant/cover/garage/rollerdoor/set"
#define MQTT_STATE_TOPIC            "homeassistant/cover/garage/rollerdoor/state"
#define MQTT_OPEN_COMMAND           "open"
#define MQTT_CLOSE_COMMAND          "close"
#define MQTT_STOP_COMMAND           "stop"
#define MQTT_RECONNECT_MS 30000
#define MQTT_MESSAGE_SIZE 1024

#define OPEN_STATE      "open"
#define OPENING_STATE   "opening"
#define CLOSED_STATE     "closed"
#define CLOSING_STATE   "closing"

// get the device ID and use it in the device config payload
String deviceID = System.deviceID();
String deviceConfig = "{\"name\": \"Garage Door\", \"unique_id\": \"" + deviceID + "\", \"command_topic\": \"" + MQTT_COMMAND_TOPIC + "\", \"state_topic\": \"" + MQTT_STATE_TOPIC + "\", \"availability_topic\": \"" + MQTT_AVAILABILITY_TOPIC  + "\", \"retain\": true, \"payload_available\": \"online\", \"payload_not_available\": \"offline\", \"payload_open\":\"" + MQTT_OPEN_COMMAND + "\", \"payload_close\": \"" + MQTT_CLOSE_COMMAND + "\",  \"payload_stop\": \"" + MQTT_STOP_COMMAND + "\", \"state_open\": \"" + OPEN_STATE + "\", \"state_opening\": \"" + OPENING_STATE + "\", \"state_closed\": \"" + CLOSED_STATE + "\", \"state_closing\": \"" + CLOSING_STATE + "\", \"optimistic\": false, \"value_template\": \"{{ value }}\"}";

// Create the MQTT client
byte server[] = { MQTT_BROKER };
MQTT client(server, MQTT_PORT, mqttMessageReceived, MQTT_MESSAGE_SIZE); 

// The relay pins.  Note that relay 2 is reserved for door 2 but is not in use here
int relay1 = D2;
int relay2 = D3;

// The magnetic switch pin
int magSwitch1 = D5;

// The magnetic switch status
int magStatus1 = 0;
int lastStatus = -1;

// This timer waits 20 seconds for the door to open or close
Timer waitForDoor(20000, refreshState, true);

// This timer is the polling loop for the magnetic switch
Timer pollMagSwitchTimer(500, pollSwitch, false);

// Poll the magnetic switch and set the state accordingly
// "lastStatus" is used to only send messages when the state has actually changed
void pollSwitch() {
    magStatus1 = digitalRead(magSwitch1);
    if (magStatus1 == HIGH && lastStatus != magStatus1) {
        setState("closed");
        lastStatus = magStatus1;
    } else if (magStatus1 == LOW && lastStatus != magStatus1) {
        setState("open");
        lastStatus = magStatus1;
    }
}

// The MQTT 'message received' callback
void mqttMessageReceived(char* topic, byte* payload, unsigned int length) {
    char p[length + 1];
    memcpy(p, payload, length);
    p[length] = NULL;
    String message(p);
    payload[length] = '\0';
    String strTopic = String(topic).toLowerCase();
    String strPayload = String((char*)payload);
    
    Particle.publish("DEBUG", "MQTT message received on topic: " + strTopic + " : " + strPayload, PRIVATE);
    
	// process the message
	if (strTopic == MQTT_DISCOVERY_TOPIC) {
		// Home Assistant will send the payload back after the device discovery.
		// We use this as a signal to let HA know the device is available and to
		// update the state.		
        sendAvailable();
        delay(1000);
        refreshState();
	} else if (strTopic == MQTT_HASSIO_STATUS_TOPIC && strPayload == MQTT_HASSIO_ONLINE_PAYLOAD) {
		// Home Assistant has restarted.  Trigger a device discovery to refresh.
        addDevice();
    } else if (strTopic == MQTT_COMMAND_TOPIC && strPayload == MQTT_OPEN_COMMAND) {
        openGarage();
    } else if (strTopic == MQTT_COMMAND_TOPIC && strPayload == MQTT_CLOSE_COMMAND) {
        closeGarage();
    }
}

// Send a MQTT message to a topic
void sendMqttMessage(String topic, String message) {
    String msg = "Sending message to ";
    msg += topic + ": " + message;
    client.publish(topic, message);
}

// Send the "available" message to HA
void sendAvailable() {
    sendMqttMessage(MQTT_AVAILABILITY_TOPIC, MQTT_HASSIO_ONLINE_PAYLOAD);
}

// Add the device to HA triggering a 'discovery'
void addDevice() {
    sendMqttMessage(MQTT_DISCOVERY_TOPIC, deviceConfig);
}

// Remove this device from HA by sending an empty payload per the HA docs
void removeDevice() {
    sendMqttMessage(MQTT_DISCOVERY_TOPIC, "");
}

// Send a state message to HA
void setState(String state) {
    Particle.publish("DEBUG", "State: " + state, PRIVATE);
    sendMqttMessage(MQTT_STATE_TOPIC, state);
}

// Connect to the MQTT instance
void connectMQTT() {
    Particle.publish("DEBUG", "Connecting to MQTT broker", PRIVATE);
    client.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASSWORD);
    client.subscribe(MQTT_HASSIO_STATUS_TOPIC);
    client.subscribe(MQTT_COMMAND_TOPIC);
    client.subscribe(MQTT_DISCOVERY_TOPIC);
}

// Refresh the door position state by reading the mag sensor and setting status
void refreshState() {
    magStatus1 = digitalRead(magSwitch1);
    if (magStatus1 == HIGH) {
        setState(CLOSED_STATE);
        lastStatus = magStatus1;
    } else if (magStatus1 == LOW) {
        setState(OPEN_STATE);
        lastStatus = magStatus1;
    }
}

// setup the controller
void setup() {
    // configure the mag sensor
    pinMode(magSwitch1, INPUT_PULLDOWN);

    // configure the relays
    pinMode(relay1, OUTPUT);
    pinMode(relay2, OUTPUT);
    digitalWrite(relay1, HIGH);
    digitalWrite(relay2, HIGH);
    
	// connect and add the device
    connectMQTT();
    addDevice();
    
	// start the polling loop timer
    pollMagSwitchTimer.start();
}

// the main controller loop
void loop() {
	// wait for a message
    if (client.isConnected()) {
        client.loop();
    }
    else // reconnect if we lost the connection
    {
        Particle.publish("ERROR", "MQTT client disconnected.  Reconnecting in 30s", PRIVATE);
        delay(MQTT_RECONNECT_MS);   
        connectMQTT();
    }
}

// Determine if the door is closed by reading the switch,
// setting state, and returning the result
int isClosed() {
    magStatus1 = digitalRead(magSwitch1);
    if (magStatus1 == HIGH) {
        setState(CLOSED_STATE);
        return 1;
    } else {
        setState(OPEN_STATE);
        return 0;
    }
}

// Open the door
int openGarage() {
    if (isClosed()) {
        setState(OPENING_STATE);
        digitalWrite(relay1, LOW);
        delay(1000);
        digitalWrite(relay1, HIGH);
        delay(1000);
        waitForDoor.start();
    } else {
        Particle.publish("DEBUG", "Garage is already open", PRIVATE);
        return 0;
    }
    return 0;
}

// Close the door
int closeGarage() {
    if (!isClosed()) {
        setState(CLOSING_STATE);
        digitalWrite(relay1, LOW);
        delay(1000);
        digitalWrite(relay1, HIGH);
        delay(1000);
        waitForDoor.start();
    } else {
        Particle.publish("DEBUG", "Garage is already closed", PRIVATE);
        return 0;
    }
    return 0;
}


