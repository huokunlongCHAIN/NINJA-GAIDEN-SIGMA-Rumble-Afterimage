#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <vector>
#include <cstring>

// Language/resource changes are accepted only when the complete compiled code,
// unwind table, memory layout and all three input/vtable anchors match this family.
namespace GameCompatibility {
enum class Kind {Unsupported,Original,Localized,Original104};
inline const char* Name(Kind kind){return kind==Kind::Original?"1.0.0.2-original":kind==Kind::Localized?"1.0.0.2-localized":kind==Kind::Original104?"1.0.0.4-original":"unsupported";}
inline bool SameLayout(Kind a,Kind b){return a!=Kind::Unsupported&&b!=Kind::Unsupported&&((a==Kind::Original104)==(b==Kind::Original104));}
// Configuration RVAs retain the 1.0.0.2 coordinate system, including after F5.
// Map only the seven verified code locations; script/data offsets are unchanged.
inline uintptr_t CodeRva(Kind kind,uintptr_t rva){
    if(kind==Kind::Original104)switch(rva){
        case 0x6344de:case 0x634bcd:case 0x6352b3:case 0x68e7f7:
        case 0x6906c4:case 0x690770:case 0x699450:return rva+0x30;
    }
    return rva;
}
inline const char* UnwindHash(Kind kind){return kind==Kind::Original104?"4A86A4DD23E0CC9D58DC556FC71E3F3D31009B15E14D0A89C85F8871705D788C":"DC27B2B7A5B54EC068FCC28EE7A2EA4F67BCDC720313DB9B3942C17FE38B921D";}
inline bool Sha256(const BYTE* data,size_t length,char hex[65]){
    if(length>0xffffffffu)return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE digest[32];
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
            BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0 &&
            BCryptHashData(hash,const_cast<BYTE*>(data),ULONG(length),0)>=0 &&
            BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(ok){const char digits[]="0123456789ABCDEF";for(unsigned i=0;i<32;++i){hex[2*i]=digits[digest[i]>>4];hex[2*i+1]=digits[digest[i]&15];}hex[64]=0;}
    return ok;
}
inline Kind InspectBytes(const BYTE* data,size_t size){
    if(!data||size<64+sizeof(IMAGE_NT_HEADERS64)||size>128*1024*1024)return Kind::Unsupported;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||size_t(dos->e_lfanew)>size-sizeof(IMAGE_NT_HEADERS64))return Kind::Unsupported;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(data+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       nt->FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
       nt->FileHeader.NumberOfSections<4||nt->FileHeader.NumberOfSections>96||
       nt->OptionalHeader.ImageBase!=0x140000000ull||nt->OptionalHeader.SizeOfImage<0x435f000||
       nt->OptionalHeader.SizeOfImage>0x6000000)return Kind::Unsupported;
    size_t at=size_t(dos->e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt->FileHeader.SizeOfOptionalHeader;
    if(at>size||size_t(nt->FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>size-at)return Kind::Unsupported;
    const auto* sections=reinterpret_cast<const IMAGE_SECTION_HEADER*>(data+at);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        if(uint64_t(s.PointerToRawData)+s.SizeOfRawData>size||uint64_t(s.VirtualAddress)+s.Misc.VirtualSize>nt->OptionalHeader.SizeOfImage)return Kind::Unsupported;
    }
    auto find=[&](const char* name)->const IMAGE_SECTION_HEADER*{
        const IMAGE_SECTION_HEADER* result=nullptr;
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
            char label[9]{};memcpy(label,sections[i].Name,8);if(!strcmp(label,name)){if(result)return nullptr;result=sections+i;}
        }return result;
    };
    auto code=find(".text");char hash[65]{};
    if(!code||!Sha256(data+code->PointerToRawData,code->SizeOfRawData,hash))return Kind::Unsupported;
    const bool aligned=!strcmp(hash,"AF01591152D1A11C0F3A0A6C6080DC588EA3EEFE11BFF07C7383195ADD6020EF");
    if(aligned&&(!Sha256(data,size,hash)||strcmp(hash,"E9F77BA801470403B819A0CDD21BB02B472C06A16952515160FE355888F85459")))return Kind::Unsupported;
    Kind kind=!strcmp(hash,"FA7D1B9AB50F64492A1534416BDB3C337BE93D3CE05C4902A6C22A89E4A94046")?Kind::Original:
              !strcmp(hash,"DE08180112CD7A27F0F3DFF0214AF0387C771F6FD35427DC02D75A56C4F96909")?Kind::Localized:
              (aligned||!strcmp(hash,"8D940668F039CAF52E68083BE53C7F3D2A52131D908D52A64295C65F980B13D4"))?Kind::Original104:Kind::Unsupported;
    if(kind==Kind::Unsupported)return kind;
    bool newCore=kind==Kind::Original104;
    const char* names[]={".text",".rdata",".data",".pdata"};
    const DWORD layouts[][3]={{0x1000,aligned?9547776u:newCore?9545740u:9545644u,newCore?9546240u:9545728u},{0x91c000,aligned?2252800u:2250382u,2250752},{0xb42000,aligned?58441728u:newCore?58438236u:58438268u,871936},{0x42fe000,aligned?397312u:397152u,397312}};
    for(unsigned i=0;i<4;++i){auto s=find(names[i]);if(!s||s->VirtualAddress!=layouts[i][0]||s->Misc.VirtualSize!=layouts[i][1]||s->SizeOfRawData!=layouts[i][2]||s->PointerToRawData>size||s->SizeOfRawData>size-s->PointerToRawData)return Kind::Unsupported;}
    bool loaderEntry=false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
        if(!memcmp(s.Name,".bind\0\0\0",8)&&(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&nt->OptionalHeader.AddressOfEntryPoint>=s.VirtualAddress&&uint64_t(nt->OptionalHeader.AddressOfEntryPoint)<uint64_t(s.VirtualAddress)+s.Misc.VirtualSize)loaderEntry=true;
    }
    if(nt->OptionalHeader.AddressOfEntryPoint!=(newCore?0x83bd68u:0x83bd08u)&&!loaderEntry)return Kind::Unsupported;
    auto readQword=[&](DWORD rva,uint64_t expected){
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=sections[i];
            if(rva<s.VirtualAddress)continue;uint64_t delta=uint64_t(rva)-s.VirtualAddress;
            if(delta+8>s.SizeOfRawData||uint64_t(s.PointerToRawData)+delta+8>size)continue;
            uint64_t value=0;memcpy(&value,data+s.PointerToRawData+delta,8);return value==expected;
        }return false;
    };
    if(!readQword(0x959990,0x14057e6a0ull)||!readQword(0x91c900,0x8000000000000002ull)||!readQword(0x91c8f8,0x8000000000000003ull)||!readQword(0x91c908,0x8000000000000004ull))return Kind::Unsupported;
    auto unwind=find(".pdata");if(!Sha256(data+unwind->PointerToRawData,unwind->SizeOfRawData,hash)||strcmp(hash,UnwindHash(kind)))return Kind::Unsupported;
    return kind;
}
inline Kind InspectLoaded(HMODULE module){
    // The unwind table contains RVAs, so its fingerprint survives ASLR and
    // runtime initialization. Validate this separately from the disk read view.
    BYTE header[4096]{};SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),module,header,sizeof(header),&got)||got!=sizeof(header))return Kind::Unsupported;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(header);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||size_t(dos->e_lfanew)>sizeof(header)-sizeof(IMAGE_NT_HEADERS64))return Kind::Unsupported;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(header+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->FileHeader.SizeOfOptionalHeader!=240||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->OptionalHeader.SizeOfImage<0x435f000||nt->OptionalHeader.SizeOfImage>0x6000000||nt->FileHeader.NumberOfSections<4||nt->FileHeader.NumberOfSections>96)return Kind::Unsupported;
    size_t table=size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64);
    if(table+size_t(nt->FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>sizeof(header))return Kind::Unsupported;
    auto sections=reinterpret_cast<const IMAGE_SECTION_HEADER*>(header+table);unsigned found=0;
    bool aligned=false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(!memcmp(sections[i].Name,".pdata\0\0",8)){
        aligned=sections[i].Misc.VirtualSize==397312&&nt->OptionalHeader.SizeOfImage==0x461e000;
        if(++found!=1||sections[i].VirtualAddress!=0x42fe000||(!aligned&&sections[i].Misc.VirtualSize!=397152)||sections[i].SizeOfRawData!=397312)return Kind::Unsupported;
    }
    if(found!=1)return Kind::Unsupported;
    std::vector<BYTE> bytes(397312);
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const BYTE*>(module)+0x42fe000,bytes.data(),bytes.size(),&got)||got!=bytes.size())return Kind::Unsupported;
    char hash[65]{};if(!Sha256(bytes.data(),bytes.size(),hash))return Kind::Unsupported;
    if(!aligned&&!strcmp(hash,UnwindHash(Kind::Original)))return Kind::Original;
    if(!strcmp(hash,UnwindHash(Kind::Original104)))return Kind::Original104;
    return Kind::Unsupported;
}
inline Kind InspectFile(const wchar_t* path){
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return Kind::Unsupported;
    LARGE_INTEGER length{};if(!GetFileSizeEx(file,&length)||length.QuadPart<64||length.QuadPart>128*1024*1024){CloseHandle(file);return Kind::Unsupported;}
    std::vector<BYTE> bytes(size_t(length.QuadPart));DWORD read=0;
    bool ok=ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)&&read==bytes.size();CloseHandle(file);
    return ok?InspectBytes(bytes.data(),bytes.size()):Kind::Unsupported;
}
}
