#pragma once
#include <windows.h>
#include <optional>
#include <cstdint>
#include <map>

namespace gv {
struct CpuTimes {uint64_t idle=0,kernel=0,user=0;};
std::optional<float> CpuUsage(CpuTimes before,CpuTimes after);
class SystemLoad {
public:
    SystemLoad()=default;
    SystemLoad(const SystemLoad&)=delete;
    SystemLoad& operator=(const SystemLoad&)=delete;
    ~SystemLoad();
    void Sample(unsigned metrics=3);
    void Reset();
    std::optional<float> cpu;
    std::optional<float> disk,network;
    std::optional<double> download,upload;
    uint64_t memoryUsed=0,memoryTotal=0;
    unsigned samples=0;
    unsigned Metrics()const{return metrics_;}
private:
    std::optional<CpuTimes> before_;
    struct NetworkCounter {uint64_t received=0,sent=0,receiveSpeed=0,sendSpeed=0;};
    std::map<uint64_t,NetworkCounter> networkBefore_;
    uint64_t networkTime_=0;
    void* diskQuery_{};void* diskCounter_{};bool diskAttempted_=false;
    unsigned metrics_=0;
};
}
