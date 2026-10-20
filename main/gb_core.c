#include "gb_core.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "crystal_profile.h"
#define PEANUT_FULL_GBC_SUPPORT 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#if defined(ESP_PLATFORM) && defined(CRYSTAL_IRAM_CORE)
#include "esp_attr.h"
#define CORE_HOT IRAM_ATTR
#else
#define CORE_HOT
#endif
/* Only hot CPU handlers move to instruction RAM; tables stay in flash. */
#define CORE_LOCATION___gb_read CORE_HOT
#define CORE_LOCATION___gb_write CORE_HOT
#define CORE_LOCATION___gb_execute_cb CORE_HOT
#define CORE_LOCATION___gb_step_cpu CORE_HOT
#define CORE_LOCATION___gb_draw_line
#define CORE_LOCATION_gb_run_frame
#define CORE_LOCATION_op_cycles
#define CORE_LOCATION_TAC_CYCLES
#define __not_in_flash_func(x) CORE_LOCATION_##x x
#define RGB555_TO_RGB444(c) ((((c)>>3)&0xf00)|(((c)>>2)&0xf0)|(((c)>>1)&0xf))
#include "peanut_gb.h"

static struct gb_s game;
static const uint8_t *rom_data;
static size_t rom_size;
static uint8_t cart_ram[GB_CART_RAM];
static uint16_t framebuffer[GB_WIDTH*GB_HEIGHT];
static unsigned frames, errors;
static int dirty;
static unsigned graphics_divisor=2;
#ifdef CORE_TEST
static unsigned test_lines;
#endif
static jmp_buf fault_escape;
static int running;
static int event_profile;
static unsigned pending_events;

static uint8_t CORE_HOT read_rom(struct gb_s *g, const uint_fast32_t a) {
    /* Check both physical ROM bank and opcode-fetch PC, not arbitrary data reads. */
    if(a==CRYSTAL_CAPTURE_ROM && event_profile && g->cpu_reg.pc.reg==CRYSTAL_CAPTURE_PC+1 &&
       g->wram[0x1000+CRYSTAL_BATTLE_MODE-0xd000]==1 &&
       g->wram[0x1000+CRYSTAL_BATTLE_TYPE-0xd000]!=2 &&
       g->wram[0x1000+CRYSTAL_BATTLE_TYPE-0xd000]!=3)pending_events|=4;
    if(event_profile &&
       ((a==CRYSTAL_SAVE_RETURN_ROM && g->cpu_reg.pc.reg==CRYSTAL_SAVE_RETURN_PC+1) ||
        (a==CRYSTAL_SAVE_MENU_ROM && g->cpu_reg.pc.reg==CRYSTAL_SAVE_MENU_PC+1)))
        pending_events|=CORE_EVENT_GAME_SAVED;
    return a < rom_size ? rom_data[a] : 0xff;
}
static uint8_t CORE_HOT read_ram(struct gb_s *g, const uint_fast32_t a) {
    (void)g;
    return a < sizeof(cart_ram) ? cart_ram[a] : 0xff;
}
static void CORE_HOT write_ram(struct gb_s *g, const uint_fast32_t a, const uint8_t v) {
    (void)g;
    if (a < sizeof(cart_ram) && cart_ram[a] != v) { cart_ram[a]=v; dirty=1; }
}
static void error_cb(struct gb_s *g, const enum gb_error_e e, const uint16_t a) {
    (void)g;
    ++errors;
    printf("CORE_ERROR code=%d address=%04x frame=%u\n",(int)e,a,frames);
    if (running) longjmp(fault_escape,1);
}
static void draw_line(struct gb_s *g, const uint8_t *p, const uint_fast8_t y) {
    if (y >= GB_HEIGHT) return;
#ifdef CORE_TEST
    ++test_lines;
#endif
    static const uint16_t mono[4]={0xffff,0xad55,0x52aa,0x0000};
    for (unsigned x=0;x<GB_WIDTH;x++) {
        if (g->cgb.cgbMode) {
            uint16_t c=g->cgb.fixPalette[p[x]&63];
            /* Core's fixed palette is RGB555, LCD framebuffer is RGB565. */
            framebuffer[y*GB_WIDTH+x]=((c&0x7c00)<<1)|((c&0x03e0)<<1)|((c&0x0200)>>4)|(c&31);
        } else framebuffer[y*GB_WIDTH+x]=mono[p[x]&3];
    }
}
int core_init(const uint8_t *rom, size_t size) {
    if (size < 0x150 || rom[0x148] > 8 || rom[0x149] > 3 || size != (32768u << rom[0x148])) return -1;
    rom_data=rom; rom_size=size;graphics_divisor=2;
#ifdef CORE_TEST
    test_lines=0;
#endif
    memset(&game,0,sizeof(game)); memset(cart_ram,0xff,sizeof(cart_ram));
    memset(framebuffer,0,sizeof(framebuffer)); frames=errors=0; dirty=running=event_profile=0;pending_events=0;
    int result=gb_init(&game,read_rom,read_ram,write_ram,error_cb,NULL);
    if (result) return result;
    gb_init_lcd(&game,draw_line);
    game.direct.joypad=0xff;
    return 0;
}
void core_frame(uint8_t pressed) {
    game.direct.joypad=(uint8_t)~pressed;
    /* At 1.25x, render one in three complete guest frames. CPU/timers still run.
     * gb_run_frame returns at VBlank; the next override applies to its next picture. */
    if(game.direct.frame_skip && graphics_divisor>2)
        game.display.frame_skip_count=((frames+1)%graphics_divisor)==0;
    running=1;
    if (!setjmp(fault_escape)) gb_run_frame(&game);
    running=0;
    ++frames;
}
void core_tick_rtc(void) { gb_tick_rtc(&game); }
uint16_t *core_pixels(void) { return framebuffer; }
uint8_t *core_save_ram(void) { return cart_ram; }
int core_save_dirty(void) { return dirty; }
void core_save_clean(void) { dirty=0; }
void core_get_rtc(uint8_t out[5]) { memcpy(out,game.cart_rtc,5); }
void core_set_rtc(const uint8_t in[5]) { memcpy(game.cart_rtc,in,5); }
size_t core_state_bytes(void) { return sizeof(game)+sizeof(cart_ram)+sizeof(framebuffer); }
unsigned core_frames(void) { return frames; }
uint16_t core_pc(void) { return game.cpu_reg.pc.reg; }
unsigned core_errors(void) { return errors; }
void core_set_frameskip(int e) { game.direct.frame_skip=e!=0; }
void core_set_graphics_divisor(unsigned n) { graphics_divisor=n==3?3:2; }
uint8_t core_read_wram(uint16_t a) { return a>=0xc000 && a<0xe000 ? __gb_read(&game,a):0; }
uint8_t core_read_wram_bank(unsigned bank,uint16_t a) {
    if(bank==0 && a>=0xc000 && a<0xd000)return game.wram[a-0xc000];
    if(bank>=1 && bank<=7 && a>=0xd000 && a<0xe000)return game.wram[bank*0x1000+a-0xd000];
    return 0;
}
void core_event_profile(int enabled) { event_profile=enabled!=0;pending_events=0; }
unsigned core_take_events(void) { unsigned e=pending_events;pending_events=0;return e; }
/* Early-game rename only. Let the game's SAVE routine write its own checksums.
 * Refuse after receiving Pokemon, when changing OT records needs more care. */
int core_rename_new_trainer(const char *name) {
    size_t n=strlen(name);
    if(!event_profile || n<1 || n>7 || core_read_wram_bank(1,0xd22d)!=0 ||
       core_read_wram_bank(1,0xdcd7)!=0)return -1;
    uint8_t encoded[11];memset(encoded,0x50,sizeof(encoded));
    for(size_t i=0;i<n;i++) {
        if(name[i]>='A' && name[i]<='Z')encoded[i]=0x80+name[i]-'A';
        else if(name[i]>='a' && name[i]<='z')encoded[i]=0xa0+name[i]-'a';
        else return -1;
    }
    memcpy(&game.wram[0x1000+0x47d],encoded,sizeof(encoded));return 0;
}
#ifdef CORE_TEST
unsigned core_test_drawn_lines(void) { return test_lines; }
void core_test_write(unsigned bank,uint16_t a,uint8_t value) {
    if(bank==0 && a>=0xc000 && a<0xd000)game.wram[a-0xc000]=value;
    if(bank>=1 && bank<=7 && a>=0xd000 && a<0xe000)game.wram[bank*0x1000+a-0xd000]=value;
}
uint8_t core_test_rom_read(uint32_t address,uint16_t pc) {
    game.cpu_reg.pc.reg=pc;return read_rom(&game,address);
}
uint16_t core_test_step(unsigned bank,uint16_t pc) {
    game.selected_rom_bank=bank;game.cpu_reg.pc.reg=pc;__gb_step_cpu(&game);return game.cpu_reg.hl.reg;
}
#endif
