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
        Serial.print("Camera class running on core ");
        camera_init();
        Serial.println(xPortGetCoreID());
        frame = pFrame;
        xLastWakeTime = xTaskGetTickCount();
    }

    void captureImage()
    {
        //  Grab a frame from the camera and query its size
        camera_fb_t *fb{nullptr};

        fb = esp_camera_fb_get();
        const size_t sizePicture = fb->len;

        //  If frame size is more that we have previously allocated - request  125% of the current frame space
        if (sizePicture > camSize[currentFrame])
        {
            camSize[currentFrame] = sizePicture + sizePicture;
            camBuf[currentFrame] = allocateMemory(camBuf[currentFrame], camSize[currentFrame]);
        }

        //  Copy current frame into local buffer
        auto b = (uint8_t *) fb->buf;
        memcpy(camBuf[currentFrame], b, sizePicture);
        esp_camera_fb_return(fb);

        taskYIELD();
        if (xSemaphoreTake(frame->frameSync, xFrequency))
        {
            if(sizePicture > camSize[currentFrame])
            {
                frame->buffToSend = allocateMemory(frame->buffToSend, camSize[currentFrame]);
            }
            frame->buffSize = sizePicture;
            frame->buffToSend = camBuf[currentFrame];
            xSemaphoreGive(frame->frameSync);
        }
        taskYIELD();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }


private:
    Frame *frame{};
    TickType_t xLastWakeTime;
    static constexpr uint8_t FPS{20};
    const TickType_t xFrequency{pdMS_TO_TICKS(1000 / FPS)};

    //  Pointers to the 2 frames, their respective sizes and index of the current frame
    static constexpr uint8_t numberOfFrames{2};

    uint8_t *camBuf[numberOfFrames]{};
    size_t camSize[numberOfFrames]{};

    uint8_t currentFrame{0};
};
