#include "ui.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>

namespace gv {
std::vector<const WindowInfo*> Surface::OrbitWindows()const{
    std::vector<const WindowInfo*> result;if(monitor<0||monitor>=static_cast<int>(app_.monitors.size()))return result;
    // Keep chronological order, including separate Explorer windows.
    auto bounds=app_.monitors[monitor].bounds;HMONITOR target=MonitorFromRect(&bounds,MONITOR_DEFAULTTONEAREST);
    result.reserve(app_.openWindows.size());for(const auto& window:app_.openWindows)if(IsWindow(window.hwnd)&&MonitorFromWindow(window.hwnd,MONITOR_DEFAULTTONEAREST)==target)result.push_back(&window);
    return result;
}
void Surface::ScrollOrbit(int delta){
    auto windows=OrbitWindows();BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    bool animate=animations&&!GetSystemMetrics(SM_REMOTESESSION);pressedHit_.reset();if(GetCapture()==hwnd)ReleaseCapture();
    if(tooltip_)SendMessageW(tooltip_,TTM_POP,0,0);
    orbit_.Scroll(-delta/static_cast<float>(WHEEL_DELTA),static_cast<int>(windows.size()),GetTickCount64(),animate);
    SyncOrbitTimer();InvalidateRect(hwnd,nullptr,FALSE);
}
void Surface::SyncOrbitTimer(){
    bool wanted=orbit_.Running()||orbitHover_.Running();if(wanted==orbitTimerActive_)return;
    orbitTimerActive_=wanted;if(wanted)SetTimer(hwnd,83,16,nullptr);else KillTimer(hwnd,83);
}
void Surface::TickOrbit(){
    if(!rail||!app_.config.ui.railOrbit||!IsWindowVisible(hwnd)){orbit_.Finish();orbitHover_.Reset();SyncOrbitTimer();return;}
    auto now=GetTickCount64();orbit_.Tick(now);orbitHover_.Tick(now);
    SyncOrbitTimer();InvalidateRect(hwnd,nullptr,FALSE);
}
void Surface::HoverOrbit(int hit){
    if(!rail||!app_.config.ui.railOrbit)return;
    int slot=hit>=0&&hit<static_cast<int>(hits_.size())?hits_[hit].orbitSlot:-1;
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    orbitHover_.Target(slot,GetTickCount64(),animations&&!GetSystemMetrics(SM_REMOTESESSION));
    SyncOrbitTimer();
    // Pointer events only update the target. The existing 16 ms timer owns
    // animated frames, even when mouse polling is much faster than the display.
    if(!orbitTimerActive_)InvalidateRect(hwnd,nullptr,FALSE);
}
bool Surface::ContainsHit(const Hit& hit,float x,float y)const{
    if(hit.orbitSlot<0)return x>=hit.box.x&&y>=hit.box.y&&x<hit.box.x+hit.box.w&&y<hit.box.y+hit.box.h;
    float scale=std::min(orbitFrame_.w,orbitFrame_.h)/280.f;if(scale<=0)return false;
    x=(x-orbitFrame_.x-orbitFrame_.w/2)/scale;y=(y-orbitFrame_.y-orbitFrame_.h/2)/scale;
    // The resting silhouette remains a stable target while the segment lifts.
    return ContainsOrbitSegment(hit.orbitSlot,orbitVisible_,x,y)||ContainsOrbitSegment(hit.orbitSlot,orbitVisible_,x,y,orbitHover_.Value(hit.orbitSlot));
}
void Surface::DrawOrbit(float width,float height){
    float scale=std::min(width,height)/280.f,cx=width/2,cy=height/2;
    D2D1_MATRIX_3X2_F previous;rt_->GetTransform(&previous);
    orbitFrame_={previous._31,previous._32,width*previous._11,height*previous._22};
    auto windows=OrbitWindows();int count=static_cast<int>(windows.size());orbit_.Limit(count);auto layout=LayoutOrbit(count,orbit_.Position());
    // Icon extraction may call another process. Snapshot only the fields needed
    // for the visible slots, so reentrant window events cannot invalidate refs.
    // Process paths, class names and app IDs are not copied on animation frames.
    std::array<WindowInfo,OrbitCapacity*2> visible;
    auto copy=[](WindowInfo& dest,const WindowInfo& source){dest.hwnd=source.hwnd;dest.pid=source.pid;dest.title=source.title;};
    for(int i=0;i<layout.visible;++i){copy(visible[i*2],*windows[layout.slots[i].before]);copy(visible[i*2+1],*windows[layout.slots[i].after]);}
    orbitVisible_=layout.visible;if(rail&&hover_>=layout.visible){hover_=-1;orbitHover_.Reset();}
    SyncOrbitTimer();
    int segments=std::max(1,layout.visible);
    // One widened band is reused for every slot and across paints. The stroke
    // outline is computed once, instead of re-stroking nine arcs every frame.
    // Splitting it in half also covers a single window's almost-complete circle.
    if(!orbitArc_||orbitArcCount_!=segments){
        orbitArc_.Reset();orbitArcCount_=0;ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
        if(SUCCEEDED(app_.d2d->CreatePathGeometry(&path))&&SUCCEEDED(path->Open(&sink))){
            auto arc=OrbitSegment(0,segments);auto point=[](float angle){return D2D1::Point2F(OrbitRadius*std::cos(angle),OrbitRadius*std::sin(angle));};
            sink->BeginFigure(point(arc.start),D2D1_FIGURE_BEGIN_HOLLOW);
            for(int half=1;half<=2;++half)sink->AddArc(D2D1::ArcSegment(point(arc.start+arc.sweep*half/2),D2D1::SizeF(OrbitRadius,OrbitRadius),0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(SUCCEEDED(sink->Close())){
                ComPtr<ID2D1PathGeometry> band;ComPtr<ID2D1GeometrySink> outline;
                if(SUCCEEDED(app_.d2d->CreatePathGeometry(&band))&&SUCCEEDED(band->Open(&outline))){
                    outline->SetFillMode(D2D1_FILL_MODE_WINDING);
                    if(SUCCEEDED(path->Widen(OrbitBandWidth,nullptr,D2D1::Matrix3x2F::Identity(),.1f,outline.Get()))&&SUCCEEDED(outline->Close())){orbitArc_=std::move(band);orbitArcCount_=segments;}
                }
            }
        }
    }
    if(orbitArc_&&orbitRasterPart_!=2){
        auto panel=Ink(UiTheme().panel),accent=Ink(UiTheme().accent);
        for(int i=0;i<segments;++i){
            float amount=orbitHover_.Value(i);auto lift=LiftOrbitSegment(i,segments,amount);
            if(amount>0){
                // A barely visible resting footprint prevents edge-hover flicker
                // with Windows' per-pixel hit testing of layered windows.
                rt_->SetTransform(D2D1::Matrix3x2F::Rotation(-i*360.f/segments)*D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(cx,cy)*previous);
                auto footprint=panel;footprint.a=2/255.f;brush_->SetColor(footprint);rt_->FillGeometry(orbitArc_.Get(),brush_.Get());
            }
            auto color=panel;float tint=.065f*amount;color.r+=(accent.r-color.r)*tint;color.g+=(accent.g-color.g)*tint;color.b+=(accent.b-color.b)*tint;brush_->SetColor(color);
            rt_->SetTransform(D2D1::Matrix3x2F::Rotation(-i*360.f/segments)*D2D1::Matrix3x2F::Scale(scale*lift.scale,scale*lift.scale)*D2D1::Matrix3x2F::Translation(cx+lift.x*scale,cy+lift.y*scale)*previous);rt_->FillGeometry(orbitArc_.Get(),brush_.Get());
        }
        rt_->SetTransform(previous);
    }
    if(orbitRasterPart_==1)return;
    brush_->SetColor(Ink(UiTheme().border));for(int i=0;i<segments;++i){float angle=-OrbitTau/4-i*OrbitTau/segments,x=std::cos(angle),y=std::sin(angle);rt_->DrawLine(D2D1::Point2F(cx+x*100*scale,cy+y*100*scale),D2D1::Point2F(cx+x*120*scale,cy+y*120*scale),brush_.Get(),scale);}
    if(!count){GlyphIcon(Glyph::Windows,{cx-10*scale,cy+100*scale,20*scale,20*scale},Ink(UiTheme().muted));return;}
    int pages=(count+OrbitCapacity-1)/OrbitCapacity,page=1+static_cast<int>(std::ceil(orbit_.Position()/OrbitCapacity));
    auto number=std::to_wstring(page);
    // Centre the visible glyph, rather than the font's asymmetric line box.
    CachedText(number,{cx-40*scale,cy-40*scale,80*scale,80*scale},30*scale,Ink(UiTheme().text),500,false,FontRole::Interface,true,true);
    CenterText(number+L" из "+std::to_wstring(pages),{cx-70*scale,cy+23*scale,140*scale,22*scale},14*scale,Ink(UiTheme().muted));
    if(orbitRasterPart_==2)return;
    HWND foreground=GetForegroundWindow();
    for(int i=0;i<layout.visible;++i){
        auto cell=layout.slots[i];auto lift=LiftOrbitSegment(i,layout.visible,orbitHover_.Value(i));float iconScale=scale*lift.scale,x=cx+lift.x*scale+(cell.x-140)*iconScale,y=cy+lift.y*scale+(cell.y-140)*iconScale;
        const auto& before=visible[i*2];const auto& after=visible[i*2+1];const auto& selected=cell.blend>=.5f?after:before;HWND handle=selected.hwnd;DWORD pid=selected.pid;
        Box hit{x-18*iconScale,y-18*iconScale,36*iconScale,36*iconScale};
        bool active=selected.hwnd==foreground;
        if(active){brush_->SetColor(Ink(UiTheme().accent,.16f));rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),17*iconScale,17*iconScale),brush_.Get());}
        hits_.push_back({hit,[this,handle,pid]{DWORD owner=0;GetWindowThreadProcessId(handle,&owner);if(!IsWindow(handle)||owner!=pid)return;if(handle==GetForegroundWindow())ShowWindowAsync(handle,SW_MINIMIZE);else app_.ActivateWindow(handle);},false});
        hits_.back().context=[this,handle,pid]{for(const auto& window:app_.openWindows)if(window.hwnd==handle&&window.pid==pid){OpenWindowMenu({window});break;}};hits_.back().tooltip=selected.title;hits_.back().orbitSlot=i;
        Box icon{x-13*iconScale,y-13*iconScale,26*iconScale,26*iconScale};
        if(cell.before==cell.after)WindowIcon(before,icon);else{WindowIcon(before,icon,1-cell.blend);WindowIcon(after,icon,cell.blend);}
    }
}
}
