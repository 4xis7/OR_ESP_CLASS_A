#include <Arduino.h>
//Client A of MB01 GPS
#include "WiFi.h"
#include <esp_now.h>
#include <TinyGPS++.h>
#include <ArduinoJson.h>

//#define RXD2 16 //iTSD 
//#define TXD2 17 //iTSD
//#define RXD1 18 //Readout
//#define TXD1 19 //Readout

#define RXD1 16 //Readout 
#define TXD1 17 //Readout
#define RXD2 26 //GPS
#define TXD2 22 //GPS
TinyGPSPlus gps;

uint8_t broadcastAddress_server[] = {0x58,0xbf,0x25,0xba,0x88,0xe8}; // Comp node

TaskHandle_t Task1;
TaskHandle_t Task2;

// 👉 เพิ่มตรงนี้
void Task1code(void * pvParameters);
void Task2code(void * pvParameters);
void gps_to_iot();

esp_now_peer_info_t peerInfo;

// ================== ✅ LED ==================
const int LED_wifi = 5;
const int LED_RS   = 19;
const int LED_TTL  = 18;
//LED4
const int LED_power = 12;

// ===========================================

// ================== ✅ แก้เฉพาะนี้ ==================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
 
  if (status == ESP_NOW_SEND_SUCCESS){
    Serial.println("OK");  
    digitalWrite(LED_wifi, HIGH);   // 🔥 ติดค้าง
  }
  else{
    Serial.println("FAIL");  
    digitalWrite(LED_wifi, LOW);    // 🔥 ดับ
  }
}
// ===================================================

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  
  String dataIn;
  
  for (int i=0;i<len;i++) {
    dataIn += (char)incomingData[i];
  }

  if(dataIn == "gps" || dataIn == "GPS"){
    Serial.println("GPS True");
    gps_to_iot();
  } else {
    Serial.println("Else cond.");
    gps_to_iot();
  }

  Serial.print(dataIn);
  Serial1.print(dataIn);
  Serial.flush();
  
}

void displayInfo()
{
  DynamicJsonDocument doc(1024);
  doc["sensor"] = "gps";

  Serial.print(F("Location: ")); 
  if (gps.location.isValid())
  {
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print(gps.location.lng(), 6);

    String pos = "lat:";
    pos.concat(String(gps.location.lat()));
    pos.concat("lng");
    pos.concat(String(gps.location.lng()));

    Serial.println(pos);
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.print(F("  Date/Time: "));
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.print(F(" "));
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("."));
    if (gps.time.centisecond() < 10) Serial.print(F("0"));
    Serial.print(gps.time.centisecond());
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.println();
}

// ================== GPS ==================
void gps_to_iot()
{
  DynamicJsonDocument doc(1024);
  doc["sensor"] = "gps";
  String msg;
  
  Serial.print(F("Location: ")); 
  if (gps.location.isValid())
  {
    String pos = "{\"lat\":";
    pos.concat(String(gps.location.lat(), 6));
    pos.concat(",\"lng\":");
    pos.concat(String(gps.location.lng(), 6));
    pos.concat("}");

    Serial.println(pos);
    msg = pos;
  }
  else
  {
    Serial.print(F("INVALID"));
    String pos = "{\"lat\":\"INVALID\",\"lng\":\"INVALID\"}";
    Serial.println(pos);
    msg = pos;
  }

  String data_send = "MB1L:" + msg + "\r\n";
  esp_now_send(broadcastAddress_server, (uint8_t*)data_send.c_str(), data_send.length());
  delay(100);
}

// ================== SETUP ==================
void setup(){
  pinMode(2 , OUTPUT);
  pinMode(4 , OUTPUT);
pinMode(LED_power, OUTPUT);
  digitalWrite(LED_power, HIGH);
  // ✅ เพิ่ม LED
  pinMode(LED_wifi, OUTPUT);
  pinMode(LED_RS, OUTPUT);
  pinMode(LED_TTL, OUTPUT);

  digitalWrite(LED_wifi, LOW);
  digitalWrite(LED_RS, LOW);
  digitalWrite(LED_TTL, LOW);

  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  memcpy(peerInfo.peer_addr, broadcastAddress_server, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  xTaskCreatePinnedToCore(Task1code, "Task1", 10000, NULL, 0, &Task1, 0);
  

  xTaskCreatePinnedToCore(Task2code, "Task2", 10000, NULL, 0, &Task2, 1);

}

// ================== TASK1 ==================
void Task1code( void * pvParameters ){

  Serial.print("Task1 running on core ");
  Serial.println(xPortGetCoreID());

  for(;;){

    if(Serial1.available() > 0){

      digitalWrite(LED_RS, HIGH);   // 🔥 ติด

      String msg = Serial1.readStringUntil('\r');

      digitalWrite(LED_RS, LOW);    // 🔥 ดับ

      String data_send = "MB1L:" + msg + "\r\n";
      esp_now_send( broadcastAddress_server, (uint8_t*)data_send.c_str(), data_send.length());
      delay(100);
    }

    if(Serial.available() > 0){
      String msgWire = Serial.readStringUntil('\r');
      String data_send = "MB1L:" + msgWire + "\r\n";
      esp_now_send( broadcastAddress_server, (uint8_t*)data_send.c_str(), data_send.length());
      delay(100);
    }
  } 
  delay(10);
}

// ================== TASK2 ==================
void Task2code( void * pvParameters ){

  Serial.print("Task2 running on core ");
  Serial.println(xPortGetCoreID());

  for(;;){

    

    while (Serial2.available() > 0){

      digitalWrite(LED_TTL, HIGH);   // 🔥 ติด

      if (gps.encode(Serial2.read()))
        displayInfo();

      digitalWrite(LED_TTL, LOW);    // 🔥 ดับ
    }

    if (millis() > 5000 && gps.charsProcessed() < 10){
      Serial.println(F("No GPS detected: check wiring."));
    }
  }

  delay(10);
}

// ================== LOOP ==================
void loop(){
  delay(100);
}