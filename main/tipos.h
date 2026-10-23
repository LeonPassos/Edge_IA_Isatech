#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <cJSON.h>

// Bits de notificacao enviados a taskControle (Serial ou MQTT).
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
#define CMD_INICIAR (1UL << 0)
#define CMD_PARAR   (1UL << 1)
