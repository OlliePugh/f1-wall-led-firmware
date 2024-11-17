#include "driverService.h"
#include <ArduinoJson.h>

const String DriverService::endpoint = "http://192.168.1.187:8080/drivers";

DriverService::DriverService(WiFiClient *client)
{
    this->client = client;
}

int DriverService::fetchData(Driver *drivers)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected");
        return 0;
    }
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(*client, DriverService::endpoint);
    int httpResponseCode = http.GET();

    int i = 0;

    if (httpResponseCode > 0)
    {
        Serial.print("Driver HTTP Response code: ");
        Serial.println(httpResponseCode);
        JsonDocument doc;
        deserializeJson(doc, http.getStream());
        for (JsonVariant driver : doc.as<JsonArray>())
        {
            int driverNumber = driver["driverNumber"].as<int>();
            String name = driver["nameAcronym"].as<String>();
            Driver driverInstance = {driverNumber, name};
            drivers[i++] = driverInstance;
        }
    }
    else
    {
        Serial.print("Driver error code: ");
        Serial.println(httpResponseCode);
    }
    Serial.println("Finished fetching drivers");
    http.end();
    return i;
}