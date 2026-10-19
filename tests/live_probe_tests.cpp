#include "core.h"
#include "live_probe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)return 2;winrt::init_apartment();gv::fs::path directory=gv::fs::absolute(argv[1]);gv::fs::create_directories(directory);
        if(gv::LiveProbeRunning()||gv::StartLiveProbe(nullptr,directory))throw std::runtime_error("probe must be opt-in and require an owner");
        HWND owner=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!owner||!gv::StartLiveProbe(owner,directory,5))throw std::runtime_error("cannot start an isolated probe");
        {gv::LiveProbeScope scope(gv::ProbeArea::OrbitRaster);gv::LiveProbeCount(gv::ProbeCount::RasterFrames);Sleep(2);}
        gv::LiveProbeWindowEvent(EVENT_OBJECT_SHOW);gv::TickLiveProbe();
        auto read=[&]{std::ifstream file(directory/L"ui-performance.json",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});return gv::json::JsonObject::Parse(gv::Wide(bytes));};
        auto report=read();auto samples=report.GetNamedArray(L"samples");auto sample=samples.GetObjectAt(samples.Size()-1);
        if(!report.GetNamedBoolean(L"active")||sample.GetNamedObject(L"areas").GetNamedObject(L"orbitRaster").GetNamedNumber(L"calls")!=1||sample.GetNamedObject(L"counts").GetNamedNumber(L"rasterFrames")!=1||sample.GetNamedObject(L"windowEvents").GetNamedNumber(std::to_wstring(EVENT_OBJECT_SHOW))!=1)throw std::runtime_error("probe report did not preserve counters and scopes");
        Sleep(5100);gv::TickLiveProbe();if(gv::LiveProbeRunning()||read().GetNamedBoolean(L"active"))throw std::runtime_error("probe failed to stop at its deadline");
        DestroyWindow(owner);std::cout<<"Live probe: opt-in, counters, scope timing, report and bounded stop passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
