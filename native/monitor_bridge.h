#pragma once
// Preview telemetry is sampled on the existing worker. Hooks never wait on UI.
// Protocol v1 uses fixed-width little-endian fields and a single-writer seqlock.
#pragma pack(push,1)
struct MonitorSnapshot {
    uint32_t magic,version,bytes,seq,pid,ready;
    uint64_t tick,startTick,outputTick;
    uint32_t focus,muted,testing,controller,connected,buttons,lt,rt;
    int32_t lx,ly,rx,ry;
    uint32_t left,right,result,scriptOffset,dropped,reloadAck,reloadCount,commandResult;
    wchar_t log[260];
};
struct MonitorCommand {uint32_t magic,version,serial,command,ack,result;};
#pragma pack(pop)
static_assert(sizeof(MonitorSnapshot)==648,"monitor protocol layout");
static_assert(sizeof(MonitorCommand)==24,"command protocol layout");
static HANDLE monitorMap,monitorControlMap;
static MonitorSnapshot* monitorView;
static MonitorCommand* monitorControl;
static std::atomic<DWORD> monitorPacked[4],monitorResult[4];
static std::atomic<ULONGLONG> monitorOutputTick[4];
static ULONGLONG monitorLastSample=0;
static DWORD monitorReloads=0,monitorHandled=0,monitorCommandResult=0;
struct DualSenseSnapshot {
 uint32_t magic,version,bytes,seq,pid,enabled,connected,transport,slot,firmware,improved,error;
 uint64_t inputTick,writeTick;
};
static_assert(sizeof(DualSenseSnapshot)==64,"DualSense telemetry layout");
static HANDLE dualSenseMap;static DualSenseSnapshot* dualSenseView;
static DWORD SendMotorOutput(DWORD index,XINPUT_VIBRATION* vibration) {
    DWORD result=WriteControllerOutput(index,vibration);
    if(index<4 && vibration){
        monitorPacked[index]=DWORD(vibration->wLeftMotorSpeed)|(DWORD(vibration->wRightMotorSpeed)<<16);
        monitorResult[index]=result;monitorOutputTick[index]=GetTickCount64();
    }
    return result;
}
static void MonitorOpen() {
    wchar_t name[100];swprintf_s(name,L"Local\\NGS1RumbleMonitor-%lu",GetCurrentProcessId());
    monitorMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(MonitorSnapshot),name);
    if(monitorMap)monitorView=static_cast<MonitorSnapshot*>(MapViewOfFile(monitorMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(MonitorSnapshot)));
    swprintf_s(name,L"Local\\NGS1RumbleControl-%lu",GetCurrentProcessId());
    monitorControlMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(MonitorCommand),name);
    if(monitorControlMap)monitorControl=static_cast<MonitorCommand*>(MapViewOfFile(monitorControlMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(MonitorCommand)));
    if(monitorView){memset(monitorView,0,sizeof(*monitorView));monitorView->magic=0x4d52474e;monitorView->version=1;monitorView->bytes=sizeof(*monitorView);}
    if(monitorControl){memset(monitorControl,0,sizeof(*monitorControl));monitorControl->magic=0x434d474e;monitorControl->version=1;}
    swprintf_s(name,L"Local\\NGS1DualSense-%lu",GetCurrentProcessId());
    dualSenseMap=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(DualSenseSnapshot),name);
    if(dualSenseMap)dualSenseView=static_cast<DualSenseSnapshot*>(MapViewOfFile(dualSenseMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(DualSenseSnapshot)));
    if(dualSenseView){memset(dualSenseView,0,sizeof(*dualSenseView));dualSenseView->magic=0x3553474e;dualSenseView->version=1;dualSenseView->bytes=sizeof(DualSenseSnapshot);}
}
static DWORD MonitorPollCommand() {
    if(!monitorControl || monitorControl->magic!=0x434d474e || monitorControl->version!=1)return 0;
    DWORD serial=monitorControl->serial;MemoryBarrier();
    if(!serial || serial==monitorHandled)return 0;
    monitorHandled=serial;
    return monitorControl->command;
}
static void MonitorAcknowledge(DWORD result) {
    monitorCommandResult=result;
    if(monitorControl){monitorControl->result=result;MemoryBarrier();monitorControl->ack=monitorHandled;}
}
static void MonitorUpdate(bool ready) {
    if(!monitorView)return;
    ULONGLONG now=GetTickCount64();if(now-monitorLastSample<16)return;monitorLastSample=now;
    MonitorSnapshot value{};value.pid=GetCurrentProcessId();value.ready=ready;
    value.tick=now;value.startTick=startTick;value.focus=foreground;value.muted=muted;value.testing=testing;
    DWORD index=lastController;value.controller=index;
    XINPUT_STATE input{};
    if(index<4 && ReadControllerState(index,&input)==ERROR_SUCCESS){
        value.connected=1;value.buttons=input.Gamepad.wButtons;value.lt=input.Gamepad.bLeftTrigger;value.rt=input.Gamepad.bRightTrigger;
        value.lx=input.Gamepad.sThumbLX;value.ly=input.Gamepad.sThumbLY;value.rx=input.Gamepad.sThumbRX;value.ry=input.Gamepad.sThumbRY;
    }
    if(index<4){DWORD packed=monitorPacked[index];value.left=packed&65535;value.right=packed>>16;value.result=monitorResult[index];value.outputTick=monitorOutputTick[index];}
    value.scriptOffset=ReadPlayerScriptOffset();
    if(dualSenseView){
        DualSenseSnapshot device{};device.pid=GetCurrentProcessId();device.enabled=dualSenseEnabled;
        device.connected=DualSenseRoute(0);device.transport=dualSense.IsBluetooth()?2:1;device.slot=0;
        device.firmware=dualSense.Firmware();device.improved=dualSense.Improved();device.error=dualSense.LastError();
        device.inputTick=dualSense.LastInputTick();device.writeTick=dualSense.LastWriteTick();
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&dualSenseView->seq));MemoryBarrier();
        memcpy(reinterpret_cast<BYTE*>(dualSenseView)+16,reinterpret_cast<BYTE*>(&device)+16,sizeof(device)-16);
        MemoryBarrier();InterlockedIncrement(reinterpret_cast<volatile LONG*>(&dualSenseView->seq));
    }
    value.dropped=dropped;value.reloadAck=monitorHandled;value.reloadCount=monitorReloads;value.commandResult=monitorCommandResult;
    wcscpy_s(value.log,logPath);
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&monitorView->seq));MemoryBarrier();
    // Preserve the sequence field while publishing the rest of the snapshot.
    memcpy(reinterpret_cast<BYTE*>(monitorView)+16,reinterpret_cast<BYTE*>(&value)+16,sizeof(value)-16);
    MemoryBarrier();InterlockedIncrement(reinterpret_cast<volatile LONG*>(&monitorView->seq));
}
