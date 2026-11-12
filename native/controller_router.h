#pragma once
#include <windows.h>
#include <xinput.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
class ControllerRouter {
 XINPUT_GAMEPAD previousNative[4]{},previousDS{};bool hadDS=false;
 ULONGLONG dsActivity=0,nativeActivity=0;int nativeChoice=0;
 std::atomic<bool> selectedDS{false};std::atomic<int> selectedNative{0};
 static bool Activity(const XINPUT_GAMEPAD& old,const XINPUT_GAMEPAD& value){
  if(value.wButtons&~old.wButtons)return true;
  if(value.bLeftTrigger>30&&abs(int(value.bLeftTrigger)-old.bLeftTrigger)>8)return true;
  if(value.bRightTrigger>30&&abs(int(value.bRightTrigger)-old.bRightTrigger)>8)return true;
  const SHORT before[]={old.sThumbLX,old.sThumbLY,old.sThumbRX,old.sThumbRY};const SHORT after[]={value.sThumbLX,value.sThumbLY,value.sThumbRX,value.sThumbRY};
  for(unsigned i=0;i<4;++i)if(abs(int(after[i]))>(i<2?7849:8689)&&abs(int(after[i])-before[i])>2048)return true;
  return false;
 }
public:
 bool Poll(DWORD(WINAPI* get)(DWORD,XINPUT_STATE*),DualSenseHid& ds,bool prefer){
  auto now=GetTickCount64();XINPUT_STATE sony{};bool ready=ds.GetState(&sony)==ERROR_SUCCESS;
  if(ready){if(!hadDS){previousDS=sony.Gamepad;if(prefer)dsActivity=now;}else if(Activity(previousDS,sony.Gamepad))dsActivity=now;previousDS=sony.Gamepad;}
  hadDS=ready;unsigned mask=0;for(DWORD i=0;i<4;++i){XINPUT_STATE native{};if(get(i,&native)==ERROR_SUCCESS){mask|=1u<<i;
    if(Activity(previousNative[i],native.Gamepad)&&(now-dsActivity>50||!ready)){nativeActivity=now;nativeChoice=int(i);}previousNative[i]=native.Gamepad;
   }else previousNative[i]={};}
  if(!(mask&(1u<<nativeChoice))){nativeChoice=0;for(unsigned i=0;i<4;++i)if(mask&(1u<<i)){nativeChoice=int(i);break;}}
  bool useDS=ready&&(!mask||dsActivity>nativeActivity||(dsActivity==nativeActivity&&prefer));
  bool changed=selectedDS.exchange(useDS)!=useDS;int old=selectedNative.exchange(nativeChoice);return changed||(!useDS&&old!=nativeChoice);
 }
 bool IsDualSense()const{return selectedDS;}
 DWORD NativeIndex(DWORD gameIndex)const{int active=selectedNative;if(gameIndex==0)return DWORD(active);if(gameIndex==DWORD(active)&&active!=0)return ~0u;return gameIndex;}
};
