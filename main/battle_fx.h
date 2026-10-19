#pragma once
#include <stdint.h>
typedef uint8_t (*battle_read_fn)(unsigned bank,uint16_t address);
enum { BATTLE_PLAYER_HURT=1, BATTLE_ENEMY_HURT=2 };
typedef struct {
    uint16_t hp,max_hp;
    uint8_t species,slot,warmup;
} battle_side;
typedef struct { battle_side player,enemy;uint8_t mode; } battle_fx_state;
unsigned battle_fx_update(battle_fx_state *state,battle_read_fn read);
