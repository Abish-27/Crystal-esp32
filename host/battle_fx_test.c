#include "battle_fx.h"
#include "crystal_profile.h"
#include "gb_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[8][4096];
static uint8_t read_bank(unsigned bank,uint16_t a) { return ram[bank][a&4095]; }
static void set(unsigned b,uint16_t a,uint8_t v) { ram[b][a&4095]=v; }
static void hp(unsigned b,uint16_t a,unsigned value) { set(b,a,value>>8);set(b,a+1,value); }
static void settle(battle_fx_state *s) { for(int i=0;i<4;i++)assert(!battle_fx_update(s,read_bank)); }
void core_test_write(unsigned bank,uint16_t address,uint8_t value);
uint16_t core_test_step(unsigned bank,uint16_t pc);
static void capture_case(uint8_t *rom,size_t size,int enabled,unsigned mode,unsigned type,unsigned expected) {
    assert(core_init(rom,size)==0);core_event_profile(enabled);
    core_test_write(1,CRYSTAL_BATTLE_MODE,mode);core_test_write(1,CRYSTAL_BATTLE_TYPE,type);
    /* Execute LD HL,$d214 at the hook PC (synthetic by default). */
    assert(core_test_step(3,CRYSTAL_CAPTURE_PC)==0xd214);
    assert(core_take_events()==expected);assert(core_take_events()==0);assert(core_errors()==0);
}
int main(int argc,char **argv) {
    assert(argc<=2);battle_fx_state s={0};
    assert(!battle_fx_update(&s,read_bank));
    set(1,CRYSTAL_BATTLE_MODE,1);set(0,CRYSTAL_PLAYER_SPECIES,155);set(1,CRYSTAL_ENEMY_SPECIES,161);
    hp(0,CRYSTAL_PLAYER_HP,100);hp(0,CRYSTAL_PLAYER_MAX_HP,100);
    hp(1,CRYSTAL_ENEMY_HP,50);hp(1,CRYSTAL_ENEMY_MAX_HP,50);settle(&s);
    hp(0,CRYSTAL_PLAYER_HP,90);assert(battle_fx_update(&s,read_bank)==BATTLE_PLAYER_HURT);
    assert(!battle_fx_update(&s,read_bank));
    hp(1,CRYSTAL_ENEMY_HP,45);assert(battle_fx_update(&s,read_bank)==BATTLE_ENEMY_HURT);
    hp(0,CRYSTAL_PLAYER_HP,80);hp(1,CRYSTAL_ENEMY_HP,40);
    assert(battle_fx_update(&s,read_bank)==(BATTLE_PLAYER_HURT|BATTLE_ENEMY_HURT));
    hp(0,CRYSTAL_PLAYER_HP,100);assert(!battle_fx_update(&s,read_bank)); /* Healing. */
    set(1,CRYSTAL_PLAYER_SLOT,1);hp(0,CRYSTAL_PLAYER_HP,10);settle(&s); /* Same-species switch. */
    hp(0,CRYSTAL_PLAYER_HP,9);assert(battle_fx_update(&s,read_bank)==BATTLE_PLAYER_HURT);
    set(1,CRYSTAL_ENEMY_SPECIES,163);hp(1,CRYSTAL_ENEMY_MAX_HP,20);hp(1,CRYSTAL_ENEMY_HP,20);settle(&s);
    hp(1,CRYSTAL_ENEMY_HP,0);assert(battle_fx_update(&s,read_bank)==BATTLE_ENEMY_HURT); /* Faint once. */
    assert(!battle_fx_update(&s,read_bank));
    set(1,CRYSTAL_BATTLE_MODE,2);hp(1,CRYSTAL_ENEMY_HP,20);settle(&s);
    set(0,CRYSTAL_ENEMY_SLOT,1);hp(1,CRYSTAL_ENEMY_HP,2);settle(&s);
    hp(1,CRYSTAL_ENEMY_HP,1);assert(battle_fx_update(&s,read_bank)==BATTLE_ENEMY_HURT);
    hp(1,CRYSTAL_ENEMY_HP,65535);assert(!battle_fx_update(&s,read_bank)); /* Invalid intermediate byte pair. */
    hp(1,CRYSTAL_ENEMY_HP,1);settle(&s);
    set(1,CRYSTAL_BATTLE_MODE,0);assert(!battle_fx_update(&s,read_bank));
    set(1,CRYSTAL_BATTLE_MODE,1);settle(&s); /* New encounter doesn't inherit old HP. */
    set(1,CRYSTAL_BATTLE_TYPE,3);hp(0,CRYSTAL_PLAYER_HP,1);assert(!battle_fx_update(&s,read_bank));
    uint8_t *rom=calloc(1,0x200000);assert(rom);
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        assert(fread(rom,1,0x200000,f)==0x200000);assert(fgetc(f)==EOF);fclose(f);
        puts("Mode: user-supplied ROM; verify its identity before running");
    } else {
        /* Generated test cartridge, not a commercial ROM or extracted asset. */
        rom[0x143]=0x80;rom[0x147]=0x10;rom[0x148]=6;rom[0x149]=3;
        uint8_t checksum=0;
        for(unsigned i=0x134;i<=0x14c;i++)checksum=checksum-rom[i]-1;
        rom[0x14d]=checksum;
        /* LD HL,$d214 at the hook location exercises the fetch predicate. */
        rom[CRYSTAL_CAPTURE_ROM]=0x21;
        rom[CRYSTAL_CAPTURE_ROM+1]=0x14;rom[CRYSTAL_CAPTURE_ROM+2]=0xd2;
        puts("Mode: synthetic cartridge; no commercial ROM required");
    }
    assert(rom[CRYSTAL_CAPTURE_ROM]==0x21 && rom[CRYSTAL_CAPTURE_ROM+1]==0x14 && rom[CRYSTAL_CAPTURE_ROM+2]==0xd2);
    capture_case(rom,0x200000,1,1,0,4); /* Confirmed ordinary catch. */
    capture_case(rom,0x200000,1,1,4,4); /* Fishing catch. */
    capture_case(rom,0x200000,1,1,6,4); /* Bug contest catch, including PC/party-full flow. */
    capture_case(rom,0x200000,1,1,3,0); /* Tutorial excluded. */
    capture_case(rom,0x200000,1,1,2,0); /* Debug excluded. */
    capture_case(rom,0x200000,1,2,0,0); /* Trainer battle cannot be a catch. */
    capture_case(rom,0x200000,0,1,0,0); /* Unknown ROM profile disabled. */
    assert(core_rename_new_trainer("Abish")==-1);
    core_event_profile(1);core_test_write(1,0xd22d,0);core_test_write(1,0xdcd7,0);
    assert(core_rename_new_trainer("Abish")==0);
    const unsigned char name[]={0x80,0xa1,0xa8,0xb2,0xa7,0x50};
    for(unsigned i=0;i<sizeof(name);i++)assert(core_read_wram_bank(1,0xd47d+i)==name[i]);
    assert(core_rename_new_trainer("Bad!")==-1);
    core_test_write(1,0xdcd7,1);assert(core_rename_new_trainer("Other")==-1);
    for(unsigned pc=0x699c;pc<=0x699f;pc+=3) {
        assert(core_init(rom,0x200000)==0);core_event_profile(1);
        core_test_write(1,CRYSTAL_BATTLE_MODE,1);core_test_write(1,CRYSTAL_BATTLE_TYPE,0);
        core_test_step(3,pc);assert(core_take_events()==0);
    }
    core_test_write(1,0xd216,0x12);core_test_write(2,0xd216,0x34);
    assert(core_read_wram_bank(1,0xd216)==0x12 && core_read_wram_bank(2,0xd216)==0x34);
    free(rom);puts("PASS damage, healing, switching, fainting, encounter reset, invalid HP, banked RAM, capture-fetch predicate and profile/tutorial guards");
    return 0;
}
