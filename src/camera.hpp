#pragma once

#include <Arduino.h>

#include "esp_camera.h"
#include "OV2640.h"
#include "memoryAllocation.hpp"
#include "imageParams.hpp"

class Camera
{
public:
    Camera() = delete;

    explicit Camera(Frame *pFrame)
    {
        assert(not(pFrame == nullptr) and "Err: pointer must be initialized");
        Serial.print("Camera class running on core ");
        camera_init();
        Serial.println(xPortGetCoreID());
        frame = pFrame;
    }

    void captureImage()
    {
        //  Grab a frame from the camera and query its size
        camera_fb_t *fb{nullptr};

        fb = esp_camera_fb_get();
        const size_t sizePicture = fb->len;

        //  If frame size is more that we have previously allocated - request  125% of the current frame space
        if (sizePicture > camSize)
        {
            camSize = sizePicture + sizePicture / 4;
            camBuf = allocateMemory(camBuf, camSize);
        }

        //  Copy current frame into local buffer
        auto bufferPointer = (uint8_t *) fb->buf;
        memcpy(camBuf, bufferPointer, sizePicture);
        esp_camera_fb_return(fb);

        uint8_t bufferUpToDate{frame->bufferUpToDate};
        bufferUpToDate++;
        bufferUpToDate %= Frame::numberOfFrameSaved;

        if (not xSemaphoreTake(frame->frameSync[bufferUpToDate], waitingTicks))
        {
            bufferUpToDate++;
            bufferUpToDate %= Frame::numberOfFrameSaved;
            if (not xSemaphoreTake(frame->frameSync[bufferUpToDate], waitingTicks))
            {
                bufferUpToDate = 0xFF;
                Serial.println("Fail copying buffer");
            }
        }
        if (bufferUpToDate != 0xFF)
        {
            if (sizePicture > frame->buffSize[bufferUpToDate])
            {
                frame->buffToSend[bufferUpToDate] = allocateMemory(frame->buffToSend[bufferUpToDate], sizePicture);
                frame->buffSize[bufferUpToDate] = sizePicture;
            }
            frame->frameSize[bufferUpToDate] = sizePicture;
            memcpy(frame->buffToSend[bufferUpToDate], camBuf, sizePicture);
            xSemaphoreGive(frame->frameSync[bufferUpToDate]);
            frame->bufferUpToDate = bufferUpToDate;
        }
    }

    static void resizeCamera(const uint8_t &resolution)
    {
        camera_init(static_cast<framesize_t>(resolution));
    }


private:
    const TickType_t waitingTicks{pdMS_TO_TICKS(4)};

    Frame *frame{};

    uint8_t *camBuf{};
    size_t camSize{};
};
