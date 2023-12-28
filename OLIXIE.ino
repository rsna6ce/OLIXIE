#include <string.h>
#include <WiFi.h>
#include "esp_wps.h"
#include <Wire.h>
#include <time.h>
#include "fonts.h"

//#define USE_U8G2
#ifdef USE_U8G2
 #include <U8g2lib.h> //u8g2 by oliver ver.2.33.25
#else
 #include <Adafruit_GFX.h>
 #include <Adafruit_SSD1306.h>
#endif

#define WIRE_FREQ 400*1000 /*fast mode*/
#define OLED_COUNT 8
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
#define PIN_SDA 21
#define PIN_SCL 22

#ifdef USE_U8G2
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, PIN_SDA, PIN_SCL, U8X8_PIN_NONE);
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, WIRE_FREQ);
#endif

// TTP223 capacitive switch
#define PIN_SW 2
#define SW_ON  HIGH
#define SW_OFF LOW

// set ssid and password to use specific ssid (if not set WPS available)
char* ssid = "";
char* password = "";

const int ssid_pass_buff_len = 64;
char ssid_buff[ssid_pass_buff_len]={};
char pass_buff[ssid_pass_buff_len]={};

static int wifi_status = WL_DISCONNECTED;
static bool wps_success = false;
#define WIFI_TIMEOUT_SEC 10
#define WPS_TIMEOUT 120
#define JST 3600*9
void setup()
{
    Serial.begin(115200);

#ifdef USE_U8G2
    // convert font format Adafruit to u8g2
    for (int i=0; i<(sizeof(font_table)/sizeof(FONT)); i++) {
        unsigned char* p = font_table[i].bitmap;
        for (int j=0; j<epd_bitmap_width*epd_bitmap_height/8; j++) {
            unsigned char a = p[j];
            a=(a & 0x0F)<<4 |(a & 0xF0)>>4;
            a=(a & 0x33)<<2 |(a & 0xCC)>>2;
            a=(a & 0x55)<<1 |(a & 0xAA)>>1;
            p[j] = a;
        }
    }
#endif

    // i2c settings
    Serial.println("Wire.begin");
    Wire.begin();
    Wire.setClock(WIRE_FREQ);
    for(int i=0; i<OLED_COUNT; i++) {
        switch_tca9548a(i);
#ifdef USE_U8G2
        u8g2.setBusClock(WIRE_FREQ);
        u8g2.begin();
        u8g2.setBitmapMode(false /* solid */);
        u8g2.setDrawColor(1);
#else
        if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
            Serial.println(F("SSD1306 allocation failed"));
            esp_restart();
        }
#endif
    }
    char myname[] = {(char)0xff, 'O','L','I','X','I', 'E', (char)0xff};
    display_ascii(&myname[0], false);
    
    Serial.println("To specify the SSID, press the y key within 3 seconds.");
    delay(3*1000);
    if (Serial.available() > 0) {
        int inmyte = Serial.read();
        if (inmyte == 'y') {
            String frush_str = Serial.readString(); // flush buffer
            input_ssid_pass(ssid_buff, pass_buff);
            ssid = ssid_buff;
            password = pass_buff;
        }
    }

    // try Wifi connection
    Serial.println("WiFi.begin");
    display_ascii("WIFI....", false);
    if (strlen(ssid)>0 && strlen(password)>0) {
        // specific ssid
        WiFi.begin(ssid, password);
    } else {
        // ssid last connected
        WiFi.begin();
    }
    for (int i=0; (i<WIFI_TIMEOUT_SEC*2)&&(wifi_status!= WL_CONNECTED); i++) {
        Serial.println(".");
        wifi_status = WiFi.status();
        delay(500);
    }
    if (wifi_status == WL_CONNECTED) {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(WiFi.SSID());
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        display_ascii("SUCCESS.", false);
        delay(1*1000);
    } else {
        // try WPS connection
        Serial.println("Failed to connect");
        Serial.println("Starting WPS");
        display_ascii("WPS.....", false);
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
            display_ascii("FAILED..", false);
        } else {
            display_ascii("SUCCESS.", false);
        }
        delay(1*1000);
        display_ascii("RESTART.", false);
        delay(1*1000);
        esp_restart();
    }

    // NTP configuration
    Serial.println("configTime");
    display_ascii("NTP.....", false);
    delay(1*1000);
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(1*1000);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        // may be wifh weak reset
        Serial.println("reboot ...");
        display_ascii("FAILED..", false);
        delay(1*1000);
        display_ascii("RESTART.", false);
        delay(1*1000);
        esp_restart();
    } else {
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
        display_ascii("SUCCESS.", false);
        delay(1*1000);
    }

    // switch
    pinMode(PIN_SW, INPUT_PULLUP);
}

// Serial.readStringUntil do now work in ESP32...
String SerialReasStringUntilCRLF() {
    Serial.setTimeout(100);
    String ret = "";
    String temp_str = "";
    while (true) {
        if (Serial.available() > 0) {
            temp_str = Serial.readString();
            if (temp_str.endsWith("\n") || temp_str.endsWith("\r")) {
                temp_str.replace("\n", "");
                temp_str.replace("\r", "");
                ret += temp_str;
                break;
            } else {
                ret += temp_str;
            }
        }
    }
    return ret;
}

void input_ssid_pass(char* ssid, char* pass) {
    while(true) {
        Serial.println("input SSID and press Enter.");
        String temp_ssid = SerialReasStringUntilCRLF();

        Serial.println("input password and press Enter.");
        String temp_pass = SerialReasStringUntilCRLF();

        Serial.println("SSID: " + temp_ssid + "\r\n" +
                       "pass: " + temp_pass + "\r\n" +
                       "OK? input yes or no and Enter key.");
        String temp_ret = SerialReasStringUntilCRLF();
        if (temp_ret == "yes") {
            temp_ssid.toCharArray(ssid, ssid_pass_buff_len);
            temp_pass.toCharArray(pass, ssid_pass_buff_len);
            break;
        }
    }
    return;
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

const unsigned char* get_font_bitmap(char ascii_code, bool bold) {
    int len = sizeof(font_table) / sizeof(FONT);
    for (int i=0; i<len; i++) {
        if (font_table[i].ascii_code == ascii_code && font_table[i].bold == bold) {
            return font_table[i].bitmap;
        }
    }
    return NULL;
}

void display_ascii(int index_degit, char ascii_code, bool bold) {
    switch_tca9548a(index_degit);
    const unsigned char* bmp = get_font_bitmap(ascii_code, bold);
#ifdef USE_U8G2
    u8g2.clearBuffer();
    if (bmp) {
        u8g2.drawXBM( (u8g2.getDisplayWidth()-epd_bitmap_width), 0, epd_bitmap_width, epd_bitmap_height, bmp);
    }
    u8g2.sendBuffer();
#else
    display.clearDisplay();
    if (bmp) {
        display.drawBitmap((display.width()-epd_bitmap_width ), 0, bmp, epd_bitmap_width, epd_bitmap_height, 1);
    }
    display.display();
#endif
}

void display_ascii(char *ascii_codes, bool bold) {
    bool null_found = false;
    for (int i=0; i<OLED_COUNT; i++) {
        if (!null_found) {
            null_found |= (ascii_codes[i]==0);
            display_ascii(i, ascii_codes[i], bold);
        } else {
            display_ascii(i, ' ', bold);
        }
    }
}


static int prev_sec = -1;
static int prev_mday = -1;
static int colon_blink_counter = 0;
void display_clock() {
    time_t t;
    struct tm *tm;
    t = time(NULL);
    tm = localtime(&t);

    if (prev_mday != tm->tm_mday) {
        prev_mday = tm->tm_mday;
        configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
        struct tm timeinfo;
        getLocalTime(&timeinfo);
    }

    if (prev_sec != tm->tm_sec) {
        prev_sec = tm->tm_sec;
        colon_blink_counter = 0;
        display_ascii(0, '0'+tm->tm_hour/10, false);
        display_ascii(1, '0'+tm->tm_hour%10, false);
        display_ascii(2, ':', false);
        display_ascii(3, '0'+tm->tm_min/10, false);
        display_ascii(4, '0'+tm->tm_min%10, false);
        display_ascii(5, ':', false);
        display_ascii(6, '0'+tm->tm_sec/10, false);
        display_ascii(7, '0'+tm->tm_sec%10, false);
    } else {
        // ':' blink
        if (colon_blink_counter == 50) {
            display_ascii(2, ' ', false);
            display_ascii(5, ' ', false);
        }
    }
    colon_blink_counter++;
}


struct SCENARIO {
  unsigned int roulette_ms;
  char final_ascii;
};
SCENARIO scenario[] = {
    {40*120, '1'},
    {0 *120, '.'},
    {25*120, '0'},
    {10*120, '4'},
    {35*120, '8'},
    {15*120, '5'},
    {20*120, '9'},
    {30*120, '6'},
};
const unsigned int scenario_blank_ms = 1000;
const unsigned int scenario_roulette_ms = 7000;
const unsigned int scenario_bold_ms = 800;
unsigned int scenario_start_ms = 0;
unsigned int prev_switch = SW_OFF;
unsigned int divergence_count = 0;
bool divergence_meter = false;
void display_divergence_meter() {
    unsigned int past_ms = millis() - scenario_start_ms;
    if (past_ms < scenario_blank_ms) {
        display_ascii("        ", false);
    } else if (past_ms < scenario_blank_ms + scenario_roulette_ms) {
        unsigned int max_roulette_ms = 0;
        for (int i=0; i<OLED_COUNT; i++) {
            if (max_roulette_ms < scenario[i].roulette_ms) {
                max_roulette_ms = scenario[i].roulette_ms;
            }
        }
        bool bold = (scenario_blank_ms + max_roulette_ms < past_ms && past_ms < scenario_blank_ms + max_roulette_ms + scenario_bold_ms);
        for (int i=0; i<OLED_COUNT; i++) {
            if (past_ms-scenario_blank_ms < scenario[i].roulette_ms) {
                display_ascii(i, '0' + (((divergence_count+i)*7)%10), false);
            } else {
                display_ascii(i, scenario[i].final_ascii, bold);
            }
        }
    } else {
        divergence_meter = false;
    }
    divergence_count++;
}

uint64_t previous_millis = 0;
void loop()
{
    uint64_t current_millis = millis();
    if ((WiFi.status() != WL_CONNECTED) && (current_millis - previous_millis >= WIFI_TIMEOUT_SEC*1000*2)) {
        display_ascii("RECONN..", false);
        WiFi.disconnect();
        WiFi.reconnect();
        previous_millis = current_millis;
    }
    unsigned int curr_switch = digitalRead(PIN_SW);
    if (prev_switch != curr_switch) {
        prev_switch = curr_switch;
        if (curr_switch == SW_ON) {
            divergence_meter = true;
            scenario_start_ms = millis();
        }
    }
    if (divergence_meter) {
        display_divergence_meter();
    } else {
        display_clock();
        delay(10);
    }
}
