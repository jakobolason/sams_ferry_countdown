#include "RTC.h"
#include <time.h>
#include <ArduinoJson.h>


#include "rpCalls.h"

String urlEncodeUTF8(String str) {
    String encoded = "";
    char c;
    char buf[4];  // Buffer to store encoded characters
    for (size_t i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            // Safe characters remain unchanged
            encoded += c;
        } else {
            // Convert to %XX format
            sprintf(buf, "%%%02X", (unsigned char)c);
            encoded += buf;
        }
    }
    return encoded;
}


void searchLocation(String searchInput, String* placeId, WiFiClient *client, char api_key[]) {
    Serial.println("Attempting to connect to rejseplanen");
    if (!client->connect("www.rejseplanen.dk", 80)) {
      Serial.println("Failed to connect to rejseplanen");
      return;
    }
  
    Serial.println("Connection successful!");
    client->print("GET /api/location.name?maxNo=1&type=S&format=json&input=");
    client->print(searchInput);
    client->print("&accessId=");
    client->print(api_key);
    client->println(" HTTP/1.1");
    client->println("Host: rejseplanen.dk");
    client->println("Connection: close");
    client->println();
  
    // Wait for the server's response
    unsigned long timeout = millis();
    while (client->available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client->stop();
        return;
      }
    }
    String jsonResponse = "{";
    while (client->available()) {
      // Skip everything until we find the first '{'
      client->readStringUntil('{');
      // Add back the '{' we just consumed
      // Now read the rest of the response
      jsonResponse += client->readString();
    }
    Serial.print("Full response: ");
    Serial.println(jsonResponse);
    client->stop();

  
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      delay(10000);
      return;
    }
    Serial.println("JSON parsed correctly!");
    JsonObject docObject = doc.as<JsonObject>();
    JsonArray locationsArray = docObject["stopLocationOrCoordLocation"].as<JsonArray>();
    JsonObject stopLocation = locationsArray[0]["StopLocation"];
    String locationId = stopLocation["id"];
    Serial.print("FOUND ID!!!: ");
    Serial.println(locationId);
    *placeId = locationId;


    // if (docObject["stopLocationOrCoordLocation"].is<JsonArray>() {
    //   JsonArray locationsArray = docObject["stopLocationOrCoordLocation"];
      
      
    //   if (locationsArray[0].containsKey("StopLocation")) {
    //     JsonObject stopLocation = locationsArray[0]["StopLocation"];
    //     Serial.println("Location found:");
    //     // Now lets get the id - which is all we want
    //     if (stopLocation.containsKey("id")) {
    //       String locationId = stopLocation["id"];
    //       Serial.print("FOUND ID!!!: ");
    //       Serial.println(locationId);
    //       *placeId = locationId;
    //     } else {
    //       Serial.println("NO id found in response");
    //     }
    //   } else {
    //     Serial.println("No StopLocation found in response");
    //   }
    // } else {
    //     Serial.println("No stopLocationOrCoordLocation found in response");
    // }
  }
  
void searchTrip(String from, WiFiClient* client, char api_key[], datetimeBuffers* buffer) {
    Serial.println("Attempting to connect to rejseplanen");
    if (!client->connect("www.rejseplanen.dk", 80)) {
        Serial.println("Failed to connect to rejseplanen");
        return;
    }
  
    Serial.println("Connection successful! Trying to get trip info now");
    // now insert the parameters (using print allows us to more clearly represent the request)
    client->print("GET /api/departureBoard?accessId=");
    client->print(api_key);
    client->print("&format=json&id=");
    client->print(from);
    client->println(" HTTP/1.1");
    client->println("User-Agent: Arduino/1.0");
    client->println("Cache-Control: no-cache");
    client->println("Host: rejseplanen.dk");
    client->println("Connection: close");
    client->println();
    Serial.println("GET request sent");
  
    // Wait for the server's response
    unsigned long timeout = millis();
    while (client->available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client->stop();
        return;
      }
    }
    String headerResponse = "";
    String jsonResponse = "";
    // int sizeOfResponse = 0;
    String statusLine = client->readStringUntil('\n'); // Read the first line (HTTP status)
    Serial.println(statusLine); // Debug output
  
    if (statusLine.startsWith("HTTP/")) {
        int statusCode = statusLine.substring(9, 12).toInt(); // Extract status code (e.g., 200, 400)
        Serial.print("HTTP Status Code: ");
        Serial.println(statusCode);
  
        if (statusCode != 200) {
            Serial.println("Error: Bad response from server.");
            return; // Stop further processing
        }
    } else {
      Serial.println("!!!--- No status code?");
    }
    // // Read and discard headers
    while (client->available()) {
        String line = client->readStringUntil('\n');
        if (line == "\r") break;  // End of headers (last line is just "\r\n")
        if (line.startsWith("Content-Length:")) {
          // then get the length, to get the right amount of memory allocated
          String size = line.substring(line.indexOf(":")+1);
          // sizeOfResponse = size.toInt();
          Serial.println("size: " + size);
        }
    }
  
    // Now read the JSON body
    jsonResponse = client->readString();
  
  
    // Serial.print("Full response: ");
    // Serial.println(jsonResponse);
  
    // JsonDocument filter;
    // filter["Departure"];
    // filter["direction"] = true;
    // filter["time"] = true;
    // filter["date"] = true;
    JsonDocument doc;
    
    client->stop();
    DeserializationError error = deserializeJson(doc, jsonResponse); // , DeserializationOption::Filter(filter)
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      delay(10000);
      return;
    }

    Serial.println("JSON parsed correctly!");
    JsonArray departures = doc["Departure"];

    for(JsonObject v : departures) {
      String dir = v["direction"];
      time_t milliseconds = stringToUnixTime(v["date"], v["time"]);
      if (dir.compareTo("Aarhus Havn, Dokk1 (færge)") == 0) {// then they are equal
        buffer->aarhusTime = milliseconds;
        buffer->aarhusThere = true;
        Serial.println("Found aarhus time");
      } else if (dir.compareTo("Hou Havn (færge)") == 0) {
        buffer->houTime = milliseconds;
        buffer->houThere = true;
        Serial.println("Found hou time");
      } else {
        Serial.println("There was an unknonw direction found here: " + dir);
      }
    }
}

time_t stringToUnixTime(const char* dateStr, const char* timeStr) {
    char dateTimeStr[50]; // Ensure the buffer is large enough
    snprintf(dateTimeStr, sizeof(dateTimeStr), "%s %s", dateStr, timeStr);

    struct tm timeinfo = {};
    strptime(dateTimeStr, "%Y-%m-%d %H:%M:%S", &timeinfo);
    time_t seconds = mktime(&timeinfo);
    return seconds;
}