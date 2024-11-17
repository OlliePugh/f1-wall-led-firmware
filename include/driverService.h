#include <Arduino.h>
#include <HTTPClient.h>

#ifndef DRIVERSERVICE_H
#define DRIVERSERVICE_H

struct Driver
{
    uint8_t driverNumber;
    String name;
};

class DriverService
{
public:
    DriverService(
        WiFiClient *client);
    int fetchData(Driver *drivers);
    static const String endpoint;

private:
    SemaphoreHandle_t *xMutex;
    WiFiClient *client;
};

#endif // DRIVERSERVICE_H