#include <Arduino.h>
#include "WeatherStation.h"
#include <arduino-timer.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

// ==================== RGB LED ПІНИ ====================
#define RGB_RED 16
#define RGB_GREEN 4
#define RGB_BLUE 17

// ==================== LDR НАЛАШТУВАННЯ ДЛЯ ПІДСВІТКИ ====================
#define LDR_PIN 34
#define LDR_DARK 0
#define LDR_LIGHT 200
#define MIN_BRIGHTNESS 15
#define MAX_BRIGHTNESS 200

// ==================== BMP280 ====================
#define I2C_SDA 27
#define I2C_SCL 22
#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BMP280 bmp;
bool bmpAvailable = false;
float bmpTemperature = 0.0;
float bmpPressure = 0.0;

sensorData fromSensor;
int watchDog = 0;
auto timer = timer_create_default();

// ==================== ФУНКЦІЯ RGB ====================
void setRGBbyTemperature(float temp) {
  int red = 0, green = 0, blue = 0;
  
  if (temp < -5) { red = 0; green = 0; blue = 255; }
  else if (temp < 0) { red = 0; green = map(temp, -5, 0, 0, 100); blue = 255; }
  else if (temp < 10) { red = 0; green = map(temp, 0, 10, 100, 255); blue = map(temp, 0, 10, 255, 100); }
  else if (temp < 18) { red = 0; green = 255; blue = map(temp, 10, 18, 100, 51); }
  else if (temp < 20) { red = map(temp, 18, 20, 0, 100); green = 255; blue = 0; }
  else if (temp < 25) { red = map(temp, 20, 25, 100, 255); green = 255; blue = 0; }
  else if (temp < 30) { red = 255; green = map(temp, 25, 30, 255, 100); blue = 0; }
  else { red = 255; green = 0; blue = 0; }
  
  red = constrain(red, 0, 255);
  green = constrain(green, 0, 255);
  blue = constrain(blue, 0, 255);
  
  analogWrite(RGB_RED, red);
  analogWrite(RGB_GREEN, green);
  analogWrite(RGB_BLUE, blue);
  
  Serial.printf("🌡️ Temp: %.1f°C → RGB: %d,%d,%d\n", temp, red, green, blue);
}

// ==================== ЯСКРАВІСТЬ ЕКРАНУ ====================
void updateScreenBrightness() {
  int ldrValue = analogRead(LDR_PIN);
  ldrValue = constrain(ldrValue, LDR_DARK, LDR_LIGHT);
  int brightness = map(ldrValue, LDR_DARK, LDR_LIGHT, MAX_BRIGHTNESS, MIN_BRIGHTNESS);
  brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
  analogWrite(21, brightness);
  Serial.printf("💡 LDR: %d → Brightness: %d\n", ldrValue, brightness);
}

// ==================== ОНОВЛЕННЯ ВНУТРІШНЬОЇ ТЕМПЕРАТУРИ ====================
bool updateInternalTemperature(void *) {
  if (bmpAvailable) {
    bmpTemperature = bmp.readTemperature();
    bmpPressure = bmp.readPressure() / 100.0F;
    Serial.printf("🏠 Внутрішня: %.1f°C, Тиск: %.1f hPa\n", bmpTemperature, bmpPressure);
    // ОНОВЛЮЄМО ТІЛЬКИ НИЖНЮ ЧАСТИНУ
    drawForecast(0, 0, 0, 0);
  }
  return true;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.print("WeatherStation ");
  Serial.println(VW_Version);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!bmp.begin(0x76)) {
    Serial.println("❌ BMP280 not found!");
    bmpAvailable = false;
  } else {
    bmpAvailable = true;
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("✅ BMP280 initialized!");
  }

  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setHostname(staHostname);
  
  if(!wifiManager.autoConnect("ESP32_Weather", "12345678")) {
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  }

  Serial.println("INFO: connected to WiFi");

  setupApi();
  getTime(NULL);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(WS_BLACK);
  
  pinMode(21, OUTPUT);
  updateScreenBrightness();

  fromSensor.t = 0.0;
  fromSensor.h = 0.0;
  fromSensor.b = 100;
  
  // ПОЧАТКОВЕ МАЛЮВАННЯ
  drawTime(NULL);
  drawSensor(fromSensor.t, fromSensor.h, fromSensor.b, TFT_RED);
  drawForecast(0, 0, 0, 0);
  
  getForecast(NULL);
  
  timer.every(500, drawTime);
  timer.every(60 * 60 * 1000, getTime);
  timer.every(15 * 60 * 1000, getForecast);
  timer.every(180 * 1000, updateInternalTemperature, nullptr);
  timer.every(5000, [](void*) { 
    updateScreenBrightness();
    return true; 
  }, nullptr);
}

void loop() {
  server.handleClient();
  timer.tick();
}

// ==================== ФУНКЦІЇ ЧАСУ ====================
DateTime parseISO8601(const String& iso8601) {
  DateTime dt;
  sscanf(iso8601.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d",
         &dt.year, &dt.month, &dt.day,
         &dt.hour, &dt.minute, &dt.second);
  return dt;
}

bool getTime(void *) {
  Serial.println("⏰ Fetching time...");
  HTTPClient http;
  http.begin(timeServer);
  http.setTimeout(5000);
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, payload);
    if (!error) {
      String datetime = jsonDoc["dateTime"];
      if (datetime.length() > 0) {
        DateTime dt = parseISO8601(datetime);
        rtc.setTime(dt.second, dt.minute, dt.hour, dt.day, dt.month, dt.year);
        Serial.println("✅ Time updated");
      }
    }
  }
  http.end();
  return true;
}

bool getSensor(void *) {
  watchDog++;
  if (fromSensor.is_update) {
    fromSensor.is_update = false;
    watchDog = 0;
    drawSensor(fromSensor.t, fromSensor.h, fromSensor.b, WS_WHITE);
  } else {
    if (watchDog > 15) drawSensor(fromSensor.t, fromSensor.h, fromSensor.b, TFT_RED);
  }
  return true;
}

int convertOWMtoWMO(int owmCode) {
  if (owmCode == 800) return 0;
  if (owmCode == 801) return 1;
  if (owmCode == 802) return 2;
  if (owmCode == 803 || owmCode == 804) return 3;
  if (owmCode >= 200 && owmCode < 300) return 95;
  if (owmCode >= 300 && owmCode < 400) return 61;
  if (owmCode >= 500 && owmCode < 600) return 61;
  if (owmCode >= 600 && owmCode < 700) return 71;
  if (owmCode >= 700 && owmCode < 800) return 45;
  return 0;
}

bool getForecast(void *) {
  Serial.println("🌤️ Fetching weather data...");
  
  if (bmpAvailable) {
    bmpTemperature = bmp.readTemperature();
    bmpPressure = bmp.readPressure() / 100.0F;
    Serial.printf("🏠 Внутрішня: %.1f°C, Тиск: %.1f hPa\n", bmpTemperature, bmpPressure);
  }
  
  HTTPClient http;
  http.begin(weatherServer);
  http.setTimeout(10000);
  int httpResponseCode = http.GET();

  if (httpResponseCode == 200) {
    String payload = http.getString();
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, payload);
    
    if (!error) {
      int owmCode = jsonDoc["weather"][0]["id"] | 0;
      int wmoCode = convertOWMtoWMO(owmCode);
      float currTemp = jsonDoc["main"]["temp"] | 0.0;
      int currHumi = jsonDoc["main"]["humidity"] | 0;
      float minT = jsonDoc["main"]["temp_min"] | 0.0;
      float maxT = jsonDoc["main"]["temp_max"] | 0.0;
      int rainProba = jsonDoc["clouds"]["all"] | 0;
      
      Serial.printf("🌡️ Зовнішня: %.1f°C, Вологість: %d%%, Min: %.1f, Max: %.1f\n", 
                    currTemp, currHumi, minT, maxT);
      
      fromSensor.t = currTemp;
      fromSensor.h = currHumi;
      fromSensor.is_update = true;
      
      setRGBbyTemperature(currTemp);
      
      // ПРИМУСОВЕ ОНОВЛЕННЯ ВСЬОГО ЕКРАНУ
      drawTime(NULL);
      drawSensor(fromSensor.t, fromSensor.h, fromSensor.b, WS_WHITE);
      drawForecast(wmoCode, minT, maxT, rainProba);
      
      Serial.println("✅ Display updated");
    }
  } else {
    Serial.printf("❌ HTTP error: %d\n", httpResponseCode);
  }
  http.end();
  return true;
}

// ==================== WEB API ====================
void setupApi() {
  Serial.println("INFO: Web server started");
  server.on("/post-data", HTTP_POST, handlePost);
  server.begin();
}

void handlePost() {
  if (server.hasArg("plain")) {
    String payload = server.arg("plain");
    JsonDocument jsonDoc;
    deserializeJson(jsonDoc, payload);
    fromSensor.t = jsonDoc["temperature"] | fromSensor.t;
    fromSensor.h = jsonDoc["humidity"] | fromSensor.h;
    fromSensor.b = jsonDoc["batt_per"] | fromSensor.b;
    fromSensor.is_update = true;
    drawSensor(fromSensor.t, fromSensor.h, fromSensor.b, WS_WHITE);
    server.send(200, "application/json", "{}");
  } else {
    server.send(200, "application/json", "{}");
  }
}

// ==================== ПОГОДНІ ІКОНКИ ====================
const uint16_t* getIconFromWMO(int wmo) {
  if (wmo == 0) return sunny;
  if (wmo == 1) return mainlysunny;
  if (wmo == 2) return partlycloudy;
  if (wmo == 3) return cloudy;
  if (wmo == 45 || wmo == 48) return fog;
  if (wmo == 51 || wmo == 61 || wmo == 80 || wmo == 83) return lightrain;
  if (wmo == 53 || wmo == 55 || wmo == 63 || wmo == 65) return rain;
  if (wmo == 56 || wmo == 57 || wmo == 66 || wmo == 67) return freezingrain;
  if (wmo == 71) return lightsnow;
  if (wmo == 73 || wmo == 75 || wmo == 77) return snow;
  if (wmo == 85 || wmo == 86) return snow;
  if (wmo >= 95 && wmo <= 99) return storms;
  return unknown;
}

String getDescriptionFromWMO(int wmo) {
  if (wmo == 0) return "Sunny";
  if (wmo == 1) return "Mainly Sunny";
  if (wmo == 2) return "Partly Cloudy";
  if (wmo == 3) return "Cloudy";
  if (wmo == 45 || wmo == 48) return "Fog";
  if (wmo == 51 || wmo == 61 || wmo == 80 || wmo == 83) return "Light Rain";
  if (wmo == 53 || wmo == 55 || wmo == 63 || wmo == 65) return "Rain";
  if (wmo == 56 || wmo == 57 || wmo == 66 || wmo == 67) return "Freezing Rain";
  if (wmo == 71) return "Light Snow";
  if (wmo == 73 || wmo == 75 || wmo == 77) return "Snow";
  if (wmo == 85 || wmo == 86) return "Snow Showers";
  if (wmo >= 95 && wmo <= 99) return "Thunderstorm";
  return "Unknown";
}

// ==================== МАЛЮВАННЯ ====================
bool drawTime(void *) {
  struct tm now = rtc.getTimeStruct();
  char tempo[20];
  
  sprite.createSprite(320, 50);
  sprite.fillSprite(WS_BLACK);
  sprite.setTextColor(WS_WHITE);
  
  sprite.loadFont(arialround14);
  sprite.setTextDatum(MC_DATUM);
  sprintf(tempo, "%s %02d %s %4d", days[rtc.getDayofWeek()], now.tm_mday, months[now.tm_mon], now.tm_year + 1900);
  sprite.drawString(tempo, 160, 15);
  
  sprintf(tempo, "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
  sprite.loadFont(arialround36);
  sprite.drawString(tempo, 160, 40);
  
  sprite.pushSprite(0, 0);
  sprite.deleteSprite();
  return true;
}

void drawSensor(float t, float h, float b, short tempColor) {
  char tempo[10];
  
  sprite.createSprite(170, 136);
  sprite.fillSprite(WS_BLACK);
  sprite.setTextColor(WS_BLUE);
  
  sprite.loadFont(arialround14);
  sprite.setTextDatum(CR_DATUM);
  sprite.drawString(townName, 160, 20);
  
  sprite.loadFont(arialround36);
  sprite.setTextColor(tempColor);
  sprintf(tempo, "%.1f C", t);
  sprite.setTextDatum(MC_DATUM);
  sprite.drawString(tempo, 60, 75);
  
  sprite.loadFont(arialround14);
  sprintf(tempo, "%d %%", (int)(h + 0.5));
  sprite.drawString(tempo, 60, 110);
  
  drawBatLevel(sprite, 130, 60, (int)(b + 0.5));
  
  sprite.pushSprite(150, 50);
  sprite.deleteSprite();
}

void drawBatLevel(TFT_eSprite &spr, int sprX, int sprY, int level) {
  spr.drawFastVLine(sprX, sprY, 58, WS_WHITE);
  spr.drawFastVLine(sprX + 30, sprY, 58, WS_WHITE);
  spr.drawFastVLine(sprX + 10, sprY - 4, 4, WS_WHITE);
  spr.drawFastVLine(sprX + 20, sprY - 4, 4, WS_WHITE);
  spr.drawFastHLine(sprX, sprY, 10, WS_WHITE);
  spr.drawFastHLine(sprX + 10, sprY - 4, 10, WS_WHITE);
  spr.drawFastHLine(sprX + 20, sprY, 10, WS_WHITE);
  spr.drawFastHLine(sprX, sprY + 58, 30, WS_WHITE);
  
  if (level > 85) spr.fillRect(sprX + 2, sprY + 2, 27, 10, TFT_GREEN);
  if (level > 65) spr.fillRect(sprX + 2, sprY + 13, 27, 10, TFT_GREEN);
  if (level > 45) spr.fillRect(sprX + 2, sprY + 24, 27, 10, TFT_GREEN);
  if (level > 25) spr.fillRect(sprX + 2, sprY + 35, 27, 10, TFT_ORANGE);
  spr.fillRect(sprX + 2, sprY + 46, 27, 10, TFT_RED);
}

void drawForecast(int wmo, float minT, float maxT, short rainProba) {
  char tempo[20];
  
  // Іконка погоди
  sprite.createSprite(150, 150);
  sprite.setSwapBytes(true);
  sprite.fillSprite(WS_BLACK);
  sprite.pushImage(15, 15, 128, 128, getIconFromWMO(wmo));
  sprite.pushSprite(0, 45);
  sprite.deleteSprite();
  
  // Текстовий блок
  sprite.createSprite(320, 75);
  sprite.fillSprite(WS_BLACK);
  
  sprite.setTextColor(TFT_CYAN);
  sprite.loadFont(arialround14);
  sprintf(tempo, "House %.1f C", bmpTemperature);
  sprite.drawString(tempo, 50, 15);
  
  sprite.setTextColor(TFT_YELLOW);
  sprintf(tempo, "Pressure %.0f hPa", bmpPressure);
  sprite.drawString(tempo, 160, 15);
  
  sprite.setTextColor(WS_YELLOW);
  sprite.setTextDatum(CR_DATUM);
  sprite.drawString(getDescriptionFromWMO(wmo), 310, 15);
  
  sprite.setTextColor(WS_BLUE);
  sprintf(tempo, "Min %.1f C", minT);
  sprite.setTextDatum(CL_DATUM);
  sprite.drawString(tempo, 10, 45);
  
  sprintf(tempo, "Max %.1f C", maxT);
  sprite.setTextDatum(MC_DATUM);
  sprite.drawString(tempo, 160, 45);
  
  sprintf(tempo, "Clouds %d%%", rainProba);
  sprite.setTextDatum(CR_DATUM);
  sprite.drawString(tempo, 310, 45);
  
  sprite.pushSprite(0, 185);
  sprite.deleteSprite();
}