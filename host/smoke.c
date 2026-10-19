#include "gb_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void image(const char *dir,unsigned frame) {
    char path[1024]; snprintf(path,sizeof(path),"%s/frame-%06u.ppm",dir,frame);
    FILE *out=fopen(path,"wb"); if (!out) { perror(path); exit(2); }
    fprintf(out,"P6\n160 144\n255\n");
    uint16_t *p=core_pixels();
    for(unsigned i=0;i<GB_WIDTH*GB_HEIGHT;i++) {
        unsigned c=p[i];
        unsigned char rgb[3]={((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31};
        fwrite(rgb,1,3,out);
    }
    fclose(out);
}
int main(int argc,char **argv) {
    if(argc<4) { fprintf(stderr,"usage: smoke rom output-dir frames [input-script] [cartridge-save]\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) { perror("rom");return 2; }
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    uint8_t *rom=malloc(size); if(!rom||fread(rom,1,size,f)!=(size_t)size)return 2;fclose(f);
    int init=core_init(rom,size); printf("init=%d state_bytes=%zu\n",init,core_state_bytes());if(init)return 1;
    if(argc>5) {
        FILE *saved=fopen(argv[5],"rb");
        if(saved) {
            uint8_t rtc[5];
            if(fread(core_save_ram(),1,GB_CART_RAM,saved)!=GB_CART_RAM || fread(rtc,1,5,saved)!=5)return 2;
            core_set_rtc(rtc);core_save_clean();fclose(saved);
            printf("cartridge_save_loaded=1\n");
        }
    }
    unsigned n=(unsigned)atoi(argv[3]);
    FILE *script=argc>4?fopen(argv[4],"r"):NULL;
    unsigned at=~0u,duration=0,mask=0,until=0; uint8_t held=0;
    if(script) { if(fscanf(script,"%u %u %u",&at,&duration,&mask)!=3)at=~0u; }
    clock_t start=clock();
    for(unsigned frame=0;frame<n;frame++) {
        if(frame==until)held=0;
        if(frame==at) {
            held=(uint8_t)mask;until=frame+duration;
            if(fscanf(script,"%u %u %u",&at,&duration,&mask)!=3)at=~0u;
        }
        core_frame(held);
        if((frame+1)%60==0)core_tick_rtc();
        if((frame+1)%300==0) { image(argv[2],frame+1);printf("frame=%u pc=%04x errors=%u dirty=%d\n",frame+1,core_pc(),core_errors(),core_save_dirty()); }
        if(core_errors())return 1;
    }
    image(argv[2],n);
    if(argc>5) {
        uint8_t rtc[5];core_get_rtc(rtc);
        FILE *saved=fopen(argv[5],"wb");if(!saved)return 2;
        if(fwrite(core_save_ram(),1,GB_CART_RAM,saved)!=GB_CART_RAM || fwrite(rtc,1,5,saved)!=5)return 2;
        fclose(saved);
    }
    printf("frames=%u host_seconds=%.3f errors=%u\n",n,(double)(clock()-start)/CLOCKS_PER_SEC,core_errors());
    if(script)fclose(script);
    free(rom);return 0;
}
