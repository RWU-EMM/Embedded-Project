#include <stdint.h>


#include <WiFiNINA.h>
#include "sam.h"

#include "wifi_creds.h"
#include "mqtt_creads.h"

typedef enum {
  _SERIAL_BAUDRATE = 115200,
  _MAX_WIFI_RECONNECT_ATTEMPT = 5,     // max: uint8_t
  _WIFI_RECONNECT_ATTEMPT_DELAY = 10,  // sec, max:uint8_t

} priv_defines_e;

// defines
// wifi client for ops
WiFiClient wifiClient;
// mqtt client for ops
MqttClient mqttClient(wifiClient);


// function Prototypes
void software_reset();
void connect_wifi(const char *ssid, const char *pass);
IPAddress get_ip();
void connect_mqtt_broker(const char *broker, const uint16_t port);
void mqtt_on_msg_cb(int msg_size) ;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(_SERIAL_BAUDRATE);

  connect_wifi(ssid, pass);
  IPAddress ip = get_ip();
  Serial.print("IP Address: ");
  Serial.println(ip);

  connect_mqtt_broker(broker, port);

  mqtt_subs_topic(test_topic_nano);

  // set the message receive callback
  mqttClient.onMessage(mqtt_on_msg_cb);
}

void loop() {
  // put your main code here, to run repeatedly:
  // @docs : call poll() regularly to allow the library to receive MQTT messages and
  // send MQTT keep alive which avoids being disconnected by the broker
  mqttClient.poll();
}

void connect_wifi(const char *ssid, const char *pass) {
  wl_status_t status = WL_IDLE_STATUS;
  uint8_t reconnect_count = _MAX_WIFI_RECONNECT_ATTEMPT;
  while (WL_CONNECTED != status) {
    Serial.print("Attempting to connect to network: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    // @todo: feature: make it updatedbale parameter
    status = (wl_status_t)WiFi.begin(ssid, pass);
    reconnect_count--;
    if (0 == reconnect_count) {
      Serial.println("Failed to Connect to the Netweok");
      Serial.println("Please Check Wi-Fi Credentails");
      // @todo : Feature: should we restart here ? or have while wait ?
      // @todo: Feature : heart_beat_led blink -> on to indicate fault
      break;
    }
    // wait few seconds for connection or reconnection:
    delay(_WIFI_RECONNECT_ATTEMPT_DELAY);
  }
  // connected to the Network
  if (WL_CONNECTED == status) {
    Serial.println("Successfully Connected to the Netweok");
    Serial.println("---------------------------------------");
  }
  // reconnect too failed: system reset
}

void connect_mqtt_broker(const char *broker, const uint16_t port) {
  Serial.print("Attempting to Connect to the Broekr: ");
  Serial.println(broker);
  uint8_t status = mqttClient.connect(broker, port);
  // @tood: fix: add its own Macro
  uint8_t reconnect_count = _MAX_WIFI_RECONNECT_ATTEMPT;
  while (!status) {
    Serial.print("MQTT connection failed! Error code: ");
    Serial.println(mqttClient.connectError());
    status = mqttClient.connect(broker, port);
    reconnect_count--;
    if (0 == reconnect_count) {
      Serial.println("Failed to Connect to the Broker");
      Serial.println("Please Check Broker Credentails");
      // @todo : Feature: should we restart here ? or have while wait ?
      // @todo: Feature : heart_beat_led blink -> on to indicate fault
      break;
    }
    // wait few seconds for connection or reconnection:
    // @todo: fix: gives its own Macro
    delay(_WIFI_RECONNECT_ATTEMPT_DELAY);
  }
  if (status) {
    Serial.println("Successfully Connected to the Broker");
    Serial.println("---------------------------------------");
  }
}

IPAddress get_ip() {
  return WiFi.localIP();
}

void software_reset() {
  NVIC_SystemReset();
}

void mqtt_on_msg_cb(int msg_size) {
  // we received a message, print out the topic and contents
  Serial.println("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(msg_size);
  Serial.println(" bytes:");

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();

  // @test: remove: feedback for testing
  mqtt_pubs_char(test_topic_postman,"ok");

}

void mqtt_subs_topic(const char *topic) {
  Serial.print("Subscribing to topic: ");
  Serial.println(topic);
  mqttClient.subscribe(topic);
}

void mqtt_pubs_char(const char *topic, const char *msg) {
  // send message, the Print interface can be used to set the message contents
  mqttClient.beginMessage(topic);
  mqttClient.print(msg);
  mqttClient.endMessage();
}