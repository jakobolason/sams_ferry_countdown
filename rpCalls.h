#ifndef _RP_CALLS_H
#define _RP_CALLS_H

#include <WiFi.h>
#include "RTC.h"

/* Holds time slot for both destinations
 * And boolean for if they exist
 * !Notice: when time has expired, boolean should be set to false
*/
struct datetimeBuffers {
    time_t aarhusTime;
    time_t houTime;
    bool aarhusThere;
    bool houThere;
};

enum class ProgramCodes {
    SUCCESSFULL,
    BAD_REQUEST,
    HANDSHAKE_FAIL,
    FAULTY_ORIGINID,
    NO_TRIPS,
    JSON_PARSING_FAIL,
    CLIENT_TIMEOUT,
};

time_t stringToUnixTime(const char* dateStr, const char* timeStr);
String urlEncodeUTF8(String str);
ProgramCodes searchLocation(String searchInput, String* placeId, WiFiClient *client, char api_key[]);
ProgramCodes searchTrip(String from, WiFiClient* client, char api_key[], datetimeBuffers* buffer, int duration = 500);

#endif //_RP_CALLS_H