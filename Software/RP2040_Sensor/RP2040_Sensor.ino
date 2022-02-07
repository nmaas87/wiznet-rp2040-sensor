/*
  Ethernet Connected Environmental Sensor
  A wired, reliable environmental sensor for supervising air- and enviroment quality for safekeeping workers.
  Witten for the WIZnet Ethernet HAT Contest 2022 by Nico Maas @ 2022-02-06

  used libraries
  - earlephilhower arduino-pico https://github.com/earlephilhower/arduino-pico @ 1.19.3
  - WIZnet-ArduinoEthernet  https://github.com/WIZnet-ArduinoEthernet/Ethernet @ master
  - 256dpi Arduino-MQTT https://github.com/256dpi/arduino-mqtt @ 2.5.0
  - Adafruit BME280 https://github.com/adafruit/Adafruit_BME280_Library @ 2.2.2
  - ArduinoJson https://github.com/bblanchon/ArduinoJson @ 6.19.1

  re-used examples
  - DhcpAddressPrinter (WIZnet-ArduinoEthernet)
  - ArduinoEthernetShield (Arduino-MQTT)
  - bme280_unified (Adafruit BME280)
  - TelAire T6713 Application Note / I2C ( https://paramair.de/app/uploads/2018/01/Amphenol-Application-note-T6713-CO2-Module.pdf )
 */

#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <MQTT.h>
#include <ArduinoJson.h>
#include <Adafruit_BME280.h>

// User settings to configure
// Sensor ID (set to different if multiple sensors in the network)
byte id = 1;
// Enter the IP of the Mosquitto Server
IPAddress server(192, 168, 4, 115);
// Enter a MAC address for your Wiznet RP2040 below
byte mac[] = {
  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02
};

// No settings beyond this point needed

// Ethernet and MQTT
EthernetClient net;
MQTTClient client;
unsigned long lastMillis = 0;

// BME280
Adafruit_BME280 bme; // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();

// CO2 Meter Amphenol T6713
#define ADDR_6713 0x15 // default I2C address
int data [4];
int CO2ppmValue = 0;

void connect() {
  // connect to MQTT server
  Serial.print("connecting...");
  while (!client.connect("arduino", "public", "public")) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nconnected!");
  // here you could subscribe to a topic
  // e.g. if you want to make the sensor remote controlled
  // client.subscribe("/hello");
  // client.unsubscribe("/hello");
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
  // Note: Do not use the client in the callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable,
  // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void setup() {
  // Ethernet Setup
  Ethernet.init(17); // W5100S-EVB-Pico

  // Open serial communications
  Serial.begin(115200);

  // I2C setup
  Wire.begin();
  Wire.setClock(400000L);

  // BME280 Init
  if (!bme.begin(0x76)) {
    Serial.println(F("bme280InitErr"));
    for(;;); // Don't proceed, loop forever
  }

  // start the Ethernet connection:
  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // no point in carrying on, so do nothing forevermore:
    while (true) {
      delay(1);
    }
  }
  // print your local IP address:
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());

  //client.begin("public.cloud.shiftr.io", net);
  client.begin(server, net); 
  client.onMessage(messageReceived);
  connect();
}

void loop() {
  // keep Ethernet and DHCP working
  switch (Ethernet.maintain()) {
    case 1:
      //renewed fail
      Serial.println("Error: renewed fail");
      break;
    case 2:
      //renewed success
      Serial.println("Renewed success");
      //print your local IP address:
      Serial.print("My IP address: ");
      Serial.println(Ethernet.localIP());
      break;
    case 3:
      //rebind fail
      Serial.println("Error: rebind fail");
      break;
    case 4:
      //rebind success
      Serial.println("Rebind success");
      //print your local IP address:
      Serial.print("My IP address: ");
      Serial.println(Ethernet.localIP());
      break;
    default:
      //nothing happened
      break;
  }
  
  // keep MQTT alive
  client.loop();  
  
  // reconnect MQTT if connection broken
  if (!client.connected()) {
    connect();
  }

  // measure and publish a message roughly every 60 seconds.
  if (millis() - lastMillis > 57500) {
    // Allocate the JSON document with 200 bytes memory
    StaticJsonDocument<200> sensor;

    // set sensor id in MQTT message - to identify multiple sensors
    sensor["id"] = id;
    
    // BME280
    sensors_event_t temp_event, pressure_event, humidity_event;
    bme_temp->getEvent(&temp_event);
    bme_pressure->getEvent(&pressure_event);
    bme_humidity->getEvent(&humidity_event);
    // get sensor data
    Serial.print(float(temp_event.temperature));
    Serial.print("C - ");
    Serial.print(float(pressure_event.pressure));
    Serial.print("hPa - ");
    Serial.print(float(humidity_event.relative_humidity));
    Serial.print("% - ");
    // output to serial
    sensor["temperature"] = float(temp_event.temperature);
    sensor["pressure"] = float(pressure_event.pressure);
    sensor["humidity"] = float(humidity_event.relative_humidity);
    // output to MQTT message
    
    // Amphenol T6713
    Wire.beginTransmission(ADDR_6713);
    Wire.write(0x04); 
    Wire.write(0x13); 
    Wire.write(0x8B); 
    Wire.write(0x00); 
    Wire.write(0x01);
    Wire.endTransmission();
    delay(2500);
    // read report of current gas measurement in ppm after giving sensor time for analysis
    Wire.requestFrom(ADDR_6713, 4);
    // request 4 bytes from slave device
    data[0] = Wire.read();
    data[1] = Wire.read();
    data[2] = Wire.read();
    data[3] = Wire.read();
    CO2ppmValue = int((data[2] * 0xFF ) + data[3]);
    // calculate ppm value
    Serial.print(CO2ppmValue);
    Serial.println("ppm");
    // output to serial
    sensor["co2"] = CO2ppmValue;
    // output to MQTT message
        
    // prepare JSON and send via MQTT
    char buffer[256];
    size_t n = serializeJson(sensor, buffer);
    client.publish("/sensors/" + id, buffer, n);
    lastMillis = millis();
  }
}
