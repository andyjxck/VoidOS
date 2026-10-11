/* Host test: runs firmware/sensors.c HR engine against a synthetic 72 BPM pulse at several main-loop periods.
   cc -O1 -Ifirmware -Iinclude -Icore -o /tmp/hrtest_bin tests/hr/test_hr.c -lm && /tmp/hrtest_bin */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "../../firmware/sensors.c"
static uint32_t T; static double bpm_true=72; static int quiet=0;
int i2c_read_reg(uint8_t a,uint8_t r,uint8_t*d,unsigned n){
  if(a==0x44&&r==0x08){ double t=T/1000.0, ph=fmod(t*bpm_true/60.0,1.0);
    double w = quiet?0: (0.5+0.5*sin(6.2831853*ph)+0.15*sin(12.566*ph+1.0));
    double noise=((rand()%1000)/1000.0-0.5)*(quiet?60:25);
    int v=(int)(20000+quiet*0+ w*400 + noise + 300*sin(t*0.3));
    memset(d,0,n); d[1]=(v>>8)&0xff; d[2]=((v>>4)&0x0f); d[7]=v&0x0f; return 1;}
  if(n) memset(d,0,n); return 1;}
int i2c_write_reg(uint8_t a,uint8_t r,uint8_t v){(void)a;(void)r;(void)v;return 1;}
int i2c_write_regs(uint8_t a,uint8_t r,const uint8_t*s,unsigned n){(void)a;(void)r;(void)s;(void)n;return 1;}
void hal_delay_ms(uint32_t m){T+=m;}
int main(){ int loops[]={50,100,150,200,300};
 for(int li=0;li<5;li++){ T=1000; hrs_ok=1; daq_n=0; last_daq_ms=0; bpm=0;bpm_valid=0;memset(hra,0,sizeof hra);spec_reset=1;
  int first=-1; for(int i=0;i<60000/loops[li];i++){T+=loops[li]; hrs_sample(T); if(bpm_valid&&first<0)first=T-1000;}
  printf("snr=%.2f beats=%u win_max=%d | ",last_snr,beats,(int)win_max); printf("loop %3dms: bpm=%d fs=%.1f first lock at %d ms\n",loops[li],bpm,hr_fs,first);}
 return 0;}
