#include <Arduino.h>

#include <WiFiMulti.h>
#include <WebServer.h>

#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "canvas_htm.h"

#include "stream.hpp"
#include "camera.hpp"
#include "imageParams.hpp"

#include "../private/wifiLogin.h"
// creat a new wifiLogin.h in your project:
//
//#pragma once
//
//struct WifiLogin {
//    const char *ssid;
//    const char *password;
//};
//
//WifiLogin wifiLogins[]{{"ssid1", "password1"},
//                       {"ssid2", "password2"},
//                       {"ssid3", "password3"}};


WebServer wifiServer(80);
WiFiMulti wiFiMulti;

// ==== Handle connection request from clients ===============================
void steamServerHtml()
{
    Serial.printf("new client, sending base of html page\n");

    auto client = wifiServer.client();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print(canvas_htm);
    client.println();
}


// ==== Handle connection request from clients ===============================
void handleNotFound()
{
    auto client = wifiServer.client();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print("try another one!");
    client.println();
}

[[noreturn]] void captureProcess(void *parameter)
{
    Serial.println("Start captureProcess task");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static constexpr uint8_t FPS{50};
    const TickType_t xFrequency{pdMS_TO_TICKS(1000 / FPS)};

    auto *frame = static_cast<Frame *>(parameter);

    Camera cameraTool{frame};

    for (;;)
    {
        cameraTool.captureImage();
        taskYIELD();

        uint8_t element;

        for (int i = 0; i < Frame::queueSize; i++)
        {
            if (xQueueReceive(frame->queueFrameSize, &element, 5))
            {
                Serial.printf("resize to: %d\n", element);
                Camera::resizeCamera(element);
            }
        }
        taskYIELD();
    }
}

[[noreturn]] void mainTask(void *parameter)
{
    Streaming::StreamOverWebsocket streamOverWebsocket{};

    auto *frameState = static_cast<Frame *>(parameter);

    for (;;)
    {
        wifiServer.handleClient();
        streamOverWebsocket.loop(frameState);

        taskYIELD();
    }
}

[[noreturn]] void idlTask0(void *parameter)
{
    for (;;)
    {
        Serial.printf("Idle on core %d\n", xPortGetCoreID());
        taskYIELD();
    }
}

[[noreturn]] void idlTask1(void *parameter)
{
    for (;;)
    {
        Serial.printf("Idle on core %d\n", xPortGetCoreID());
        taskYIELD();
    }
}

// ==== SETUP method ==================================================================
void setup()
{
    // Setup Serial connection:
    Serial.begin(115200);
    delay(1000); // wait for a second to let Serial connect

    for (const auto &wifiLogin : wifiLogins)
    {
        wiFiMulti.addAP(wifiLogin.ssid, wifiLogin.password);
    }

    while (wiFiMulti.run() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    //  Registering webserver handling routines
    wifiServer.on("/stream", HTTP_GET, steamServerHtml);
    wifiServer.onNotFound(handleNotFound);

    //  Starting webserver
    wifiServer.begin();

    Serial.print("\nConnected to ");
    Serial.print(WiFi.SSID());
    Serial.print(" with IP address: ");
    Serial.println(WiFi.localIP());

    Serial.printf("Web server started, open %s in a web browser\n", WiFi.localIP().toString().c_str());

    auto *frameState = new Frame;

    TaskHandle_t tCaptureImage;
    TaskHandle_t tMainTask;

    xTaskCreatePinnedToCore(
            captureProcess,     /* Function to implement the task */
            "captureProcess",            /* Name of the task */
            30000,              /* Stack size in words */
            frameState,         /* Task input parameter */
            2,                  /* Priority of the task */
            &tCaptureImage,     /* Task handle. */
            0);                 /* Core where the task should run */

    xTaskCreatePinnedToCore(
            mainTask,     /* Function to implement the task */
            "mainTask",            /* Name of the task */
            10000,              /* Stack size in words */
            frameState,         /* Task input parameter */
            2,                  /* Priority of the task */
            &tMainTask,      /* Task handle. */
            1);                 /* Core where the task should run */
}

void loop()
{
    vTaskDelay(1000);
}
