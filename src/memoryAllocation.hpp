#pragma once

#include <Arduino.h>

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