#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <TinyGPS++.h>
#include <SPIFFS.h>

// =====================================================
// PIN
// =====================================================

#define RXD1 16
#define TXD1 17

#define RXD2 26
#define TXD2 22

#define BUTTON_PIN 35

const int LED_wifi = 5;
const int LED_RS   = 19;
const int LED_TTL  = 18;
const int LED_POWER = 12;

// =====================================================
// AP CONFIG
// =====================================================

const char* AP_SSID     = "ESP32_CONFIG_A";
const char* AP_PASSWORD = "12345678";

// =====================================================
// GLOBAL
// =====================================================

TinyGPSPlus gps;
WebServer   server(80);
Preferences prefs;

TaskHandle_t Task1;
TaskHandle_t Task2;
TaskHandle_t Task3;

int buttonState = LOW;
int lastButtonState = LOW;

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
unsigned long lastGpsData = 0;
unsigned long rsLedTimer  = 0;
unsigned long gpsLedTimer = 0;

void Task1code(void * pvParameters);
void Task2code(void * pvParameters);
void Task3code(void * pvParameters);
void gps_to_iot();

uint8_t           peerMac[6];
bool              peerReady = false;
esp_now_peer_info_t peerInfo;

// =====================================================
// ตรวจสอบ MAC Format
// =====================================================

bool isValidMac(String mac)
{
    mac.toUpperCase();

    if (mac.length() != 17)
        return false;

    for (int i = 0; i < 17; i++)
    {
        if (i == 2 || i == 5 || i == 8 ||
            i == 11 || i == 14)
        {
            if (mac[i] != ':')
                return false;
        }
        else
        {
            if (!isxdigit(mac[i]))
                return false;
        }
    }

    return true;
}

// =====================================================
// MAC STRING -> BYTE
// =====================================================

bool macStringToBytes(String macStr, uint8_t *mac)
{
    if (macStr.length() != 17)
        return false;

    int values[6];

    if (sscanf(macStr.c_str(),
               "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1],
               &values[2], &values[3],
               &values[4], &values[5]) != 6)
    {
        return false;
    }

    for (int i = 0; i < 6; ++i)
        mac[i] = (uint8_t)values[i];

    return true;
}

// =====================================================
// ตรวจสอบว่ามี MAC หรือไม่
// =====================================================

bool hasPeerSaved()
{
    prefs.begin("config", true);

    String macStr = prefs.getString("peer", "");

    prefs.end();

    return (macStr.length() == 17);
}

// =====================================================
// LOAD PEER MAC จาก NVS
// =====================================================

void loadPeer()
{
    prefs.begin("config", true);
    String macStr = prefs.getString("peer", "");
    prefs.end();

    if (macStr == "")
    {
        Serial.println("NO PEER");
        return;
    }

    Serial.print("PEER = ");
    Serial.println(macStr);

    if (!macStringToBytes(macStr, peerMac))
    {
        Serial.println("INVALID MAC");
        return;
    }

    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK)
    {
        Serial.println("PEER ADDED");
        peerReady = true;
    }
    else
    {
        Serial.println("ADD PEER FAIL");
    }
}

// =====================================================
// WEB: สร้างหน้าจาก index.html ใน SPIFFS
// แทรก THIS MAC และ MASTER MAC ก่อนส่ง
// =====================================================

void handleRoot()
{
    if (!SPIFFS.exists("/index.html"))
    {
        server.send(404, "text/plain", "index.html not found");
        return;
    }

    File f = SPIFFS.open("/index.html", "r");
    String html = "";
    while (f.available())
        html += (char)f.read();
    f.close();

    prefs.begin("config", true);
    String savedPeer = prefs.getString("peer", "NOT SET");
    prefs.end();

    html.replace("%MY_MAC%",   WiFi.macAddress());
    html.replace("%PEER_MAC%", savedPeer);

    server.send(200, "text/html", html);
}

// =====================================================
// WEB HANDLER: บันทึกข้อมูล (แก้ไขใหม่เรียบร้อย)
// =====================================================
void handleSave()
{
    if (!server.hasArg("mac"))
    {
        server.send(400, "text/plain", "No MAC");
        return;
    }

    String mac = server.arg("mac");
    mac.trim();
    mac.toUpperCase();

    if (!isValidMac(mac))
    {
        String errorHtml = "<html><meta charset='UTF-8'><body style='font-family:sans-serif; text-align:center; padding-top:50px;'>";
        errorHtml += "<h2 style='color:red;'>ERROR: รูปแบบ MAC Address ไม่ถูกต้อง!</h2>";
        errorHtml += "<p>กรุณากรอกในรูปแบบ XX:XX:XX:XX:XX:XX</p>";
        errorHtml += "<br><a href='/' style='padding:10px 20px; background:#ccc; text-decoration:none; color:black; border-radius:5px;'>กลับไปแก้ไข</a>";
        errorHtml += "</body></html>";
        server.send(200, "text/html", errorHtml);
        return;
    }

    prefs.begin("config", false);
    prefs.putString("peer", mac);      
    prefs.end();

    Serial.println("MAC SAVED: " + mac);

    String successHtml = "<html><meta charset='UTF-8'><body style='font-family:sans-serif; text-align:center; padding-top:50px;'>";
    successHtml += "<div style='display:inline-block; border:2px solid #4CAF50; padding:30px; border-radius:10px; background-color:#f9f9f9;'>";
    successHtml += "<h2 style='color:#4CAF50;'>✓ บันทึกข้อมูลเรียบร้อยแล้ว!</h2>";
    successHtml += "<p style='font-size:18px;'>บันทึกค่าใหม่เป็น: <b>" + mac + "</b> สำเร็จ</p>";
    successHtml += "<p style='color:#666;'>กำลังเตรียมรีสตาร์ทบอร์ดเพื่อเข้าโหมดทำงานปกติ...</p>";
    successHtml += "</div>";
    successHtml += "</body></html>";

    server.send(200, "text/html", successHtml);

    delay(3000);   
    ESP.restart();
}


// =====================================================
// ESP-NOW SEND CALLBACK
// =====================================================

void OnDataSent(const uint8_t *mac_addr,
                esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        Serial.println("OK");
        digitalWrite(LED_wifi, HIGH);
    }
    else
    {
        Serial.println("FAIL");
        digitalWrite(LED_wifi, LOW);
    }
}

// =====================================================
// ESP-NOW RECEIVE CALLBACK
// =====================================================

void OnDataRecv(const uint8_t *mac,
                const uint8_t *incomingData,
                int len)
{
    String dataIn = "";

    for (int i = 0; i < len; i++)
        dataIn += (char)incomingData[i];

    dataIn.trim();  // ตัด \r \n และ space ออก

    Serial.print("RECV: ");
    Serial.println(dataIn);
 
    Serial.println("ESP-NOW CALLBACK");

    // ตรวจสอบคำสั่ง "gps" (case-insensitive)
    if (dataIn.equalsIgnoreCase("gps"))
    {
        // บันทึก MAC ของ Master ที่ส่งคำสั่งมา
        // เพื่อส่งกลับไปหาต้นทางโดยตรง
        memcpy(peerMac, mac, 6);

        // เพิ่ม peer ชั่วคราวถ้ายังไม่มี
        if (!esp_now_is_peer_exist(mac))
        {
            esp_now_peer_info_t tmpPeer;
            memcpy(tmpPeer.peer_addr, mac, 6);
            tmpPeer.channel = 0;
            tmpPeer.encrypt = false;
            esp_now_add_peer(&tmpPeer);
        }

        gps_to_iot();  // ส่งพิกัดกลับ
    }
    else
    {
        // ข้อมูลอื่น → relay ต่อไปยัง Serial1 เหมือนเดิม
        Serial.print(dataIn);
        Serial1.print(dataIn);
        Serial.flush();
    }
}

// =====================================================
// GPS -> ส่ง ESP-NOW
// =====================================================

void gps_to_iot()
{
    String msg;

    if (gps.location.isValid())
    {
        String pos = "{\"lat\":";
        pos.concat(String(gps.location.lat(), 6));
        pos.concat(",\"lng\":");
        pos.concat(String(gps.location.lng(), 6));
        pos.concat("}");
        msg = pos;
    }
    else
    {
        msg = "{\"lat\":\"INVALID\",\"lng\":\"INVALID\"}";
    }

    String data_send = "MB1L:" + msg + "\r\n";

    if (peerReady)
    {
        esp_now_send(peerMac,
                     (uint8_t*)data_send.c_str(),
                     data_send.length());
    }

    delay(100);
}

// =====================================================
// button_Web Config
// =====================================================

void startConfigMode()
{
    esp_now_deinit();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.println("CONFIG MODE ENABLED");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/restart", HTTP_POST, handleRestart);

    server.begin();

    while (true)
    {
        server.handleClient();
        delay(10);
    }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    pinMode(LED_wifi, OUTPUT);
    pinMode(LED_RS,   OUTPUT);
    pinMode(LED_TTL,  OUTPUT);
    pinMode(LED_POWER, OUTPUT);
 
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    digitalWrite(LED_wifi, LOW);
    digitalWrite(LED_RS,   LOW);
    digitalWrite(LED_TTL,  LOW);
    digitalWrite(LED_POWER, HIGH);

    Serial.begin(9600);
    Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1);
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

    if (!SPIFFS.begin(true)) 
    {
        Serial.println("SPIFFS ERROR");
    }

    if (!hasPeerSaved())
    {
        Serial.println("NO MASTER MAC");
        startConfigMode();
    }
    else 
    {
        WiFi.mode(WIFI_STA);

        Serial.print("STA MAC Address: ");
        Serial.println(WiFi.macAddress());

        if (esp_now_init() != ESP_OK)
        {
            Serial.println("ESP NOW ERROR");
            return;
        }

        esp_now_register_send_cb(OnDataSent);
        esp_now_register_recv_cb(OnDataRecv);

        loadPeer();

        xTaskCreatePinnedToCore(
            Task1code, "Task1",
            4096, NULL, 1, &Task1, 1
        );
        
        xTaskCreatePinnedToCore(
            Task2code, "Task2",
            4096, NULL, 1, &Task2, 1
        );

        xTaskCreatePinnedToCore(
            Task3code, "Task3",
            4096, NULL, 2, &Task3, 1
        );
    }
}

// =====================================================
// TASK1 - Serial Relay -> ESP-NOW
// =====================================================

void Task1code(void * pvParameters)
{
    for (;;)
    {
        if (Serial1.available() > 0)
        {
            rsLedTimer = millis();
            digitalWrite(LED_RS, HIGH);

            String msg = Serial1.readStringUntil('\r');
            String data_send = "MB1L:" + msg + "\r\n";

            Serial.println(data_send);

            if (peerReady)
                esp_now_send(peerMac,
                             (uint8_t*)data_send.c_str(),
                             data_send.length());

            delay(100);
        }

        if (Serial.available() > 0)
        {
            String msgWire = Serial.readStringUntil('\r');
            String data_send = "MB1L:" + msgWire + "\r\n";

            if (peerReady)
                esp_now_send(peerMac,
                             (uint8_t*)data_send.c_str(),
                             data_send.length());

            delay(100);
        }

        if (millis() - rsLedTimer > 100)
        {
            digitalWrite(LED_RS, LOW);
        }

        delay(10);
    }
}

// =====================================================
// TASK2 - GPS Decode
// =====================================================

void Task2code(void * pvParameters)
{
    unsigned long lastPrint = millis();

    for (;;)
    {
        while (Serial2.available() > 0)
        {
            gps.encode(Serial2.read());
            digitalWrite(LED_TTL, !digitalRead(LED_TTL));

            gpsLedTimer = millis();
            lastGpsData = millis();
        }

        if (millis() - lastPrint > 1000)
        {
            lastPrint = millis();

            if (millis() - lastGpsData > 1000)
            {
                Serial.println("No GPS detected");
            }
            else
            {
                Serial.print("Location: ");

                if (gps.location.isValid())
                {
                    Serial.print(gps.location.lat(), 6);
                    Serial.print(",");
                    Serial.print(gps.location.lng(), 6);
                }
                else
                {
                    Serial.print("INVALID");
                }

                Serial.print("  Date/Time: ");

                if (gps.date.isValid() && gps.time.isValid())
                {
                    Serial.print(gps.date.day());
                    Serial.print("/");
                    Serial.print(gps.date.month());
                    Serial.print("/");
                    Serial.print(gps.date.year());
                    Serial.print(" ");

                    int thaiHour = gps.time.hour() + 7;
                    if (thaiHour >= 24) thaiHour -= 24;

                    if (thaiHour < 10) Serial.print("0");
                    Serial.print(thaiHour);
                    Serial.print(":");

                    if (gps.time.minute() < 10) Serial.print("0");
                    Serial.print(gps.time.minute());
                    Serial.print(":");

                    if (gps.time.second() < 10) Serial.print("0");
                    Serial.print(gps.time.second());
                    Serial.print(".");

                    if (gps.time.centisecond() < 10) Serial.print("0");
                    Serial.print(gps.time.centisecond());
                }
                else
                {
                    Serial.print("0/0/2000 00:00:00.00");
                }

                Serial.println();
            }
        }

        delay(10);
        if (millis() - gpsLedTimer > 50)
        {
            digitalWrite(LED_TTL, LOW);
        }
    }
}

// =====================================================
// TASK3 - CONFIG BUTTON
// =====================================================

void Task3code(void * pvParameters)
{
    for (;;)
    {
        int reading = digitalRead(BUTTON_PIN);

        if (reading != lastButtonState)
        {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > debounceDelay)
        {
            if (reading != buttonState)
            {
                buttonState = reading;

                // ตรวจจับการกดปุ่ม (Active LOW -> HIGH ตามเงื่อนไขเดิมของคุณ)
                if (buttonState == HIGH)
                {
                    Serial.println("CONFIG BUTTON");
                    startConfigMode();
                }
            }
        }

        lastButtonState = reading;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    delay(100);
}
