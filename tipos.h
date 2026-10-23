#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Bits de notificacao enviados a taskControle (Serial ou MQTT).
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
constexpr uint32_t CMD_INICIAR = 1UL << 0;
constexpr uint32_t CMD_PARAR = 1UL << 1;
