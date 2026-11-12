#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <dinput.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <atomic>
#include "MinHook.h"
#include "mixer.h"
#include "waveform.h"
#include "attack_context.h"
#include "charge_state.h"
#include "charge_curve.h"
#include "output_gain.h"
#include "contact_filter.h"
#include "aerial_landing.h"
#include "action_sustain.h"
#include "game_compatibility.h"
#include "dualsense_hid.h"
#include "controller_router.h"
#include "icon_renderer.h"

bool StartAfterimage(const wchar_t* root, GameCompatibility::Kind kind, ControllerRenderHooks shared);

// Experimental event-driven rumble. Rules match accepted game sound requests,
// caller RVAs and source scopes; controller buttons never synthesize attacks.
// This installation's existing file layer returns its companion executable
// when the game reads its own EXE. The installer still verifies the active file.
static const char* kReadViewHash = "a524379d0386d4530411b77f42f0190af9bb51dc4a5a665b5b90cbac3970d406";
static HMODULE selfModule, realDinput, realXinput;
static uintptr_t gameBase;
static GameCompatibility::Kind gameKind=GameCompatibility::Kind::Unsupported;
static uintptr_t CodeRva(uintptr_t rva){return GameCompatibility::CodeRva(gameKind,rva);}
static wchar_t root[MAX_PATH], logPath[MAX_PATH], iniPath[MAX_PATH];
static FILE* logFile;
static SRWLOCK queueLock = SRWLOCK_INIT;
static std::atomic<bool> foreground{false}, muted{false}, testing{false};
static std::atomic<DWORD> lastController{0}, testPacked{0}, dropped{0};
static std::atomic<unsigned> eventGeneration{0};
static std::atomic<bool> stopRequested{false};
static std::atomic<DWORD> nativePacked[4];
static std::atomic<DWORD> controllerButtons[4];
static ULONGLONG startTick;
static INIT_ONCE proxyOnce = INIT_ONCE_STATIC_INIT;
typedef DWORD (WINAPI* GetStateFn)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* SetStateFn)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI* CapabilitiesFn)(DWORD,DWORD,XINPUT_CAPABILITIES*);
typedef void (*PadFn)(void*, float, float);
typedef int (*SoundFn)(unsigned, void*);
typedef int (*ContactFn)(void*,void*,void*,unsigned,unsigned);
static GetStateFn realGetState;
static SetStateFn realSetState;
static CapabilitiesFn realCapabilities;
static DualSenseHid dualSense;
static ControllerRouter controllerRouter;
static bool rumbleEnabled=true,dualSenseEnabled=false,preferDualSense=true,autoIcons=false;
typedef bool (*InputClassifyFn)(const GUID*);
typedef BOOL (CALLBACK* EnumDeviceFn)(const DIDEVICEINSTANCEW*,void*);
static InputClassifyFn originalInputClassify;
static EnumDeviceFn originalEnumDevice;
static std::atomic<bool> dualSenseRefreshNeeded{false};
static std::atomic<ULONGLONG> dualSenseFilterUntil{0};
static bool DualSenseRoute(DWORD index){
    return dualSenseEnabled&&index==0&&dualSense.IsConnected()&&controllerRouter.IsDualSense();
}
static DWORD ReadControllerState(DWORD index,XINPUT_STATE* state){
    if(DualSenseRoute(index))return dualSense.GetState(state);
    DWORD native=dualSenseEnabled?controllerRouter.NativeIndex(index):index;
    return native==~0u||!realGetState?ERROR_DEVICE_NOT_CONNECTED:realGetState(native,state);
}
static DWORD WriteControllerOutput(DWORD index,XINPUT_VIBRATION* request){
    XINPUT_VIBRATION quiet{};if(!rumbleEnabled)request=&quiet;
    if(DualSenseRoute(index))return dualSense.Submit(request);
    DWORD native=dualSenseEnabled?controllerRouter.NativeIndex(index):index;
    return native==~0u?ERROR_DEVICE_NOT_CONNECTED:realSetState(native,request);
}
static DWORD WINAPI ObserveCapabilities(DWORD index,DWORD flags,XINPUT_CAPABILITIES* capabilities){
    if(!DualSenseRoute(index)){DWORD native=dualSenseEnabled?controllerRouter.NativeIndex(index):index;return native==~0u?ERROR_DEVICE_NOT_CONNECTED:realCapabilities(native,flags,capabilities);}
    if(!capabilities)return ERROR_BAD_ARGUMENTS;
    if(flags!=0&&flags!=XINPUT_FLAG_GAMEPAD)return ERROR_BAD_ARGUMENTS;
    memset(capabilities,0,sizeof(*capabilities));capabilities->Type=XINPUT_DEVTYPE_GAMEPAD;
    capabilities->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;capabilities->Flags=1;
    capabilities->Gamepad.wButtons=0xf3ff;capabilities->Gamepad.bLeftTrigger=capabilities->Gamepad.bRightTrigger=255;
    capabilities->Gamepad.sThumbLX=capabilities->Gamepad.sThumbLY=capabilities->Gamepad.sThumbRX=capabilities->Gamepad.sThumbRY=32767;
    capabilities->Vibration.wLeftMotorSpeed=capabilities->Vibration.wRightMotorSpeed=65535;return ERROR_SUCCESS;
}
static bool ObserveInputClassify(const GUID* product){
    if(product&&product->Data1==0x0ce6054c&&dualSenseEnabled&&dualSense.IsConnected())return true;
    return originalInputClassify(product);
}
static BOOL CALLBACK ObserveEnumDevice(const DIDEVICEINSTANCEW* item,void* context){
    if(item&&item->guidProduct.Data1==0x0ce6054c&&dualSenseEnabled&&dualSense.IsConnected()){
        auto now=GetTickCount64();
        // Remove an already registered DirectInput instance for one scan, then
        // let the game's next scan register it through the XInput path.
        if(dualSenseRefreshNeeded.exchange(false))dualSenseFilterUntil=now+600;
        if(now<dualSenseFilterUntil.load())return DIENUM_CONTINUE;
    }
    return originalEnumDevice(item,context);
}
static PadFn originalPad;
static SoundFn originalSound;
static ContactFn originalContact;
static thread_local ContactCapture contactCapture;
static void* padHookTarget;
static bool captureStacks = true;
static bool logSoundEvents = true;
static DWORD pulseStrength = 16000;
static RumbleMixer mixer;
static Waveform waveforms[20];
static AttackContext attackContext;
static ChargeState chargeState;
static ChargeCurveConfig chargeCurve;
static unsigned gameGainPercent=150,chargeGainPercent=125;
static AerialLanding aerialLanding;
static SustainedAction sustainedActions[16];
static unsigned sustainedCount=0;
static ActionSustainState actionSustain;
static unsigned ReadPlayerScriptOffset() {
    uintptr_t bank=0,pc=0;BYTE header[16]{};SIZE_T bytes=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0xcc0ab0+0x26d0),&bank,sizeof(bank),&bytes) ||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0x3757570+0x25d0),&pc,sizeof(pc),&bytes) ||
       !bank || pc<bank || pc-bank>=1889840 ||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(bank),header,sizeof(header),&bytes))return 0;
    unsigned length=0;memcpy(&length,header+12,4);
    return !memcmp(header,"chr_dat",8)&&length==1889840?unsigned(pc-bank):0;
}
static ULONGLONG dedupLast[32]{};
static std::atomic<bool> ownsOutput{false};
static std::atomic<DWORD> ownedPacked{0};
struct Rule {
    unsigned code=0, codeMask=0xffffffff, excludeCode=0,excludeMask=0,scope=0, scopeMask=0;
    unsigned excludeScope=0, excludeScopeMask=0;
    uintptr_t caller=0, stackRva=0;
    DWORD left=0,right=0,duration=0,cooldown=0,delay=0;
    int wave=-1,contextWave=-1;
    unsigned segment=0,group=1,priority=3,dedup=0,setContext=0,cancelGroup=0,contextAge=450;
    unsigned chargeAction=0;
    unsigned landingAction=0,dedupWindow=12;
    unsigned scriptMin=0,scriptMax=0,contactRole=0;
    int contactMode=-1;
    char label[80]{};
    ULONGLONG last=0;
};
static Rule rules[128];
static unsigned ruleCount=0;
static std::atomic<bool> requireSoundStacks{false};

struct Event;
static void HandleSoundEvent(const Event& event);

struct Event {
    ULONGLONG tick;
    unsigned kind, index;
    float left, right;
    DWORD result, buttons;
    uintptr_t object, caller, stack[8];
    USHORT frames;
    unsigned generation,actorSlot;
    uintptr_t scriptPc;
    ContactCapture contact;
    unsigned playerScriptOffset;
};
static Event queue[2048];
static unsigned queueRead, queueWrite;

#include "monitor_bridge.h"

static void Print(const char* format, ...) {
    if (!logFile) return;
    fprintf(logFile, "%llu ", GetTickCount64() - startTick);
    va_list args; va_start(args, format); vfprintf(logFile, format, args); va_end(args);
    fputc('\n', logFile); fflush(logFile);
}

static ULONGLONG ConfigNumber(const wchar_t* section,const wchar_t* key,ULONGLONG fallback,const wchar_t* path) {
    wchar_t text[64]; GetPrivateProfileStringW(section,key,L"",text,64,path);
    if(!text[0] || text[0]==L'-')return fallback;
    wchar_t* end=nullptr; auto value=_wcstoui64(text,&end,0);
    return end==text ? fallback : value;
}

static int ConfigWave(const wchar_t* section,const wchar_t* key,const wchar_t* path) {
    wchar_t value[32];GetPrivateProfileStringW(section,key,L"",value,32,path);
    if(!value[0])return -1;
    if(value[0]!=L'W'||value[1]<L'0'||value[1]>L'1'||value[2]<L'0'||value[2]>L'9'||value[3])return -2;
    return int(value[1]-L'0')*10+int(value[2]-L'0');
}

static void LoadWaveforms() {
    memcpy(waveforms,kTableWaveforms,sizeof(waveforms));
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\NGS1RumbleWaves.ini",root);
    for(unsigned i=1;i<20;++i) {
        wchar_t section[8];swprintf_s(section,L"W%02u",i);
        auto& effect=waveforms[i];
        unsigned end=0;bool valid=true;
        for(unsigned n=0;n<effect.count;++n) {
            auto& segment=effect.segments[n];
            wchar_t key[40];
            swprintf_s(key,L"Segment%uOffsetMs",n+1);segment.offset=unsigned(ConfigNumber(section,key,segment.offset,path));
            swprintf_s(key,L"Segment%uDurationMs",n+1);segment.duration=unsigned(ConfigNumber(section,key,segment.duration,path));
            swprintf_s(key,L"Segment%uLeft",n+1);segment.left=unsigned(ConfigNumber(section,key,segment.left,path));
            swprintf_s(key,L"Segment%uRight",n+1);segment.right=unsigned(ConfigNumber(section,key,segment.right,path));
            if(segment.offset>2000||segment.duration>2000||segment.offset+segment.duration>2000||
               segment.left>65535||segment.right>65535)valid=false;
            if(segment.offset+segment.duration>end)end=segment.offset+segment.duration;
        }
        if(!valid){effect=kTableWaveforms[i];Print("INVALID_WAVE W%02u; using workbook defaults",i);}
        else effect.length=end;
    }
    chargeCurve=ChargeCurveConfig{};
    chargeCurve.enabled=ConfigNumber(L"ChargeCurve",L"Enabled",1,path)!=0;
    auto bounded=[&](const wchar_t* key,unsigned fallback,unsigned maximum){
        auto value=ConfigNumber(L"ChargeCurve",key,fallback,path);
        return unsigned(value>maximum?maximum:value);
    };
    chargeCurve.firstRampMs=bounded(L"FirstRampMs",1350,5000);
    chargeCurve.secondRampMs=bounded(L"SecondRampMs",1940,5000);
    chargeCurve.gapMs=bounded(L"TransitionGapMs",60,300);
    chargeCurve.utGapMs=bounded(L"UTTransitionGapMs",60,300);
    chargeCurve.start={WORD(bounded(L"StartLeft",3277,65535)),WORD(bounded(L"StartRight",7864,65535))};
    chargeCurve.et={WORD(bounded(L"ETLeft",9830,65535)),WORD(bounded(L"ETRight",18350,65535))};
    chargeCurve.ut={WORD(bounded(L"UTLeft",16384,65535)),WORD(bounded(L"UTRight",27525,65535))};
    gameGainPercent=unsigned(ConfigNumber(L"Output",L"GainPercent",150,path));
    if(gameGainPercent>200)gameGainPercent=200;
    chargeGainPercent=unsigned(ConfigNumber(L"Output",L"ChargeGainPercent",125,path));
    if(chargeGainPercent>200)chargeGainPercent=200;
    Print("WAVEFORMS_LOADED count=20");
    Print("CHARGE_CURVE enabled=%u first_ms=%u second_ms=%u et_gap_ms=%u ut_gap_ms=%u game_gain_percent=%u charge_gain_percent=%u",chargeCurve.enabled,
          chargeCurve.firstRampMs,chargeCurve.secondRampMs,chargeCurve.gapMs,chargeCurve.utGapMs,gameGainPercent,chargeGainPercent);
}

static void LoadRules() {
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\NGS1RumbleRules.ini",root);
    ruleCount=0;sustainedCount=0;actionSustain.reset();bool needsStacks=false;
    attackContext.clear();chargeState.clear();aerialLanding.clear();memset(dedupLast,0,sizeof(dedupLast));
    LoadWaveforms();
    for(unsigned n=1;n<=16;++n){
        wchar_t section[32];swprintf_s(section,L"Sustain%u",n);
        if(!GetPrivateProfileIntW(section,L"Enabled",0,path))continue;
        SustainedAction action;
        action.first=unsigned(ConfigNumber(section,L"ScriptMin",0,path));
        action.last=unsigned(ConfigNumber(section,L"ScriptMax",0,path));
        action.kind=unsigned(ConfigNumber(section,L"Kind",1,path));
        action.maxMs=unsigned(ConfigNumber(section,L"MaxDurationMs",6000,path));
        auto motor=[&](const wchar_t* key){auto v=ConfigNumber(section,key,0,path);return uint16_t(v>65535?65535:v);};
        action.motors={motor(L"Left"),motor(L"Right")};
        action.hit={motor(L"HitLeft"),motor(L"HitRight")};
        action.hitMs=unsigned(ConfigNumber(section,L"HitDurationMs",180,path));
        action.hitCooldown=unsigned(ConfigNumber(section,L"HitCooldownMs",40,path));
        if(!action.first || action.last<=action.first || action.last>1889840 ||
           action.kind<1 || action.kind>2 || action.maxMs<100 || action.maxMs>15000 ||
           action.hitMs>500 || action.hitCooldown>5000 || (!action.motors.left&&!action.motors.right)){
            Print("INVALID_SUSTAIN section=%u",n);continue;
        }
        wchar_t label[80];GetPrivateProfileStringW(section,L"Label",L"",label,80,path);
        WideCharToMultiByte(CP_UTF8,0,label,-1,action.label,sizeof(action.label),nullptr,nullptr);
        sustainedActions[sustainedCount++]=action;needsStacks=true;
    }
    Print("SUSTAINS_LOADED count=%u",sustainedCount);
    for(unsigned n=1;n<=128;++n) {
        wchar_t section[32];swprintf_s(section,L"Rule%u",n);
        if(!GetPrivateProfileIntW(section,L"Enabled",0,path))continue;
        Rule rule;
        rule.code=unsigned(ConfigNumber(section,L"Code",0,path));
        rule.codeMask=unsigned(ConfigNumber(section,L"CodeMask",0xffffffff,path));
        rule.excludeCode=unsigned(ConfigNumber(section,L"ExcludeCode",0,path));
        rule.excludeMask=unsigned(ConfigNumber(section,L"ExcludeCodeMask",0,path));
        rule.scope=unsigned(ConfigNumber(section,L"Scope",0,path));
        rule.scopeMask=unsigned(ConfigNumber(section,L"ScopeMask",0xffffffff,path));
        rule.excludeScope=unsigned(ConfigNumber(section,L"ExcludeScope",0,path));
        rule.excludeScopeMask=unsigned(ConfigNumber(section,L"ExcludeScopeMask",0,path));
        rule.caller=CodeRva(uintptr_t(ConfigNumber(section,L"Caller",0,path)));
        rule.stackRva=CodeRva(uintptr_t(ConfigNumber(section,L"RequireStackRVA",0,path)));
        rule.left=DWORD(ConfigNumber(section,L"Left",0,path));if(rule.left>65535)rule.left=65535;
        rule.right=DWORD(ConfigNumber(section,L"Right",0,path));if(rule.right>65535)rule.right=65535;
        rule.duration=DWORD(ConfigNumber(section,L"DurationMs",100,path));if(rule.duration>500)rule.duration=500;
        rule.cooldown=DWORD(ConfigNumber(section,L"CooldownMs",60,path));if(rule.cooldown>5000)rule.cooldown=5000;
        rule.delay=DWORD(ConfigNumber(section,L"DelayMs",0,path));if(rule.delay>500)rule.delay=500;
        rule.wave=ConfigWave(section,L"Waveform",path);
        rule.contextWave=ConfigWave(section,L"ContextWaveform",path);
        rule.segment=unsigned(ConfigNumber(section,L"Segment",0,path));
        rule.group=unsigned(ConfigNumber(section,L"Group",1,path));
        rule.priority=unsigned(ConfigNumber(section,L"Priority",3,path));
        rule.dedup=unsigned(ConfigNumber(section,L"DedupGroup",0,path));
        rule.setContext=unsigned(ConfigNumber(section,L"SetContext",0,path));
        rule.chargeAction=unsigned(ConfigNumber(section,L"ChargeAction",0,path));
        rule.landingAction=unsigned(ConfigNumber(section,L"LandingAction",0,path));
        rule.dedupWindow=unsigned(ConfigNumber(section,L"DedupWindowMs",12,path));
        rule.scriptMin=unsigned(ConfigNumber(section,L"ScriptMin",0,path));
        rule.scriptMax=unsigned(ConfigNumber(section,L"ScriptMax",0,path));
        rule.contactRole=unsigned(ConfigNumber(section,L"ContactRole",0,path));
        rule.contactMode=int(ConfigNumber(section,L"ContactMode",0xffffffff,path));
        rule.cancelGroup=unsigned(ConfigNumber(section,L"CancelGroup",0,path));
        rule.contextAge=unsigned(ConfigNumber(section,L"ContextMaxAgeMs",450,path));
        if(rule.dedup>=32||rule.setContext>3||rule.chargeAction>7||rule.landingAction>3||rule.dedupWindow>500||rule.contextAge>1000||rule.wave==-2||rule.contextWave==-2)continue;
        if(rule.contactRole>2 || rule.contactMode>1 || rule.contactMode<-1 ||
           (rule.scriptMax && rule.scriptMax<=rule.scriptMin))continue;
        // Contact trains and throws require real, separate contact/landing events.
        if((rule.wave==5||rule.wave==18)&&!rule.segment)continue;
        if(rule.wave>=0 && rule.segment>kTableWaveforms[rule.wave].count)continue;
        if(rule.wave<0 && (!rule.duration || (!rule.left&&!rule.right)))continue;
        wchar_t label[80];GetPrivateProfileStringW(section,L"Label",L"",label,80,path);
        WideCharToMultiByte(CP_UTF8,0,label,-1,rule.label,sizeof(rule.label),nullptr,nullptr);
        if(rule.stackRva)needsStacks=true;
        rules[ruleCount++]=rule;
    }
    requireSoundStacks=needsStacks;
    Print("RULES_LOADED count=%u",ruleCount);
}

static void HandleSoundEvent(const Event& event) {
    if(!rumbleEnabled)return;
    if(!foreground || muted || testing)return;
    if(event.generation!=eventGeneration.load())return;
    ULONGLONG eventTime=startTick+event.tick;
    if(GetTickCount64()-eventTime>120)return;
    int actionIndex=FindSustainedAction(sustainedActions,sustainedCount,event.playerScriptOffset);
    if(actionIndex>=0 && !actionSustain.suppressed(actionIndex)){
        auto& action=sustainedActions[actionIndex];bool damagePath=false;
        for(unsigned i=0;i<event.frames;++i)if(event.stack[i]==gameBase+CodeRva(0x6906c4))damagePath=true;
        if(event.buttons==0x02000000)Print("SUSTAIN_CUE label=%s code=%08x source_ms=%llu source_scope=%08lx",action.label,event.index,event.tick,event.buttons);
        // Mode 0 plus the damage-sound path and player instigator establish a
        // real enemy contact. Cast cues, empty casts and enemy/player damage
        // never schedule a synthetic maximum-strength hit.
        if(CanSustainedHit(action,event.contact,gameBase+0xcc0ab0,damagePath,eventTime)){
            action.lastHit=eventTime;
            mixer.add(eventTime,action.hit.left,action.hit.right,action.hitMs,true,13,5);
            Print("SUSTAIN_HIT label=%s code=%08x L=%u R=%u ms=%u source_ms=%llu source_scope=%08lx",action.label,event.index,
                  action.hit.left,action.hit.right,action.hitMs,event.tick,event.buttons);
        }
    }
    unsigned monitorSkipMask=0;
    for(unsigned n=0;n<ruleCount;++n) {
        Rule& rule=rules[n];
        if((event.index&rule.codeMask)!=(rule.code&rule.codeMask) || (event.buttons&rule.scopeMask)!=(rule.scope&rule.scopeMask)
           || (rule.caller&&event.caller!=rule.caller))continue;
        if(rule.excludeMask && (event.index&rule.excludeMask)==(rule.excludeCode&rule.excludeMask))continue;
        if(rule.excludeScopeMask && (event.buttons&rule.excludeScopeMask)==(rule.excludeScope&rule.excludeScopeMask))continue;

        if(rule.stackRva) {
            bool found=false;
            for(unsigned i=0;i<event.frames;++i)if(event.stack[i]==gameBase+rule.stackRva){found=true;break;}
            if(!found)continue;
        }
        const char* skip=nullptr;
        if(rule.scriptMax && (event.playerScriptOffset<rule.scriptMin || event.playerScriptOffset>=rule.scriptMax))skip="script_range";
        else if(!MatchContact(event.contact,gameBase+0xcc0ab0,rule.contactMode,rule.contactRole))skip="contact_filter";
        else if(rule.last && eventTime-rule.last<rule.cooldown)skip="cooldown";
        else if(rule.dedup && dedupLast[rule.dedup] && eventTime-dedupLast[rule.dedup]<rule.dedupWindow)skip="dedup";
        if(skip){unsigned bit=!strcmp(skip,"script_range")?1:!strcmp(skip,"contact_filter")?2:!strcmp(skip,"cooldown")?4:8;
            if(!(monitorSkipMask&bit)){Print("RULE_SKIPPED rule=%u label=%s code=%08x reason=%s source_ms=%llu source_scope=%08lx",n+1,rule.label,event.index,skip,event.tick,event.buttons);monitorSkipMask|=bit;}continue;}
        if(rule.landingAction==1)aerialLanding.begin(eventTime);
        else if(rule.landingAction==2){if(!aerialLanding.consume(eventTime))continue;}
        else if(rule.landingAction==3)aerialLanding.clear();
        if(rule.chargeAction==7 && chargeState.releaseReady(eventTime))continue;
        if(rule.chargeAction==1)chargeState.begin(eventTime);
        else if(rule.chargeAction==2||rule.chargeAction==3) {
            if(!chargeState.ready(rule.chargeAction-1,eventTime))continue;
        } else if(rule.chargeAction==4) {
            if(!chargeState.release(eventTime))continue;
        } else if(rule.chargeAction==5)chargeState.clear();
        else if(rule.chargeAction==6)chargeState.finish(eventTime);
        rule.last=eventTime;
        if(rule.dedup)dedupLast[rule.dedup]=eventTime;
        if(rule.setContext==3)attackContext.clear();
        else if(rule.setContext)attackContext.set(rule.setContext,eventTime);
        if(rule.cancelGroup)mixer.cancel(rule.cancelGroup);
        int wave=rule.wave;
        if(rule.contextWave>=0 && attackContext.heavy(eventTime,rule.contextAge))wave=rule.contextWave;
        bool curveStage=chargeCurve.enabled && rule.chargeAction>=1 && rule.chargeAction<=3;
        if(curveStage) {} // Continuous state curve replaces W06/W07/W08 accents.
        else if(wave>=0)PlayWaveform(mixer,unsigned(wave),eventTime+rule.delay,rule.group,rule.priority,rule.segment,waveforms);
        else mixer.add(eventTime+rule.delay,rule.left,rule.right,rule.duration);
        Print("%s rule=%u label=%s code=%08x wave=%d segment=%u group=%u L=%lu R=%lu ms=%lu delay=%lu source_ms=%llu source_scope=%08lx",
              curveStage?"CHARGE_CURVE_STAGE":wave==0?"CONTEXT_EVENT":"RULE_TRIGGER",n+1,rule.label,event.index,wave,rule.segment,rule.group,
              rule.left,rule.right,rule.duration,rule.delay,event.tick,event.buttons);
    }
}

static uintptr_t ReturnRva(void* ptr) {
    uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
    return v >= gameBase && v < gameBase + 0x4600000 ? v - gameBase : v;
}

static void Enqueue(Event& event, bool stack, bool required=false) {
    event.tick = GetTickCount64() - startTick;
    event.generation=eventGeneration;
    if (stack && (captureStacks||required))
        event.frames = CaptureStackBackTrace(2, 8, reinterpret_cast<void**>(event.stack), nullptr);
    if (!TryAcquireSRWLockExclusive(&queueLock)) { ++dropped; return; }
    unsigned next = (queueWrite + 1) % 2048;
    if (next == queueRead) ++dropped;
    else { queue[queueWrite] = event; queueWrite = next; }
    ReleaseSRWLockExclusive(&queueLock);
}

static void Drain() {
    for (unsigned count = 0; count != 2048; ++count) {
        Event event{};
        AcquireSRWLockExclusive(&queueLock);
        if (queueRead == queueWrite) { ReleaseSRWLockExclusive(&queueLock); break; }
        event = queue[queueRead]; queueRead = (queueRead + 1) % 2048;
        ReleaseSRWLockExclusive(&queueLock);
        if (event.kind == 4) {
            if(logSoundEvents) {
            fprintf(logFile, "%llu SOUND_REQUEST code=%08x scope=%08lx result=%08lx parameters=%llx caller=%llx actor_slot=%u script_pc=%llx player_script_offset=%x contact_source=%llx contact_target=%llx contact_aux=%llx contact_mode=%u contact_flags=%u stack=",
                    event.tick,event.index,static_cast<unsigned long>(event.buttons),static_cast<unsigned long>(event.result),
                    static_cast<unsigned long long>(event.object),static_cast<unsigned long long>(event.caller),
                    event.actorSlot,static_cast<unsigned long long>(event.scriptPc),event.playerScriptOffset,
                    static_cast<unsigned long long>(event.contact.source),static_cast<unsigned long long>(event.contact.target),
                    static_cast<unsigned long long>(event.contact.aux),event.contact.mode,event.contact.flags);
            for(unsigned i=0;i<event.frames;++i)fprintf(logFile,"%llx,",static_cast<unsigned long long>(ReturnRva(reinterpret_cast<void*>(event.stack[i]))));
            fputc('\n',logFile);
            }
            HandleSoundEvent(event);
            continue;
        }
        fprintf(logFile, "%llu %s index=%u L=%.6g R=%.6g result=%lu buttons=%04lx object=%llx caller=%llx",
                event.tick, event.kind == 1 ? "PAD_REQUEST" : event.kind == 2 ? "XINPUT_SET" : "INPUT_CHANGE",
                event.index, event.left, event.right, static_cast<unsigned long>(event.result),
                static_cast<unsigned long>(event.buttons), static_cast<unsigned long long>(event.object),
                static_cast<unsigned long long>(event.caller));
        for (USHORT n = 0; n < event.frames; ++n)
            fprintf(logFile, " %llx", static_cast<unsigned long long>(ReturnRva(reinterpret_cast<void*>(event.stack[n]))));
        fputc('\n', logFile);
    }
    fflush(logFile);
}

static DWORD Pack(WORD left, WORD right) { return DWORD(left) | (DWORD(right) << 16); }
static XINPUT_VIBRATION Unpack(DWORD value) { return {WORD(value), WORD(value >> 16)}; }
static XINPUT_VIBRATION MaxOutput(XINPUT_VIBRATION a,XINPUT_VIBRATION b) {
    return {a.wLeftMotorSpeed>b.wLeftMotorSpeed?a.wLeftMotorSpeed:b.wLeftMotorSpeed,
            a.wRightMotorSpeed>b.wRightMotorSpeed?a.wRightMotorSpeed:b.wRightMotorSpeed};
}

static DWORD WINAPI ObserveSet(DWORD index, XINPUT_VIBRATION* request) {
    if (!request) return SendMotorOutput(index, request);
    DWORD packed = Pack(request->wLeftMotorSpeed, request->wRightMotorSpeed);
    DWORD previous = index < 4 ? nativePacked[index].exchange(packed) : ~packed;
    XINPUT_VIBRATION output = *request;
    if (muted || !foreground) output = {};
    else if (testing && index == lastController) output = Unpack(testPacked);
    else if (ownsOutput && index==lastController) output=MaxOutput(output,Unpack(ownedPacked));
    DWORD result = SendMotorOutput(index, &output);
    static std::atomic<unsigned> first{0};
    if (previous != packed || first++ < 8) {
        Event event{}; event.kind = 2; event.index = index; event.left = request->wLeftMotorSpeed;
        event.right = request->wRightMotorSpeed; event.result = result;
        event.caller = ReturnRva(__builtin_return_address(0));
        Enqueue(event, packed != 0 || previous != packed);
    }
    return result;
}

static DWORD WINAPI ObserveGet(DWORD index, XINPUT_STATE* state) {
    DWORD result = ReadControllerState(index, state);
    if (result == ERROR_SUCCESS && state && index < 4) {
        lastController = index;
        static std::atomic<DWORD> previous[4];
        DWORD buttons = state->Gamepad.wButtons | (DWORD(state->Gamepad.bLeftTrigger > 100) << 16)
                        | (DWORD(state->Gamepad.bRightTrigger > 100) << 17);
        controllerButtons[index]=buttons;
        DWORD oldButtons=previous[index].exchange(buttons);
        if(foreground && ((buttons&~oldButtons)&(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK)))stopRequested=true;
        if (oldButtons != buttons) {
            Event event{}; event.kind = 3; event.index = index; event.buttons = buttons;
            event.caller = ReturnRva(__builtin_return_address(0)); Enqueue(event, false);
        }
    }
    return result;
}

static void ObservePad(void* pad, float left, float right) {
    static std::atomic<unsigned> count{0};
    static std::atomic<DWORD> previousL{~0u}, previousR{~0u};
    DWORD l, r; memcpy(&l, &left, 4); memcpy(&r, &right, 4);
    bool changedL = previousL.exchange(l) != l, changedR = previousR.exchange(r) != r;
    if (changedL || changedR || count++ < 8) {
        Event event{}; event.kind = 1; event.left = left; event.right = right;
        event.object = reinterpret_cast<uintptr_t>(pad);
        event.caller = ReturnRva(__builtin_return_address(0));
        Enqueue(event, true);
    }
    originalPad(pad, left, right);
}

static int ObserveSound(unsigned code, void* parameters) {
    uintptr_t caller = ReturnRva(__builtin_return_address(0));
    int result = originalSound(code, parameters);
    if (foreground && result != -1) {
        Event event{}; event.kind=4; event.index=code; event.result=DWORD(result);
        event.buttons=*reinterpret_cast<DWORD*>(static_cast<BYTE*>(parameters)+0x214);
        event.object=reinterpret_cast<uintptr_t>(parameters); event.caller=caller;
        event.contact=contactCapture;
        event.actorSlot=~0u;
        uintptr_t actor=0;SIZE_T bytes=0;BYTE slot=0xff;
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0x37fdd80),&actor,sizeof(actor),&bytes)) {
            // This is the animation interpreter's object, not the physical
            // collision-array base. The verified interpreter code uses +25d0.
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0x37fdd88),&slot,sizeof(slot),&bytes);
            if(slot<64) {
                event.actorSlot=slot;
                ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(actor+0x25d0),&event.scriptPc,sizeof(event.scriptPc),&bytes);
            }
        }
        uintptr_t bank=0,playerPc=0;BYTE header[16]{};
        if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0xcc0ab0+0x26d0),&bank,sizeof(bank),&bytes) &&
           ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(gameBase+0x3757570+0x25d0),&playerPc,sizeof(playerPc),&bytes) &&
           playerPc>=bank && playerPc-bank<1889840 &&
           ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(bank),header,sizeof(header),&bytes)) {
            unsigned size=0;memcpy(&size,header+12,4);
            if(!memcmp(header,"chr_dat",8) && size==1889840)event.playerScriptOffset=unsigned(playerPc-bank);
        }
        Enqueue(event,captureStacks||requireSoundStacks,requireSoundStacks);
    }
    return result;
}

// Read-only collision provenance. Keeping this thread-local avoids attributing
// another thread's unrelated voice or animation sound to the active collision.
static int ObserveContact(void* source,void* target,void* aux,unsigned mode,unsigned flags) {
    auto previous=contactCapture;
    contactCapture={reinterpret_cast<uintptr_t>(source),reinterpret_cast<uintptr_t>(target),
                    reinterpret_cast<uintptr_t>(aux),mode,flags};
    int result=originalContact(source,target,aux,mode,flags);
    contactCapture=previous;
    return result;
}

static BOOL CALLBACK LoadProxy(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]; GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\dinput8.dll"); realDinput = LoadLibraryW(path);
    return realDinput != nullptr;
}

static FARPROC DinputProc(const char* name) {
    if (!InitOnceExecuteOnce(&proxyOnce, LoadProxy, nullptr, nullptr)) return nullptr;
    return GetProcAddress(realDinput, name);
}

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    auto fn = reinterpret_cast<HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN)>(DinputProc("DirectInput8Create"));
    return fn ? fn(instance, version, iid, out, outer) : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(DinputProc("DllCanUnloadNow")); return fn ? fn() : S_FALSE;
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID cls, REFIID iid, LPVOID* out) {
    auto fn = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*)>(DinputProc("DllGetClassObject")); return fn ? fn(cls,iid,out) : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(DinputProc("DllRegisterServer")); return fn ? fn() : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    auto fn = reinterpret_cast<HRESULT(WINAPI*)()>(DinputProc("DllUnregisterServer")); return fn ? fn() : E_FAIL;
}

static bool HashFile(const wchar_t* path, char hex[65]) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool success = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    BYTE buffer[32768], digest[32]; DWORD bytes = 0;
    while (success) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { success = false; break; }
        if (!bytes) break;
        success = BCryptHashData(hash, buffer, bytes, 0) >= 0;
    }
    success = success && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(file);
    if (success) { for (unsigned i = 0; i < 32; ++i) sprintf(hex + 2*i, "%02x", digest[i]); hex[64] = 0; }
    return success;
}

static bool PatchIAT(DWORD rva, void* replacement, void* expected) {
    auto slot = reinterpret_cast<void**>(gameBase + rva);
    if (*slot != expected) { Print("IAT_CONFLICT rva=%lx; keeping existing pointer", static_cast<unsigned long>(rva)); return false; }
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, replacement);
    DWORD ignored; VirtualProtect(slot, sizeof(void*), old, &ignored);
    return true;
}

static void StopAll() {
    dualSense.Stop();
    if(realSetState){XINPUT_VIBRATION quiet{};for(DWORD i=0;i<4;++i)realSetState(i,&quiet);}
    ++eventGeneration;attackContext.clear();chargeState.clear();aerialLanding.clear();memset(dedupLast,0,sizeof(dedupLast));
    actionSustain.stop(sustainedActions,sustainedCount,ReadPlayerScriptOffset());
    testing = false; testPacked = 0;
    mixer.clear();ownsOutput=false;ownedPacked=0;
    XINPUT_VIBRATION zero{};
    for (DWORD index = 0; index < 4; ++index) { nativePacked[index] = 0; SendMotorOutput(index, &zero); }
}

static bool GameForeground() {
    DWORD process = 0; GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}

static DWORD WINAPI Worker(void*) {
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                      reinterpret_cast<LPCWSTR>(Worker), &pinned);
    startTick = GetTickCount64();
    GetModuleFileNameW(selfModule, root, MAX_PATH);
    wchar_t* slash = wcsrchr(root, L'\\'); if (!slash) return 0; *slash = 0;
    wchar_t directory[MAX_PATH]; swprintf_s(directory, L"%s\\NGS1Rumble", root); CreateDirectoryW(directory, nullptr);
    SYSTEMTIME time; GetLocalTime(&time);
    swprintf_s(logPath, L"%s\\diagnostic-%04u%02u%02u-%02u%02u%02u-%lu.log", directory,
               time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId());
    swprintf_s(iniPath, L"%s\\NGS1Rumble.ini", root);
    logFile = _wfsopen(logPath, L"wb", _SH_DENYNO); if (!logFile) return 0;
    Print("NGS1 Modular Controller Core 0.7.3 pid=%lu", GetCurrentProcessId());
    wchar_t executable[MAX_PATH]; GetModuleFileNameW(nullptr, executable, MAX_PATH);
    char hash[65]{}; bool hashOk = HashFile(executable, hash);
    Print("exe_sha256=%s", hashOk ? hash : "READ_ERROR");
    auto compatibility=GameCompatibility::InspectFile(executable);
    bool companionView=hashOk&&!strcmp(hash,kReadViewHash);
    if (!hashOk || (compatibility==GameCompatibility::Kind::Unsupported&&!companionView)) { Print("UNSUPPORTED_EXE: no compatible core layout; no game hooks or controller output installed"); fclose(logFile); logFile = nullptr; return 0; }
    Print("DISK_COMPATIBILITY family=%s companion_read_view=%u",companionView&&compatibility==GameCompatibility::Kind::Unsupported?"companion-read-view":GameCompatibility::Name(compatibility),companionView);
    gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    gameKind=GameCompatibility::InspectLoaded(GetModuleHandleW(nullptr));
    if(gameKind==GameCompatibility::Kind::Unsupported||(!companionView&&!GameCompatibility::SameLayout(compatibility,gameKind))){Print("LIVE_CORE_LAYOUT_MISMATCH: no hooks installed");return 0;}
    Print("LIVE_CORE_PROFILE=%s sound_rva=%llx contact_rva=%llx",GameCompatibility::Name(gameKind),static_cast<unsigned long long>(CodeRva(0x699450)),static_cast<unsigned long long>(CodeRva(0x690770)));
    rumbleEnabled=GetPrivateProfileIntW(L"Features",L"Rumble",1,iniPath)!=0&&GetPrivateProfileIntW(L"Rumble",L"Enabled",1,iniPath)!=0;
    const bool wantDualSense=GetPrivateProfileIntW(L"Features",L"DualSense",0,iniPath)!=0;
    const bool wantAfterimage=GetPrivateProfileIntW(L"Features",L"Afterimage",0,iniPath)!=0;
    auto startVisuals=[&] {
        if(wantAfterimage)Print("AFTERIMAGE initialized=%u",StartAfterimage(root,gameKind,
            {ControllerIcons::SharedOriginal,ControllerIcons::ConnectAfterimage}));
    };
    if(!rumbleEnabled&&!wantDualSense){
        startVisuals();
        lastController=~0u;
        MonitorOpen();
        MonitorUpdate(true);
        return 0;
    }
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(gameBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) { Print("BAD_PE_HEADER"); return 0; }
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(gameBase + dos->e_lfanew);
    const BYTE livePad[] = {0x48,0x83,0xec,0x28,0xf3,0x0f,0x59,0x0d,0xa8,0xc1,0x4c,0x00,0x48,0x8d,0x54,0x24,0x38,0x8b,0x89,0xa0,0x00,0x00,0x00,0xf3};
    const BYTE liveGet[] = {0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0xb7,0xfb,0x67,0x00,0x48,0x33,0xc4,0x48,0x89,0x44,0x24};
    const BYTE liveContact[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x48,0x89,0x48,0x08,0x53,0x48,0x83,0xec,0x70};
    const BYTE liveSound[]={0x40,0x55,0x53,0x56,0x57,0x48,0x8d,0xac,0x24,0x28,0xfe,0xff,0xff};
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || memcmp(reinterpret_cast<void*>(gameBase+0x57e6a0),livePad,sizeof(livePad))
        || memcmp(reinterpret_cast<void*>(gameBase+0x57dab0),liveGet,sizeof(liveGet))
        || memcmp(reinterpret_cast<void*>(gameBase+CodeRva(0x690770)),liveContact,sizeof(liveContact))
        || memcmp(reinterpret_cast<void*>(gameBase+CodeRva(0x699450)),liveSound,sizeof(liveSound))
        || *reinterpret_cast<uintptr_t*>(gameBase+0x959990) != gameBase+0x57e6a0) {
        Print("LIVE_CODE_FINGERPRINT_MISMATCH: no controller hooks installed"); startVisuals(); return 0;
    }
    Print("LIVE_CODE_FINGERPRINT_OK");
    captureStacks = GetPrivateProfileIntW(L"Rumble", L"CaptureStacks", 1, iniPath) != 0;
    logSoundEvents = GetPrivateProfileIntW(L"Rumble", L"CaptureSoundEvents", 1, iniPath) != 0;
    if(rumbleEnabled)LoadRules();
    pulseStrength = GetPrivateProfileIntW(L"Rumble", L"TestStrength", 16000, iniPath);
    if (pulseStrength > 25000) pulseStrength = 25000;
    wchar_t system[MAX_PATH]; GetSystemDirectoryW(system, MAX_PATH); wcscat_s(system, L"\\XInput1_3.dll");
    realXinput = LoadLibraryW(system);
    if (!realXinput) { Print("XINPUT_LOAD_FAILED error=%lu", GetLastError()); startVisuals(); return 0; }
    realGetState = reinterpret_cast<GetStateFn>(GetProcAddress(realXinput,"XInputGetState"));
    realSetState = reinterpret_cast<SetStateFn>(GetProcAddress(realXinput,"XInputSetState"));
    realCapabilities=reinterpret_cast<CapabilitiesFn>(GetProcAddress(realXinput,"XInputGetCapabilities"));
    if (!realGetState || !realSetState||!realCapabilities) { Print("XINPUT_EXPORT_FAILED"); startVisuals(); return 0; }
    if(*reinterpret_cast<void**>(gameBase+0x91c900)!=reinterpret_cast<void*>(realGetState)||
       *reinterpret_cast<void**>(gameBase+0x91c8f8)!=reinterpret_cast<void*>(realSetState)||
       *reinterpret_cast<void**>(gameBase+0x91c908)!=reinterpret_cast<void*>(realCapabilities)){
        Print("XINPUT_IAT_FINGERPRINT_MISMATCH: no controller hooks installed");startVisuals();return 0;
    }
    foreground = GameForeground();
    // Verify the exact original prologue as well as the complete file hash.
    const BYTE prologue[] = {0x48,0x83,0xec,0x28,0xf3,0x0f,0x59,0x0d,0xa8,0xc1,0x4c,0x00};
    padHookTarget = reinterpret_cast<void*>(gameBase + 0x57E6A0);
    MH_STATUS status = MH_Initialize();
    if (!rumbleEnabled&&status==MH_OK){Print("RUMBLE_FEATURE_DISABLED");}
    else if (status == MH_OK && !memcmp(padHookTarget, prologue, sizeof(prologue))) {
        status = MH_CreateHook(padHookTarget, reinterpret_cast<void*>(ObservePad), reinterpret_cast<void**>(&originalPad));
        if (status == MH_OK) status = MH_EnableHook(padHookTarget);
    } else { Print("PAD_PROLOGUE_OR_HOOK_INIT_MISMATCH"); status = MH_ERROR_UNSUPPORTED_FUNCTION; }
    Print("pad_hook=%s", MH_StatusToString(status));
    if(status==MH_OK&&rumbleEnabled) {
        const BYTE expectedContact[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x48,0x89,0x48,0x08,0x53,0x48,0x83,0xec,0x70};
        void* target=reinterpret_cast<void*>(gameBase+CodeRva(0x690770));
        if(!memcmp(target,expectedContact,sizeof(expectedContact))) {
            MH_STATUS contactStatus=MH_CreateHook(target,reinterpret_cast<void*>(ObserveContact),reinterpret_cast<void**>(&originalContact));
            if(contactStatus==MH_OK)contactStatus=MH_EnableHook(target);
            Print("contact_provenance_hook=%s; diagnostic_only=1",MH_StatusToString(contactStatus));
        }else Print("CONTACT_PROLOGUE_MISMATCH: provenance capture skipped");
    }
    if (status == MH_OK && rumbleEnabled && (ruleCount || logSoundEvents)) {
        const BYTE soundPrologue[]={0x40,0x55,0x53,0x56,0x57,0x48,0x8d,0xac,0x24,0x28,0xfe,0xff,0xff};
        void* soundTarget=reinterpret_cast<void*>(gameBase+CodeRva(0x699450));
        if (!memcmp(soundTarget,soundPrologue,sizeof(soundPrologue))) {
            MH_STATUS soundStatus=MH_CreateHook(soundTarget,reinterpret_cast<void*>(ObserveSound),reinterpret_cast<void**>(&originalSound));
            if(soundStatus==MH_OK) soundStatus=MH_EnableHook(soundTarget);
            Print("sound_hook=%s",MH_StatusToString(soundStatus));
        } else Print("SOUND_PROLOGUE_MISMATCH: audio event capture skipped");
    }
    bool getHook = PatchIAT(0x91C900, reinterpret_cast<void*>(ObserveGet), reinterpret_cast<void*>(realGetState));
    bool setHook = PatchIAT(0x91C8F8, reinterpret_cast<void*>(ObserveSet), reinterpret_cast<void*>(realSetState));
    dualSenseEnabled=wantDualSense;
    autoIcons=dualSenseEnabled&&ControllerIcons::HasIconData&&GetPrivateProfileIntW(L"Features",L"AutoIcons",1,iniPath)!=0;
    preferDualSense=GetPrivateProfileIntW(L"DualSense",L"PreferDualSense",1,iniPath)!=0;
    bool capsHook=PatchIAT(0x91C908,reinterpret_cast<void*>(ObserveCapabilities),reinterpret_cast<void*>(realCapabilities));
    if(!getHook||!setHook||!capsHook)dualSenseEnabled=false;
    if(dualSenseEnabled){
        const BYTE classifyPrefix[]={0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
        const BYTE enumPrefix[]={0x4c,0x8b,0xc2,0x48,0x8b,0xd1,0x33,0xc9,0xe9};
        void* classifyTarget=reinterpret_cast<void*>(gameBase+0x57dcf0);
        void* enumTarget=reinterpret_cast<void*>(gameBase+0x575cd0);
        MH_STATUS classifyStatus=MH_ERROR_UNSUPPORTED_FUNCTION,enumStatus=MH_ERROR_UNSUPPORTED_FUNCTION;
        if(!memcmp(classifyTarget,classifyPrefix,sizeof(classifyPrefix))&&!memcmp(enumTarget,enumPrefix,sizeof(enumPrefix))){
            classifyStatus=MH_CreateHook(classifyTarget,reinterpret_cast<void*>(ObserveInputClassify),reinterpret_cast<void**>(&originalInputClassify));
            enumStatus=MH_CreateHook(enumTarget,reinterpret_cast<void*>(ObserveEnumDevice),reinterpret_cast<void**>(&originalEnumDevice));
            if(classifyStatus==MH_OK&&enumStatus==MH_OK){classifyStatus=MH_EnableHook(classifyTarget);enumStatus=MH_EnableHook(enumTarget);}
        }
        if(classifyStatus!=MH_OK||enumStatus!=MH_OK){dualSenseEnabled=false;MH_DisableHook(classifyTarget);MH_DisableHook(enumTarget);}
        Print("DUALSENSE_GAME_INPUT classify=%s enumeration=%s",MH_StatusToString(classifyStatus),MH_StatusToString(enumStatus));
    }
    Print("DUALSENSE enabled=%u prefer=%u capabilities_hook=%u",dualSenseEnabled,preferDualSense,capsHook);
    if(autoIcons&&!ControllerIcons::Initialize(Print))Print("ICON_RENDERER_FAILED");
    startVisuals();
    Print("FEATURES rumble=%u dualsense=%u auto_icons=%u",rumbleEnabled,dualSenseEnabled,autoIcons);
    Print("iat_get=%u iat_set=%u base=%llx", getHook, setHook, static_cast<unsigned long long>(gameBase));
    Print("READY F5=reload_rules F6=test_left_then_right F7=marker F9=STOP; experimental_event_rules=%u",ruleCount);
    MonitorOpen();
    bool previousF5=false,previousF6=false, previousF7=false, previousF9=false,previousEsc=false;
    bool oldFocus=foreground;
    ULONGLONG testStart=0, heartbeat=startTick;
    DWORD previousConnected=~0u;
    unsigned marker=0;
    for (;;) {
        if(dualSenseEnabled)dualSense.Poll();
        static bool oldDualSense=false;
        bool dsConnected=dualSenseEnabled&&dualSense.IsConnected();
        if(dsConnected!=oldDualSense){StopAll();dualSenseRefreshNeeded=dsConnected;dualSenseFilterUntil=0;Print("DUALSENSE_STATE connected=%u transport=%s firmware=%04x improved=%u error=%lu",dsConnected,dualSense.IsBluetooth()?"Bluetooth":"USB",dualSense.Firmware(),dualSense.Improved(),dualSense.LastError());oldDualSense=dsConnected;}
        if(dualSenseEnabled&&controllerRouter.Poll(realGetState,dualSense,preferDualSense)){StopAll();Print("ACTIVE_CONTROLLER source=%s",controllerRouter.IsDualSense()?"DualSense":"XInput");}
        if(autoIcons)ControllerIcons::Select(DualSenseRoute(0));
        ULONGLONG now=GetTickCount64(); bool focus=GameForeground(); foreground=focus;
        if (focus != oldFocus) { if (!focus) StopAll(); Print("FOCUS %u", focus); oldFocus=focus; }
        bool f5=(GetAsyncKeyState(VK_F5)&0x8001)!=0, f6=(GetAsyncKeyState(VK_F6)&0x8001)!=0, f7=(GetAsyncKeyState(VK_F7)&0x8001)!=0, f9=(GetAsyncKeyState(VK_F9)&0x8001)!=0;
        bool esc=(GetAsyncKeyState(VK_ESCAPE)&0x8001)!=0;
        bool menuStop=stopRequested.exchange(false);
        if(focus && ((esc&&!previousEsc)||menuStop)) {StopAll();Print("MENU_INPUT_STOP");}
        previousEsc=esc;
        DWORD monitorCommand=MonitorPollCommand();
        if(monitorCommand){
            DWORD commandResult=0;
            if(monitorCommand==1){StopAll();if(rumbleEnabled)LoadRules();actionSustain.stop(sustainedActions,sustainedCount,ReadPlayerScriptOffset());++monitorReloads;Print("RULES_RELOADED monitor=1");}
            else if(monitorCommand==2){muted=true;StopAll();Print("EMERGENCY_STOP monitor=1");}
            else if(monitorCommand==3){StopAll();muted=false;Print("RUMBLE_RESUMED monitor=1");}
            else commandResult=ERROR_INVALID_PARAMETER;
            MonitorAcknowledge(commandResult);
        }
        if(focus && f5 && !previousF5){StopAll();if(rumbleEnabled)LoadRules();actionSustain.stop(sustainedActions,sustainedCount,ReadPlayerScriptOffset());++monitorReloads;Print("RULES_RELOADED");}
        if (focus && f9 && !previousF9) { muted=true; StopAll(); Print("EMERGENCY_STOP; F6 clears mute for self-test"); }
        if (focus && f7 && !previousF7) Print("USER_MARKER %u", ++marker);
        if (rumbleEnabled&&focus && f6 && !previousF6 && !testing) {
            XINPUT_STATE state{}; DWORD chosen=lastController; DWORD result=ReadControllerState(chosen,&state);
            if (result!=ERROR_SUCCESS) for (DWORD i=0;i<4;++i) if ((result=ReadControllerState(i,&state))==ERROR_SUCCESS) { chosen=i; break; }
            if (result==ERROR_SUCCESS) { mixer.clear();ownsOutput=false;ownedPacked=0;lastController=chosen; muted=false; testStart=now; testing=true; Print("SELF_TEST_BEGIN index=%lu strength=%lu", chosen,pulseStrength); }
            else Print("SELF_TEST_NO_CONTROLLER result=%lu", result);
        }
        previousF5=f5;previousF6=f6;previousF7=f7;previousF9=f9;
        Drain();
        now=GetTickCount64();
        if (testing) {
            ULONGLONG elapsed=now-testStart; XINPUT_VIBRATION output{};
            if (elapsed<150) output.wLeftMotorSpeed=WORD(pulseStrength);
            else if (elapsed>=250 && elapsed<400) output.wRightMotorSpeed=WORD(pulseStrength);
            testPacked=Pack(output.wLeftMotorSpeed,output.wRightMotorSpeed);
            DWORD result=SendMotorOutput(lastController,&output);
            if (elapsed>=400 || !focus || muted || result!=ERROR_SUCCESS) {
                StopAll(); Print("SELF_TEST_END result=%lu stopped=1", result);
            }
        }
        if (rumbleEnabled&&!testing && focus && !muted) {
            MotorOutput pulse=mixer.sample(now,4,true);
            int oldAction=actionSustain.active();
            unsigned scriptOffset=ReadPlayerScriptOffset();
            MotorOutput actionPulse=actionSustain.sample(sustainedActions,sustainedCount,scriptOffset,now);
            int newAction=actionSustain.active();
            if(oldAction!=newAction){
                if(oldAction>=0)Print("SUSTAIN_STOP label=%s script=%x",sustainedActions[oldAction].label,scriptOffset);
                if(newAction>=0)Print("SUSTAIN_START label=%s L=%u R=%u script=%x",sustainedActions[newAction].label,actionPulse.left,actionPulse.right,scriptOffset);
            }
            if(actionPulse.left>pulse.left)pulse.left=actionPulse.left;
            if(actionPulse.right>pulse.right)pulse.right=actionPulse.right;
            MotorOutput chargePulse=mixer.sample(now,4,false);
            DWORD controller=lastController;
            bool chargeHeld=controller<4 && (controllerButtons[controller]&XINPUT_GAMEPAD_Y)!=0;
            if(chargeHeld)chargeState.renew(now);
            else if(chargeState.sustaining(now)){chargeState.finish(now);mixer.cancel(4);}
            MotorOutput sustained=SampleChargeCurve(chargeState,chargeCurve,now,chargeHeld);
            if(sustained.left>chargePulse.left)chargePulse.left=sustained.left;
            if(sustained.right>chargePulse.right)chargePulse.right=sustained.right;
            pulse=MixGameAndCharge(pulse,chargePulse,gameGainPercent,chargeGainPercent);
            bool active=pulse.left||pulse.right;
            ownedPacked=Pack(pulse.left,pulse.right);
            bool wasActive=ownsOutput.exchange(active);
            if(active||wasActive) {
                DWORD index=lastController;
                XINPUT_VIBRATION output=MaxOutput(Unpack(nativePacked[index<4?index:0]),Unpack(ownedPacked));
                DWORD result=SendMotorOutput(index,&output);
                if(active&&!wasActive)Print("OUTPUT_START index=%lu result=%lu",index,result);
                if(!active&&wasActive)Print("OUTPUT_STOP index=%lu result=%lu",index,result);
                if(result!=ERROR_SUCCESS){StopAll();Print("OUTPUT_ERROR result=%lu",result);}
            }
        }
        if (now-heartbeat>=1000) {
            DWORD connected=0; XINPUT_STATE state{};
            for (DWORD i=0;i<4;++i) if (ReadControllerState(i,&state)==ERROR_SUCCESS) connected|=1u<<i;
            if (connected!=previousConnected) { Print("CONTROLLERS mask=%lx", connected); previousConnected=connected; }
            DWORD lost=dropped.exchange(0); if(lost) Print("DROPPED_EVENTS %lu",lost);
            heartbeat=now;
        }
        MonitorUpdate(status==MH_OK && getHook && setHook);
        Sleep(4);
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule=module; DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr); if(thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH && realSetState) {
        XINPUT_VIBRATION zero{};if(testing||ownsOutput)SendMotorOutput(lastController, &zero);
        dualSense.EmergencyStop();
    }
    return TRUE;
}
