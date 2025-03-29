#ifndef _RP_CALLS_H
#define _RP_CALLS_H

#include <WiFi.h>
#include "RTC.h"

/* 
    Struct for each element in buffer
*/
struct datetimeelement {
    time_t timeStamp;
    String stringTime;
    bool to; // 0 is hou, 1 is aarhus
};

struct datetimebuffer {
    datetimeelement buffer[4];
    uint8_t size;
    String lastDate;
    String lastTime;
};

enum class ProgramCodes {
    SUCCESSFULL,
    ERROR,
    WIFI_ERR,
    BAD_REQUEST,
    HANDSHAKE_FAIL,
    FAULTY_ORIGINID,
    NO_TRIPS,
    JSON_PARSING_FAIL,
    CLIENT_TIMEOUT,
    TOO_LARGE_REQUEST
};

time_t stringToUnixTime(const char* dateStr, const char* timeStr);
String urlEncodeUTF8(String str);
ProgramCodes searchLocation(String searchInput, String* placeId, WiFiClient *client, char api_key[]);
ProgramCodes searchTrip(String from, WiFiClient* client, char api_key[], datetimebuffer* buffer, 
                        int duration = 500, bool useLastDateTime = false);
void sortBuffer(datetimebuffer &buffer, time_t currentTime);
const char* incrementMinutes(const char* timeStr);
String convertMillisToDateTime(long long milliseconds);

#endif //_RP_CALLS_H