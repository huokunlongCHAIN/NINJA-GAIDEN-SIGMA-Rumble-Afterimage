#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;
 std::filesystem::path dll=std::filesystem::absolute(argv[1]);
 auto logs=dll.parent_path()/L"NGS1Rumble";
 if(std::filesystem::exists(logs)){std::cerr<<"Use a fresh component folder for the startup test.\n";return 3;}
 HMODULE module=LoadLibraryW(dll.c_str());
 if(!module){std::cerr<<"LoadLibrary failed: "<<GetLastError()<<"\n";return 4;}
 Sleep(100);
 if(std::filesystem::exists(logs)){std::cerr<<"Core started during DLL loading.\n";return 5;}
 using CreateFn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
 auto create=reinterpret_cast<CreateFn>(GetProcAddress(module,"DirectInput8Create"));
 if(!create)return 6;
 for(int i=0;i<2;++i){
  IDirectInput8W* input=nullptr;
  HRESULT result=create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr);
  if(FAILED(result)||!input){std::cerr<<"DirectInput forwarding failed: "<<std::hex<<result<<"\n";return 7;}
  input->Release();
 }
 unsigned count=0;
 for(auto& file:std::filesystem::directory_iterator(logs))if(file.path().extension()==L".log")++count;
 if(count!=1){std::cerr<<"Core initialization did not run exactly once.\n";return 8;}
 std::cout<<"PASS: no initialization in DllMain; first input call starts one worker; both DirectInput calls forward successfully.\n";
 return 0;
}
