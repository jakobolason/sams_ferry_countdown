
#include <Wifi.h>
#include "RTC.h"

#include "timeSync.h"

IPAddress timeServer(162, 159, 200, 123); // pool.ntp.org NTP server
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address) {
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    Udp.beginPacket(address, 123); //NTP requests are to port 123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

/**
 * Calculates the current unix time, that is the time in seconds since Jan 1 1970.
 * It will try to get the time from the NTP server up to `maxTries` times,
 * then convert it to Unix time and return it.
 * You can optionally specify a time zone offset in hours that can be positive or negative.
*/
unsigned long getUnixTime(int8_t timeZoneOffsetHours, uint8_t maxTries){
    // Try up to `maxTries` times to get a timestamp from the NTP server, then give up.
    for (size_t i = 0; i < maxTries; i++){
        sendNTPpacket(timeServer); // send an NTP packet to a time server
        // wait to see if a reply is available
        delay(1000);

        if (Udp.parsePacket()) {
            Serial.println("Packet received.");
            Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

            //the timestamp starts at byte 40 of the received packet and is four bytes,
            //or two words, long. First, extract the two words:
            unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
            
            // Combine the four bytes (two words) into a long integer
            // this is NTP time (seconds since Jan 1 1900):
            unsigned long secsSince1900 = highWord << 16 | lowWord;

            // Now convert NTP time into everyday time:
            // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
            const unsigned long seventyYears = 2208988800UL;
            unsigned long secondsSince1970 = secsSince1900 - seventyYears + (timeZoneOffsetHours * 3600);
            return secondsSince1970;
        } else {
            Serial.println("Packet not received. Trying again.");
        }
    }
    return 0;
}

void updateTime(){
    yield();  
    Serial.println("\nStarting connection to NTP server...");
    auto unixTime = getUnixTime(TIMEZONE_OFFSET_HOURS, 25);
    Serial.print("Unix time = ");
    Serial.println(unixTime);
    if(unixTime == 0){
        unixTime = getUnixTime(2,25);
    }
    RTCTime timeToSet = RTCTime(unixTime);
    RTC.setTime(timeToSet);
    Serial.println("Time updated.");
}

void beginUDP() {
    Udp.begin(LOCAL_PORT);
}