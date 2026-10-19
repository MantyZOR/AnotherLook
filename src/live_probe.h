#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>

namespace gv {
enum class ProbeArea {SurfaceMessages,MouseMove,HitTest,PaintBoard,PaintRail,PaintTool,OrbitRaster,OrbitVector,Network,RefreshWindows,SyncPanels,IconLookup,TraySync,EventMessages,Present,PointerNetwork,PointerHit,PointerHover,PointerCursor,TooltipPop,BoardCards,EndDraw,SystemSample,Count};
enum class ProbeCount {RasterFrames,RasterFallbacks,RasterBuilds,MouseLeaves,IconMisses,SurfaceMoves,SurfaceHitTests,SurfacePaints,SurfaceTimers,SurfacePositions,SurfaceNotifications,Count};
bool LiveProbeRunning();
uint64_t LiveProbeClock();
void LiveProbeRecord(ProbeArea area,uint64_t started);
void LiveProbeCount(ProbeCount counter);
void LiveProbeWindowEvent(DWORD event);
bool StartLiveProbe(HWND owner,const std::filesystem::path& directory,unsigned seconds=30);
void TickLiveProbe();
inline constexpr UINT_PTR LiveProbeTimer=94;
class LiveProbeScope {
    ProbeArea area_;uint64_t started_=0;
public:
    explicit LiveProbeScope(ProbeArea area):area_(area){if(LiveProbeRunning())started_=LiveProbeClock();}
    ~LiveProbeScope(){if(started_)LiveProbeRecord(area_,started_);}
};
}
