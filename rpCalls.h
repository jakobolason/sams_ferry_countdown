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

time_t stringToUnixTime(const char* dateStr, const char* timeStr);
String urlEncodeUTF8(String str);
void searchLocation(String searchInput, String* placeId, WiFiClient *client, char api_key[]);
void searchTrip(String from, WiFiClient* client, char api_key[], datetimeBuffers* buffer);

#endif //_RP_CALLS_H