#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
enum { LED_PLAYER_HURT=1, LED_ENEMY_HURT=2, LED_CAUGHT=4, LED_SAVED=8, LED_ERROR=16, LED_STARTUP=32 };
esp_err_t badge_led_init(void);
void badge_led_event(unsigned events);
void badge_led_tick(bool paused);
void badge_led_level(unsigned level);
unsigned badge_led_get_level(void);
unsigned badge_led_errors(void);
