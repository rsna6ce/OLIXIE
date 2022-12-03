#include <string.h>
#include <WiFi.h>
#include "esp_wps.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include "fonts.h"

#define OLED_COUNT 8
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// set ssid and password to use specific ssid (if not set WPS available)
const char* ssid = "";
const char* password = "";

static int wifi_status = WL_DISCONNECTED;
static bool wps_success = false;
#define WIFI_TIMEOUT 10
#define WPS_TIMEOUT 120
#define JST 3600*9
void setup()
{
    Serial.begin(115200);

    // i2c settings
    Serial.println("Wire.begin");
    Wire.begin();
    for(int i=0; i<OLED_COUNT; i++) {
        switch_tca9548a(i);
        if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
            Serial.println(F("SSD1306 allocation failed"));
            esp_restart();
        }
    }
    char myname[] = {(char)0xff, 'O','L','I','X','I', 'E', (char)0xff};
    display_ascii(&myname[0]);
    delay(3*1000);

    // try Wifi connection
    Serial.println("WiFi.begin");
    display_ascii("WIFI....");
    if (strlen(ssid)>0 && strlen(password)>0) {
        // specific ssid
        WiFi.begin(ssid, password);
    } else {
        // ssid last connected
        WiFi.begin();
    }
    for (int i=0; (i<WIFI_TIMEOUT*2)&&(wifi_status!= WL_CONNECTED); i++) {
        Serial.println(".");
        wifi_status = WiFi.status();
        delay(500);
    }
    if (wifi_status == WL_CONNECTED) {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        display_ascii("SUCCESS.");
        delay(1*1000);
    } else {
        // try WPS connection
        Serial.println("Failed to connect");
        Serial.println("Starting WPS");
        display_ascii("WPS.....");
        WiFi.onEvent(WiFiEvent);
        WiFi.mode(WIFI_MODE_STA);
        wpsInitConfig();
        wpsStart();
        for (int i=0; (i<WPS_TIMEOUT*2)&&(!wps_success); i++) {
            Serial.println(".");
            // wps_success is updated in callback
            delay(500);
        }
        if (!wps_success) {
            display_ascii("FAILED..");
        } else {
            display_ascii("SUCCESS.");
        }
        delay(1*1000);
        display_ascii("RESTART.");
        delay(1*1000);
        esp_restart();
    }

    // NTP configuration
    Serial.println("configTime");
    display_ascii("NTP.....");
    delay(1*1000);
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(1*1000);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        // may be wifh weak reset
        Serial.println("reboot ...");
        display_ascii("FAILED..");
        delay(1*1000);
        display_ascii("RESTART.");
        delay(1*1000);
        esp_restart();
    } else {
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
        display_ascii("SUCCESS.");
        delay(1*1000);
    }
}

/******** from WPS example ********/
#define ESP_WPS_MODE      WPS_TYPE_PBC
#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"

static esp_wps_config_t config;
void wpsInitConfig(){
  config.wps_type = ESP_WPS_MODE;
  strcpy(config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(config.factory_info.device_name, ESP_DEVICE_NAME);
}

void wpsStart(){
    if(esp_wifi_wps_enable(&config)){
        Serial.println("WPS Enable Failed");
    } else if(esp_wifi_wps_start(0)){
        Serial.println("WPS Start Failed");
    }
}

void wpsStop(){
    if(esp_wifi_wps_disable()){
        Serial.println("WPS Disable Failed");
    }
}

String wpspin2string(uint8_t a[]){
  char wps_pin[9];
  for(int i=0;i<8;i++){
    wps_pin[i] = a[i];
  }
  wps_pin[8] = '\0';
  return (String)wps_pin;
}

void WiFiEvent(WiFiEvent_t event, arduino_event_info_t info){
  switch(event){
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("Station Mode Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("Connected to :" + String(WiFi.SSID()));
      Serial.print("Got IP: ");
      Serial.println(WiFi.localIP());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Disconnected from station, attempting reconnection");
      WiFi.reconnect();
      break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      Serial.println("WPS Successfull, stopping WPS and connecting to: " + String(WiFi.SSID()));
      wpsStop();
      delay(10);
      WiFi.begin();
      wps_success = true;
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      Serial.println("WPS Failed, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      Serial.println("WPS Timedout, retrying");
      wpsStop();
      wpsStart();
      break;
    case ARDUINO_EVENT_WPS_ER_PIN:
      Serial.println("WPS_PIN = " + wpspin2string(info.wps_er_pin.pin_code));
      break;
    default:
      break;
  }
}
/******** from WPS example ********/

// switch i2c multiplexer
void switch_tca9548a(uint8_t bus_no){
  Wire.beginTransmission(0x70);
  Wire.write(1 << bus_no);
  Wire.endTransmission();
}

const unsigned char* get_font_bitmap(char ascii_code) {
    int len = sizeof(font_table) / sizeof(FONT);
    for (int i=0; i<len; i++) {
        if (font_table[i].ascii_code == ascii_code) {
            return font_table[i].bitmap;
        }
    }
    return NULL;
}

void display_ascii(int index_degit, char ascii_code) {
    switch_tca9548a(index_degit);
    const unsigned char* bmp = get_font_bitmap(ascii_code);
    display.clearDisplay();
    if (bmp) {
        display.drawBitmap((display.width()-epd_bitmap_width ), 0, bmp, epd_bitmap_width, epd_bitmap_height, 1);
    }
    display.display();
}

void display_ascii(char *ascii_codes) {
    bool null_found = false;
    for (int i=0; i<OLED_COUNT; i++) {
        if (!null_found) {
            null_found |= (ascii_codes[i]==0);
            display_ascii(i, ascii_codes[i]);
        } else {
            display_ascii(i, ' ');
        }
    }
}

static int prev_sec = -1;
static int colon_blink_counter = 0;
void loop()
{
    if (wifi_status != WL_CONNECTED) {
        delay(100);
        return;
    }
    time_t t;
    struct tm *tm;
    t = time(NULL);
    tm = localtime(&t);

    if (prev_sec != tm->tm_sec) {
        prev_sec = tm->tm_sec;
        colon_blink_counter = 0;
        display_ascii(0, '0'+tm->tm_hour/10);
        display_ascii(1, '0'+tm->tm_hour%10);
        display_ascii(2, ':');
        display_ascii(3, '0'+tm->tm_min/10);
        display_ascii(4, '0'+tm->tm_min%10);
        display_ascii(5, ':');
        display_ascii(6, '0'+tm->tm_sec/10);
        display_ascii(7, '0'+tm->tm_sec%10);
    } else {
        // ':' blink
        if (colon_blink_counter == 50) {
            display_ascii(2, ' ');
            display_ascii(5, ' ');
        }
    }

    colon_blink_counter++;
    delay(10);
}
