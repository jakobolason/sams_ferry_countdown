/*

  AUR4 Clock
  RTC clock using NTP server to sync time

  Created using the Arduino Uno R4 Wifi example code - RTC_NTPSync, initially created by Sebastian Romero @sebromero  

 Instructions:
 1. Change the WiFi credentials in the arduino_secrets.h file to match your WiFi network.
 2. Set the orientation using the #define ORIENTATION 0 or 1
 3. Set timezone offset hours using the #define TIMEZONE_OFFSET_HOURS according to your localization
*/

// #include "led-matrix.h"
// #include "Arduino_LED_Matrix.h"
#include "RTC.h"
#include <WiFi.h>
#include <time.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "arduino_secrets.h"
#include "timeSync.h"
#include "rpCalls.h"

#define LOGGING  // for debug purposes

// #define ORIENTATION 1  // 0 (up is where the ESP32 is), 1 (up is where the Qwiic is)

unsigned long currentMillis;
unsigned long previousMillis = 0;

// please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
char api_key[] = API_KEY;   // Unique api key to Rejseplanen API 2.0

int wifiStatus = WL_IDLE_STATUS;

char baseUrl[] = "www.rejseplanen.dk/api/";
WiFiClient client;
int port = 443;

String saelvigId = "A=1@O=S%C3%A6lvig%20Havn%20(f%C3%A6rge)@X=10549354@Y=55864255@U=86@L=110000501@B=1@p=1740134093";

datetimebuffer dateTimeBuffer = {};

// sets up u8g2 for the correct display (using sda and scd pins on arduino uno r4 wifi)
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);


void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Debug(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Debug(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Debug(" dBm");
}

void connectToWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Debug("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Debug("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Debug(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(ssid, pass);
  }
  delay(5000);
  Debug("Connected to WiFi");
  printWifiStatus();
}

ProgramCodes controlApiCalls(ProgramCodes code, boolean useLastDateTime = false) {
  switch (code) {
    case ProgramCodes::SUCCESSFULL:
      break;
    case ProgramCodes::FAULTY_ORIGINID:
      // Call searchLocation and get new id
      searchLocation("Sælvig", &saelvigId, &client, api_key);
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, useLastDateTime = useLastDateTime);
      Debug("NEW ID");
      break;
    case ProgramCodes::NO_TRIPS:
      // TODO: Call searchTrip, but for the next day
      Debug("Adding extra lenght to duration");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 700, useLastDateTime);  //
      break;
    case ProgramCodes::BAD_REQUEST:
      Debug("BAD REQUEST NOT IMPLEMENTED");
      break;
    case ProgramCodes::JSON_PARSING_FAIL:
      // Debug("SOMETHING WRONG WITH JSON PARSING");  // handle this the same way as TOO_LARGE_REQUEST
    case ProgramCodes::TOO_LARGE_REQUEST:
      // Give a smaller duration
      Debug("TOO Large request");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 300, useLastDateTime);  // To counteract the NO_TRIPS error
      break;
    default:
      Serial.print("Error in API request");
      break;
  }
  return code;
}

// post: updated timebuffer
ProgramCodes updateTimes(uint8_t maxTries = 0) {
  dateTimeBuffer.size = 0;
  if (maxTries > 5) {
    Debug("WARNING: Endless loop");
    // To avoid an endless loop, just return from here
    delay(2000);
    return ProgramCodes::ERROR;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Debug("A successfull wifi connnection was not established!");
    return ProgramCodes::WIFI_ERR;
  }
  // 1st attempt
  ProgramCodes code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer);
  code = controlApiCalls(code);
  // maybe a bit reckless, but could be cool to try and fill up the buffer
  int counter = 0;
  while (dateTimeBuffer.size < 3 && counter < 3) {
    Debug("less than 3, nr: " + String(counter));
    code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 500, true);
    delay(500);
    code = controlApiCalls(code, true);
    counter++;
    // don't use the return code, because if it doesn't work then it's okay
    if (counter == 2)  // if the 3rd attempt was still unsuccessfull, then last if-statement in loop will handle this
      return code == ProgramCodes::SUCCESSFULL ? ProgramCodes::SUCCESSFULL : ProgramCodes::ERROR;
  }


  //  if the exception handling didn't work, try again but with upper-cap
  if (code != ProgramCodes::SUCCESSFULL) {
    Debug("UPDATE ATTEMPT WAS UNSUCCESSFULL");
    return updateTimes(maxTries++);
  }
  return ProgramCodes::SUCCESSFULL;  // == code
}


bool formatDateTime(int val) {
  return val / 10 != 0;
}

void showTimes(time_t currentMsTime, bool showTimeStamp) {
  time_t nextShip = 0;
  u8g2.clearBuffer();                  // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB14_tr);  // choose a suitable font
  u8g2.drawRFrame(0, 0, 128, 64, 7);
  // Draw first ferry time as large, with a vertical bar beneath
  nextShip = dateTimeBuffer.buffer[0].timeStamp;
  // I want a vertical bar below the first ferry time
  double diffSecs = difftime(nextShip, currentMsTime);
  if (isnan(diffSecs) || isinf(diffSecs)) {
    Debug("Overflow or invalid time difference detected!");
    diffSecs = 0;
  }

  int minutesUntil = diffSecs / 60;
  int hoursUntil = minutesUntil / 60;
  // use as rest, showing hours and minutes correctly
  int minutesRemaining = minutesUntil % 60;
  int dayUntil = hoursUntil / 24;
  const char* to = (dateTimeBuffer.buffer[0].to == 1) ? "Hou" : "Aarhus";

  // display each departure fetched
  if (dayUntil != 0) {
    char daysCountdown[16];
    snprintf(daysCountdown, sizeof(daysCountdown), "%d dage", dayUntil);
    u8g2.drawStr(10, 20, daysCountdown);
    u8g2.setFont(u8g2_font_ncenB08_tr);  // switch to smaller font for space reasons
    u8g2.drawStr(85, 20, to);
  } else {
    u8g2.drawHLine(10, 32, 108);
    char time_countdown[9] = "";
    if (showTimeStamp) {
      strncpy(time_countdown, dateTimeBuffer.buffer[0].stringTime.c_str(), sizeof(time_countdown) - 1);
      time_countdown[sizeof(time_countdown) - 1] = '\0';  // Ensure null termination
    } else {
      // %02d ensures that there's a leading 0
      snprintf(time_countdown, sizeof(time_countdown), "T-%02d:%02d", hoursUntil, minutesRemaining);
    }
    u8g2.drawStr(10, 31, time_countdown);
    u8g2.setFont(u8g2_font_ncenB08_tr);  // switch to smaller font for space reasons
    u8g2.drawStr(85, 31, to);
  }

  for (int i = 1; i < dateTimeBuffer.size; ++i) {
    nextShip = dateTimeBuffer.buffer[i].timeStamp;
    double diffSecs = difftime(nextShip, currentMsTime);
    if (isnan(diffSecs) || isinf(diffSecs)) {
      Debug("Overflow or invalid time difference detected!");
      diffSecs = 0;
    }
    int minutesUntil = diffSecs / 60;
    int hoursUntil = minutesUntil / 60;
    // use as rest, showing hours and minutes correctly
    int minutesRemaining = minutesUntil % 60;
    int dayUntil = hoursUntil / 24;
    const char* to = (dateTimeBuffer.buffer[i].to == 1) ? "Hou" : "Aarhus";


    // display each departure fetched
    if (dayUntil != 0) {
      char daysCountdown[16];
      snprintf(daysCountdown, sizeof(daysCountdown), "%d dage", dayUntil);
      u8g2.drawStr(10, 15 + 15*i, daysCountdown);
      u8g2.drawStr(85, 15 + 15*i, to);
    } else {
        char time_countdown[10] = "";
      if (showTimeStamp) {
        strncpy(time_countdown, dateTimeBuffer.buffer[i].stringTime.c_str(), sizeof(time_countdown) - 1);
        time_countdown[sizeof(time_countdown) - 1] = '\0';  // Ensure null termination
      } else {
        // 02d ensures that there's a leading 0
        snprintf(time_countdown, sizeof(time_countdown), "T-%02d:%02d", hoursUntil, minutesRemaining);
      }
      u8g2.drawStr(30, 32 + i * 13, time_countdown);
      u8g2.drawStr(85, 32 + i * 13, to);
    }
  }
  u8g2.sendBuffer();  // transfer internal memory to the display
}

void setup() {
  Serial.begin(9600);
  u8g2.begin();
  delay(100); // give a bit of delay to let u8g2 settle
  // u8g2.clearBuffer();          // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB24_tr);  // choose a suitable font
  u8g2.drawStr(0, 40, "loading");
  u8g2.sendBuffer();  // transfer internal memory to the display

  connectToWiFi();
  beginUDP();

  RTC.begin();
  updateTime();
  // matrix.begin();

  updateTimes(0);
}

void loop() {
  currentMillis = millis();
  if (currentMillis - previousMillis > 432000) {  //update every 12 hours
    updateTime();
    previousMillis = currentMillis;
  }
  RTCTime currentTime;
  RTC.getTime(currentTime);
  time_t currentMsTime = currentTime.getUnixTime();
  // Debug("Current time from millis: " + convertMillisToDateTime(currentMsTime*1000));

  if (dateTimeBuffer.size < 1 || dateTimeBuffer.buffer[0].timeStamp + 60 <= currentMsTime) {
    // check boundaries
    if (dateTimeBuffer.size < 1) {
      // update ferry times
      Debug("There are no times back? What went wrong");
      ProgramCodes code = updateTimes();
      if (code == ProgramCodes::ERROR) {
        u8g2.clearBuffer();                  // clear the internal memory
        u8g2.setFont(u8g2_font_ncenB14_tr);  // choose a suitable font
        u8g2.drawStr(0, 20, "Der skete en fejl,");
        u8g2.drawStr(0, 40, "prøv igen senere");
        u8g2.sendBuffer();
        delay(60000);  // Delay a minute before trying again
      }
      delay(1000);
      return;
    } else {
      // we always want 3 entries (or will try to)
      sortBuffer(dateTimeBuffer, currentMsTime);
      ProgramCodes code = updateTimes();
      if (code == ProgramCodes::ERROR) {
        u8g2.clearBuffer();                  // clear the internal memory
        u8g2.setFont(u8g2_font_ncenB08_tr);  // choose a suitable font
        u8g2.drawStr(0, 20, "Der skete en fejl, prøv igen senere");
        u8g2.sendBuffer();
        delay(60000);  // Delay a minute before trying again
      }
      return;
    }
  }

  // now there is no way for the nextShip to have sailed already
  showTimes(currentMsTime, true);

  if (dateTimeBuffer.size < 3)
    delay(60000);
  delay(20000);  // Change this to 60 000 (60s) to only update every 60s
}
