#pragma once
#include <stdint.h>
#include <stddef.h>
#define GB_WIDTH 160
#define GB_HEIGHT 144
#define GB_CART_RAM 32768
enum { KEY_A=1, KEY_B=2, KEY_SELECT=4, KEY_START=8, KEY_RIGHT=16, KEY_LEFT=32, KEY_UP=64, KEY_DOWN=128 };
int core_init(const uint8_t *rom, size_t size);
void core_frame(uint8_t pressed);
void core_tick_rtc(void);
uint16_t *core_pixels(void);
uint8_t *core_save_ram(void);
int core_save_dirty(void);
void core_save_clean(void);
void core_get_rtc(uint8_t out[5]);
void core_set_rtc(const uint8_t in[5]);
size_t core_state_bytes(void);
unsigned core_frames(void);
uint16_t core_pc(void);
unsigned core_errors(void);
void core_set_frameskip(int enabled);
void core_set_graphics_divisor(unsigned divisor);
uint8_t core_read_wram(uint16_t address);
uint8_t core_read_wram_bank(unsigned bank,uint16_t address);
enum { CORE_EVENT_GAME_SAVED=0x100 };
void core_event_profile(int enabled);
unsigned core_take_events(void);
int core_rename_new_trainer(const char *name);
