#pragma once
#include <d3d11.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <array>
#include "render_vtable.h"
#include "controller_glyphs.h"
#ifdef NGS1_LOCAL_ICONS
#include "icon_data.generated.h"
#endif
#include "render_hook_bridge.h"

namespace ControllerIcons {
struct Run {uint32_t offset;std::vector<BYTE> bytes;};
struct Patch {uint32_t width,height,size;uint64_t xbox,ps;std::vector<Run> runs;};
struct Pair {ID3D11ShaderResourceView* xbox;ID3D11ShaderResourceView* ps;ID3D11Texture2D* texture=nullptr;std::vector<BYTE> xboxPixels,psPixels;UINT pitch=0;int applied=0;bool mutableTexture=false;};
static std::vector<Patch> patches;
static std::vector<Pair> pairs;
static std::unordered_map<ID3D11ShaderResourceView*,size_t> views;
static std::unordered_map<ID3D11Resource*,size_t> textures;
static std::vector<ID3D11ShaderResourceView*> pending;
static std::unordered_map<ID3D11DeviceContext*,int> modes;
static std::recursive_mutex lock;
static std::atomic<bool> desiredPS{false};
static std::atomic<bool> enabled{false};
static std::atomic<const AfterimageDrawHandlers*> afterimage{nullptr};
static void(*log)(const char*,...);
static const GUID tag={0x5de32fb4,0x43ba,0x4ab3,{0x99,0x62,0x3c,0x7d,0x85,0x44,0x2e,0xf1}};
typedef void(STDMETHODCALLTYPE* SetFn)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
typedef void(STDMETHODCALLTYPE* DrawFn)(ID3D11DeviceContext*,UINT,UINT);
typedef void(STDMETHODCALLTYPE* IndexedFn)(ID3D11DeviceContext*,UINT,UINT,INT);
typedef void(STDMETHODCALLTYPE* InstancedFn)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
typedef void(STDMETHODCALLTYPE* IndexedInstancedFn)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
typedef void(STDMETHODCALLTYPE* ExecuteFn)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
static std::array<RenderVtable::Slot,7> entries{};
template<class T>inline T Original(ID3D11DeviceContext* context,unsigned index){
 auto* address=RenderVtable::Table(context)+index;
 for(const auto& entry:entries)if(entry.address==address)return reinterpret_cast<T>(entry.original);
 return reinterpret_cast<T>(*address);
}
template<class T>struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** out(){return &p;}T* operator->(){return p;}};
inline uint64_t Hash(const BYTE* data,size_t size){uint64_t h=14695981039346656037ull;for(size_t i=0;i<size;++i)h=(h^data[i])*1099511628211ull;return h;}
inline bool Parse(){
#ifdef NGS1_LOCAL_ICONS
 const BYTE* cursor=kControllerIconData,*end=cursor+sizeof(kControllerIconData);
 auto read=[&](void* out,size_t n){if(size_t(end-cursor)<n)return false;memcpy(out,cursor,n);cursor+=n;return true;};
 char magic[8];uint32_t version,count;if(!read(magic,8)||memcmp(magic,"NGSICN02",8)||!read(&version,4)||version!=1||!read(&count,4)||count>16)return false;
 for(uint32_t i=0;i<count;++i){Patch p{};uint32_t runs;
  if(!read(&p.width,4)||!read(&p.height,4)||!read(&p.xbox,8)||!read(&p.ps,8)||!read(&p.size,4)||!read(&runs,4)||p.width!=512||(p.height!=32&&p.height!=512)||p.size!=p.width*p.height*4||runs>10000)return false;
  uint32_t last=0;for(uint32_t n=0;n<runs;++n){Run r{};uint32_t length;if(!read(&r.offset,4)||!read(&length,4)||r.offset<last||length>p.size||r.offset>p.size-length)return false;r.bytes.resize(length);if(!read(r.bytes.data(),length))return false;last=r.offset+length;p.runs.push_back(std::move(r));}patches.push_back(std::move(p));
 }return cursor==end;
#else
 return true;
#endif
}

inline bool MakeAlternate(std::vector<BYTE>& pixels,unsigned width,unsigned height,bool& wasPS,bool& generated){
 const uint64_t hash=Hash(pixels.data(),pixels.size());
 for(const auto& patch:patches){
  if(patch.width!=width||patch.height!=height||(patch.xbox!=hash&&patch.ps!=hash))continue;
  wasPS=patch.ps==hash;
  for(const auto& run:patch.runs)
   for(size_t i=0;i<run.bytes.size();++i)pixels[run.offset+i]^=run.bytes[i];
  return Hash(pixels.data(),pixels.size())==(wasPS?patch.xbox:patch.ps);
 }
 wasPS=false;
 generated=ControllerGlyphs::IsXboxAtlas(width,height,hash)&&ControllerGlyphs::Render(pixels,width,height);
 return generated;
}
inline Pair* Find(ID3D11DeviceContext* context,ID3D11ShaderResourceView* view){
 if(!view)return nullptr;auto known=views.find(view);if(known!=views.end())return &pairs[known->second];if(pairs.size()>=64)return nullptr;
 Ref<ID3D11Resource> resource;view->GetResource(resource.out());if(!resource.p)return nullptr;
 Ref<ID3D11Texture2D> texture;if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return nullptr;
 auto existing=textures.find(texture.p);if(existing!=textures.end()&&pairs[existing->second].mutableTexture){views[view]=existing->second;return &pairs[existing->second];}
 D3D11_TEXTURE2D_DESC desc;texture->GetDesc(&desc);if(desc.Width!=512||(desc.Height!=32&&desc.Height!=512)||desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1)return nullptr;
 if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_B8G8R8A8_TYPELESS&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS)return nullptr;
 UINT marker=0,markerSize=4;if(SUCCEEDED(texture->GetPrivateData(tag,&markerSize,&marker))&&marker==1)return nullptr;
 if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){if(marker!=2){marker=2;texture->SetPrivateData(tag,4,&marker);view->AddRef();pending.push_back(view);}return nullptr;}
 D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;view->GetDesc(&srvDesc);if(srvDesc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D)return nullptr;
 Ref<ID3D11Device> device;context->GetDevice(device.out());D3D11_TEXTURE2D_DESC stagingDesc=desc;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;stagingDesc.MiscFlags=0;
 Ref<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&stagingDesc,nullptr,staging.out())))return nullptr;
 context->CopyResource(staging.p,texture.p);D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped)))return nullptr;
 std::vector<BYTE> pixels(size_t(desc.Width)*desc.Height*4);for(UINT row=0;row<desc.Height;++row)memcpy(pixels.data()+size_t(row)*desc.Width*4,static_cast<BYTE*>(mapped.pData)+size_t(row)*mapped.RowPitch,desc.Width*4);context->Unmap(staging.p,0);
 std::vector<BYTE> source=pixels;
 bool wasPS=false,generated=false;
 if(!MakeAlternate(pixels,desc.Width,desc.Height,wasPS,generated)){
  static unsigned misses=0;
  if(log&&misses++<8)log("ICON_CANDIDATE_UNMATCHED width=%u height=%u format=%u hash=%llx",desc.Width,desc.Height,
   desc.Format,static_cast<unsigned long long>(Hash(source.data(),source.size())));
  marker=1;texture->SetPrivateData(tag,4,&marker);return nullptr;
 }
 if(desc.Usage==D3D11_USAGE_DEFAULT){Pair pair{};pair.xbox=pair.ps=view;view->AddRef();pair.texture=texture.p;pair.texture->AddRef();pair.mutableTexture=true;pair.pitch=desc.Width*4;pair.applied=wasPS?1:0;pair.xboxPixels=wasPS?pixels:source;pair.psPixels=wasPS?source:pixels;size_t index=pairs.size();pairs.push_back(std::move(pair));views[view]=index;textures[texture.p]=index;if(log)log("ICON_TEXTURE_MATCH width=%u height=%u format=%u source=%s mutable=1 generated=%u",desc.Width,desc.Height,desc.Format,wasPS?"PS":"Xbox",generated);return &pairs[index];}
 D3D11_TEXTURE2D_DESC otherDesc=desc;otherDesc.Usage=D3D11_USAGE_IMMUTABLE;otherDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;otherDesc.CPUAccessFlags=0;otherDesc.MiscFlags=0;
 D3D11_SUBRESOURCE_DATA initial{pixels.data(),desc.Width*4,0};Ref<ID3D11Texture2D> other;
 if(FAILED(device->CreateTexture2D(&otherDesc,&initial,other.out())))return nullptr;
 ID3D11ShaderResourceView* otherView=nullptr;if(FAILED(device->CreateShaderResourceView(other.p,&srvDesc,&otherView)))return nullptr;
 view->AddRef();Pair pair{};pair.xbox=wasPS?otherView:view;pair.ps=wasPS?view:otherView;size_t index=pairs.size();pairs.push_back(std::move(pair));views[pairs[index].xbox]=index;views[pairs[index].ps]=index;
 if(log)log("ICON_TEXTURE_MATCH width=%u height=%u format=%u source=%s pairs=%u generated=%u",desc.Width,desc.Height,desc.Format,wasPS?"PS":"Xbox",unsigned(pairs.size()),generated);return &pairs[index];
}
inline void Sync(ID3D11DeviceContext* context){if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;std::lock_guard<std::recursive_mutex> guard(lock);
 auto queue=std::move(pending);pending.clear();for(auto view:queue){try{Find(context,view);}catch(...){}view->Release();}
 int mode=desiredPS?1:0;for(auto& pair:pairs)if(pair.mutableTexture&&pair.applied!=mode){const auto& pixels=mode?pair.psPixels:pair.xboxPixels;context->UpdateSubresource(pair.texture,0,nullptr,pixels.data(),pair.pitch,0);pair.applied=mode;}
}
inline void Refresh(ID3D11DeviceContext* context){
 Sync(context);if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 std::lock_guard<std::recursive_mutex> guard(lock);int mode=desiredPS?1:0;auto previous=modes.find(context);if(previous!=modes.end()&&previous->second==mode)return;
 ID3D11ShaderResourceView* current[128]{};context->PSGetShaderResources(0,128,current);ID3D11ShaderResourceView* replacement[128]{};bool changed=false;
 for(unsigned i=0;i<128;++i){replacement[i]=current[i];auto pair=Find(context,current[i]);if(pair&&!pair->mutableTexture){replacement[i]=mode?pair->ps:pair->xbox;changed|=replacement[i]!=current[i];}}
 if(changed)Original<SetFn>(context,8)(context,0,128,replacement);for(auto view:current)if(view)view->Release();modes[context]=mode;
}
static void Set(SetFn next,ID3D11DeviceContext* context,UINT first,UINT count,ID3D11ShaderResourceView* const* input){
 if(!enabled||!input||count>128){next(context,first,count,input);return;}
 static std::atomic<unsigned> calls{0};if(calls++<2&&log)log("ICON_SRV_ACTIVITY context_type=%u",context->GetType());
 try{std::lock_guard<std::recursive_mutex> guard(lock);ID3D11ShaderResourceView* output[128]{};bool ps=desiredPS;
  for(UINT i=0;i<count;++i){auto pair=Find(context,input[i]);output[i]=pair&&!pair->mutableTexture?(ps?pair->ps:pair->xbox):input[i];}next(context,first,count,output);
 }catch(...){next(context,first,count,input);}
}
static void Draw(DrawFn next,ID3D11DeviceContext* c,UINT n,UINT first){try{if(enabled)Refresh(c);}catch(...){}next(c,n,first);}
static void Indexed(IndexedFn next,ID3D11DeviceContext* c,UINT n,UINT first,INT vertex){
 try{if(enabled)Refresh(c);}catch(...){}
 const auto* handlers=afterimage.load();
 if(!handlers||!handlers->indexed(next,c,n,first,vertex))next(c,n,first,vertex);
}
static void Instanced(InstancedFn next,ID3D11DeviceContext* c,UINT n,UINT instances,UINT first,UINT instance){try{if(enabled)Refresh(c);}catch(...){}next(c,n,instances,first,instance);}
static void IndexedInstanced(IndexedInstancedFn next,ID3D11DeviceContext* c,UINT n,UINT instances,UINT first,INT vertex,UINT instance){
 try{if(enabled)Refresh(c);}catch(...){}
 const auto* handlers=afterimage.load();
 if(!handlers||!handlers->indexedInstanced(next,c,n,instances,first,vertex,instance))
  next(c,n,instances,first,vertex,instance);
}
static void Execute(ExecuteFn next,ID3D11DeviceContext* c,ID3D11CommandList* commands,BOOL restore){
 try{if(enabled)Sync(c);}catch(...){}
 const auto* handlers=afterimage.load();
 if(!handlers||!handlers->execute(next,c,commands,restore))next(c,commands,restore);
}
inline void* SharedOriginal(void** table,unsigned slot){
 if(!enabled)return nullptr;
 for(const auto& entry:entries)if(entry.address==table+slot&&*entry.address==entry.observer)return entry.original;
 return nullptr;
}
inline void ConnectAfterimage(const AfterimageDrawHandlers* handlers){afterimage=handlers;}
inline bool Initialize(void(*logger)(const char*,...)){
 log=logger;if(!Parse())return false;wchar_t path[MAX_PATH];GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d11.dll");HMODULE module=LoadLibraryW(path);if(!module)return false;
 auto create=reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(module,"D3D11CreateDevice"));if(!create)return false;
 Ref<ID3D11Device> device;Ref<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
 HRESULT result=create(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&level,1,D3D11_SDK_VERSION,device.out(),nullptr,context.out());if(FAILED(result)){if(log)log("ICON_DEVICE_FAILED result=%08lx",result);return false;}
 Ref<ID3D11DeviceContext> deferred;
 device->CreateDeferredContext(0,deferred.out());
 auto* table=RenderVtable::Table(context.p);
 auto* deferredTable=deferred.p?RenderVtable::Table(deferred.p):table;
 entries[0]={table+8,table[8],reinterpret_cast<void*>(RenderVtable::Observer<SetFn,Set>::Call<entries,0>),8,log};
 entries[1]={table+12,table[12],reinterpret_cast<void*>(RenderVtable::Observer<IndexedFn,Indexed>::Call<entries,1>),12,log};
 entries[2]={table+13,table[13],reinterpret_cast<void*>(RenderVtable::Observer<DrawFn,Draw>::Call<entries,2>),13,log};
 entries[3]={table+20,table[20],reinterpret_cast<void*>(RenderVtable::Observer<IndexedInstancedFn,IndexedInstanced>::Call<entries,3>),20,log};
 entries[4]={table+21,table[21],reinterpret_cast<void*>(RenderVtable::Observer<InstancedFn,Instanced>::Call<entries,4>),21,log};
 entries[5]={table+58,table[58],reinterpret_cast<void*>(RenderVtable::Observer<ExecuteFn,Execute>::Call<entries,5>),58,log};
 if(deferredTable!=table)
  entries[6]={deferredTable+8,deferredTable[8],reinterpret_cast<void*>(RenderVtable::Observer<SetFn,Set>::Call<entries,6>),8,log};
 for(size_t i=0;i<entries.size()&&entries[i].address;++i){
  auto& entry=entries[i];
  if(!entry.Exchange(entry.original,entry.observer)){
   if(log)log("ICON_VTABLE_INSTALL_FAILED slot=%u",entry.index);
   while(i)entries[--i].Restore();
   return false;
  }
 }
 enabled=true;
 if(log)log("ICON_RENDERER_READY builtin=1 custom_variants=%u",unsigned(patches.size()));return true;
}
inline void Select(bool ps){bool old=desiredPS.exchange(ps);if(old!=ps&&log)log("ICON_MODE target=%s",ps?"PS":"Xbox");}
}
