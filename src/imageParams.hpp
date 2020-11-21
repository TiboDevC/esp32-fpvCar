#pragma once

#include <Arduino.h>

struct Frame
{
    static constexpr uint8_t numberOfFrameSaved{2};
    Frame()
    {
        for(uint8_t indexSemaphor{}; indexSemaphor < numberOfFrameSaved; indexSemaphor++)
        {
            frameSync[indexSemaphor] = xSemaphoreCreateBinary();
            xSemaphoreGive(frameSync[indexSemaphor]);
        }
    }
    SemaphoreHandle_t frameSync[numberOfFrameSaved]{};

    uint8_t bufferUpToDate{};

    uint8_t *buffToSend[numberOfFrameSaved]{};
    size_t buffSize[numberOfFrameSaved]{};
    size_t frameSize[numberOfFrameSaved]{};
};