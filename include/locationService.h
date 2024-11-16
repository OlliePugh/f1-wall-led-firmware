#include <Arduino.h>
#include <ESP32Time.h>
#include <HTTPClient.h>

#ifndef LOCATIONSERVICE_H
#define LOCATIONSERVICE_H

struct LocationDto
{
    int driverNumer;
    float position;
    uint64_t occuredAt;
};

class LocationService
{
public:
    LocationService(
        ESP32Time *rtc,
        WiFiClient *client);
    int fetchData(LocationDto *locations);
    static const String endpoint;

private:
    ESP32Time *rtc;
    WiFiClient *client;
};

#endif // LOCATIONSERVICE_H