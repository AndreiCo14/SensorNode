#pragma once
// ─── FreeRTOS compatibility shim for ESP8266 Arduino (single-threaded) ───────
// ESP8266 Arduino runs a non-OS SDK with no FreeRTOS.
// This header provides stub implementations so the same code compiles on both.
// All operations are safe because there is no actual concurrency on ESP8266.

#ifdef ESP8266

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// ── Primitive types ───────────────────────────────────────────────────────────
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE   ((BaseType_t)1)
#define pdFALSE  ((BaseType_t)0)
#define pdPASS   pdTRUE
#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)

static inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }

// ── Semaphores (no-op: single-threaded, no contention) ───────────────────────
struct _Sem { bool taken; };
typedef _Sem* SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    auto* s = new _Sem; s->taken = false; return s;
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline void       xSemaphoreGive(SemaphoreHandle_t) {}

// ── Simple ring-buffer queue ──────────────────────────────────────────────────
struct _Queue {
    uint8_t* buf;
    size_t   itemSz;
    size_t   cap;
    volatile size_t head, tail, cnt;
};
typedef _Queue* QueueHandle_t;

static inline QueueHandle_t xQueueCreate(size_t cap, size_t itemSz) {
    auto* q   = new _Queue;
    q->buf    = new uint8_t[cap * itemSz];
    q->itemSz = itemSz; q->cap = cap;
    q->head = q->tail = q->cnt = 0;
    return q;
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    if (!q || q->cnt >= q->cap) return pdFALSE;
    memcpy(q->buf + q->tail * q->itemSz, item, q->itemSz);
    q->tail = (q->tail + 1) % q->cap; q->cnt++;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t) {
    if (!q || q->cnt == 0) return pdFALSE;
    memcpy(item, q->buf + q->head * q->itemSz, q->itemSz);
    q->head = (q->head + 1) % q->cap; q->cnt--;
    return pdTRUE;
}

// ── Tasks (registration stub; actual execution via loop()) ───────────────────
typedef void* TaskHandle_t;

#define xTaskCreate(fn, name, stack, param, prio, handle) \
    do { if (handle) *(handle) = nullptr; } while(0)
#define xTaskCreatePinnedToCore(fn, name, stack, param, prio, handle, core) \
    do { if (handle) *(handle) = nullptr; } while(0)
#define uxTaskGetStackHighWaterMark(h) ((uint32_t)0)
#define vTaskDelay(ticks)   delay(ticks)
#define vTaskSuspend(h)     do { yield(); delay(10); } while(0)

// ── Task notifications (stub) ─────────────────────────────────────────────────
#define xTaskNotifyWait(a, b, val, ticks) do { if(val) *(val)=0; } while(0)
#define xTaskNotify(handle, value, action) do{} while(0)
#define eSetBits 1
#define eNoAction 0

#endif // ESP8266
