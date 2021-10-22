#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// ++++++++++++  add your code here ++++++++++++++++++
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "credentials.h"
//#include "DHT.h"                     // include DHT lbrary
#include <time.h>
#include <Wire.h>
extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions 
}
#include <Adafruit_ADS1X15.h>

// var

// RTC
#define RTCaddr_toggleFlag 64 //one int is 1-bucket addr. (4 - Bytes)
#define RTCaddr_unix_time 65
#define RTCaddr_index 66
#define RTCaddr_num_data_saved 67
#define RTCaddr_sensor_status 68

#define RTCMEMORYSTART 69
#define RTCMEMORYLEN 127
#define max_save_data 14 //(127-69)/4 //

extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}

typedef struct {
  int battery;
  int Solar;
  int unix_time;
} rtcStore;

rtcStore rtcMem;

//int i;
int buckets;
int toggleFlag;   // if false than it is the first run of the program
int msg_index = 0;    // mqtt msg counter
int sensor_status = 0;
// 0 - OK
// 1 - have data in the RTC
// 2 - err read DTH sensor
int num_data_saved = 0;    // data saved n RTC
int batt = 0;
int now_int = 0;

bool WiFi_connected = 0;
bool mqtt_connected = 0;
bool ntp_connected = 0;
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
const int analognPin  = A0;    // Analog nput pn

WiFiClient espClient;
PubSubClient client(espClient);
#define MAX_MSG_SIZE 254
char msg[MAX_MSG_SIZE + 1];

Adafruit_ADS1115 ads;

const char* NTP_SERVER = "il.pool.ntp.org";
const char* TZ_INFO    = "IST-2IDT,M3.4.4/26,M10.5.0";  // enter your time zone (https://remotemonitoringsystems.ca/time-zone-abbreviations.php)
tm timeinfo;
time_t now;
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define BAUD 115200

#define SENSOR_READ_INTERVAL 1 //min
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


// +++++++++++++++++++++++++++++++++++++++++++++++++++

//#ifndef STASSID
//#define STASSID "sayag2"
//#define STAPSK  "8774483abc"
//#endif
//
//const char* ssid = STASSID;
//const char* password = STAPSK;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// setup
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void setup() {
  Serial.begin(BAUD, SERIAL_8N1, SERIAL_FULL);
  Serial.println(" ");
  Serial.println("Booting");

  setup_wifi();

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
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
  Serial.println("Ready");
  for (byte i = 0; i < 2; i++) delay(500); // delay(1000) may cause hang

  // ++++++++++++  add your code here ++++++++++++++++++
  WiFi_connected = 0;
  mqtt_connected = 0;
  ntp_connected = 0;

  ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV

  if (!ads.begin(0x48)) {
    Serial.println("Failed to initialize ADS.");
    while (1);
  }

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  // Initialize a NTPclient to get time
  Serial.println("Initialize a NTP");
  configTime(0, 0, NTP_SERVER);
  // See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv for Timezone codes for your region
  setenv("TZ", TZ_INFO, 1);
  delay(100);
  if (getNTPtime(10)) {  // wait up to 10sec to sync
    ntp_connected = 1;
  } else {
    Serial.println("Time not set");
    ntp_connected = 0;
  }
  Serial.println("setup End");
  // ++++++++++++++++++++++++++++++++++++++++++++++++++++++
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Loop
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void loop() {
  ArduinoOTA.handle();
  // ++++++  add your code here +++++++++++

  //~~~~~~~~~~~ RTC   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  buckets = (sizeof(rtcMem) / 4);
  if (buckets == 0) buckets = 1;
  Serial.print("Buckets ");
  Serial.println(buckets);
  //~~~~~~~~~~~ ck if it the fierst time   ~~~~~~~~~~~
  system_rtc_mem_read(RTCaddr_toggleFlag, &toggleFlag, 4);   // the toggleFlag is (int) therfore it size is 4 bytes
  Serial.print("toggle Flag ");
  Serial.println(toggleFlag);
  if (toggleFlag != 1) { //if false than it is the fierst run
    Serial.print("it is the firest run ");
    for (byte i = 0; i < 10; i++) {
      Serial.print(".");
      delay(500);
      ArduinoOTA.handle();
    }

    if (ntp_connected == 1) { // all good
      toggleFlag = true;
      system_rtc_mem_write(RTCaddr_toggleFlag, &toggleFlag, 4);
      msg_index = 0;
      system_rtc_mem_write(RTCaddr_index, &msg_index, 4);
      sensor_status = 0; //status s OK
      system_rtc_mem_write(RTCaddr_sensor_status, &sensor_status, 4);
      num_data_saved = 0;
      system_rtc_mem_write(RTCaddr_num_data_saved, &num_data_saved, 4);
      now_int = (int)now;
      system_rtc_mem_write(RTCaddr_unix_time , &now_int, 4);
    }
    else { // firest time and no ntp
      Serial.println("the sensor started and no NTP, going to sleep to 5 minute");
      ESP.deepSleep(SENSOR_READ_INTERVAL * 60000000, WAKE_RFCAL);
    }
  }
  else { // not the first time
    system_rtc_mem_read(RTCaddr_index, &msg_index, 4);
    system_rtc_mem_read(RTCaddr_sensor_status, &sensor_status, 4);
    system_rtc_mem_read(RTCaddr_num_data_saved, &num_data_saved, 4);
    if (ntp_connected == 0) { // read the last know time from RTC
      system_rtc_mem_read(RTCaddr_unix_time , &now_int, 4);
      now = (time_t)now_int;
    }
    else {
      now_int = (int)now;
    }
  }
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Serial.print("Sensor Status :");
  Serial.println(sensor_status);
  Serial.print("num of data saved :");
  Serial.println(num_data_saved);

  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  if ((sensor_status == 1) && (mqtt_connected == 1)) { //ck if there is stored data
    for (int  ii = 1;  ii <= num_data_saved; ++ii)
    {
      int rtcPos = RTCMEMORYSTART +  (ii - 1) * buckets;
      system_rtc_mem_read(rtcPos, &rtcMem, sizeof(rtcMem));
      Serial.println(rtcPos);

      make_Json(rtcMem);  // -> msg
      //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      Serial.print("Publsh message: ");
      Serial.println(msg);
      client.publish(MQTT_TOPIC_OUT, msg);
      msg_index = msg_index + 1;
      delay(250);
    }
    num_data_saved = 0;
    sensor_status = 0;
    Serial.println("All data saved are sent");
  }
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  get_sensor_data();  //-> rtcMem
  Serial.print(" mqtt connected: ");
  Serial.println(mqtt_connected);

  if (mqtt_connected == 1) { // all is ok

    make_Json(rtcMem);  // ->msg

    Serial.print("Publsh message: ");
    Serial.println(msg);
    client.publish(MQTT_TOPIC_OUT, msg);
    msg_index = msg_index + 1;
    delay(250);

    //~~~~~~
    system_rtc_mem_write(RTCaddr_index, &msg_index, 4);
    system_rtc_mem_write(RTCaddr_sensor_status, &sensor_status, 4);
    system_rtc_mem_write(RTCaddr_num_data_saved, &num_data_saved, 4);
    now_int = now_int + SENSOR_READ_INTERVAL * 60;
    system_rtc_mem_write(RTCaddr_unix_time , &now_int, 4);
    delay(10);
    Serial.println("before 5 min sleep");
    ESP.deepSleep(SENSOR_READ_INTERVAL * 60000000, WAKE_RFCAL); //
    delay(100);
  }
  else {    //no mqtt therefor stor the data int RTC
    Serial.println("no mqtt service therefore going to sleep for 5 min");//half-hour");
    Serial.print("Save Data at RTC addr :");
    num_data_saved = num_data_saved + 1;

    sensor_status = 1; //data stored
    system_rtc_mem_write(RTCaddr_sensor_status, &sensor_status, 4);
    system_rtc_mem_write(RTCaddr_num_data_saved, &num_data_saved, 4);
    //system_rtc_mem_write(RTCaddr_index, &msg_index, 4);
    now_int = now_int + SENSOR_READ_INTERVAL * 60;
    system_rtc_mem_write(RTCaddr_unix_time , &now_int, 4);

    if (num_data_saved <= max_save_data) {
      int rtcPos = RTCMEMORYSTART + (num_data_saved - 1) * buckets;
      system_rtc_mem_write(rtcPos, &rtcMem, sizeof(rtcMem));
      Serial.println(rtcPos);
    }

    ESP.deepSleep(SENSOR_READ_INTERVAL * 60000000, WAKE_RFCAL); //
    delay(100);
  }

}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  //  WiFi.persistent(false);
  //  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP); // Default is WIFI_NONE_SLEEP
  WiFi.setOutputPower(13); // 0~20.5dBm, Default max

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  //  WiFi.config(ip, gateway, subnet); // For Static IP
  //  WiFi.begin(ssid, password);

  int counter = 0;
  while ((WiFi.status() != WL_CONNECTED) && (counter < 100) ) {
    WiFi_connected = 0;
    delay(500);
    Serial.print(".");
    //if (++counter > 100) ESP.restart();
    counter = counter + 1;
  }

  if (WiFi.status() == WL_CONNECTED)  {
    WiFi_connected = 1;
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else {
    WiFi_connected = 0;
    Serial.println("");
    Serial.println("WiFi not connected");
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.write(payload[i]);
  }
  Serial.write('\n');
  Serial.print("payload :  ");
  Serial.println((char *)payload);
  Serial.println();

  DynamicJsonDocument doc((MAX_MSG_SIZE + 1));
  deserializeJson(doc, payload);
  double led    = doc["data"][0];
  //  if (led == 1) {
  //    digitalWrite(led_pin, HIGH);
  //  }
  //  else {
  //    digitalWrite(led_pin, LOW);
  //  }

}
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void reconnect() {
  // Loop until we're reconnected
  int mqtt_reconnect_try = 0;
  while (!client.connected()) {
    mqtt_connected = 0;
    Serial.print("Attempting MQTT connection...");
    if (client.connect(MQTT_ID, MQTT_USER, MQTT_PSWD)) {
      mqtt_reconnect_try = 0;
      Serial.println("connected");
      // Wait 5 seconds before retrying
      for (byte i = 0; i < 2; i++) delay(500); // delay(1000) may cause hang
      // Once connected, publish an announcement...
      // client.publish(MQTT_TOPIC_OUT, "hello world");
      // ... and resubscribe
      client.subscribe(MQTT_TOPIC_IN);
      mqtt_connected = 1;
    }
    else {
      mqtt_reconnect_try++;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      if (mqtt_reconnect_try > 5) {
        Serial.println(" ");
        Serial.println("failed 5 times in a row");
        mqtt_connected = 0;
        break;
      }
      else {
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        for (byte i = 0; i < 10; i++) delay(500); // delay(5000) may cause hang
      }
    }
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool getNTPtime(int sec) {
  uint32_t start = millis();
  do {
    time(&now); // The time() function only calls the NTP server every hour. So you can always use getNTPtime()
    localtime_r(&now, &timeinfo);
    Serial.print(".");
    delay(100);
  } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));
  if (timeinfo.tm_year <= (2016 - 1900)) {
    return false;  // the NTP call was not successful
  }
  else {
    Serial.println(" ");
    Serial.print("now ");
    Serial.println(now);
    return true;
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void make_Json(rtcStore rtcData) {
  StaticJsonDocument < MAX_MSG_SIZE + 1 > doc; //256 is the RAM allocated to ths document.
  doc["Solar"] = 1;
  doc["Msg#"] = msg_index;
  doc["status"] = sensor_status;
  doc["time"] = rtcData.unix_time;

  // Check f any reads faled and ext early (to try agan)
  JsonArray data = doc.createNestedArray("data");
  //data.add((float)rtcData.Temp / 10);
  data.add(rtcData.Solar);
  data.add(rtcData.battery);

  int b = serializeJson(doc, msg); // Generate the mnfed JSON and send it to the msg char array
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void get_sensor_data() {
  Serial.println("Read sensor data");
  int16_t adc2;
  float  volts2;
  adc2 = ads.readADC_SingleEnded(2);
  volts2 = ads.computeVolts(adc2);
  rtcMem.Solar = (int)adc2;

  Serial.println("-----------------------------------------------------------");
  Serial.print("AIN2: "); Serial.print(adc2); Serial.print("  "); Serial.print(volts2); Serial.println("V");

  batt = analogRead(analognPin);
  rtcMem.battery = batt;
  rtcMem.unix_time = now_int;
}
