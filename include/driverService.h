#include <Arduino.h>
#include <HTTPClient.h>

#ifndef DRIVERSERVICE_H
#define DRIVERSERVICE_H

class DriverService
{
public:
    DriverService(
        WiFiClient *client);
    int fetchData(uint8_t *drivers);
    static const String endpoint;

private:
    SemaphoreHandle_t *xMutex;
    WiFiClient *client;
};

#endif // DRIVERSERVICE_H