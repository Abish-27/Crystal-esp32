/* SPDX-License-Identifier: GPL-3.0-only
 * Hardware initialization adapted from hackthenorth/badge-firmware.
 * Modified standalone emulator integration, 2026-10-18.
 * See THIRD_PARTY_NOTICES.md for upstream revision and attribution.
 */
#include "gb_core.h"
#include "badge_led.h"
#include "battle_fx.h"
#include "crystal_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "esp_system.h"

#define PIN_MOSI 10
#define PIN_CLK 1
#define PIN_CS 2
#define PIN_DC 0
#define PIN_RST 4
#define PIN_LOAD 20
#define PIN_SHIFT_CLK 21
#define PIN_DATA 7
#define PIN_START 9
#define PIN_LED 3
#define SAVE_MAGIC 0x43525953u
#define SAVE_SLOT 0x10000u
#define STRIPE_ROWS 16

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t lcd_done;
static uint16_t *stripe;
static uint16_t *display_snapshot;
static SemaphoreHandle_t display_idle;
static TaskHandle_t display_task;
static uint8_t map_x[320],map_y[240];
static int view_width=320,view_height=240;
static unsigned drawn_interval;
static portMUX_TYPE display_stats_lock=portMUX_INITIALIZER_UNLOCKED;
static const esp_partition_t *save_partition;
static unsigned sequence;
static int active_slot=-1;
static int paused, render_divisor=3, scale=4, core_skip=1;
static uint8_t injected;
static int64_t injection_until;
static unsigned frames_interval;
static int64_t report_time;
static uint64_t emulation_us, display_us;
static char input[128];
static unsigned input_used;
static uint16_t button_candidate,button_stable;
static int64_t button_changed;
static int battle_profile;
static battle_fx_state battle_state;
static unsigned hurt_events,hit_events,catch_events;

/* Last-completed-slot survives interruption while another slot is written. */
typedef struct {
    uint32_t magic, version, sequence, bytes, crc;
    uint8_t rtc[5];
    uint8_t reserved[7];
} save_header;
_Static_assert(sizeof(save_header)==32,"Save slot header must remain 32 bytes");

static void reply(const char *format,...) {
    char line[512];
    va_list ap;va_start(ap,format);
    int n=vsnprintf(line,sizeof(line),format,ap);va_end(ap);
    if(n>0)usb_serial_jtag_write_bytes(line,n<(int)sizeof(line)?n:sizeof(line)-1,pdMS_TO_TICKS(500));
}
static bool transfer_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *e,void *context) {
    (void)io;(void)e;(void)context;
    BaseType_t wake=pdFALSE;
    xSemaphoreGiveFromISR(lcd_done,&wake);
    /* IDF's SPI LCD post-callback ignores the boolean return value. */
    portYIELD_FROM_ISR(wake);
    return false;
}
static void draw_rect(int x,int y,int w,int h) {
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,x,y,x+w,y+h,stripe));
    if(xSemaphoreTake(lcd_done,pdMS_TO_TICKS(2000))!=pdTRUE)abort();
}
static void clear_display(void) {
    memset(stripe,0,320*STRIPE_ROWS*2);
    for(int y=0;y<240;y+=STRIPE_ROWS)draw_rect(0,y,320,STRIPE_ROWS);
}
static void configure_view(void) {
    view_width=scale==1?160:scale==2?240:scale==3?266:320;
    view_height=scale==1?144:scale==2?216:240;
    for(int x=0;x<view_width;x++)map_x[x]=x*160/view_width;
    for(int y=0;y<view_height;y++)map_y[y]=y*144/view_height;
}
static void display_init(void) {
    lcd_done=xSemaphoreCreateBinary();configASSERT(lcd_done);
    spi_bus_config_t bus={.mosi_io_num=PIN_MOSI,.miso_io_num=-1,.sclk_io_num=PIN_CLK,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=320*STRIPE_ROWS*2};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t ic={.cs_gpio_num=PIN_CS,.dc_gpio_num=PIN_DC,
        .spi_mode=0,.pclk_hz=40000000,.trans_queue_depth=2,.lcd_cmd_bits=8,.lcd_param_bits=8,
        .on_color_trans_done=transfer_done};
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&ic,&io));
    esp_lcd_panel_dev_config_t pc={.reset_gpio_num=PIN_RST,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io,&pc,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel,true,false));
    stripe=heap_caps_malloc(320*STRIPE_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);configASSERT(stripe);
    configure_view();clear_display();ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
}
static void render_pixels(const uint16_t *fb) {
    int w=view_width,h=view_height;
    int left=(320-w)/2,top=(240-h)/2;
    for(int y=0;y<h;y+=STRIPE_ROWS) {
        int rows=h-y<STRIPE_ROWS?h-y:STRIPE_ROWS;
        for(int row=0;row<rows;row++) {
            const uint16_t *line=fb+map_y[y+row]*160;
            for(int x=0;x<w;x++) {
                uint16_t c=line[map_x[x]];
                stripe[row*w+x]=(c<<8)|(c>>8);
            }
        }
        draw_rect(left,top+y,w,rows);
    }
}
static void display_worker(void *unused) {
    (void)unused;
    while(1) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        int64_t began=esp_timer_get_time();
        render_pixels(display_snapshot);
        portENTER_CRITICAL(&display_stats_lock);
        display_us+=esp_timer_get_time()-began;
        ++drawn_interval;
        portEXIT_CRITICAL(&display_stats_lock);
        xSemaphoreGive(display_idle);
    }
}
static void display_start(void) {
    display_snapshot=malloc(GB_WIDTH*GB_HEIGHT*sizeof(uint16_t));configASSERT(display_snapshot);
    display_idle=xSemaphoreCreateBinary();configASSERT(display_idle);
    xSemaphoreGive(display_idle);
    configASSERT(xTaskCreate(display_worker,"lcd",4096,NULL,2,&display_task)==pdPASS);
}
static void queue_render(void) {
    if(xSemaphoreTake(display_idle,0)!=pdTRUE)return;
    memcpy(display_snapshot,core_pixels(),GB_WIDTH*GB_HEIGHT*sizeof(uint16_t));
    xTaskNotifyGive(display_task);
}
static void set_scale(int value) {
    if(!display_idle || xSemaphoreTake(display_idle,pdMS_TO_TICKS(2000))!=pdTRUE)return;
    scale=value;configure_view();
    memcpy(display_snapshot,core_pixels(),GB_WIDTH*GB_HEIGHT*sizeof(uint16_t));
    clear_display();render_pixels(display_snapshot);
    xSemaphoreGive(display_idle);
}
static void buttons_init(void) {
    gpio_config_t out={.pin_bit_mask=(1ULL<<PIN_LOAD)|(1ULL<<PIN_SHIFT_CLK)|(1ULL<<PIN_LED),.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_LOAD,1);gpio_set_level(PIN_SHIFT_CLK,0);gpio_set_level(PIN_LED,0);
    gpio_config_t in={.pin_bit_mask=(1ULL<<PIN_DATA)|(1ULL<<PIN_START),.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&in));
}
static uint16_t raw_buttons(void) {
    gpio_set_level(PIN_LOAD,0);esp_rom_delay_us(1);gpio_set_level(PIN_LOAD,1);esp_rom_delay_us(1);
    uint16_t pressed=0;
    for(unsigned i=0;i<8;i++) {
        if(!gpio_get_level(PIN_DATA))pressed|=1u<<i;
        gpio_set_level(PIN_SHIFT_CLK,1);esp_rom_delay_us(1);gpio_set_level(PIN_SHIFT_CLK,0);esp_rom_delay_us(1);
    }
    if(!gpio_get_level(PIN_START))pressed|=1u<<8;
    int64_t now=esp_timer_get_time();
    if(pressed!=button_candidate) { button_candidate=pressed;button_changed=now; }
    if(now-button_changed>=10000)button_stable=button_candidate;
    return button_stable;
}
static uint8_t map_buttons(uint16_t b) {
    uint8_t m=0;
    if(b&1)m|=KEY_A;
    if(b&2)m|=KEY_B;
    if(b&4)m|=KEY_SELECT;
    if(b&8)m|=KEY_DOWN;
    if(b&16)m|=KEY_LEFT;
    if(b&32)m|=KEY_RIGHT;
    if(b&64)m|=KEY_UP;
    if(b&256)m|=KEY_START;
    return m;
}
static uint32_t save_crc(const save_header *h,const uint8_t *ram) {
    uint32_t c=esp_rom_crc32_le(0,h->rtc,5);return esp_rom_crc32_le(c,ram,GB_CART_RAM);
}
static void save_load(void) {
    save_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"gamesave");
    if(!save_partition) { reply("SAVE unavailable\n");return; }
    save_header best={0},h;
    uint8_t *scratch=malloc(GB_CART_RAM);configASSERT(scratch);
    for(int slot=0;slot<2;slot++) {
        uint32_t offset=slot*SAVE_SLOT;
        if(esp_partition_read(save_partition,offset,&h,sizeof(h))!=ESP_OK||h.magic!=SAVE_MAGIC||h.version!=1||h.bytes!=GB_CART_RAM)continue;
        if(esp_partition_read(save_partition,offset+sizeof(h),scratch,GB_CART_RAM)!=ESP_OK||save_crc(&h,scratch)!=h.crc)continue;
        if(active_slot<0 || (int32_t)(h.sequence-best.sequence)>0) {
            best=h;active_slot=slot;
            memcpy(core_save_ram(),scratch,GB_CART_RAM);core_set_rtc(h.rtc);
        }
    }
    free(scratch);sequence=best.sequence;core_save_clean();
    reply("SAVE loaded=%d sequence=%u\n",active_slot>=0,sequence);
}
static void save_write(void) {
    if(!save_partition)return;
    int target=active_slot==0?1:0;
    save_header h={.magic=SAVE_MAGIC,.version=1,.sequence=sequence+1,.bytes=GB_CART_RAM};
    core_get_rtc(h.rtc);h.crc=save_crc(&h,core_save_ram());
    uint32_t offset=target*SAVE_SLOT;
    esp_err_t e=esp_partition_erase_range(save_partition,offset,SAVE_SLOT);
    if(e==ESP_OK)e=esp_partition_write(save_partition,offset+sizeof(h),core_save_ram(),GB_CART_RAM);
    if(e==ESP_OK)e=esp_partition_write(save_partition,offset,&h,sizeof(h));
    if(e==ESP_OK) { active_slot=target;sequence=h.sequence;core_save_clean(); }
    badge_led_event(e==ESP_OK?LED_SAVED:LED_ERROR);
    reply("SAVE result=%s sequence=%u\n",esp_err_to_name(e),sequence);
}
static void status(void) {
    reply("STATUS frames=%u pc=%04x errors=%u dirty=%d heap=%lu largest=%lu core=%u paused=%d render=%d scale=%d skip=%d save_seq=%u led=%u led_errors=%u profile=%d hurt=%u hit=%u caught=%u buttons=%03x\n",
        core_frames(),core_pc(),core_errors(),core_save_dirty(),(unsigned long)esp_get_free_heap_size(),
        (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)core_state_bytes(),paused,render_divisor,scale,core_skip,sequence,
        badge_led_get_level(),badge_led_errors(),battle_profile,hurt_events,hit_events,catch_events,button_stable);
}
static uint8_t key_name(const char *s) {
    if(!strcasecmp(s,"a"))return KEY_A;
    if(!strcasecmp(s,"b"))return KEY_B;
    if(!strcasecmp(s,"start"))return KEY_START;
    if(!strcasecmp(s,"select")||!strcasecmp(s,"home"))return KEY_SELECT;
    if(!strcasecmp(s,"up"))return KEY_UP;
    if(!strcasecmp(s,"down"))return KEY_DOWN;
    if(!strcasecmp(s,"left"))return KEY_LEFT;
    if(!strcasecmp(s,"right"))return KEY_RIGHT;
    return 0;
}
static void screenshot(void) {
    reply("FRAME 160 144 46080\n");
    const uint8_t *bytes=(const uint8_t *)core_pixels();
    for(unsigned i=0;i<46080;i+=1024) {
        unsigned n=46080-i<1024?46080-i:1024;
        int written=usb_serial_jtag_write_bytes(bytes+i,n,pdMS_TO_TICKS(1000));
        if(written!=(int)n)return;
    }
    reply("\nFRAME_END\n");
}
static void command(char *text) {
    char verb[24]={0},arg[32]={0};int value=0;
    sscanf(text,"%23s %31s %d",verb,arg,&value);
    if(!strcmp(verb,"status"))status();
    else if(!strcmp(verb,"shot"))screenshot();
    else if(!strcmp(verb,"name"))reply("RENAME result=%d; use the game's SAVE menu to persist\n",core_rename_new_trainer(arg));
    else if(!strcmp(verb,"pause")) { paused=1;reply("OK paused\n"); }
    else if(!strcmp(verb,"resume")) { paused=0;reply("OK resumed\n"); }
    else if(!strcmp(verb,"press")||!strcmp(verb,"hold")) {
        uint8_t key=key_name(arg);if(!key)reply("ERROR key\n");
        else { injected=key;int ms=value>0?value:150;if(ms>10000)ms=10000;injection_until=esp_timer_get_time()+ms*1000LL;reply("OK input %s %d\n",arg,ms); }
    }
    else if(!strcmp(verb,"render")) { int n=atoi(arg);if(n>=1&&n<=12)render_divisor=n;reply("OK render=%d\n",render_divisor); }
    else if(!strcmp(verb,"skip")) { int n=atoi(arg);if(n==0||n==1) { core_skip=n;core_set_frameskip(n); }reply("OK skip=%d\n",core_skip); }
    else if(!strcmp(verb,"scale")) { int n=atoi(arg);if(n>=1&&n<=4)set_scale(n);reply("OK scale=%d\n",scale); }
    else if(!strcmp(verb,"led")) { int n=atoi(arg);if(n>=0&&n<=3)badge_led_level(n);reply("OK led=%u\n",badge_led_get_level()); }
    else if(!strcmp(verb,"ledtest")) {
        unsigned e=!strcmp(arg,"hurt")?LED_PLAYER_HURT:!strcmp(arg,"hit")?LED_ENEMY_HURT:
            !strcmp(arg,"catch")?LED_CAUGHT:!strcmp(arg,"save")?LED_SAVED:0;
        if(e)badge_led_event(e);
        reply("LED_TEST pattern=%s simulated=%d\n",arg,e!=0);
    }
    else if(!strcmp(verb,"save"))save_write();
    else if(!strcmp(verb,"reset")) { save_write();esp_restart(); }
    else reply("COMMANDS status shot pause resume press KEY [ms] hold KEY ms render 1..12 scale 1..4 skip 0|1 led 0..3 ledtest hurt|hit|catch|save save reset\n");
    reply("crystal> ");
}
static void console_poll(void) {
    uint8_t bytes[64];int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
    for(int i=0;i<n;i++) {
        char c=bytes[i];
        if(c=='\r'||c=='\n') { if(input_used) { input[input_used]=0;command(input);input_used=0; } }
        else if(c>=32&&c<127&&input_used<sizeof(input)-1)input[input_used++]=c;
    }
}
void app_main(void) {
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=8192,.rx_buffer_size=1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    usb_serial_jtag_vfs_use_driver();
    buttons_init();
    esp_err_t led_result=badge_led_init();
    reply("LED_INIT %s\n",esp_err_to_name(led_result));
    display_init();
    const esp_partition_t *rom=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"gamerom");
    configASSERT(rom&&rom->size==0x200000);
    const void *mapped;esp_partition_mmap_handle_t handle;
    ESP_ERROR_CHECK(esp_partition_mmap(rom,0,rom->size,ESP_PARTITION_MMAP_DATA,&mapped,&handle));
    int init=core_init(mapped,rom->size);
    reply("CRYSTAL_TEST init=%d core_bytes=%u CPU=160MHz ROM=2MiB no_audio no_radio\n",init,(unsigned)core_state_bytes());
    if(init) { badge_led_event(LED_ERROR);while(1) { console_poll();badge_led_tick(true);vTaskDelay(pdMS_TO_TICKS(50)); } }
    battle_profile=esp_rom_crc32_le(0,mapped,rom->size)==CRYSTAL_ROM_CRC32;
    core_event_profile(battle_profile);
    core_set_frameskip(core_skip);
    save_load();display_start();status();reply("crystal> ");
    badge_led_event(LED_STARTUP);
    int64_t rtc_time=esp_timer_get_time(),auto_save=rtc_time,next_frame=rtc_time;
    report_time=rtc_time;
    uint16_t previous=0;
    while(1) {
        console_poll();int64_t now=esp_timer_get_time();
        uint16_t raw=raw_buttons();
        uint16_t gameplay_buttons=raw;
        /* HOME+START: explicit save and pause. HOME alone is GB Select. */
        if((raw&260)==260 && (previous&260)!=260) { save_write();paused=!paused;status(); }
        if((raw&260)==260)gameplay_buttons&=~260;
        /* Full-screen remains fixed during normal play; scale is USB-only. */
        if((raw&36)==36) {
            gameplay_buttons&=~36;
            if((previous&36)!=36) { render_divisor=render_divisor==4?2:render_divisor+1;status(); }
        }
        if((raw&6)==6) {
            gameplay_buttons&=~6;
            if((previous&6)!=6) { badge_led_level(badge_led_get_level()?0:2);status(); }
        }
        previous=raw;
        if(now>=injection_until)injected=0;
        if(!paused) {
            int64_t t=esp_timer_get_time();core_frame(map_buttons(gameplay_buttons)|injected);emulation_us+=esp_timer_get_time()-t;
            if(battle_profile) {
                unsigned e=battle_fx_update(&battle_state,core_read_wram_bank)|core_take_events();
                if(e&BATTLE_PLAYER_HURT)++hurt_events;
                if(e&BATTLE_ENEMY_HURT)++hit_events;
                if(e&LED_CAUGHT)++catch_events;
                if(e) { badge_led_event(e);reply("BATTLE_LED events=%u hurt=%u hit=%u caught=%u\n",e,hurt_events,hit_events,catch_events); }
            }
            ++frames_interval;
            if(core_frames()%render_divisor==0)queue_render();
            if(core_errors()) { paused=1;badge_led_event(LED_ERROR);status(); }
            next_frame+=16743;
            int64_t wait=next_frame-esp_timer_get_time();
            if(wait>1000)vTaskDelay(pdMS_TO_TICKS(wait/1000));
            /* Keep pacing debt through a slow LCD frame; cap catch-up at 100 ms. */
            else if(wait < -100000)next_frame=esp_timer_get_time();
        } else vTaskDelay(pdMS_TO_TICKS(10));
        now=esp_timer_get_time();
        badge_led_tick(paused!=0);
        if(now-rtc_time>=1000000) { unsigned seconds=(now-rtc_time)/1000000;if(seconds>60)seconds=60;for(unsigned i=0;i<seconds;i++)core_tick_rtc();rtc_time+=seconds*1000000LL; }
        if(now-auto_save>=30000000) { if(core_save_dirty())save_write();auto_save=now; }
        if(now-report_time>=10000000) {
            uint64_t elapsed=now-report_time;
            portENTER_CRITICAL(&display_stats_lock);
            unsigned drawn=drawn_interval;uint64_t drawing_time=display_us;
            drawn_interval=0;display_us=0;
            portEXIT_CRITICAL(&display_stats_lock);
            reply("PERF emu_fps=%.2f draw_fps=%.2f cpu_ms=%.2f draw_ms=%.2f heap=%lu errors=%u\n",
                frames_interval*1000000.0/elapsed,drawn*1000000.0/elapsed,
                frames_interval?emulation_us/1000.0/frames_interval:0.0,
                drawn?drawing_time/1000.0/drawn:0.0,(unsigned long)esp_get_free_heap_size(),core_errors());
            report_time=now;frames_interval=0;emulation_us=0;
        }
    }
}
