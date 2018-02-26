﻿#include "Adafruit_SSD1306.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <BlynkSimpleEsp8266.h>

#define project "HomeSprinkler"

#define PUMP_PIN D5          /* вывод с ШИМ  */       //  объявляем константу с указанием номера вывода, к которому подключен силовой ключ
#define TIME_WAITING 60                               //  объявляем константу для хранения времени ожидания после полива               (в секундах)     от 0 до 99


enum modState_t { MODE_OFF, MODE_IDLE, MODE_ACTIVE, MODE_SPRINKLING };

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
const char blynk_auth[] = "3f2069fd89fb45e39f754b284a4ee42a";

#include <ESP8266mDNS.h>               // ArduinoOTA
#include <ArduinoOTA.h>

// SCL GPIO5
// SDA GPIO4
#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

ulong loop_timer = 0;                 // 0.1 sec loop timer
byte loop_count = 0;                  // loop counter

int pump = LOW;
uint16_t arrMoisture[10];                             //  объявляем массив для хранения 10 последних значений влажности почвы
uint32_t valMoisture;                                 //  объявляем переменную для расчёта среднего значения влажности почвы
uint32_t timWatering;                                 //  объявляем переменную для хранения времени начала последнего полива           (в миллисекундах)
uint32_t timSketch;                                   //  объявляем переменную для хранения времени прошедшего с момента старта скетча (в миллисекундах)
uint16_t timDuration = 5;    /* по умолчанию */       //  объявляем переменную для хранения длительности полива                        (в секундах)     от 0 до 99
uint16_t limMoisture = 0;    /* по умолчанию */       //  объявляем переменную для хранения пороговой влажности почвы                  (для вкл насоса) от 0 до 999
uint8_t  modState = 0;       /* при старте   */       //  объявляем переменную для хранения состояния устройства: 0-не активно, 1-ожидание, 2-активно, 3-полив, 4-установка пороговой влажности, 5-установка времени полива

void setup()
{
	// Initializing serial port for debugging purposes
	Serial.begin(115200);
	delay(10);

	pinMode(PUMP_PIN, OUTPUT);                        //  переводим вывод PUMP_PIN в режим выхода
	digitalWrite(PUMP_PIN, LOW);                      //  выключаем насос
	timWatering = 0;                                  //  сбрасываем время начала последнего полива

													  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
												// init done

												// Show image buffer on the display hardware.
												// Since the buffer is intialized with an Adafruit splashscreen
												// internally, this will display the splashscreen.
	display.display();
	delay(1000);

	// Clear the buffer.
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);

	display.println("Connecting to Wifi..");
	display.display();

	//WiFiManager
	//Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;
	//reset settings - for testing
	//wifiManager.resetSettings();

	//sets timeout until configuration portal gets turned off
	//useful to make it all retry or go to sleep
	//in seconds
	wifiManager.setTimeout(180);

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified name
	//here  "AutoConnectAP"
	//and goes into a blocking loop awaiting configuration
	if (!wifiManager.autoConnect(project)) {
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//if you get here you have connected to the WiFi
	display.println("connected...yeey :)");
	display.display();

	Blynk.config(blynk_auth);

	//OTA
	ArduinoOTA.onStart([]() {
		Serial.println("Start OTA");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
		delay(5000);
		ESP.restart();
	});
	ArduinoOTA.setHostname(project);
	ArduinoOTA.begin();
}

void set_pump(int state)
{
	int npump = state ? HIGH : LOW;
	if (pump != npump) {
		pump = npump;
		digitalWrite(PUMP_PIN, pump);
		Blynk.virtualWrite(0, pump);
	}
}

void every_second()
{
	uint16_t val = analogRead(A0);

	valMoisture = 0;
	for (int i = 0; i < 9; i++)
	{
		arrMoisture[i] = arrMoisture[i + 1];
	}
	arrMoisture[9] = val;
	for (int i = 0; i < 10; i++)
	{
		valMoisture += arrMoisture[i];
	}
	valMoisture /= 10; // вычисляем среднее значение влажности почвы

	Blynk.virtualWrite(1, valMoisture);

	// Clear the buffer.
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);

	display.print("Cur: ");
	display.println(valMoisture);
	display.print("Lim: ");
	display.println(limMoisture);
	display.println("Mode:");
	display.setTextColor(BLACK, WHITE);
	switch (modState)
	{
	case MODE_OFF:
		display.println("OFF");
		break;
	case MODE_IDLE:
		display.println("IDLE");
		break;
	case MODE_ACTIVE:
		display.println("ACTIVE");
		break;
	case MODE_SPRINKLING:
		display.println("SPRINKLING");
		break;
	default:
		break;
	}
	display.display();
}

void loop()
{
	timSketch = millis();                             //  читаем текущее время с момента старта скетча
	if (timWatering > timSketch) {
		timWatering = 0;                              //  обнуляем время начала последнего полива, если произошло переполнение millis()
	}

	if (millis() > loop_timer)
	{
		// every 0.1 second
		loop_timer = millis() + 100;
		loop_count++;

		if (loop_count == 10)
		{
			// every second
			loop_count = 0;
			every_second();
		}
	}

	if (WiFi.status() == WL_CONNECTED)
	{
		//ota loop
		ArduinoOTA.handle();

		Blynk.run();
	}

	//*******Управление устройством:*******
	switch (modState) {
	case MODE_OFF:                                    //  Устройство не активно
		if (timDuration && limMoisture) {             //  если заданы длительность полива и пороговая влажность
			modState = MODE_ACTIVE;
		}
		break;
	case MODE_IDLE:                                   //  Устройство в режиме ожидания (после полива)
		if (timDuration + TIME_WAITING - ((timSketch - timWatering) / 1000) <= 0) {
			modState = MODE_ACTIVE;                   //  если закончилось время ожидания
		}
		break;
	case MODE_ACTIVE:                                 //  Устройство активно
		if (!timDuration || !limMoisture) {           //  если не заданы длительность полива или пороговая влажность
			modState = MODE_OFF;
		}
		else if (valMoisture <= limMoisture) {        //  если текущая влажность почвы меньше пороговой
			timWatering = timSketch;
			modState = MODE_SPRINKLING;
			analogWrite(PUMP_PIN, 1023);
		}
		break;
	case MODE_SPRINKLING:                             //  Устройство в режиме полива
		if (timDuration - ((timSketch - timWatering) / 1000) <= 0) {
			modState = MODE_IDLE;                     //  если закончилось время полива
			analogWrite(PUMP_PIN, 0);
		}
		break;
	}

	//  yield();     // yield == delay(0), delay contains yield, auto yield in loop
	delay(0);  // https://github.com/esp8266/Arduino/issues/2021
}

// Every time we connect to the cloud...
BLYNK_CONNECTED() {
	// Request the latest state from the server
	//Blynk.syncVirtual(V2);

	// Alternatively, you could override server state using:
	Blynk.virtualWrite(0, pump);
	Blynk.virtualWrite(1, valMoisture);
	Blynk.virtualWrite(2, timDuration);
	Blynk.virtualWrite(3, limMoisture);
}

BLYNK_READ(0)
{
	Blynk.virtualWrite(0, pump);
}

BLYNK_WRITE(0) {
	int par = param.asInt();
	set_pump(par);
}

BLYNK_READ(1)
{
	Blynk.virtualWrite(1, valMoisture);
}

BLYNK_READ(2)
{
	Blynk.virtualWrite(2, timDuration);
}

BLYNK_WRITE(2) {
	timDuration = param.asInt();
	if (timDuration < 0) {
		timDuration = 0;
	}
	else if (timDuration > 99) {
		timDuration = 99;
	}
}

BLYNK_READ(3)
{
	Blynk.virtualWrite(3, limMoisture);
}

BLYNK_WRITE(3) {
	limMoisture = param.asInt();
	if (limMoisture < 0) {
		limMoisture = 0;
	}
	else if (limMoisture > 999) {
		limMoisture = 999;
	}
}
