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


ProgramCodes searchLocation(String searchInput, String* placeId, WiFiClient *client, char api_key[]) {
    Serial.println("Attempting to connect to rejseplanen");
    if (!client->connect("www.rejseplanen.dk", 80)) {
      Serial.println("Failed to connect to rejseplanen");
      return ProgramCodes::HANDSHAKE_FAIL;
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
        return ProgramCodes::CLIENT_TIMEOUT;
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
      return ProgramCodes::JSON_PARSING_FAIL;
    }
    Serial.println("JSON parsed correctly!");
    JsonObject docObject = doc.as<JsonObject>();
    JsonArray locationsArray = docObject["stopLocationOrCoordLocation"].as<JsonArray>();
    JsonObject stopLocation = locationsArray[0]["StopLocation"];
    String locationId = stopLocation["id"];
    Serial.print("FOUND ID!!!: ");
    Serial.println(locationId);
    *placeId = locationId;
    return ProgramCodes::SUCCESSFULL;
  }
  
ProgramCodes searchTrip(String from, WiFiClient* client, char api_key[], datetimeBuffers buffer[3], int duration) {
    Serial.println("Attempting to connect to rejseplanen");
    if (!client->connect("www.rejseplanen.dk", 80)) {
        Serial.println("Failed to connect to rejseplanen");
        return ProgramCodes::HANDSHAKE_FAIL;
    }
  
    Serial.println("Connection successful! Trying to get trip info now");
    // now insert the parameters (using print allows us to more clearly represent the request)
    client->print("GET /api/departureBoard?accessId=");
    client->print(api_key);
    client->print("&format=json&maxJourneys=3&id=");
    client->print(from);
    client->print("&duration=");
    client->print(duration);
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
        return ProgramCodes::CLIENT_TIMEOUT;
      }
    }
    String headerResponse = "";
    String jsonResponse = "";
    int sizeOfResponse = 0;
    String statusLine = client->readStringUntil('\n'); // Read the first line (HTTP status)
    Serial.println(statusLine); // Debug output
  
    if (statusLine.startsWith("HTTP/")) {
        int statusCode = statusLine.substring(9, 12).toInt(); // Extract status code (e.g., 200, 400)
        Serial.print("HTTP Status Code: ");
        Serial.println(statusCode);
        
        if (statusCode == 400) { // Bad Request
            JsonDocument doc;
            deserializeJson(doc, client->readString());
            String errorCode = doc["errorCode"];
            if  (errorCode&& errorCode.compareTo("SVC_LOC")) {
                // Faulty location id, fetch new one
                return ProgramCodes::FAULTY_ORIGINID;
            }
        }
        else if (statusCode != 200) {
            Serial.println("Error: Bad response from server.");
            return ProgramCodes::BAD_REQUEST; // Stop further processing
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
          sizeOfResponse = size.toInt();
          Serial.println("size: " + size);

        }
    }
    if (sizeOfResponse > 8000) {
      return ProgramCodes::TOO_LARGE_REQUEST;
    }

    JsonDocument filter;
    filter["Departure"] = true;
    // filter["time"] = true;
    // filter["date"] = true;
    JsonDocument doc;
    
    DeserializationError error = deserializeJson(doc, *client, DeserializationOption::Filter(filter));
    client->stop();
    
    if (error) {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      delay(10000);
      return ProgramCodes::JSON_PARSING_FAIL;
    }

    Serial.println("JSON parsed correctly!");
    JsonArray departures = doc["Departure"];
    if (departures.isNull()) {
        // No departures were found, fetch from next day
        return ProgramCodes::NO_TRIPS;
    }
    uint8_t i = 0;
    // insert times into buffer
    for(JsonObject v : departures) {
      if (i >= 2) break; // boundary checking
      Serial.println("Departure here");
      String dir = v["direction"];
      bool to = (dir == "Hou Havn (færge)" ? true : false);
      time_t milliseconds = stringToUnixTime(v["date"], v["time"]);
      buffer[i].timeStamp = milliseconds;
      buffer[i].to = to;
      i++;
    }

    return ProgramCodes::SUCCESSFULL;
}

time_t stringToUnixTime(const char* dateStr, const char* timeStr) {
    char dateTimeStr[50]; // Ensure the buffer is large enough
    snprintf(dateTimeStr, sizeof(dateTimeStr), "%s %s", dateStr, timeStr);

    struct tm timeinfo = {};
    strptime(dateTimeStr, "%Y-%m-%d %H:%M:%S", &timeinfo);
    time_t seconds = mktime(&timeinfo);
    return seconds;
}