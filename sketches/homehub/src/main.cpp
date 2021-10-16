/**** 
 * homehub
 *  Compile with board WEMOS D1 R1
 * 
 * F. Guiet 
 * Creation           : End of 2018
 * Last modification  : 20211016
 * 
 * Version            : 2.0
 * 
 * History            : Huge refactoring - add mqtt toppic for each sensor
 *                      Remove first / in topic, fix bug in mqtt connect (possible infinite loop), improve debug_message function
 *                      20190310 - Add main entry door reedswitch sensor
 *                      1.4 - 20201018 - Move to Arduino Json 6 and change office sensor mac address
 *                      1.5 - 20201021 - Change salon mac address
 *                      2.0 - 20211016 - Migrate project to PlatformIO, Use of ESP8266WiFiMulti, Publish message in JSON for all sensors type, some refactoring
 *                      
 *                      
 */

//Software serial (allow debugging...)

#include <SoftwareSerial.h>
#include <ESP8266WiFiMulti.h>
//#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <secret.h>

//#define SOFTSERIAL_TX D3 
//#define SOFTSERIAL_RX D2 
#define DEBUG 0
#define MAX_RETRY 50
#define MQTT_SERVER "mqtt.guiet.lan"
#define FIRMWARE_VERSION "2.0"

#define HUB_UPSTAIRS
//#define HUB_DOWNSTAIRS

#if defined(HUB_UPSTAIRS)
  #define MQTT_CLIENT_ID "HubUpstairsMqttClient"  
  #define MQTT_HUB_TOPIC "guiet/upstairs/hub"      
  #define MQTT_HUB_TOPIC_V1 "v1/guiet/upstairs/hub"      
  #define MQTT_HUB_MESSAGE "HUB_UPSTAIRS_ALIVE"
  #define HUB_HOSTNAME "HUB_BLE_UPSTAIRS"
#endif

#if defined(HUB_DOWNSTAIRS)    
  #define MQTT_CLIENT_ID "HubDownstairsMqttClient"  
  #define MQTT_HUB_TOPIC "guiet/downstairs/hub"  
  #define MQTT_HUB_TOPIC_V1 "v1/guiet/upstairs/hub"      
  #define MQTT_HUB_MESSAGE "HUB_DOWNSTAIRS_ALIVE"  
  #define HUB_HOSTNAME "HUB_BLE_DOWNSTAIRS"
#endif

SoftwareSerial softSerial; //(SOFTSERIAL_RX, SOFTSERIAL_TX); // RX, TX

const int SERIAL_BAUD = 9600;
const int MQTT_PORT = 1883;
const int serialBufSize = 200;      //size for at least the size of incoming JSON string
static char serialbuffer[serialBufSize]; 
//const int pinHandShake = D3; //handshake pin, Wemos R1 Mini D3
unsigned long previousMillis=0;
const int INTERVAL = 30000; //Every 30s
char message_buff[200];

ESP8266WiFiMulti WiFiMulti;

WiFiClient espClient;
PubSubClient client(espClient);

struct Sensor {
    String Address;
    String Name;    
    String SensorId;
    String Temperature;
    String Humidity;
    String Battery;
    String Rssi;
    String Mqtt_topic;
    String Mqtt_topic_v1;
    String State;
    String Type;
};

//*** CHANGE IT
#define SENSORS_COUNT 4

Sensor sensors[SENSORS_COUNT];

void InitSensors() {

  //String SENSORID =  "1"; //Bureau
  //String SENSORID =  "2"; //Salon
  //String SENSORID =  "3"; //Nohe
  //String SENSORID =  "4"; //Manon
  //String SENSORID =  "5"; //Parents
  //String SENSORID =  "12"; //Bathroom (upstairs)
  
  #if defined(HUB_UPSTAIRS)
    sensors[0].Address = "d2:48:c8:a5:35:4c";
    sensors[0].Name = "Manon";
    sensors[0].SensorId = "4";
    sensors[0].Mqtt_topic = "guiet/upstairs/room_manon/sensor/4";
    sensors[0].Mqtt_topic_v1 = "v1/guiet/upstairs/room_manon/sensor/4";
    sensors[0].Type = "Environmental";
    
    sensors[1].Address = "c7:b9:43:94:24:3a";
    sensors[1].Name = "Nohé";
    sensors[1].SensorId = "3";
    sensors[1].Mqtt_topic = "guiet/upstairs/room_nohe/sensor/3";
    sensors[1].Mqtt_topic_v1 = "v1/guiet/upstairs/room_nohe/sensor/3";
    sensors[1].Type = "Environmental";
    
    sensors[2].Address = "e9:3d:63:97:39:5e";
    sensors[2].Name = "Parents";
    sensors[2].SensorId = "5";
    sensors[2].Mqtt_topic = "guiet/upstairs/room_parents/sensor/5";
    sensors[2].Mqtt_topic_v1 = "v1/guiet/upstairs/room_parents/sensor/5";
    sensors[2].Type = "Environmental";

    sensors[3].Address = "d8:15:dc:ff:2c:4d";
    sensors[3].Name = "Bathroom";
    sensors[3].SensorId = "12";
    sensors[3].Mqtt_topic = "guiet/upstairs/bathroom/sensor/12";
    sensors[3].Mqtt_topic_v1 = "v1/guiet/upstairs/bathroom/sensor/12";
    sensors[3].Type = "Environmental";
  #endif
  
  #if defined(HUB_DOWNSTAIRS)
    sensors[0].Address = "d7:e0:cf:0e:99:c1";
    sensors[0].Name = "Salon";
    sensors[0].SensorId = "2";
    sensors[0].Mqtt_topic = "guiet/downstairs/livingroom/sensor/2";    
    sensors[0].Mqtt_topic_v1 = "v1/guiet/downstairs/livingroom/sensor/2";
    sensors[0].Type = "Environmental";

    sensors[1].Address = "d9:e1:d3:f5:6e:61";
    sensors[1].Name = "Bureau";
    sensors[1].SensorId = "1";
    sensors[1].Mqtt_topic = "guiet/downstairs/office/sensor/1";
    sensors[1].Mqtt_topic_v1 = "v1/guiet/downstairs/office/sensor/1";
    sensors[1].Type = "Environmental";

    sensors[2].Address = "c5:f7:7b:9b:24:46";
    sensors[2].Name = "Reedswitch - porte entrée";
    sensors[2].SensorId = "16";
    sensors[2].Mqtt_topic = "guiet/downstairs/livingroom/sensor/16";
    sensors[2].Mqtt_topic_v1 = "";
    sensors[2].Type = "Reedswitch";
  #endif
}

void debug_message(String message, bool doReturnLine) {
  if (DEBUG) {

    if (doReturnLine)
      Serial.println(message);
    else
      Serial.print(message);

    Serial.flush();
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD); //ESP8266 default serial on UART0 is GPIO1 (TX) and GPIO3 (RX)

  //D2 = GPIO4
  //D3 = GPIO0
  softSerial.begin(SERIAL_BAUD,SWSERIAL_8N1, 4, 0, false); 
  if (!softSerial) {
    debug_message("Invalid SoftwareSerial pin configuration, check config", true);  
    ESP.restart();
  }

  InitSensors();
  
  debug_message("Ready", true);
}

int getSensorByAddress(String address, Sensor &sensor) {
  for(int i=0;i<SENSORS_COUNT;i++) {
    if (sensors[i].Address == address) {
      sensor = sensors[i];
      debug_message("Found : " + sensor.SensorId, true);
      return 1;
    }
  }

  return 0;
}

void connectToMqtt() {
  
  client.setServer(MQTT_SERVER, MQTT_PORT); 

  int retry = 0;

  debug_message("Attempting MQTT connection...", true);
  while (!client.connected()) {   
    if (client.connect(MQTT_CLIENT_ID)) {
      debug_message("connected to MQTT Broker...", true);
    }
    else {
      delay(500);
      debug_message(".", false);
      retry++;
    }

    if (retry >= MAX_RETRY) {
      ESP.restart();
    }
  }
}

void connectToWifi() 
{
  debug_message("Connecting to WiFi...", true);
    
  WiFi.mode(WIFI_STA); //Should be there, otherwise hostname is not set properly  
  WiFi.setHostname(HUB_HOSTNAME);
  
  WiFiMulti.addAP(ssid1, password1);
  WiFiMulti.addAP(ssid2, password2);

  int retry = 0;
  while (WiFiMulti.run() != WL_CONNECTED) {  
    delay(500);

    debug_message(".", false);
    retry++;  

    if (retry >= MAX_RETRY)
      ESP.restart();
  }

  if (DEBUG) {
    Serial.println ( "" );
    Serial.print ( "Connected to " );
    Serial.println ( WiFi.SSID() );
    Serial.print ( "IP address: " );
    Serial.println ( WiFi.localIP() );
    Serial.print ( "MAC: " );    
    Serial.println ( WiFi.macAddress());    
  }
}

//stealing this code from
//https://hackingmajenkoblog.wordpress.com/2016/02/01/reading-serial-on-the-arduino/
//non-blocking serial readline routine, very nice.  Allows mqtt loop to run.
int readline(int readch, char *buffer, int len)
{
  static int pos = 0;
  int rpos;

  if (readch > 0) {
    switch (readch) {
      case '\r': // Ignore new-lines
        break;
      case '\n': // Return on CR
        rpos = pos;
        pos = 0;  // Reset position index ready for next time
        return rpos;
      default:
        if (pos < len-1) {
          buffer[pos++] = readch;   //first buffer[pos]=readch; then pos++;
          buffer[pos] = 0;
        }
    }
  }
  // No end of line has been found, so return -1.
  return -1;
}

String ConvertToJSon() {
    
    DynamicJsonDocument  jsonBuffer(200);
    JsonObject root = jsonBuffer.to<JsonObject>();

    root["name"] = HUB_HOSTNAME;
    root["firmware"] = FIRMWARE_VERSION;
    root["freeheap"] = String(ESP.getFreeHeap());
  
    String result;    
    serializeJson(root, result);

    return result;
}

String ConvertToJSon(Sensor sensor) {
    
    DynamicJsonDocument  jsonBuffer(200);
    JsonObject root = jsonBuffer.to<JsonObject>();

    if (sensor.Type == "Reedswitch") {  
      root["sensorid"] = sensor.SensorId;
      root["name"] = sensor.Name;
      root["firmware"]  = FIRMWARE_VERSION;
      root["rssi"] = sensor.Rssi;
      root["battery"] = sensor.Battery;
      root["state"] = sensor.State;
    }

    if (sensor.Type == "Environmental") {  
      root["sensorid"] = sensor.SensorId;
      root["name"] = sensor.Name;
      root["firmware"]  = FIRMWARE_VERSION;
      root["rssi"] = sensor.Rssi;
      root["battery"] = sensor.Battery;
      root["humidity"] = sensor.Humidity;
      root["temperature"] = sensor.Temperature;
    }
    
    String result;    
    serializeJson(root, result);

    return result;
}

void jsonParser(char *buffer) {
  DynamicJsonDocument  jsonObj(200);
  //JsonObject& jsonObj = jsonBuffer.parseObject(buffer);
  //JsonObject jsonObj = jsonBuffer.to<JsonObject>();
  auto error = deserializeJson(jsonObj, buffer);

  if (!error)
  {
    Sensor sensor;
    int val = getSensorByAddress(jsonObj["Address"].as<String>(), sensor);

    if (val != 0) {

      if (sensor.Type == "Reedswitch") {
      
          sensor.Battery = jsonObj["Battery"].as<String>();
          sensor.State = jsonObj["State"].as<String>();
          sensor.Rssi = jsonObj["Rssi"].as<String>();

          String mess = ConvertToJSon(sensor);
      
          debug_message("Publishing : " + mess, true);
                      
          mess.toCharArray(message_buff, mess.length()+1);
          
          client.publish(sensor.Mqtt_topic.c_str() ,message_buff);
        
      }

      if (sensor.Type == "Environmental") {
      
          sensor.Temperature = jsonObj["Temperature"].as<String>();
          sensor.Battery = jsonObj["Battery"].as<String>();
          sensor.Humidity = jsonObj["Humidity"].as<String>();
          sensor.Rssi = jsonObj["Rssi"].as<String>();
      
          String mess = "SETINSIDEINFO;"+sensor.SensorId+";"+sensor.Temperature+";"+sensor.Humidity+";"+sensor.Battery+";"+sensor.Rssi;
          String messJson = ConvertToJSon(sensor);
      
          debug_message("Publishing : " + mess, true);
                      
          //Deprecated message version
          mess.toCharArray(message_buff, mess.length()+1);                    
          client.publish(sensor.Mqtt_topic.c_str() ,message_buff);

          debug_message("Publishing : " + messJson, true);

          //Publish Json version 
          messJson.toCharArray(message_buff, messJson.length()+1);
          client.publish(sensor.Mqtt_topic_v1.c_str() ,message_buff);

      }    
    }
  }
  else {
    debug_message("Error parsing : " + String(buffer), true);
  }
}

void loop() {
  
  if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_BUILTIN, LOW);  //LED on     
      connectToWifi();
      digitalWrite(LED_BUILTIN, HIGH);  //LED off
  }

  if (!client.connected()) {
    connectToMqtt();
  }

  client.loop();
  
  if (softSerial.available() > 0) {
  //if (Serial.available() > 0) {
    //received serial line of json
    if (readline(softSerial.read(), serialbuffer, serialBufSize) > 0)
    {
      //digitalWrite(pinHandShake, HIGH);
      digitalWrite(LED_BUILTIN, LOW);  //LED on
      
      debug_message("You entered: >", false);
      debug_message(serialbuffer, false);
      debug_message("<", true);

      jsonParser(serialbuffer);
      
      digitalWrite(LED_BUILTIN, HIGH);  //LED off
      //digitalWrite(pinHandShake, LOW);
    }
  }

  unsigned long currentMillis = millis();
  if (((unsigned long)(currentMillis - previousMillis) >= INTERVAL) || ((unsigned long)(millis() - previousMillis) < 0)) {
    
    //Publish alive topic every 30s

    //DEPRECATED !! Will be removed
    String mess = String(MQTT_HUB_MESSAGE) + ";" + String(FIRMWARE_VERSION) + ";" + String(ESP.getFreeHeap()); 
    mess.toCharArray(message_buff, mess.length()+1);
    client.publish(MQTT_HUB_TOPIC,message_buff);

    debug_message("Publishing : " + mess, true);    

    String messJson =  ConvertToJSon();
    messJson.toCharArray(message_buff, messJson.length()+1);
    client.publish(MQTT_HUB_TOPIC_V1,message_buff);

    debug_message("Publishing : " + messJson, true);

    // Save the current time to compare "later"
    previousMillis = currentMillis;
  }
}

void makeLedBlink(int blinkTimes, int millisecond) {

  for (int x = 0; x < blinkTimes; x++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(millisecond);
    digitalWrite(LED_BUILTIN, LOW);
    delay(millisecond);
  } 
}
