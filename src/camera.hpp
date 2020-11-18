#pragma once

#include <Arduino.h>

#include "esp_camera.h"
#include "OV2640.h"
#include "memoryAllocation.hpp"

class Camera
{
public:
    Camera()
    {
        camera_init();
        Serial.print("Camera class running on core ");
        Serial.println(xPortGetCoreID());
    }

    void captureImage()
    {
        //  Grab a frame from the camera and query its size
        camera_fb_t *fb = NULL;

        fb = esp_camera_fb_get();
        size_t s = fb->len;

        //  If frame size is more that we have previously allocated - request  125% of the current frame space
        if (s > fSize)
        {
            fSize = s + s;
            fbs = allocateMemory(fbs, fSize);
        }

        //  Copy current frame into local buffer
        auto b = (uint8_t *) fb->buf;
        memcpy(fbs, b, s);
        esp_camera_fb_return(fb);
    }

    uint8_t *getFbs() const
    {
        return fbs;
    }

    const size_t &getFSize() const
    {
        return fSize;
    }


private:
    //  Pointers to the 2 frames, their respective sizes and index of the current frame
    uint8_t *fbs = {NULL};
    size_t fSize = {0};
};
