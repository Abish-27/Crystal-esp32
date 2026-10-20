#include "gb_core.h"
#include "crystal_profile.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint16_t core_test_step(unsigned bank,uint16_t pc);
uint8_t core_test_rom_read(uint32_t address,uint16_t pc);
int main(int argc,char **argv) {
    assert(argc<=2);const size_t size=0x200000;uint8_t *rom=calloc(1,size);assert(rom);
    if(argc==2) { FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,size,f)==size);fclose(f); }
    else {
        rom[0x143]=0x80;rom[0x147]=0x10;rom[0x148]=6;rom[0x149]=3;
        for(unsigned i=0x134;i<=0x14c;i++)rom[0x14d]=rom[0x14d]-rom[i]-1;
        rom[CRYSTAL_SAVE_RETURN_ROM]=0xc9;
        rom[CRYSTAL_SAVE_MENU_ROM]=0x0e;rom[CRYSTAL_SAVE_MENU_ROM+1]=32;
    }
    assert(rom[CRYSTAL_SAVE_RETURN_ROM]==0xc9);
    assert(rom[CRYSTAL_SAVE_MENU_ROM]==0x0e && rom[CRYSTAL_SAVE_MENU_ROM+1]==32);
    const uint32_t addresses[]={CRYSTAL_SAVE_RETURN_ROM,CRYSTAL_SAVE_MENU_ROM};
    const uint16_t pcs[]={CRYSTAL_SAVE_RETURN_PC,CRYSTAL_SAVE_MENU_PC};
    for(unsigned i=0;i<2;i++) {
        assert(core_init(rom,size)==0);core_event_profile(0);core_test_step(5,pcs[i]);
        assert(core_take_events()==0);
        assert(core_init(rom,size)==0);core_event_profile(1);core_test_step(5,pcs[i]);
        assert(core_take_events()==CORE_EVENT_GAME_SAVED);assert(core_take_events()==0);assert(core_errors()==0);
        core_test_rom_read(addresses[i],pcs[i]);assert(core_take_events()==0); /* Data read, wrong fetch PC. */
        core_test_rom_read(addresses[i]-0x4000,pcs[i]+1);assert(core_take_events()==0); /* Wrong ROM bank. */
        core_test_rom_read(addresses[i]+1,pcs[i]+1);assert(core_take_events()==0); /* Adjacent byte. */
    }
    free(rom);puts("PASS completed-save hooks, profile guard, fetch PC, bank, and adjacent-byte exclusions");
}
