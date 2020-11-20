#pragma once

#include <Arduino.h>

struct Frame
{
    Frame()
    {
        xSemaphoreGive(frameSync);
    }
    SemaphoreHandle_t frameSync{xSemaphoreCreateBinary()};

    uint8_t *buffToSend{};
    size_t buffSize{};
};