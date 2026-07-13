#include "formulas.h"

// suppress warnings 
#pragma GCC diagnostic ignored "-Wparentheses"

uint32_t formula1(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Crowd
  return ((t<<1)^((t<<1)+(t>>7)&t>>12))|t>>(4-(1^7&(t>>19)))|t>>7; //crowd
  //return t%(t/(t>>9|t>>13)); //had trouble on teensy, doesnt work w/ web serial
  //return t&598?t>>4:t>>10; //ternary example (works)

}

uint32_t formula2(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
  return t*(t>>17&1?t>>16&1?24:21:16)>>4|t>>6^t>>8;
}

uint32_t formula3(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// ONE MILLION alarm clocks by rudi
  return t*t/(t>>12&t>>8&63)<<7;
}

uint32_t formula4(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
//example of a recursive formula, w is the previous bytebeat output
return w ^ (t >> (p3 >> 4)) >> (t / 6988 * t % (p1 + 1)) + (t << t / (p2 * 4));
}

uint32_t formula5(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// life 
return ((( ((((((t) >> p1) | (t)) | ((t) >> p1)) * p3) & ((5 * (t)) | ((t) >> p3)) ) | ((t) ^ (t % p2)) ) & 0xFF));
}

uint32_t formula6(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// love
return (((((t)*p1) & (t>>4)) | ((t*p3) & (t>>7)) | ((t*p2) & (t>>10))) & 0xFF); 
}

uint32_t formula7(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// music for a C64 game by skurk 
return (t*(t>>(t&4096?t*t>>12:t>>12))|t<<(t>>8)|t>>4);
}

uint32_t formula8(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// CA98 by rrrola 
return (t*(0xCA98CA98>>(t>>9&30)&15)|t>>8);
}

uint32_t formula9(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Louder clean melody by Niklas_Roy 
return (9*(t*((t>>9|t>>13)&15)&16));
}

uint32_t formula10(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// by isislovecruft
return ((~t>>2)*(2+(42&2*t*(7&t>>10))<(24&t*((3&t>>14)+2))));
}

uint32_t formula11(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Glissando 
return (t*t/(t>>13^t>>8));
}

uint32_t formula12(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Waiver
return ((t/91&t^t/90&t)-1);
}

uint32_t formula13(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Extraordinary thread of FRACTAL MUSIC
return (t>>4+t%34|t>>5+t%(t/31108&1?46:43)|t/4|t/8%35);    
}

uint32_t formula14(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Techno by Gabriel Miceli 
return (((t/10|0)^(t/10|0)-1280)%11*t/2&127)+(((t/640|0)^(t/640|0)-2)%13*t/2&127);
}

uint32_t formula15(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Dubstep-like sound by kOLbOSa_exe
return (8E5*t/(t>>2^t>>12));
}

uint32_t formula16(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Fractalized Past by lhphr
return (t>>10^t>>11)%5*((t>>14&3^t>>15&1)+1)*t%99+((3+(t>>14&3)-(t>>16&1))/3*t%99&64);
}


uint32_t formula17(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// percussive by formuladavis
return (9*t&t>>4|5*t&t>>7|3*t&t>>10)-1;
}

uint32_t formula18(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Good old fractal melody 
return (t>>9^(t>>9)-1^1)%13*t;
}

uint32_t formula19(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Recursive NIN by mk
return ((w^t)*t)/(w|t);
}

uint32_t formula20(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// noizewaves by mk
return (t>>9|t<<1)/(w*90) * (t*3/(w/80))|t/200;
}

uint32_t formula21(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Inter Time Dubstep by Federal_Loss2780
return (t>>(t>>13&31)&128)+((t&t>>12)*t>>12)+3e5/(t%16384);
}

uint32_t formula22(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// messing around with "0x___" by Zera12873
return t>>2|(0xC098C098>>(t>>10))*t|t>>10;
}

uint32_t formula23(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Arcade game by Niklas_Roy
return t*((t>>9|t>>13)&15)&129;
}

uint32_t formula24(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// sine-nomath by erlehmann
return (t&15)*(-t&15)*(((t&16)>>3)-1)*128/65+128;
}

uint32_t formula25(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Noise Hit 1
return ((50&t-177)>>356%t)*20 * (t<2000);  
}

uint32_t formula26(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// White noise snare drum
return (w+t+(w>>2))&874356 * (t<2000);    
}

uint32_t formula27(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Sqrt Kick
return (((int(sqrt(t % 16384)) << 5) & 64) << 1) * (t<16384); // Sqrt K
}

uint32_t formula28(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// zap kick
return ((1200/t)*150) * (t<2000);   
}

uint32_t formula29(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Tech kick
return (((1250&t-17)>>6%t)*40) * (t<2000);   
}

uint32_t formula30(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// hihat
return (3000-(3000%t))*(23/t)*(w+t) * (t<2000);
}

uint32_t formula31(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// tuned hat
return ((t^99)/(t>>4))<<1;
}

uint32_t formula32(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// Noise snare
//return ((520 - t%(t%w))/(t>>11));

//extra zap bd
return (((90000/t)*150) / (t<5000)); 
}

uint32_t formula33(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// double noise snare
return ((800 - t%(t%w))/(t>>11));
}

uint32_t formula34(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// open snare
return ((890&t%(t%126))/(t>>7))<<3;
}

uint32_t formula35(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// metallic
return ((8999&t%(t&11))/(t>>8))<<6;
}

uint32_t formula36(uint32_t t, uint8_t w, uint32_t p1, uint32_t p2, uint32_t p3) {
// vinal pop
return ((28959^(t>>2)%(t%230))/(t>>3))<<2;
}




#pragma GCC diagnostic ignored "-Wparentheses"
