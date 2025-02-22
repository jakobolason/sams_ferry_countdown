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
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>

#include "arduino_secrets.h"
#include "timeSync.h"

#define LOGGING // for debug purposes

#define ORIENTATION 1 // 0 (up is where the ESP32 is), 1 (up is where the Qwiic is)

unsigned long currentMillis;
unsigned long previousMillis = 0;

byte currentFrame[NO_OF_ROWS][NO_OF_COLS];
byte rotatedFrame[NO_OF_ROWS][NO_OF_COLS];

position first = {5, 0}; // position of first digit
position second = {0, 0}; // etc.
position third = {5, 7};
position fourth = {0, 7};

// please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID; // your network SSID (name)
char pass[] = SECRET_PASS; // your network password (use for WPA, or use as key for WEP)
char api_key[] = API_KEY;   // Unique api key to Rejseplanen API 2.0

int wifiStatus = WL_IDLE_STATUS;

char baseUrl[] = "www.rejseplanen.dk/api/";
WiFiClient client;
int port = 443;

String saelvigId = "A=1@O=Sælvig Havn (færge)@X=10549354@Y=55864255@U=86@L=110000501@B=1@p=1740046653@";

ArduinoLEDMatrix matrix;

void setDigit(position digitPosition, const byte digit[][5]){
  for(byte r = 0; r < 3; r++){
    for(byte c = 0; c < 5; c++){
      currentFrame[r+digitPosition.row][c+digitPosition.col] = digit[r][c];
    }
  }
}

void rotateFrame(){
  for(byte r = 0; r < NO_OF_ROWS; r++){
    for(byte c = 0; c < NO_OF_COLS; c++){
      rotatedFrame[r][c] = currentFrame[NO_OF_ROWS-1-r][NO_OF_COLS-1-c];
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

void connectToWiFi(){
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

void searchLocation(String searchInput, String* placeId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("A successfull wifi connnection was not established!");
    return;
  }

  Serial.println("Attempting to connect to rejseplanen");
  if (!client.connect("www.rejseplanen.dk", 80)) {
    Serial.println("Failed to connect to rejseplanen");
  }

  Serial.println("Connection successful!");
  client.println("GET /api/location.name?accessId=706bd956-cb75-4b03-8509-f36210d10ac2&maxNo=1&type=S&format=json&input=Sælvig%20Havn%20(færge) HTTP/1.1");
  client.println("Host: rejseplanen.dk");
  client.println("Connection: close");
  client.println();

  // Wait for the server's response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  String headerResponse = "";
  String jsonResponse = "";
  bool inJson = false;
  while (client.available()) {
    // Skip everything until we find the first '{'
    client.readStringUntil('{');
    // Add back the '{' we just consumed
    jsonResponse = "{";
    // Now read the rest of the response
    jsonResponse += client.readString();
  }
  Serial.print("Full response: ");
  Serial.println(jsonResponse);


  DynamicJsonDocument doc(4096);
  
  DeserializationError error = deserializeJson(doc, jsonResponse);
  client.stop();
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    delay(10000);
    return;
  }
  Serial.println("JSON parsed correctly!");

  if (doc.containsKey("stopLocationOrCoordLocation")) {
    JsonArray locationsArray = doc["stopLocationOrCoordLocation"];
    
    if (locationsArray[0].containsKey("StopLocation")) {
      JsonObject stopLocation = locationsArray[0]["StopLocation"];
      Serial.println("Location found:");
      // Now lets get the id - which is all we want
      if (stopLocation.containsKey("id")) {
        String locationId = stopLocation["id"];
        Serial.print("FOUND ID!!!: ");
        Serial.println(locationId);
        placeId* = locationId;
      } else {
        Serial.println("NO id found in response");
      }
    } else {
      Serial.println("No StopLocation found in response");
    }
  } else {
      Serial.println("No stopLocationOrCoordLocation found in response");
  }
}

void setup() {
  Serial.begin(9600);
  connectToWiFi();
  beginUDP();

  RTC.begin();
  updateTime();
  matrix.begin();  
  searchLocation("Sælvig", &saelvigId);
  delay(60000);
}

void loop() {  
  currentMillis = millis();
  if(currentMillis - previousMillis > 43200000){ // 1000 * 60 * 60 * 12
    updateTime();
    previousMillis = currentMillis;
  }

  RTCTime currentTime;
  RTC.getTime(currentTime);
  time_t currentMsTime = currentTime.getUnixTime();
  time_t nextShip = 1739617200000 / 1000; // 15/02/2025 12:00
  if (nextShip < currentMsTime) {
    nextShip = currentMsTime;
  }

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
  if (ORIENTATION == 1){
    rotateFrame();
  }
  matrix.renderBitmap(currentFrame, NO_OF_ROWS, NO_OF_COLS);   


  delay(1000);   
}
