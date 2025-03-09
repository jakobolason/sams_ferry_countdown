#ifndef _TIME_SYNC_H
#define _TIME_SYNC_H

#define TIMEZONE_OFFSET_HOURS 1
constexpr unsigned int LOCAL_PORT = 2390;  // local port to listen for UDP packets
constexpr int NTP_PACKET_SIZE = 48; // NTP timestamp is in the first 48 bytes of the message

void sendNTPpacket(IPAddress& address);
unsigned long getUnixTime(int8_t timeZoneOffsetHours = 0, uint8_t maxTries = 5);
void updateTime();
void beginUDP();

#endif //_TIME_SYNC_H