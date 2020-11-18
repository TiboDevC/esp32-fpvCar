#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#include "memoryAllocation.hpp"


static constexpr uint8_t invalidWebSocketId{0xFF};

struct StreamInfo
{
    uint8_t webSocketClientId{invalidWebSocketId};
    bool isPilot{false};

    explicit StreamInfo(const uint8_t &clientId)
    {
        webSocketClientId = clientId;
    }
};

std::vector<StreamInfo> streamInfoAllClients;

class StreamOverWebsocket
{
public:
    void streamImgToAllClients(const uint16_t &camSize, uint8_t *camBuf);

    void voidSendText(char *stringToSend, const uint8_t &webSocketClientId)
    {
        webSocket.sendTXT(webSocketClientId, stringToSend);
    }

    StreamOverWebsocket()
    {
        Serial.print("Camera StreamOverWebsocket running on core ");
        Serial.println(xPortGetCoreID());
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
    }

    const WebSocketsServer &getWebSocket() const
    {
        return webSocket;
    }

    void checkMessageArrival()
    {
        webSocket.loop();
    }

private:
    uint8_t *bufferToSend = {NULL};
    size_t bufferToSendSize = {0};
    WebSocketsServer webSocket{81};

    static uint8_t getIndexBywebsocketId(const uint8_t &webSocketClientId);

    // Called when receiving any WebSocket message
    static void onWebSocketEvent(uint8_t num,
                                 WStype_t type,
                                 uint8_t *payload,
                                 size_t length);
};

void StreamOverWebsocket::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {

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
                        }
                    } else
                    {
                        const uint8_t index{getIndexBywebsocketId(num)};
                        streamInfoAllClients[index].isPilot = false;
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

uint8_t StreamOverWebsocket::getIndexBywebsocketId(const uint8_t &webSocketClientId) {
    for (const auto &streamInfoAllClient : streamInfoAllClients)
    {
        if (webSocketClientId == streamInfoAllClient.webSocketClientId)
        {
            return webSocketClientId;
        }
    }
    return invalidWebSocketId;
}

void StreamOverWebsocket::streamImgToAllClients(const uint16_t &camSize, uint8_t *camBuf) {
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

    for (const auto &webSocketClientId : streamInfoAllClients)
    {
        const bool successSend = webSocket.sendBIN(webSocketClientId.webSocketClientId, bufferToSend,
                                                   bufferToSendSize);
        if (not successSend)
        {
            Serial.printf("Unsuccess sending to %d\n", webSocketClientId.webSocketClientId);
        }
    }
}
