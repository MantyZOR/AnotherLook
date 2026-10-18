#include <sdkddkver.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include "system_load.h"
#include <algorithm>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <pdhmsg.h>

namespace gv {
std::optional<float> CpuUsage(CpuTimes before,CpuTimes after){
    if(after.idle<before.idle||after.kernel<before.kernel||after.user<before.user)return {};
    uint64_t idle=after.idle-before.idle,total=(after.kernel-before.kernel)+(after.user-before.user);
    if(!total||idle>total)return {};return std::clamp(100.f*static_cast<float>(total-idle)/static_cast<float>(total),0.f,100.f);
}
SystemLoad::~SystemLoad(){Reset();}
void SystemLoad::Reset(){
    before_.reset();cpu.reset();disk.reset();network.reset();download.reset();upload.reset();networkBefore_.clear();networkTime_=0;
    if(diskQuery_)PdhCloseQuery(diskQuery_);diskQuery_=diskCounter_=nullptr;diskAttempted_=false;metrics_=0;
}
void SystemLoad::Sample(unsigned metrics){
    metrics&=15;if(metrics!=metrics_){Reset();metrics_=metrics;}
    FILETIME idle{},kernel{},user{};auto ticks=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    if(metrics&1){if(GetSystemTimes(&idle,&kernel,&user)){CpuTimes now{ticks(idle),ticks(kernel),ticks(user)};cpu=before_?CpuUsage(*before_,now):std::nullopt;before_=now;}else{cpu.reset();before_.reset();}}
    if(metrics&2){MEMORYSTATUSEX memory{sizeof(memory)};if(GlobalMemoryStatusEx(&memory)){memoryTotal=memory.ullTotalPhys;memoryUsed=memoryTotal-memory.ullAvailPhys;}else memoryUsed=memoryTotal=0;}
    if(metrics&4){
        if(!diskAttempted_){diskAttempted_=true;if(PdhOpenQueryW(nullptr,0,&diskQuery_)==ERROR_SUCCESS){if(PdhAddEnglishCounterW(diskQuery_,L"\\PhysicalDisk(_Total)\\% Disk Time",0,&diskCounter_)!=ERROR_SUCCESS){PdhCloseQuery(diskQuery_);diskQuery_=diskCounter_=nullptr;}}}
        if(diskQuery_&&PdhCollectQueryData(diskQuery_)==ERROR_SUCCESS){PDH_FMT_COUNTERVALUE value{};if(PdhGetFormattedCounterValue(diskCounter_,PDH_FMT_DOUBLE,nullptr,&value)==ERROR_SUCCESS&&(value.CStatus==PDH_CSTATUS_VALID_DATA||value.CStatus==PDH_CSTATUS_NEW_DATA))disk=std::clamp(static_cast<float>(value.doubleValue),0.f,100.f);else disk.reset();}else disk.reset();
    }
    if(metrics&8){
        MIB_IF_TABLE2* table=nullptr;std::map<uint64_t,NetworkCounter> now;uint64_t time=GetTickCount64();
        if(GetIfTable2(&table)==NO_ERROR){for(ULONG i=0;i<table->NumEntries;++i){const auto& row=table->Table[i];if(row.OperStatus!=IfOperStatusUp||!row.InterfaceAndOperStatusFlags.HardwareInterface||(row.Type!=IF_TYPE_ETHERNET_CSMACD&&row.Type!=IF_TYPE_IEEE80211))continue;now[row.InterfaceLuid.Value]={row.InOctets,row.OutOctets,row.ReceiveLinkSpeed,row.TransmitLinkSpeed};}FreeMibTable(table);}
        double received=0,sent=0,receiveCapacity=0,sendCapacity=0;bool valid=networkTime_&&time>networkTime_&&!now.empty()&&now.size()==networkBefore_.size();
        for(const auto& [id,value]:now){auto previous=networkBefore_.find(id);if(previous==networkBefore_.end()||value.received<previous->second.received||value.sent<previous->second.sent){valid=false;continue;}received+=static_cast<double>(value.received-previous->second.received);sent+=static_cast<double>(value.sent-previous->second.sent);receiveCapacity+=value.receiveSpeed/8.;sendCapacity+=value.sendSpeed/8.;}
        if(valid){double seconds=(time-networkTime_)/1000.;download=received/seconds;upload=sent/seconds;network=static_cast<float>(std::clamp(std::max(receiveCapacity?*download/receiveCapacity:0,sendCapacity?*upload/sendCapacity:0)*100,0.,100.));}else{download.reset();upload.reset();network.reset();}
        networkBefore_=std::move(now);networkTime_=time;
    }
    ++samples;
}
}
