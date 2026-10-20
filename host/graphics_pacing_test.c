#include "gb_core.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
unsigned core_test_drawn_lines(void);
int main(void) {
    const size_t size=0x200000;uint8_t *rom=calloc(1,size);assert(rom);
    rom[0x100]=0xc3;rom[0x101]=0;rom[0x102]=1; /* JP $100, independent test program. */
    rom[0x143]=0x80;rom[0x147]=0x10;rom[0x148]=6;rom[0x149]=3;
    for(unsigned i=0x134;i<=0x14c;i++)rom[0x14d]=rom[0x14d]-rom[i]-1;
    assert(core_init(rom,size)==0);core_set_frameskip(1);core_set_graphics_divisor(3);
    core_frame(0);assert(core_test_drawn_lines()==0);
    core_frame(0);assert(core_test_drawn_lines()==0);
    core_frame(0);unsigned one=core_test_drawn_lines();assert(one>0);
    core_frame(0);core_frame(0);assert(core_test_drawn_lines()==one);
    core_frame(0);assert(core_test_drawn_lines()==2*one);
    core_set_frameskip(0);core_frame(0);assert(core_test_drawn_lines()>2*one);
    core_set_frameskip(1);core_set_graphics_divisor(2);
    unsigned before=core_test_drawn_lines();core_frame(0);core_frame(0);assert(core_test_drawn_lines()>before);
    assert(core_frames()==9 && core_errors()==0);
    free(rom);puts("PASS 1-in-3 graphics, full CPU frames, disabled-skip override and normal-mode restoration");
}
