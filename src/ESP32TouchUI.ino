#include <Arduino.h>

#include <WebSocketsServer.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "esp_camera.h"
#include "OV2640.h"
#include "canvas_htm.h"

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

#define APP_CPU 1
#define PRO_CPU 0

WebSocketsServer webSocket(81);    // create a websocket server on port 81
WebServer wifiServer(80);
WiFiMulti wiFiMulti;
constexpr uint8_t maxClients{10};
constexpr uint8_t FPS{50};

// frameSync semaphore is used to prevent streaming buffer as it is replaced with the next frame
SemaphoreHandle_t frameSync = NULL;
SemaphoreHandle_t sendWebSocketData = NULL;


// ===== rtos task handles =========================
// Streaming is implemented with 3 tasks:
TaskHandle_t tMainLoop;
TaskHandle_t tStreamgWebSocket;   // handles client connections to the webserver
TaskHandle_t tCaptureCamera;     // handles getting picture frames from the camera and storing them locally

// Current frame information
volatile size_t camSize{};    // size of the current frame, byte
volatile uint8_t *camBuf;      // pointer to the current frame

constexpr uint8_t invalidWebSocketId{0xFF};

struct StreamInfo
{
    uint8_t *buffer{};
    size_t len{};
    uint8_t webSocketClientId{invalidWebSocketId};
    bool isActive{false};
    bool isPilot{false};
    bool isPilotInformed{true};
};

StreamInfo streamInfoAllClients[maxClients];

uint8_t getIndexBywebsocketId(const uint8_t &webSocketClientId)
{
    for (const auto &streamInfoAllClient : streamInfoAllClients)
    {
        if (webSocketClientId == streamInfoAllClient.webSocketClientId)
        {
            return webSocketClientId;
        }
    }
    return invalidWebSocketId;
}

// ==== Memory allocator that takes advantage of PSRAM if present =======================
uint8_t *allocateMemory(uint8_t *aPtr, size_t aSize)
{

    //  Since current buffer is too small, free it
    if (aPtr != NULL)
    {
        free(aPtr);
    }

    uint8_t *ptr = NULL;
    ptr = (uint8_t *) ps_malloc(aSize);

    // If the memory pointer is NULL, we were not able to allocate any memory, and that is a terminal condition.
    if (ptr == NULL)
    {
        Serial.println("Out of memory!");
        delay(5000);
        ESP.restart();
    }

    Serial.printf("\nallocateMemory: free heap (start)  : %d\n", ESP.getFreeHeap());
    return ptr;
}


// ==== Actually stream content to all connected clients ========================
[[noreturn]] void taskSteamWebsocket(void *pvParameters)
{
    Serial.println("Init taskSteamWebsocket");
    constexpr uint8_t averageSize{FPS};
    uint16_t average[averageSize];
    uint8_t averageIndex{0};

    TickType_t xLastWakeTime;
    TickType_t xFrequency;

    taskYIELD();

    xLastWakeTime = xTaskGetTickCount();
    unsigned long lastMilli = xTaskGetTickCount();
    xFrequency = pdMS_TO_TICKS(1000 / FPS);

    uint8_t *bufferToSend = {NULL};
    size_t bufferToSendSize = {0};

    for (;;)
    {
        const unsigned long lastMilliTransWifi = xTaskGetTickCount();
        xSemaphoreTake(frameSync, portMAX_DELAY);
        if (bufferToSend == NULL && camSize > 0)
        {
            bufferToSend = allocateMemory(bufferToSend, camSize);
            bufferToSendSize = camSize;
        } else
        {
            if (camSize > bufferToSendSize)
            {
                bufferToSend = allocateMemory(bufferToSend, camSize);
                bufferToSendSize = camSize;
            }
        }
        memcpy(bufferToSend, (const void *) camBuf, bufferToSendSize);
        xSemaphoreGive(frameSync);

        for (auto &streamInfoAllClient : streamInfoAllClients)
        {
            if (streamInfoAllClient.isActive)
            {
                const bool successSend = webSocket.sendBIN(streamInfoAllClient.webSocketClientId, bufferToSend,
                                                           bufferToSendSize);
                if (not streamInfoAllClient.isPilotInformed)
                {
                    if (streamInfoAllClient.isPilot)
                    {
                        webSocket.sendTXT(streamInfoAllClient.webSocketClientId, "{\"isPilot\":1}");
                    } else
                    {
                        webSocket.sendTXT(streamInfoAllClient.webSocketClientId, "{\"isPilot\":0}");
                    }
                    streamInfoAllClient.isPilotInformed = true;
                }
                if (not successSend)
                {
                    Serial.println("Unsuccess sending");
                }
            }
        }

        const unsigned long currentTick = xTaskGetTickCount();
//        Serial.printf("taskSteamWebsocket: %lums, wifi send: %lums\n", currentTick - lastMilli,
//                      currentTick - lastMilliTransWifi);
//        lastMilli = currentTick;
        average[averageIndex++] = currentTick - lastMilliTransWifi;

        if (averageIndex == averageSize)
        {
            averageIndex = 0;
            uint32_t avgResult{};
            for (const auto &avg : average)
            {
                avgResult += avg;
            }
            Serial.printf("taskSteamWebsocket avg: %d\n", avgResult / averageSize);
        }

        taskYIELD();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ==== Handle connection request from clients ===============================
[[noreturn]] void taskCaptureImageFromCamera(void *pvParameters)
{
    Serial.println("Init taskCaptureImageFromCamera");

    TickType_t xLastWakeTime;

    //  A running interval associated with currently desired frame rate
    const TickType_t xFrequency = pdMS_TO_TICKS(1000 / FPS);

    //  Pointers to the 2 frames, their respective sizes and index of the current frame
    uint8_t *fbs[2] = {NULL, NULL};
    size_t fSize[2] = {0, 0};
    int ifb = 0;

    //=== loop() section  ===================
    xLastWakeTime = xTaskGetTickCount();

    unsigned long lastMilli = xTaskGetTickCount();

    for (;;)
    {
//        Serial.print("Capturing image");
        //  Grab a frame from the camera and query its size
        camera_fb_t *fb = NULL;

        fb = esp_camera_fb_get();
        size_t s = fb->len;

        //  If frame size is more that we have previously allocated - request  125% of the current frame space
        if (s > fSize[ifb])
        {
            fSize[ifb] = s + s;
            fbs[ifb] = allocateMemory(fbs[ifb], fSize[ifb]);
        }

        //  Copy current frame into local buffer
        auto b = (uint8_t *) fb->buf;
        memcpy(fbs[ifb], b, s);
        esp_camera_fb_return(fb);

//        const unsigned long currentTick = xTaskGetTickCount();
//        Serial.printf("taskCaptureImageFromCamera: %lums\n", currentTick - lastMilli);
//        lastMilli = currentTick;

        //  Let other tasks run and wait until the end of the current frame rate interval (if any time left)
        taskYIELD();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        //  Only switch frames around if no frame is currently being streamed to a client
        //  Wait on a semaphore until client operation completes
        //    xSemaphoreTake( frameSync, portMAX_DELAY );

        //  Do not allow frame copying while switching the current frame
        xSemaphoreTake(frameSync, xFrequency);
        camBuf = fbs[ifb];
        camSize = s;
        ifb++;
        ifb &= 1;  // this should produce 1, 0, 1, 0, 1 ... sequence
        //  Let anyone waiting for a frame know that the frame is ready
        xSemaphoreGive(frameSync);

        //  Immediately let other (streaming) tasks run
        taskYIELD();
    }
}

// ==== Handle connection request from clients ===============================
void steamServerHtml(void)
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
void handleNotFound(void)
{
    auto client = wifiServer.client();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print("try another one!");
    client.println();
}


// ======== Server Connection Handler Task ==========================
[[noreturn]] void mainLoop(void *pvParameters)
{
    TickType_t xLastWakeTime;
    constexpr uint8_t WSINTERVAL = 50; // We will handle web client requests every 100 ms (10 Hz)

    const TickType_t xFrequency = pdMS_TO_TICKS(WSINTERVAL);

    // Creating frame synchronization semaphore and initializing it
    frameSync = xSemaphoreCreateBinary();
    xSemaphoreGive(frameSync);

    //=== setup section  ==================

    //  Creating RTOS task for grabbing frames from the camera
    xTaskCreatePinnedToCore(
            taskCaptureImageFromCamera,        // callback
            "taskCaptureImageFromCamera",           // name
            4 * 1024,
            NULL,
            2,
            &tCaptureCamera,
            PRO_CPU);

    //  Creating RTOS task for grabbing frames from the camera
    xTaskCreatePinnedToCore(
            taskSteamWebsocket,
            "taskSteamWebsocket",
            4 * 1024,
            NULL,
            2,
            &tStreamgWebSocket,
            APP_CPU);


    //  Registering webserver handling routines
    wifiServer.on("/stream", HTTP_GET, steamServerHtml);
    wifiServer.onNotFound(handleNotFound);

    //  Starting webserver
    wifiServer.begin();

    //=== loop() section  ===================
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        wifiServer.handleClient();
        webSocket.loop();

        //  After every server client handling request, we let other tasks run and then pause
        taskYIELD();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// Called when receiving any WebSocket message
void onWebSocketEvent(uint8_t num,
                      WStype_t type,
                      uint8_t *payload,
                      size_t length)
{

    // Figure out the type of WebSocket event
    switch (type)
    {

        // Client has disconnected
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);
            for (auto &streamInfoAllClient : streamInfoAllClients)
            {
                if (streamInfoAllClient.webSocketClientId == num)
                {
                    streamInfoAllClient.isActive = false;
                    streamInfoAllClient.isPilot = false;
                    streamInfoAllClient.webSocketClientId = invalidWebSocketId;
                    break;
                }
            }
            break;

            // New client has connected
        case WStype_CONNECTED:
        {
            IPAddress ip = webSocket.remoteIP(num);
            Serial.printf("[%u] Connection from ", num);
            Serial.println(ip.toString());
            for (auto &streamInfoAllClient : streamInfoAllClients)
            {
                if (not streamInfoAllClient.isActive)
                {
                    streamInfoAllClient.isActive = true;
                    streamInfoAllClient.webSocketClientId = num;
                    break;
                }
            }
        }
            break;

            // Echo text message back to client
        case WStype_TEXT:
        {
            Serial.printf("[%u] Text: %s\n", num, payload);
            if (payload[0] == '{')
            {
                // check if json command was received
                StaticJsonDocument<200> doc;
                deserializeJson(doc, payload);

                if (doc.containsKey("pilot"))
                {
                    if (doc["pilot"] == 1)
                    {
                        bool isThereAPilot{false};
                        for (const auto &streamInfoAllClient : streamInfoAllClients)
                        {
                            if (streamInfoAllClient.isPilot)
                            {
                                isThereAPilot = true;
                                break;
                            }
                        }
                        if (not isThereAPilot)
                        {
                            const uint8_t index{getIndexBywebsocketId(num)};
                            streamInfoAllClients[index].isPilot = true;
                            streamInfoAllClients[index].isPilotInformed = false;
                            Serial.printf("ID %d, index %d is now pilot!\n", num, getIndexBywebsocketId(num));
                        }
                    } else
                    {
                        const uint8_t index{getIndexBywebsocketId(num)};
                        streamInfoAllClients[index].isPilot = false;
                        streamInfoAllClients[index].isPilotInformed = false;
                        Serial.printf("ID %d, index %d is now spectator\n", num, getIndexBywebsocketId(num));
                    }
                }
            }
        }
            break;

        case WStype_BIN:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_PING:
        case WStype_PONG:
        default:
            break;
    }
}

// ==== SETUP method ==================================================================
void setup()
{
    // Setup Serial connection:
    Serial.begin(115200);
    delay(1000); // wait for a second to let Serial connect

    camera_init();

    for (const auto &wifiLogin : wifiLogins)
    {
        wiFiMulti.addAP(wifiLogin.ssid, wifiLogin.password);
    }

    while (wiFiMulti.run() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(500);
    }

    Serial.print("\nConnected to ");
    Serial.print(WiFi.SSID());
    Serial.print(" with IP address: ");
    Serial.println(WiFi.localIP());

    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);

    Serial.printf("Web server started, open %s in a web browser\n", WiFi.localIP().toString().c_str());

    // Start mainstreaming RTOS task
    xTaskCreatePinnedToCore(
            mainLoop,
            "mainLoop",
            3 * 1024,
            NULL,
            2,
            &tMainLoop,
            APP_CPU);
}

void loop()
{
    // this seems to be necessary to let IDLE task run and do GC
    vTaskDelay(1000);
}
