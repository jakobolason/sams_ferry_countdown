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

#include "arduino_secrets.h"
#include "timeSync.h"
#include "rpCalls.h"

#define LOGGING  // for debug purposes

#define ORIENTATION 1  // 0 (up is where the ESP32 is), 1 (up is where the Qwiic is)

unsigned long currentMillis;
unsigned long previousMillis = 0;

byte currentFrame[NO_OF_ROWS][NO_OF_COLS];
byte rotatedFrame[NO_OF_ROWS][NO_OF_COLS];

position first = { 5, 0 };   // position of first digit
position second = { 0, 0 };  // etc.
position third = { 5, 7 };
position fourth = { 0, 7 };

// please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
char api_key[] = API_KEY;   // Unique api key to Rejseplanen API 2.0

int wifiStatus = WL_IDLE_STATUS;

char baseUrl[] = "www.rejseplanen.dk/api/";
WiFiClient client;
int port = 443;

String saelvigId = "A=1@O=S%C3%A6lvig%20Havn%20(f%C3%A6rge)@X=10549354@Y=55864255@U=86@L=110000501@B=1@p=1740134093";

datetimeBuffers timeBuffer[3];
uint8_t bufferIndex = 0;

ArduinoLEDMatrix matrix;

void setDigit(position digitPosition, const byte digit[][5]) {
  for (byte r = 0; r < 3; r++) {
    for (byte c = 0; c < 5; c++) {
      currentFrame[r + digitPosition.row][c + digitPosition.col] = digit[r][c];
    }
  }
}

void rotateFrame() {
  for (byte r = 0; r < NO_OF_ROWS; r++) {
    for (byte c = 0; c < NO_OF_COLS; c++) {
      rotatedFrame[r][c] = currentFrame[NO_OF_ROWS - 1 - r][NO_OF_COLS - 1 - c];
    }
  }
  memcpy(currentFrame, rotatedFrame, sizeof rotatedFrame);
}

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
  if (maxTries > 5) {
    // To avoid an endless loop, just return from here    
    return ProgramCodes::ERROR;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("A successfull wifi connnection was not established!");
    return ProgramCodes::WIFI_ERR;
  }
  // 1st attempt
  ProgramCodes code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, timeBuffer);
  switch(code) {
    case ProgramCodes::SUCCESSFULL:
      return ProgramCodes::SUCCESSFULL;
    case ProgramCodes::FAULTY_ORIGINID:
      // Call searchLocation
      searchLocation("Sælvig", &saelvigId, &client, api_key);
      Serial.println("NEW ID");
      break;
    case ProgramCodes::NO_TRIPS:
      // TODO: Call searchTrip, but for the next day
      Serial.println("Adding extra lenght to duration");
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, timeBuffer, 600); // 
      break;
    case ProgramCodes::BAD_REQUEST:
      Serial.println("BAD REQUEST NOT IMPLEMENTED");
      break;
    case ProgramCodes::JSON_PARSING_FAIL:
      Serial.println("SOMETHING WRONG WITH JSON PARSING");
      break;
    case ProgramCodes::TOO_LARGE_REQUEST:
      // Give a smaller duration
      code = searchTrip(urlEncodeUTF8(saelvigId), &client, api_key, timeBuffer, 300); // To counteract the NO_TRIPS error
      break;
    default:
      Serial.print("Error in API request");
      break;
  }
  //  if the exception handling didn't work, try again but with upper-cap
  if (code != ProgramCodes::SUCCESSFULL) {
    return updateTimes(maxTries++);
  }
  return ProgramCodes::SUCCESSFULL;
}


void setup() {
  Serial.begin(9600);
  connectToWiFi();
  beginUDP();

  RTC.begin();
  updateTime();
  matrix.begin();

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
  if (timeBuffer[bufferIndex].timeStamp + 50 <= currentMsTime) {
    // check boundaries
    if (bufferIndex == 2) {
      // update ferry times
      updateTimes();
      return;
    } else {
      // else we increase buffer and try at next loop
      bufferIndex++;
      return;
    }
  }
  // now there is no way for the nextShip to have sailed already
  nextShip = timeBuffer[bufferIndex].timeStamp;

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
  if (dayUntil != 0) {
    // then print "{dayUntil} dage"
    setDigit(first, digits[dayUntil / 10]);
    setDigit(second, digits[dayUntil % 10]);
    setDigit(third, characters[0]);
    setDigit(fourth, characters[1]);
  } else {
    setDigit(first, digits[(int)(hoursUntil / 10)]);
    setDigit(second, digits[(int)(hoursUntil % 10)]);
    setDigit(third, digits[(int)(minutesRemaining / 10)]);
    setDigit(fourth, digits[(int)(minutesRemaining % 10)]);
  }
  if (ORIENTATION == 1) {
    rotateFrame();
  }
  matrix.renderBitmap(currentFrame, NO_OF_ROWS, NO_OF_COLS);


  delay(1000);// Change this to 60 000 (60s) to only update every 60s
}
