#pragma once

#include <Arduino.h>

struct Frame
{
    static constexpr uint8_t numberOfFrameSaved{2};
    static constexpr uint8_t queueSize{2};

    Frame()
    {
        for (auto &semaphore :frameSync)
        {
            semaphore = xSemaphoreCreateBinary();
            xSemaphoreGive(semaphore);
        }

        queueFrameSize = xQueueCreate(queueSize, sizeof(uint8_t));
    }

    SemaphoreHandle_t frameSync[numberOfFrameSaved]{};

    uint8_t bufferUpToDate{};

    uint8_t *buffToSend[numberOfFrameSaved]{};
    size_t buffSize[numberOfFrameSaved]{};
    size_t frameSize[numberOfFrameSaved]{};

    QueueHandle_t queueFrameSize{};
};