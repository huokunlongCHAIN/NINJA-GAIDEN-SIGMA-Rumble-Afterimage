#pragma once
#include <windows.h>
#include <xinput.h>
#include <stdint.h>
#include <cstring>

// Independently implemented from the public DualSense HID wire layout described
// by SDL's PS5 driver. No SDL code is linked. See reference/PROTOCOL-SOURCES.md.
namespace DualSenseProtocol {
inline uint32_t Crc(uint8_t seed,const uint8_t* data,size_t size){
 uint32_t crc=0xffffffffu;auto feed=[&](uint8_t b){crc^=b;for(unsigned n=0;n<8;++n)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));};
 feed(seed);for(size_t i=0;i<size;++i)feed(data[i]);return ~crc;
}
inline SHORT Axis(uint8_t value,bool invert=false){int x=int(value)-128;
 if(invert)return SHORT(x<=0?(-x*32767)/128:-(x*32768)/127);
 return SHORT(x<0?(x*32768)/128:(x*32767)/127);
}
inline bool Decode(const uint8_t* data,size_t size,bool bluetooth,XINPUT_GAMEPAD& state){
 if(!data||size<10)return false;const uint8_t* p=nullptr;bool simple=false;
 if(data[0]==0x31&&bluetooth&&size==78){uint32_t received=0;memcpy(&received,data+74,4);if(received!=Crc(0xa1,data,74))return false;p=data+2;}
 else if(data[0]==1&&((!bluetooth&&size==64)||(bluetooth&&(size==10||size==78)))){p=data+1;simple=bluetooth;}
 else return false;
 XINPUT_GAMEPAD result{};result.sThumbLX=Axis(p[0]);result.sThumbLY=Axis(p[1],true);result.sThumbRX=Axis(p[2]);result.sThumbRY=Axis(p[3],true);
 unsigned buttons=simple?4:7;result.bLeftTrigger=p[simple?7:4];result.bRightTrigger=p[simple?8:5];
 uint8_t a=p[buttons],b=p[buttons+1];unsigned hat=a&15;
 static const WORD hats[]={XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_UP};
 if(hat<8)result.wButtons=hats[hat];
 if(a&0x10)result.wButtons|=XINPUT_GAMEPAD_X;if(a&0x20)result.wButtons|=XINPUT_GAMEPAD_A;if(a&0x40)result.wButtons|=XINPUT_GAMEPAD_B;if(a&0x80)result.wButtons|=XINPUT_GAMEPAD_Y;
 if(b&1)result.wButtons|=XINPUT_GAMEPAD_LEFT_SHOULDER;if(b&2)result.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
 if(b&0x10)result.wButtons|=XINPUT_GAMEPAD_BACK;if(b&0x20)result.wButtons|=XINPUT_GAMEPAD_START;
 if(b&0x40)result.wButtons|=XINPUT_GAMEPAD_LEFT_THUMB;if(b&0x80)result.wButtons|=XINPUT_GAMEPAD_RIGHT_THUMB;
 state=result;return true;
}
inline size_t Output(uint8_t* report,size_t capacity,bool bluetooth,bool improved,uint8_t sequence,WORD left,WORD right,size_t usbSize=48){
 size_t size=bluetooth?78:usbSize;if(size<48||size>128||capacity<size)return 0;memset(report,0,size);
 size_t offset=bluetooth?3:1;report[0]=bluetooth?0x31:0x02;
 if(bluetooth){report[1]=uint8_t(sequence<<4);report[2]=0x10;}
 report[offset]=improved?0x02:0x03; // Motor-valid flags only: no audio volume, LED or trigger changes.
 if(improved)report[offset+38]=0x04;
 uint8_t l=uint8_t((uint32_t(left)*255+32767)/65535),r=uint8_t((uint32_t(right)*255+32767)/65535);
 report[offset+2]=improved?r:r>>1;report[offset+3]=improved?l:l>>1;
 if(bluetooth){uint32_t crc=Crc(0xa2,report,74);memcpy(report+74,&crc,4);}return size;
}
}
