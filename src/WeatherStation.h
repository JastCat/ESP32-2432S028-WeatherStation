#ifndef _WEATHER_STATION_H
#define _WEATHER_STATION_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include "ArialRounded14.h"
#include "ArialRounded36.h"
#include "weatherIcons.h"

#define VW_Version "v. 1.00"

struct sensorData {
  float t = 0.0;
  float h = 0.0;
  float b = 0.0;
  bool is_update = false;
};

// Objects to manipulate display
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Network and Web app related
WiFiManager wifiManager;
String staHostname = "WeatherStation";

// Static IP (коментуємо для DHCP)
// char static_ip[16] = "192.168.1.100";
// char static_gw[16] = "192.168.1.1";
// char static_mask[16] = "255.255.255.0";
// char static_dns[16] = "8.8.8.8";

// Краків
const String timeZone = "Europe/Warsaw";
const String townName = "Krakow";
const String townLat = "50.06143";
const String townLon = "19.93658";

// OpenWeatherMap API
const String owmApiKey = "5840935ef5e188a4502b270103491987";

WebServer server(80);

// API для часу
String timeServer = "https://timeapi.io/api/time/current/zone?timeZone=Europe/Warsaw";

// Weather API (OpenWeatherMap)
String weatherServer = "http://api.openweathermap.org/data/2.5/weather?lat=" + townLat 
                       + "&lon=" + townLon 
                       + "&units=metric&appid=" + owmApiKey;

const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
ESP32Time rtc(0); 

struct DateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  long microsecond;
};

#define WS_BLACK TFT_BLACK
#define WS_WHITE TFT_WHITE
#define WS_YELLOW TFT_YELLOW
#define WS_BLUE 0x7E3C

// forward declarations
bool drawTime(void *);
void drawSensor(float t, float h, float b, short tempColor);
void drawBatLevel(TFT_eSprite &spr, int sprX, int sprY, int level);
void drawForecast(int wmo, float minT, float maxT, short rainProba);
const uint16_t* getIconFromWMO(int wmo);
String getDescriptionFromWMO(int wmo);
int convertOWMtoWMO(int owmCode);
bool getTime(void *);
DateTime parseISO8601(const String& iso8601);
bool getSensor(void *);
bool getForecast(void *);
void setupApi();
void handlePost();

#endif