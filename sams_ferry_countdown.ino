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

// sets up u8g2 for the correct display (using sda and scd pins on arduino uno r4 wifi)
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

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
    while (true)
      ;
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

ProgramCodes controlApiCalls(ProgramCodes code, boolean useLastDateTime = false) {
  switch (code) {
    case ProgramCodes::SUCCESSFULL:
      break;
    case ProgramCodes::FAULTY_ORIGINID:
      // Call searchLocation and get new id
      searchLocation("Sælvig", &saelvigId, &client, api_key);
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, useLastDateTime = useLastDateTime);
      Serial.println("NEW ID");
      break;
    case ProgramCodes::NO_TRIPS:
      // TODO: Call searchTrip, but for the next day
      Serial.println("Adding extra lenght to duration");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, &dateTimeBuffer, 700, useLastDateTime);  //
      break;
    case ProgramCodes::BAD_REQUEST:
      Serial.println("BAD REQUEST NOT IMPLEMENTED");
      break;
    case ProgramCodes::JSON_PARSING_FAIL:
      // Serial.println("SOMETHING WRONG WITH JSON PARSING");  // handle this the same way as TOO_LARGE_REQUEST
    case ProgramCodes::TOO_LARGE_REQUEST:
      // Give a smaller duration
      Serial.println("TOO Large request");
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
  code = controlApiCalls(code);
  // maybe a bit reckless, but could be cool to try and fill up the buffer
  int counter = 0;
  while (dateTimeBuffer.size < 3 && counter < 3) {
    Serial.println("less than 3, nr: " + String(counter));
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
    Serial.println("UPDATE ATTEMPT WAS UNSUCCESSFULL");
    return updateTimes(maxTries++);
  }
  return ProgramCodes::SUCCESSFULL;  // == code
}


String formatDateTime(int val) {
  return val / 10 != 0 ? String(val) : "0" + String(val);
}

void setup() {
  Serial.begin(9600);


  u8g2.begin();
  delay(100);
  // u8g2.clearBuffer();          // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB24_tr);  // choose a suitable font
  u8g2.drawStr(0, 40, "loading");
  // u8g2.drawBox(0, 64, 128, 64);
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
  time_t currentMsTime = currentTime.getUnixTime(); // calibration scalar, since RTC is too fast
  // Serial.println("Current time from millis: " + convertMillisToDateTime(currentMsTime*1000));

  time_t nextShip = 0;
  if (dateTimeBuffer.size < 1 || dateTimeBuffer.buffer[0].timeStamp + 60 <= currentMsTime) {
    // check boundaries
    if (dateTimeBuffer.size < 1) {
      // update ferry times
      Serial.println("There are no times back? What went wrong");
      ProgramCodes code = updateTimes();
      if (code == ProgramCodes::ERROR) {
        u8g2.clearBuffer();                  // clear the internal memory
        u8g2.setFont(u8g2_font_ncenB14_tr);  // choose a suitable font
        u8g2.drawStr(0, 20, "Der skete en fejl, prøv igen senere");
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
  // else if (dateTimeBuffer.size < 3) {
  //   // i would like to keep 3 entries at all times
  //   ProgramCodes code = updateTimes();
  //     if (code == ProgramCodes::ERROR) {
  //       u8g2.clearBuffer();          // clear the internal memory
  //       u8g2.setFont(u8g2_font_ncenB14_tr); // choose a suitable font
  //       u8g2.drawStr(0, 20, "Der skete en fejl, prøv igen senere");
  //       u8g2.sendBuffer();
  //       delay(60000); // Delay a minute before trying again
  //     }
  //     return;
  // }
  // now there is no way for the nextShip to have sailed already
  u8g2.clearBuffer();                  // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB14_tr);  // choose a suitable font
  u8g2.drawRFrame(0, 0, 128, 64, 7);
  // Draw first ferry time as large, with a vertical bar beneath
  nextShip = dateTimeBuffer.buffer[0].timeStamp;
  // I want a vertical bar below the first ferry time
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
  String to = (dateTimeBuffer.buffer[0].to == 1 ? "Hou" : "Aarhus");

  // display each departure fetched
  if (dayUntil != 0) {
    String days_countdown = String(dayUntil) + " dage";
    u8g2.drawStr(10, 20, days_countdown.c_str());
    u8g2.setFont(u8g2_font_ncenB08_tr);  // switch to smaller font for space reasons
    u8g2.drawStr(85, 20, to.c_str());
  } else {
    u8g2.drawHLine(10, 32, 108);
    String time_countdown = "T-" + formatDateTime(hoursUntil) + ":" + formatDateTime(minutesRemaining);
    u8g2.drawStr(10, 31, time_countdown.c_str());
    u8g2.setFont(u8g2_font_ncenB08_tr);  // switch to smaller font for space reasons
    u8g2.drawStr(85, 31, to.c_str());
  }

  for (int i = 1; i < dateTimeBuffer.size; ++i) {
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
    String to = (dateTimeBuffer.buffer[i].to == 1 ? "Hou" : "Aarhus");


    // display each departure fetched
    if (dayUntil != 0) {
      String days_countdown = String(dayUntil) + " dage";
      u8g2.drawStr(10, 15 + 15*i, days_countdown.c_str());
      u8g2.drawStr(85, 15 + 15*i, to.c_str());
      // then print "{dayUntil} dage"
      // setDigit(first, digits[dayUntil / 10]);
      // setDigit(second, digits[dayUntil % 10]);
      // setDigit(third, characters[0]);
      // setDigit(fourth, characters[1]);
    } else {
      // Serial.println(time_countdown);
      // Serial.println(to);
      // Serial.println(i);
      String time_countdown = "T-" + formatDateTime(hoursUntil) + ":" + formatDateTime(minutesRemaining);
      u8g2.drawStr(30, 32 + i * 13, time_countdown.c_str());
      u8g2.drawStr(85, 32 + i * 13, to.c_str());
      // setDigit(first, digits[(int)(hoursUntil / 10)]);
      // setDigit(second, digits[(int)(hoursUntil % 10)]);
      // setDigit(third, digits[(int)(minutesRemaining / 10)]);
      // setDigit(fourth, digits[(int)(minutesRemaining % 10)]);
    }
  }
  u8g2.sendBuffer();  // transfer internal memory to the display
  // if (ORIENTATION == 1) {
  //   rotateFrame();
  // }
  // matrix.renderBitmap(currentFrame, NO_OF_ROWS, NO_OF_COLS);
  // u8g2.drawStr(0,10,"Hello World from countdown!");  // write something to the internal memory
  if (dateTimeBuffer.size < 3)
    delay(60000);
  delay(10000);  // Change this to 60 000 (60s) to only update every 60s
}
