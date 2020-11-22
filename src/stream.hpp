#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#include "memoryAllocation.hpp"
#include "imageParams.hpp"

namespace Streaming
{
    static constexpr uint8_t invalidValue{0xFF};

    struct StreamInfo
    {
        uint8_t webSocketClientId{invalidValue};
        bool isPilot{false};

        explicit StreamInfo(const uint8_t &clientId)
        {
            webSocketClientId = clientId;
        }
    };

    std::vector<StreamInfo> streamInfoAllClients;

    bool notifyPilotChange{false};
    uint8_t newCameraResolution{invalidValue};

    class StreamOverWebsocket
    {
    public:
        StreamOverWebsocket()
        {
            Serial.printf("Camera StreamOverWebsocket running on core %d\n", xPortGetCoreID());
            webSocket.begin();
            webSocket.onEvent(onWebSocketEvent);
        }

        void loop(Frame *frame)
        {
            streamImgToAllClients(frame);
            changeCameraResolution(frame);
            webSocket.loop();
            notifyPilotStatus();
        }

    private:

        WebSocketsServer webSocket{81};
        static constexpr uint8_t waitingTimeBeforeTryOtherBuffer{5};

        TickType_t xFrequency{pdMS_TO_TICKS(waitingTimeBeforeTryOtherBuffer)};

        static uint8_t getIndexBywebsocketId(const uint8_t &webSocketClientId);

        // Called when receiving any WebSocket message
        static void onWebSocketEvent(uint8_t num,
                                     WStype_t type,
                                     uint8_t *payload,
                                     size_t length);

        void streamImgToAllClients(Frame *frame);

        void notifyPilotStatus();

        void changeCameraResolution(Frame *frame);
    };
}

void Streaming::StreamOverWebsocket::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{

    // Figure out the type of WebSocket event
    switch (type)
    {
        // Client has disconnected
        case WStype_DISCONNECTED:
            Serial.printf("[%u] Disconnected!\n", num);

            streamInfoAllClients.erase(
                    std::remove_if(streamInfoAllClients.begin(), streamInfoAllClients.end(),
                                   [&num](const StreamInfo &o) { return (o.webSocketClientId == num); }),
                    streamInfoAllClients.end());

            break;

            // New client has connected
        case WStype_CONNECTED:
        {
            Serial.printf("[%u] New connection", num);
            const StreamInfo newClient{num};
            streamInfoAllClients.push_back(newClient);
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
                        const auto &isThereAPilot = std::find_if(
                                streamInfoAllClients.begin(), streamInfoAllClients.end(),
                                [](const StreamInfo &streamInfo) { return streamInfo.isPilot; });

                        if (isThereAPilot == streamInfoAllClients.end())
                        {
                            const uint8_t index{getIndexBywebsocketId(num)};
                            streamInfoAllClients[index].isPilot = true;
                            Serial.printf("ID %d, index %d is now pilot!\n", num, getIndexBywebsocketId(num));
                            notifyPilotChange = true;
                        }
                    } else
                    {
                        const uint8_t index{getIndexBywebsocketId(num)};
                        streamInfoAllClients[index].isPilot = false;
                        Serial.printf("ID %d, index %d is now spectator\n", num, getIndexBywebsocketId(num));
                        notifyPilotChange = true;
                    }
                } else if (doc.containsKey("frameSize"))
                {
                    newCameraResolution = doc["frameSize"];
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

uint8_t Streaming::StreamOverWebsocket::getIndexBywebsocketId(const uint8_t &webSocketClientId)
{
    for (const auto &streamInfoAllClient : streamInfoAllClients)
    {
        if (webSocketClientId == streamInfoAllClient.webSocketClientId)
        {
            return webSocketClientId;
        }
    }
    return invalidValue;
}

void Streaming::StreamOverWebsocket::streamImgToAllClients(Frame *frame)
{
    if (not streamInfoAllClients.empty() and frame != nullptr)
    {
        for (const auto &webSocketClientId : streamInfoAllClients)
        {
            uint8_t bufferUpToDate = frame->bufferUpToDate;
            if (not xSemaphoreTake(frame->frameSync[bufferUpToDate], xFrequency) or
                frame->frameSize[bufferUpToDate] == 0)
            {
                bufferUpToDate++;
                bufferUpToDate %= Frame::numberOfFrameSaved;
                if (not xSemaphoreTake(frame->frameSync[bufferUpToDate], xFrequency) or
                    frame->frameSize[bufferUpToDate] == 0)
                {
                    Serial.println("Could not find valid buffer");
                    bufferUpToDate = 0xFF;
                }
            }
            if (bufferUpToDate != 0xFF)
            {
                webSocket.sendBIN(webSocketClientId.webSocketClientId, frame->buffToSend[bufferUpToDate],
                                  frame->frameSize[bufferUpToDate]);
                xSemaphoreGive(frame->frameSync[bufferUpToDate]);
            } else
            {
                Serial.println("Err: fail finding valid buffer");
            }
        }
    }
}

void Streaming::StreamOverWebsocket::notifyPilotStatus()
{
    if (notifyPilotChange)
    {
        for (const auto &streamInfoAllClient :  streamInfoAllClients)
        {
            std::string pilotStatus = "{\"isPilot\": ";
            if (streamInfoAllClient.isPilot)
            {
                pilotStatus += "1 }";
            } else
            {
                pilotStatus += "0 }";
            }
            notifyPilotChange = false;
            webSocket.sendTXT(streamInfoAllClient.webSocketClientId, pilotStatus.c_str());
        }
    }
}

void Streaming::StreamOverWebsocket::changeCameraResolution(Frame *frame)
{
    if (newCameraResolution != invalidValue)
    {
        xQueueSend(frame->queueFrameSize, &newCameraResolution, 20);
        newCameraResolution = invalidValue;
    }
}
