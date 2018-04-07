#include <Arduino.h>
extern "C" {
  #include "user_interface.h"
}


// ---------------------------------------- ArduinoOTA_Setup() ----------------------------------------
bool ArduinoOTA_Active = false;


// ---------------------------------------- Relay ----------------------------------------
const int Relay_Number_Of = 1;
const int Relay_Pin[Relay_Number_Of] {D1};

bool Relay_On_State = HIGH;


// ------------------------------------------------------------ WiFi ------------------------------------------------------------
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Ticker.h>

WiFiClient WiFi_Client;

const char* WiFi_SSID = "NoInternetHere";
const char* WiFi_Password = "NoPassword1!";
String WiFi_Hostname = "WBath";

WiFiEventHandler gotIpEventHandler;
WiFiEventHandler disconnectedEventHandler;

Ticker wifiReconnectTimer;
#define WiFi_Reconnect_Delay 3 // in secounds


// ------------------------------------------------------------ MQTT ------------------------------------------------------------
#include <AsyncMqttClient.h>

AsyncMqttClient MQTT_Client;
Ticker mqttReconnectTimer;

IPAddress MQTT_Broker(192, 168, 0, 2);
unsigned long MQTT_Port = 1883;

String MQTT_Device_ID = WiFi_Hostname;
const char* MQTT_Username = "DasBoot";
const char* MQTT_Password = "NoSinking";

Ticker MQTT_KeepAlive_Ticker;
unsigned long MQTT_KeepAlive_Delay = 60000;

const byte MQTT_Subscribe_Topic_Number_Of = 4;
String MQTT_Subscribe_Topic[MQTT_Subscribe_Topic_Number_Of] = {"/Boat/Settings/" + WiFi_Hostname + "/#", "/Boat/System/" + WiFi_Hostname, "/Boat/All", "/Boat/Relay/" + WiFi_Hostname};

#define BoatAll 2
#define BoatRelayHostname 3

#define MQTT_Reconnect_Delay 2 // in secounds

#define MQTT_Boot_Wait_For_Connection 15000


// ------------------------------------------------------------ The_Bat() ------------------------------------------------------------
Ticker The_Bat_Ticker;
unsigned long The_Bat_Delay = 750;

int The_Bat_All_Clear = -1;

byte The_Bat_Trigger = 75;


// ------------------------------------------------------------ Echo() ------------------------------------------------------------
#define Echo_Pin_Trigger D5
#define Echo_Pin_Echo D6

String The_Bat_Target_ON = String(MQTT_Subscribe_Topic[1]) + "The_Bat_Target_ON setting";
String The_Bat_Target_OFF = String(MQTT_Subscribe_Topic[1]) + "The_Bat_Target_OFF setting";



// ############################################################ Echo() ############################################################
int Echo() {

  // Clears the Echo_Pin_Trigger
  digitalWrite(Echo_Pin_Trigger, LOW);
  delayMicroseconds(2);

  // Sets the Echo_Pin_Trigger on HIGH state for 10 micro seconds
  digitalWrite(Echo_Pin_Trigger, HIGH);
  delayMicroseconds(10);
  digitalWrite(Echo_Pin_Trigger, LOW);

  // Reads the Echo_Pin_Echo, returns the sound wave travel time in microseconds
  long Duration = pulseIn(Echo_Pin_Echo, HIGH);

  // Calculating the distance
  int Distance = Duration *0.034 / 2;

  return Distance;

} // Echo



// ############################################################ The_Bat() ############################################################
void The_Bat() {

  int Distance = Echo();

  // Reset All Clear (Boot)
  if (The_Bat_All_Clear == -1) {
    The_Bat_All_Clear = Distance;
  }

  else if (Distance         Distance != The_Bat_Last_Mesure) {

    // Distance is smaller than = ON
    if (Distance < The_Bat_Last_Mesure) {
      if (The_Bat_Last_Mesure - Distance > The_Bat_Trigger) {

        String Topic = The_Bat_Target_ON.substring(0, The_Bat_Target_ON.indexOf("&"));
        String Payload = The_Bat_Target_ON.substring(The_Bat_Target_ON.indexOf("&") + 1, The_Bat_Target_ON.length());

        MQTT_Client.publish(Topic.c_str(), 0, false, Payload.c_str());

        Serial.println("Lights ON");
      }
    }

    // Distance is larger than = OFF
    if (Distance > The_Bat_Last_Mesure) {
      if (Distance - The_Bat_Last_Mesure > The_Bat_Trigger) {
        String Topic = The_Bat_Target_OFF.substring(0, The_Bat_Target_OFF.indexOf("&"));
        String Payload = The_Bat_Target_OFF.substring(The_Bat_Target_OFF.indexOf("&") + 1, The_Bat_Target_OFF.length());

        MQTT_Client.publish(Topic.c_str(), 0, false, Payload.c_str());

        Serial.println("Lights OFF");
      }
    }

  }

  The_Bat_Last_Mesure = Distance;
} // The_Bat()



// ############################################################ Relay() ############################################################
void Relay(String Topic, String Payload) {

  if (Topic == MQTT_Subscribe_Topic[BoatAll]) {
    if (Payload.indexOf("Relay-OFF") != -1) {
      Serial.println("Relay - All OFF");
      for (int i = 0; i < Relay_Number_Of; i++) {
        if (digitalRead(Relay_Pin[i]) != !Relay_On_State) {
          digitalWrite(Relay_Pin[i], !Relay_On_State);
          Serial.println("Relay " + String(i + 1) + " changed state to: OFF");
          MQTT_Client.publish(MQTT_Subscribe_Topic[3].c_str(), 0, false, String("S-" + String(i + 1) + "-OFF").c_str());
        }
      }
    }
  }


  else if (Topic == MQTT_Subscribe_Topic[BoatRelayHostname]) {
    byte Selected_Relay = Payload.substring(0, Payload.indexOf("-")).toInt();
    // Ignore all requests thats larger then Relay_Number_Of
    if (Selected_Relay > Relay_Number_Of);

    // State request
    else if (Payload.indexOf("-?") != -1) {
      String State_String = "S-" + String(Selected_Relay) + "-";
      if (digitalRead(Selected_Relay) == Relay_On_State) State_String += "ON";
      else State_String += "OFF";
      MQTT_Client.publish(MQTT_Subscribe_Topic[3].c_str(), 0, false, State_String.c_str());
    }

    else if (Payload.indexOf("S-") == -1) {

      String State = Payload.substring(Payload.indexOf("-") + 1, Payload.length());

      bool State_Digital;
      if (State == "ON") State_Digital = Relay_On_State;
      else if (State == "OFF") State_Digital = !Relay_On_State;
      else if (State == "FLIP") State_Digital = !digitalRead(Relay_Pin[Selected_Relay - 1]);

      if (Selected_Relay <= Relay_Number_Of && digitalRead(Relay_Pin[Selected_Relay - 1]) != State_Digital) {
        digitalWrite(Relay_Pin[Selected_Relay - 1], State_Digital);
        Serial.print("Relay " + String(Selected_Relay) + " changed state to: ");
        if (State_Digital == Relay_On_State) {
          MQTT_Client.publish(MQTT_Subscribe_Topic[3].c_str(), 0, false, String("S-" + String(Selected_Relay) + "-ON").c_str());
          Serial.println("ON");
        }
        else {
          Serial.println("OFF");
          MQTT_Client.publish(MQTT_Subscribe_Topic[3].c_str(), 0, false, String("S-" + String(Selected_Relay) + "-OFF").c_str());
        }
      }
    }
  }
} // Relay()


// ############################################################ KillKillKill() ############################################################
void KillKillKill(String Topic, String Payload) {

  if (Topic == "/Boat/All" && Payload == "KillKillKill") {

    long Reboot_Delay = random(25000);

    MQTT_Client.publish(String("/Boat/System/" + WiFi_Hostname).c_str(), 0, false, String("Someone went on a killing spree, rebooting in " + String(Reboot_Delay) + " ...").c_str());
    Serial.println("Someone went on a killing spree, rebooting in" + String(Reboot_Delay));

    unsigned long Suicide_At = millis() + Reboot_Delay;

    while (millis() < Suicide_At) {
      // Wait
      delay(1);
    }

    ESP.restart();
  }

  else if (Topic == MQTT_Subscribe_Topic[1] && Payload == "KillKillKill") {

    MQTT_Client.publish(String("/Boat/System/" + WiFi_Hostname).c_str(), 0, false, "Kill command issued, rebooting in 10");
    Serial.println("Kill command issued, rebooting in 10");

    unsigned long Suicide_At = millis() + 10000;

    while (millis() < Suicide_At) {
      // Wait
    }

    ESP.restart();
  }

} // KillKillKill()


// ############################################################ UpTime_String() ############################################################
String Uptime_String() {

  unsigned long Uptime_Now = millis();

  unsigned long Uptime_Days = Uptime_Now / 86400000;
  if (Uptime_Days != 0) Uptime_Now -= Uptime_Days * 86400000;

  unsigned long Uptime_Hours = Uptime_Now / 3600000;
  if (Uptime_Hours != 0) Uptime_Now -= Uptime_Hours * 3600000;

  unsigned long Uptime_Minutes = Uptime_Now / 60000;
  if (Uptime_Minutes != 0) Uptime_Now -= Uptime_Minutes * 60000;

  unsigned long Uptime_Secunds = Uptime_Now / 1000;
  if (Uptime_Secunds != 0) Uptime_Now -= Uptime_Secunds * 1000;

  String Uptime_String = "Up for ";

  if (Uptime_Days != 0) {
    if (Uptime_Days == 1) Uptime_String += String(Uptime_Days) + " day ";
    else Uptime_String += String(Uptime_Days) + " days ";
  }

  if (Uptime_Hours != 0) {
    if (Uptime_Hours == 1) Uptime_String += String(Uptime_Hours) + " hour ";
    else Uptime_String += String(Uptime_Hours) + " hours ";
  }

  if (Uptime_Minutes != 0) Uptime_String += String(Uptime_Minutes) + " min ";
  if (Uptime_Secunds != 0) Uptime_String += String(Uptime_Secunds) + " sec ";
  if (Uptime_Now != 0) Uptime_String += String(Uptime_Now) + " ms ";

  return Uptime_String;

} // Uptime_String()


// ############################################################ connectToWifi() ############################################################
void connectToWifi() {
  Serial.println("Connecting to Wi-Fi ...");
  WiFi.begin(WiFi_SSID, WiFi_Password);
}


// ############################################################ onMqttConnect() ############################################################
void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT");

  if (MQTT_Subscribe_Topic_Number_Of > 0) {

    for (byte i = 0; i < MQTT_Subscribe_Topic_Number_Of; i++) {
      if (MQTT_Client.subscribe(MQTT_Subscribe_Topic[i].c_str(), 0)) {
        Serial.println("Subscribing to Topic: " + MQTT_Subscribe_Topic[i] + "  ... OK");
      }

      else Serial.println("Subscribing to Topic: " + MQTT_Subscribe_Topic[i] + "  ... FAILED");
    }
  }
} // onMqttConnect()


// ############################################################ onMqttSubscribe() ############################################################
void onMqttSubscribe(uint16_t packetId, uint8_t qos) {}


// ############################################################ connectToMqtt() ############################################################
void connectToMqtt() {
  Serial.println("Connecting to MQTT ...");
  MQTT_Client.connect();
}


// ############################################################ onMqttDisconnect() ############################################################
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(MQTT_Reconnect_Delay, connectToMqtt);
  }
}


// ############################################################ onMqttUnsubscribe() ############################################################
void onMqttUnsubscribe(uint16_t packetId) {}


// ############################################################ MQTT_KeepAlive() ############################################################
void MQTT_KeepAlive() {

  String Send_String = Uptime_String() + " Free Memory: " + String(system_get_free_heap_size());

  MQTT_Client.publish(String("/Boat/KeepAlive/" + WiFi_Hostname).c_str(), 0, false, Send_String.c_str());

} // MQTT_KeepAlive()


// ############################################################ MQTT_Settings() ############################################################
void MQTT_Settings(String Topic, String Payload) {

  if (Topic.indexOf("/Boat/Settings/" + WiFi_Hostname) == -1) return;

  // ############### MQTTKeepAlive ###############
  if (Topic.indexOf("MQTTKeepAlive") != -1) {

    if (Payload.toInt() != MQTT_KeepAlive_Delay) {

      MQTT_KeepAlive_Ticker.detach();

      MQTT_KeepAlive_Delay = Payload.toInt();

      MQTT_KeepAlive_Ticker.attach_ms(MQTT_KeepAlive_Delay, MQTT_KeepAlive);

      Serial.println("KeepAlive change to: " + String(MQTT_KeepAlive_Delay));
    }
  } // MQTTKeepAlive


  // ------------------------------ TheBatTargetON ------------------------------
    else if (Topic.indexOf("TheBatTargetON") != -1) {

      The_Bat_Target_ON = Payload;

      Serial.println("The_Bat_Target_ON change to: " + The_Bat_Target_ON);
  } // TheBatTargetON

  // ------------------------------ TheBatTargetOFF ------------------------------
    else if (Topic.indexOf("TheBatTargetOFF") != -1) {

      The_Bat_Target_OFF = Payload;

      Serial.println("The_Bat_Target_OFF change to: " + The_Bat_Target_OFF);
  } // TheBatTargetOFF


} // MQTT_Settings


// ############################################################ onMqttMessage() ############################################################
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {

  if (ArduinoOTA_Active == true) return;

  MQTT_Settings(topic, payload);

  Relay(topic, payload);

  KillKillKill(topic, payload);

} // Settings


// ############################################################ IPtoString() ############################################################
String IPtoString(IPAddress IP_Address) {

  String Temp_String = String(IP_Address[0]) + "." + String(IP_Address[1]) + "." + String(IP_Address[2]) + "." + String(IP_Address[3]);

  return Temp_String;

} // IPtoString


// ############################################################ ArduinoOTA_Setup() ############################################################
void ArduinoOTA_Setup() {

  ArduinoOTA.setHostname(WiFi_Hostname.c_str());
  ArduinoOTA.setPassword("StillNotSinking");

  ArduinoOTA.onStart([]() {

    MQTT_Client.publish(String("/Boat/System/" + WiFi_Hostname).c_str(), 0, false, "ArduinoOTA ... Started");
    ArduinoOTA_Active = true;
    MQTT_KeepAlive_Ticker.detach();
    The_Bat_Ticker.detach();
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    MQTT_Client.publish(String("/Boat/System/" + WiFi_Hostname).c_str(), 0, false, "ArduinoOTA ... End");
    ArduinoOTA_Active = false;
    Serial.println("End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    ArduinoOTA_Active = false;
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

} // ArduinoOTA_Setup()


// ############################################################ setup() ############################################################
void setup() {

  // ------------------------------ Random Seed ------------------------------
  randomSeed(analogRead(0));


  // ------------------------------ Serial ------------------------------
  Serial.setTimeout(50);
  Serial.begin(115200);

  // ------------------------------ Pins ------------------------------
  Serial.println("Configuring pins");

  // Relay
  for (byte i = 0; i < Relay_Number_Of; i++) {
    pinMode(Relay_Pin[i], OUTPUT);
    digitalWrite(Relay_Pin[i], !Relay_On_State);
  }

  // Echo()
  pinMode(Echo_Pin_Trigger, OUTPUT); // Sets the Echo_Pin_Trigger as an Output
  pinMode(Echo_Pin_Echo, INPUT); // Sets the Echo_Pin_Echo as an Input

  // ------------------------------ MQTT ------------------------------
  MQTT_Client.onConnect(onMqttConnect);
  MQTT_Client.onDisconnect(onMqttDisconnect);
  MQTT_Client.onUnsubscribe(onMqttUnsubscribe);
  MQTT_Client.onMessage(onMqttMessage);
  MQTT_Client.onSubscribe(onMqttSubscribe);

  MQTT_Client.setServer(MQTT_Broker, MQTT_Port);
  MQTT_Client.setCredentials(MQTT_Username, MQTT_Password);


  // ------------------------------ WiFi ------------------------------
  Serial.println("WiFi SSID: " + String(WiFi_SSID));

  WiFi.mode(WIFI_STA);
  WiFi.hostname(WiFi_Hostname);

  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event) {
    Serial.println("Connected to Wi-Fi - IP: " + IPtoString(WiFi.localIP()));
    ArduinoOTA_Setup();
    connectToMqtt();
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    Serial.println("Disconnected from Wi-Fi");
    mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
    wifiReconnectTimer.once(WiFi_Reconnect_Delay, connectToWifi);
  });

  connectToWifi();


  // ------------------------------ MQTT KeepAlive ------------------------------
  MQTT_KeepAlive_Ticker.attach_ms(MQTT_KeepAlive_Delay, MQTT_KeepAlive);


  // ------------------------------ The Bat() ------------------------------
  The_Bat_Ticker.attach_ms(The_Bat_Delay, The_Bat);


  // ------------------------------ Wait for MQTT ------------------------------
  unsigned long MQTT_Boot_Wait_Timeout_At = millis() + MQTT_Boot_Wait_For_Connection;

  while (MQTT_Client.connected() == false) {

    if (MQTT_Boot_Wait_Timeout_At < millis()) break;

    delay(250);
  }

  // ------------------------------ Boot End ------------------------------
  MQTT_Client.publish(String("/Boat/System/" + WiFi_Hostname).c_str(), 0, false, String("Booting. Free Memory: " + String(system_get_free_heap_size())).c_str());
  Serial.println("Boot done");

} // setup()


// ############################################################ loop() ############################################################
void loop() {

  while (ArduinoOTA_Active == true) {
    ArduinoOTA.handle();
  }
  ArduinoOTA.handle();

} // loop()
