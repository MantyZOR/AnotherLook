#include "live_probe.h"
#include "core.h"
#include "version.h"
#include <array>
#include <atomic>
#include <map>

namespace gv {
namespace {
constexpr const wchar_t* areaNames[]={L"surfaceMessages",L"mouseMove",L"hitTest",L"paintBoard",L"paintRail",L"paintTool",L"orbitRaster",L"orbitVector",L"network",L"refreshWindows",L"syncPanels",L"iconLookup",L"traySync",L"eventMessages",L"present",L"pointerNetwork",L"pointerHit",L"pointerHover",L"pointerCursor",L"tooltipPop",L"boardCards",L"endDraw",L"systemSample"};
constexpr const wchar_t* countNames[]={L"rasterFrames",L"rasterFallbacks",L"rasterBuilds",L"mouseLeaves",L"iconMisses",L"surfaceMoves",L"surfaceHitTests",L"surfacePaints",L"surfaceTimers",L"surfacePositions",L"surfaceNotifications"};
struct Area {uint64_t calls=0,ticks=0,maximum=0;};
struct State {
    std::array<Area,static_cast<size_t>(ProbeArea::Count)> areas{};
    std::array<uint64_t,static_cast<size_t>(ProbeCount::Count)> counts{};
    std::map<DWORD,uint64_t> events;
    std::vector<json::JsonObject> samples;
    DWORD thread=0;HWND owner{};fs::path output;
    uint64_t began=0,cpu=0,uiCpu=0,cycles=0,frequency=1;unsigned seconds=30,processors=1;
} state;
std::atomic_bool enabled=false;
std::atomic<DWORD> ownerThread=0;
uint64_t Time(FILETIME a,FILETIME b){ULARGE_INTEGER x{},y{};x.LowPart=a.dwLowDateTime;x.HighPart=a.dwHighDateTime;y.LowPart=b.dwLowDateTime;y.HighPart=b.dwHighDateTime;return x.QuadPart+y.QuadPart;}
uint64_t Cpu(bool thread){FILETIME a{},b{},kernel{},user{};if(thread)GetThreadTimes(GetCurrentThread(),&a,&b,&kernel,&user);else GetProcessTimes(GetCurrentProcess(),&a,&b,&kernel,&user);return Time(kernel,user);}
json::JsonObject Snapshot(){
    json::JsonObject sample;double elapsed=(LiveProbeClock()-state.began)*1000./state.frequency;
    auto number=[&](const wchar_t* key,double value){sample.SetNamedValue(key,json::JsonValue::CreateNumberValue(value));};
    number(L"elapsedMs",elapsed);number(L"processCpuMs",(Cpu(false)-state.cpu)/10000.);number(L"uiCpuMs",(Cpu(true)-state.uiCpu)/10000.);
    ULONG64 cycles=0;QueryProcessCycleTime(GetCurrentProcess(),&cycles);number(L"processCycles",static_cast<double>(cycles-state.cycles));
    json::JsonObject areas;for(size_t i=0;i<state.areas.size();++i){auto value=state.areas[i];json::JsonObject row;row.SetNamedValue(L"calls",json::JsonValue::CreateNumberValue(static_cast<double>(value.calls)));row.SetNamedValue(L"wallMs",json::JsonValue::CreateNumberValue(value.ticks*1000./state.frequency));row.SetNamedValue(L"maxMs",json::JsonValue::CreateNumberValue(value.maximum*1000./state.frequency));areas.SetNamedValue(areaNames[i],row);}sample.SetNamedValue(L"areas",areas);
    json::JsonObject counts;for(size_t i=0;i<state.counts.size();++i)counts.SetNamedValue(countNames[i],json::JsonValue::CreateNumberValue(static_cast<double>(state.counts[i])));sample.SetNamedValue(L"counts",counts);
    json::JsonObject events;for(auto [id,count]:state.events)events.SetNamedValue(std::to_wstring(id),json::JsonValue::CreateNumberValue(static_cast<double>(count)));sample.SetNamedValue(L"windowEvents",events);return sample;
}
void Write(bool active){
    json::JsonObject report;report.SetNamedValue(L"version",json::JsonValue::CreateStringValue(GOODVIEW_VERSION_W));report.SetNamedValue(L"probe",json::JsonValue::CreateStringValue(L"live-ui-v1"));report.SetNamedValue(L"active",json::JsonValue::CreateBooleanValue(active));report.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(GetCurrentProcessId()));report.SetNamedValue(L"uiThread",json::JsonValue::CreateNumberValue(state.thread));report.SetNamedValue(L"logicalProcessors",json::JsonValue::CreateNumberValue(state.processors));
    json::JsonArray samples;for(auto sample:state.samples)samples.Append(sample);report.SetNamedValue(L"samples",samples);WriteAtomic(state.output,std::wstring(report.Stringify()));
}
}
static_assert(std::size(areaNames)==static_cast<size_t>(ProbeArea::Count));
static_assert(std::size(countNames)==static_cast<size_t>(ProbeCount::Count));
bool LiveProbeRunning(){return enabled.load(std::memory_order_acquire)&&GetCurrentThreadId()==ownerThread.load(std::memory_order_relaxed);}
uint64_t LiveProbeClock(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
void LiveProbeRecord(ProbeArea area,uint64_t started){if(!LiveProbeRunning())return;auto elapsed=LiveProbeClock()-started;auto& value=state.areas[static_cast<size_t>(area)];++value.calls;value.ticks+=elapsed;value.maximum=std::max(value.maximum,elapsed);}
void LiveProbeCount(ProbeCount counter){if(LiveProbeRunning())++state.counts[static_cast<size_t>(counter)];}
void LiveProbeWindowEvent(DWORD event){if(LiveProbeRunning())++state.events[event];}
bool StartLiveProbe(HWND owner,const fs::path& directory,unsigned seconds){
    if(!owner||!IsWindow(owner)||GetWindowThreadProcessId(owner,nullptr)!=GetCurrentThreadId())return false;
    try{enabled.store(false,std::memory_order_release);KillTimer(owner,LiveProbeTimer);state={};state.thread=GetCurrentThreadId();ownerThread.store(state.thread,std::memory_order_relaxed);state.owner=owner;state.output=directory/L"ui-performance.json";state.seconds=std::clamp(seconds,5u,120u);state.processors=static_cast<unsigned>(std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);state.frequency=frequency.QuadPart;state.began=LiveProbeClock();state.cpu=Cpu(false);state.uiCpu=Cpu(true);QueryProcessCycleTime(GetCurrentProcess(),&state.cycles);enabled.store(true,std::memory_order_release);state.samples.push_back(Snapshot());Write(true);if(SetTimer(owner,LiveProbeTimer,1000,nullptr))return true;enabled.store(false,std::memory_order_release);return false;}catch(...){enabled.store(false,std::memory_order_release);KillTimer(owner,LiveProbeTimer);return false;}
}
void TickLiveProbe(){
    if(!LiveProbeRunning())return;
    try{state.samples.push_back(Snapshot());bool active=(LiveProbeClock()-state.began)<state.seconds*state.frequency;if(!active){enabled.store(false,std::memory_order_release);KillTimer(state.owner,LiveProbeTimer);}Write(active);if(!active)state.samples.clear();}catch(...){enabled.store(false,std::memory_order_release);KillTimer(state.owner,LiveProbeTimer);state.samples.clear();}
}
}
