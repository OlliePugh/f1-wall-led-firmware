#include "timeService.h"
#include <ArduinoJson.h>
#include <ESP32Time.h>

const String TimeService::endpoint = "time.google.com";

TimeService::TimeService(ESP32Time *rtc)
{
    this->rtc = rtc;
}

uint64_t TimeService::getTime()
{
    return static_cast<uint64_t>(rtc->getEpoch()) * 1000ULL + rtc->getMillis();
}

// NOTE: This will only work on the first run because the offset will be used to calculate the new offset
// if that makes any sense
void TimeService::setServerTime(uint64_t currentServerTime)
{
    // TODO: fix this piece of shit
    rtc->setTime(currentServerTime / 1000ULL, (currentServerTime % 1000ULL));
}

const int TimeService::OFFSET = 0;

void TimeService::setup()
{
    configTime(TimeService::OFFSET, 0, endpoint.c_str());
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        rtc->setTimeStruct(timeinfo);
    }
}