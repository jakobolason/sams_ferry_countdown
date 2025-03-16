/*

  AUR4 Clock
  RTC clock using NTP server to sync time

  Created using the Arduino Uno R4 Wifi example code - RTC_NTPSync, initially created by Sebastian Romero @sebromero  

 Instructions:
 1. Change the WiFi credentials in the arduino_secrets.h file to match your WiFi network.
 2. Set the orientation using the #define ORIENTATION 0 or 1
 3. Set timezone offset hours using the #define TIMEZONE_OFFSET_HOURS according to your localization
*/

#include "led-matrix.h"
#include "Arduino_LED_Matrix.h"
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

// byte currentFrame[NO_OF_ROWS][NO_OF_COLS];
// byte rotatedFrame[NO_OF_ROWS][NO_OF_COLS];

// position first = { 5, 0 };   // position of first digit
// position second = { 0, 0 };  // etc.
// position third = { 5, 7 };
// position fourth = { 0, 7 };

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
uint8_t bufferIndex = 0;

// sets up u8g2 for the correct display (using sda and scd pins on arduino uno r4 wifi)
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

ArduinoLEDMatrix matrix;

// void setDigit(position digitPosition, const byte digit[][5]) {
//   for (byte r = 0; r < 3; r++) {
//     for (byte c = 0; c < 5; c++) {
//       currentFrame[r + digitPosition.row][c + digitPosition.col] = digit[r][c];
//     }
//   }
// }

// void rotateFrame() {
//   for (byte r = 0; r < NO_OF_ROWS; r++) {
//     for (byte c = 0; c < NO_OF_COLS; c++) {
//       rotatedFrame[r][c] = currentFrame[NO_OF_ROWS - 1 - r][NO_OF_COLS - 1 - c];
//     }
//   }
//   memcpy(currentFrame, rotatedFrame, sizeof rotatedFrame);
// }

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void connectToWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(ssid, pass);
  }
  delay(5000);
  Serial.println("Connected to WiFi");
  printWifiStatus();
}
// post: updated timebuffer and bufferIndex set to 0
ProgramCodes updateTimes(uint8_t maxTries = 0) {
  bufferIndex = 0;
  dateTimeBuffer.size = 0;
  if (maxTries > 5) {
    Serial.println("WARNING: Endless loop");
    // To avoid an endless loop, just return from here    
    delay(2000);
    return ProgramCodes::ERROR;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("A successfull wifi connnection was not established!");
    return ProgramCodes::WIFI_ERR;
  }
  // 1st attempt
  ProgramCodes code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer);
  switch(code) {
    case ProgramCodes::SUCCESSFULL:
      return ProgramCodes::SUCCESSFULL;
    case ProgramCodes::FAULTY_ORIGINID:
      // Call searchLocation and get new id
      searchLocation("Sælvig", &saelvigId, &client, api_key);
      code = searchTrip(saelvigId, &client, api_key, &dateTimeBuffer);
      Serial.println("NEW ID");
      break;
    case ProgramCodes::NO_TRIPS:
      // TODO: Call searchTrip, but for the next day
      Serial.println("Adding extra lenght to duration");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 600); // 
      break;
    case ProgramCodes::BAD_REQUEST:
      Serial.println("BAD REQUEST NOT IMPLEMENTED");
      break;
    case ProgramCodes::JSON_PARSING_FAIL:
      Serial.println("SOMETHING WRONG WITH JSON PARSING");
      break;
    case ProgramCodes::TOO_LARGE_REQUEST:
      // Give a smaller duration
      Serial.println("TOO Large request");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 300); // To counteract the NO_TRIPS error
      break;
    default:
      Serial.print("Error in API request");
      break;
  }
  //  if the exception handling didn't work, try again but with upper-cap
  if (code != ProgramCodes::SUCCESSFULL) {
    return updateTimes(maxTries++);
  }
  return ProgramCodes::SUCCESSFULL; // == code
}

String formatDateTime(int val) {
  return val / 10 != 0 ? String(val) : "0" + String(val);
}

void setup() {
  Serial.begin(9600);


  u8g2.begin();
  delay(100);
  // u8g2.clearBuffer();          // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB24_tr); // choose a suitable font
  u8g2.drawStr(0, 40, "loading");
  // u8g2.drawBox(0, 64, 128, 64);
  u8g2.sendBuffer();          // transfer internal memory to the display

  connectToWiFi();
  beginUDP();

  RTC.begin();
  updateTime();
  // matrix.begin();


  updateTimes(0);
}

void loop() {
  currentMillis = millis();
  if (currentMillis - previousMillis > 43200000) { //update every 12 hours
    updateTime();
    previousMillis = currentMillis;
  }

  RTCTime currentTime;
  RTC.getTime(currentTime);
  time_t currentMsTime = currentTime.getUnixTime();
  time_t nextShip = 0;
  // NOTE:               THIS \---/ is used for edge case, where ferry is just about to take off
  if (dateTimeBuffer.buffer[bufferIndex].timeStamp + 60 <= currentMsTime) {
    // check boundaries
    if (bufferIndex == 2) {
      // update ferry times
      Serial.println("Ferry has sailed");
      updateTimes();
      return;
    } else {
      // else we increase buffer and try at next loop
      bufferIndex++;
      return;
    }
  }
  // now there is no way for the nextShip to have sailed already
  u8g2.clearBuffer();          // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB14_tr); // choose a suitable font
  u8g2.drawRFrame(0, 0, 128, 64, 7);
  // Draw first ferry time as large, with a vertical bar beneath
  nextShip = dateTimeBuffer.buffer[bufferIndex].timeStamp;
  // I want a vertical bar below the first ferry time
  u8g2.drawHLine(10, 32, 108);
  double diffSecs = difftime(nextShip, currentMsTime);
  if (isnan(diffSecs) || isinf(diffSecs)) {
    Serial.println("Overflow or invalid time difference detected!");
    diffSecs = 0;
  }

  int minutesUntil = diffSecs / 60;
  int hoursUntil = minutesUntil / 60;
  // use as rest, showing hours and minutes correctly
  int minutesRemaining = minutesUntil % 60;
  int dayUntil = hoursUntil / 24;

  // display each departure fetched 
  if (dayUntil != 0) {
    String days_countdown = String(dayUntil) + " dage";
    u8g2.drawStr(0, 10, days_countdown.c_str());
  } else {
    String to = (dateTimeBuffer.buffer[bufferIndex].to == 1 ? "Hou" : "Aarhus");
    String time_countdown = "T-" + formatDateTime(hoursUntil) + ":" + formatDateTime(minutesRemaining);
    u8g2.drawStr(10, 31, time_countdown.c_str());
    u8g2.setFont(u8g2_font_ncenB08_tr); // switch to smaller font for space reasons
    u8g2.drawStr(85, 31, to.c_str());
  }

  for (int i = bufferIndex+1; i < dateTimeBuffer.size; ++i) {
    nextShip = dateTimeBuffer.buffer[i].timeStamp;
    double diffSecs = difftime(nextShip, currentMsTime);
    if (isnan(diffSecs) || isinf(diffSecs)) {
      Serial.println("Overflow or invalid time difference detected!");
      diffSecs = 0;
    }
    int minutesUntil = diffSecs / 60;
    int hoursUntil = minutesUntil / 60;
    // use as rest, showing hours and minutes correctly
    int minutesRemaining = minutesUntil % 60;
    int dayUntil = hoursUntil / 24;

    // display each departure fetched 
    if (dayUntil != 0) {
      String days_countdown = String(dayUntil) + " dage";
      u8g2.drawStr(0, 10, days_countdown.c_str());
      // then print "{dayUntil} dage"
      // setDigit(first, digits[dayUntil / 10]);
      // setDigit(second, digits[dayUntil % 10]);
      // setDigit(third, characters[0]);
      // setDigit(fourth, characters[1]);
    } else {
      String to = (dateTimeBuffer.buffer[i].to == 1 ? "Hou" : "Aarhus");
      String time_countdown = "T-" + formatDateTime(hoursUntil) + ":" + formatDateTime(minutesRemaining);
      // Serial.println(time_countdown);
      // Serial.println(to);
      // Serial.println(i);
      u8g2.drawStr(30, 32+i*13, time_countdown.c_str());
      u8g2.drawStr(85, 32+i*13, to.c_str());
      // setDigit(first, digits[(int)(hoursUntil / 10)]);
      // setDigit(second, digits[(int)(hoursUntil % 10)]);
      // setDigit(third, digits[(int)(minutesRemaining / 10)]);
      // setDigit(fourth, digits[(int)(minutesRemaining % 10)]);
    }
  } 
  u8g2.sendBuffer();          // transfer internal memory to the display
  // if (ORIENTATION == 1) {
  //   rotateFrame();
  // }
  // matrix.renderBitmap(currentFrame, NO_OF_ROWS, NO_OF_COLS);
  // u8g2.drawStr(0,10,"Hello World from countdown!");  // write something to the internal memory
  delay(1000);// Change this to 60 000 (60s) to only update every 60s
}
