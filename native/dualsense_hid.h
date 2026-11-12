#pragma once
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <atomic>
#include <vector>
#include "dualsense_protocol.h"

class DualSenseHid {
 HANDLE device=INVALID_HANDLE_VALUE;OVERLAPPED read{},write{};bool reading=false,writing=false,bluetooth=false,improved=false;
 BYTE input[128]{},output[128]{};USHORT inputSize=0,outputSize=0;uint8_t sequence=0;
 SRWLOCK stateLock=SRWLOCK_INIT;XINPUT_STATE cached{};
 std::atomic<bool> connected{false};std::atomic<ULONGLONG> lastInput{0};
 std::atomic<DWORD> wanted{0};std::atomic<ULONGLONG> requested{0};
 ULONGLONG nextScan=0,lastWrite=0;DWORD sent=~0u,error=0;unsigned firmware=0;
 void Cancel(OVERLAPPED& ov,bool& pending){if(pending){CancelIoEx(device,&ov);DWORD ignored=0;GetOverlappedResult(device,&ov,&ignored,TRUE);pending=false;}}
 bool Consume(DWORD bytes){XINPUT_GAMEPAD pad{};if(!DualSenseProtocol::Decode(input,bytes,bluetooth,pad))return false;
  AcquireSRWLockExclusive(&stateLock);if(memcmp(&cached.Gamepad,&pad,sizeof(pad)))++cached.dwPacketNumber;cached.Gamepad=pad;ReleaseSRWLockExclusive(&stateLock);
  lastInput=GetTickCount64();connected=true;return true;}
 bool Open(){
  GUID guid;HidD_GetHidGuid(&guid);HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)return false;
  SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);
  for(DWORD n=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,n,&item);++n){
   DWORD size=0;SetupDiGetDeviceInterfaceDetailW(set,&item,nullptr,0,&size,nullptr);if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))continue;
   std::vector<BYTE> storage(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
   if(!SetupDiGetDeviceInterfaceDetailW(set,&item,detail,size,nullptr,nullptr))continue;
   HANDLE handle=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(handle==INVALID_HANDLE_VALUE)continue;
   HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);PHIDP_PREPARSED_DATA preparsed=nullptr;HIDP_CAPS caps{};
   bool supported=HidD_GetAttributes(handle,&attributes)&&attributes.VendorID==0x054c&&attributes.ProductID==0x0ce6&&HidD_GetPreparsedData(handle,&preparsed);
   if(supported){supported=HidP_GetCaps(preparsed,&caps)==HIDP_STATUS_SUCCESS&&caps.UsagePage==1&&caps.Usage==5;HidD_FreePreparsedData(preparsed);}
   supported=supported&&((caps.InputReportByteLength==64&&caps.OutputReportByteLength>=48&&caps.OutputReportByteLength<=128)||(caps.InputReportByteLength==78&&caps.OutputReportByteLength==78));
   if(!supported){CloseHandle(handle);continue;}
   device=handle;inputSize=caps.InputReportByteLength;outputSize=caps.OutputReportByteLength;bluetooth=inputSize==78;
   BYTE feature[64]{};feature[0]=0x20;firmware=HidD_GetFeature(device,feature,sizeof(feature))?unsigned(feature[44])|(unsigned(feature[45])<<8):0;
   improved=firmware>=0x224||(bluetooth&&firmware==0);HidD_SetNumInputBuffers(device,8);
   read.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);write.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   if(!read.hEvent||!write.hEvent){Close();continue;}
   wanted=0;sent=~0u;requested=GetTickCount64();lastInput=GetTickCount64();lastWrite=0;sequence=0;error=0;
   SetupDiDestroyDeviceInfoList(set);return true;
  }
  SetupDiDestroyDeviceInfoList(set);return false;
 }
public:
 bool IsConnected()const{return connected&&GetTickCount64()-lastInput.load()<750;}
 bool IsBluetooth()const{return bluetooth;}
 unsigned Firmware()const{return firmware;}
 bool Improved()const{return improved;}
 DWORD LastError()const{return error;}
 ULONGLONG LastInputTick()const{return lastInput;}
 ULONGLONG LastWriteTick()const{return lastWrite;}
 void Stop(){wanted=0;requested=GetTickCount64();}
 DWORD GetState(XINPUT_STATE* state){if(!state)return ERROR_BAD_ARGUMENTS;if(!IsConnected())return ERROR_DEVICE_NOT_CONNECTED;AcquireSRWLockShared(&stateLock);*state=cached;ReleaseSRWLockShared(&stateLock);return ERROR_SUCCESS;}
 DWORD Submit(const XINPUT_VIBRATION* vibration){if(!vibration)return ERROR_BAD_ARGUMENTS;if(!IsConnected())return ERROR_DEVICE_NOT_CONNECTED;wanted=DWORD(vibration->wLeftMotorSpeed)|(DWORD(vibration->wRightMotorSpeed)<<16);requested=GetTickCount64();return ERROR_SUCCESS;}
 void EmergencyStop(){
  wanted=0;if(device==INVALID_HANDLE_VALUE)return;
  Cancel(write,writing);BYTE zero[128]{};size_t size=DualSenseProtocol::Output(zero,sizeof(zero),bluetooth,improved,sequence++,0,0,outputSize);
  OVERLAPPED stop{};stop.hEvent=write.hEvent;ResetEvent(stop.hEvent);DWORD bytes=0;
  if(!WriteFile(device,zero,DWORD(size),&bytes,&stop)&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(stop.hEvent,100)!=WAIT_OBJECT_0)CancelIoEx(device,&stop);GetOverlappedResult(device,&stop,&bytes,TRUE);}
  sent=0;
 }
 void Close(){connected=false;wanted=0;if(device!=INVALID_HANDLE_VALUE){Cancel(read,reading);EmergencyStop();CloseHandle(device);device=INVALID_HANDLE_VALUE;}if(read.hEvent)CloseHandle(read.hEvent);if(write.hEvent)CloseHandle(write.hEvent);read={};write={};nextScan=GetTickCount64()+1000;}
 void Poll(){
  ULONGLONG now=GetTickCount64();if(device==INVALID_HANDLE_VALUE){if(now<nextScan)return;nextScan=now+1500;if(!Open())return;}
  for(unsigned n=0;n<32;++n){DWORD bytes=0;
   if(reading){if(!GetOverlappedResult(device,&read,&bytes,FALSE)){if(GetLastError()==ERROR_IO_INCOMPLETE)break;error=GetLastError();Close();return;}reading=false;Consume(bytes);}
   ResetEvent(read.hEvent);if(ReadFile(device,input,inputSize,&bytes,&read)){Consume(bytes);continue;}
   if(GetLastError()==ERROR_IO_PENDING){reading=true;break;}error=GetLastError();Close();return;
  }
  if(now-lastInput.load()>1500){error=ERROR_TIMEOUT;Close();return;}
  if(writing){DWORD bytes=0;if(!GetOverlappedResult(device,&write,&bytes,FALSE)){if(GetLastError()==ERROR_IO_INCOMPLETE)return;error=GetLastError();Close();return;}writing=false;}
  DWORD packed=wanted;if(packed&&now-requested.load()>750){packed=0;wanted=0;}
  if(packed==sent&&now-lastWrite<1000)return;
  size_t size=DualSenseProtocol::Output(output,sizeof(output),bluetooth,improved,sequence++,WORD(packed),WORD(packed>>16),outputSize);
  ResetEvent(write.hEvent);DWORD bytes=0;bool ok=WriteFile(device,output,DWORD(size),&bytes,&write)!=FALSE;
  if(!ok&&GetLastError()!=ERROR_IO_PENDING){error=GetLastError();Close();return;}writing=!ok;sent=packed;lastWrite=now;
 }
};
