#include <Arduino.h>
#include <ESP32Time.h>

#ifndef TIMESERVICE_H
#define TIMESERVICE_H

class TimeService
{
private:
    static int const OFFSET;

public:
    TimeService(ESP32Time *rtc);
    uint64_t getTime();
    void setServerTime(uint64_t currentServerTime);
    void setup();
    static const String endpoint;

private:
    ESP32Time *rtc;
};

#endif // TIMESERVICE_H