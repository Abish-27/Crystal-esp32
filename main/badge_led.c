#include "badge_led.h"
#include <string.h>
#include "driver/rmt_tx.h"
#include "esp_timer.h"

static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static uint8_t pixels[18],last[18];
static unsigned level=2,errors;
static int64_t next_frame,hurt_until,hit_until,catch_until,save_until,error_until,start_until;

static void show(void) {
    if(!channel || !memcmp(pixels,last,sizeof(pixels)))return;
    rmt_transmit_config_t tx={.loop_count=0,.flags.eot_level=0};
    esp_err_t e=rmt_transmit(channel,encoder,pixels,sizeof(pixels),&tx);
    if(e==ESP_OK)e=rmt_tx_wait_all_done(channel,50);
    if(e==ESP_OK)memcpy(last,pixels,sizeof(last));
    else ++errors;
}
esp_err_t badge_led_init(void) {
    rmt_tx_channel_config_t cfg={.gpio_num=3,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=10000000,.mem_block_symbols=48,.trans_queue_depth=1};
    esp_err_t e=rmt_new_tx_channel(&cfg,&channel);
    if(e!=ESP_OK)return e;
    rmt_bytes_encoder_config_t bits={
        .bit0={.level0=1,.duration0=3,.level1=0,.duration1=9},
        .bit1={.level0=1,.duration0=9,.level1=0,.duration1=3},
        .flags.msb_first=1};
    e=rmt_new_bytes_encoder(&bits,&encoder);
    if(e==ESP_OK)e=rmt_enable(channel);
    if(e!=ESP_OK) {
        if(encoder)rmt_del_encoder(encoder);
        rmt_del_channel(channel);channel=NULL;return e;
    }
    memset(last,255,sizeof(last));show();return ESP_OK;
}
static void pixel(unsigned i,unsigned r,unsigned g,unsigned b) {
    static const unsigned caps[]={0,6,12,24};
    unsigned cap=caps[level];
    pixels[i*3]=g*cap/24;pixels[i*3+1]=r*cap/24;pixels[i*3+2]=b*cap/24;
}
void badge_led_event(unsigned events) {
    int64_t now=esp_timer_get_time()/1000;
    if(events&LED_PLAYER_HURT)hurt_until=now+350;
    if(events&LED_ENEMY_HURT)hit_until=now+350;
    if(events&LED_CAUGHT) { catch_until=now+1600;hurt_until=hit_until=0; }
    if(events&LED_SAVED)save_until=now+450;
    if(events&LED_ERROR)error_until=now+900;
    if(events&LED_STARTUP)start_until=now+900;
}
void badge_led_tick(bool paused) {
    int64_t now=esp_timer_get_time()/1000;
    if(now<next_frame)return;
    next_frame=now+40;memset(pixels,0,sizeof(pixels));
    if(level) {
        if(error_until>now) {
            unsigned v=(error_until-now)%450*24/450;
            for(unsigned i=0;i<6;i++)pixel(i,v,0,0);
        } else if(catch_until>now) {
            unsigned head=((1600-(catch_until-now))/100)%6;
            pixel(head,0,24,2);pixel((head+5)%6,0,10,0);pixel((head+4)%6,0,3,0);
        } else if(save_until>now) {
            unsigned v=(save_until-now)*24/450;
            for(unsigned i=0;i<6;i++)pixel(i,0,v,v);
        } else if(start_until>now) {
            unsigned head=((900-(start_until-now))/140)%6;pixel(head,0,12,24);
        } else {
            if(paused) {
                unsigned t=now%2000;unsigned v=3+(t<1000?t:2000-t)*9/1000;
                pixel(0,v,v/3,0);pixel(1,v,v/3,0);
            }
            if(hurt_until>now) {
                unsigned v=(hurt_until-now)*24/350;
                pixel(0,v,0,0);pixel(5,v,0,0);pixel(4,v,0,0);
            }
            if(hit_until>now) {
                unsigned v=(hit_until-now)*24/350;
                pixel(1,0,v/3,v);pixel(2,0,v/3,v);pixel(3,0,v/3,v);
            }
        }
    }
    show();
}
void badge_led_level(unsigned value) {
    if(value>3)return;
    level=value;next_frame=0;
    if(!level) { memset(pixels,0,sizeof(pixels));show(); }
}
unsigned badge_led_get_level(void) { return level; }
unsigned badge_led_errors(void) { return errors; }
