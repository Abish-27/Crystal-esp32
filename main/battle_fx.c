#include "battle_fx.h"
#include "crystal_profile.h"
#include <string.h>
static uint16_t word(battle_read_fn r,unsigned bank,uint16_t a) {
    return ((uint16_t)r(bank,a)<<8)|r(bank,a+1);
}
static int side(battle_side *old,uint16_t hp,uint16_t max,uint8_t species,uint8_t slot) {
    if(!max || max>999 || hp>max || !species || species>251 || slot>5) {
        memset(old,0,sizeof(*old));return 0;
    }
    if(old->species!=species || old->slot!=slot || old->max_hp!=max) {
        *old=(battle_side){.hp=hp,.max_hp=max,.species=species,.slot=slot,.warmup=3};return 0;
    }
    int lost=!old->warmup && hp<old->hp;
    old->hp=hp;
    if(old->warmup)--old->warmup;
    return lost;
}
unsigned battle_fx_update(battle_fx_state *s,battle_read_fn r) {
    uint8_t mode=r(1,CRYSTAL_BATTLE_MODE),type=r(1,CRYSTAL_BATTLE_TYPE);
    if((mode!=1 && mode!=2) || type==2 || type==3) { memset(s,0,sizeof(*s));return 0; }
    if(mode!=s->mode) { memset(s,0,sizeof(*s));s->mode=mode; }
    unsigned events=0;
    if(side(&s->player,word(r,0,CRYSTAL_PLAYER_HP),word(r,0,CRYSTAL_PLAYER_MAX_HP),
        r(0,CRYSTAL_PLAYER_SPECIES),r(1,CRYSTAL_PLAYER_SLOT)))events|=BATTLE_PLAYER_HURT;
    if(side(&s->enemy,word(r,1,CRYSTAL_ENEMY_HP),word(r,1,CRYSTAL_ENEMY_MAX_HP),
        r(1,CRYSTAL_ENEMY_SPECIES),mode==1?0:r(0,CRYSTAL_ENEMY_SLOT)))events|=BATTLE_ENEMY_HURT;
    return events;
}
