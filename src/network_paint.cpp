#include "live_probe.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <chrono>
namespace gv {
void Surface::DrawNetwork(float width,float height,Box dirty){LiveProbeScope profile(ProbeArea::Network);
    auto began=profilePaint_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    auto cost=[&](int part){if(profilePaint_){auto now=std::chrono::steady_clock::now();paintMs_[part]+=std::chrono::duration<double,std::milli>(now-began).count();began=now;}};
    auto& scene=app_.Active();const auto theme=UiTheme();
    if(dirty.w<=0||dirty.h<=0)dirty={0,0,width,height};
    auto visible=[&](float left,float top,float right,float bottom){return right>=dirty.x-1&&bottom>=dirty.y-1&&left<=dirty.x+dirty.w+1&&top<=dirty.y+dirty.h+1;};
    auto cacheSize=D2D1::SizeU(320,200);
    if(networkBackdrop_&&(networkBackdropBase_!=theme.canvas||networkBackdrop_->GetPixelSize().width!=cacheSize.width||networkBackdrop_->GetPixelSize().height!=cacheSize.height))networkBackdrop_.Reset();
    // A bounded 256 KiB texture provides the full-screen gradient. Moving scenes
    // let Direct2D accelerate its scaling instead of retaining screen-sized copies.
    if(!theme.light&&scene.network!=L"none"&&!networkBackdrop_){
        ComPtr<ID2D1BitmapRenderTarget> backdrop;
        auto size=D2D1::SizeF(320,200);
        if(SUCCEEDED(rt_->CreateCompatibleRenderTarget(size,cacheSize,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&backdrop))){
            backdrop->BeginDraw();backdrop->Clear(Ink(theme.canvas));
            D2D1_GRADIENT_STOP stops[]={{0,Color(L"527CB7",.23f)},{.22f,Color(L"527CB7",.129f)},{.6f,Color(L"527CB7",.03f)},{1,Color(L"527CB7",0)}};
            ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1RadialGradientBrush> ambient;
            if(SUCCEEDED(backdrop->CreateGradientStopCollection(stops,4,&collection))&&SUCCEEDED(backdrop->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(size.width*.53f,size.height*.48f),D2D1::Point2F(0,0),size.width*.72f,size.height*.94f),collection.Get(),&ambient)))backdrop->FillRectangle(D2D1::RectF(0,0,size.width,size.height),ambient.Get());
            if(SUCCEEDED(backdrop->EndDraw())){backdrop->GetBitmap(&networkBackdrop_);networkBackdropBase_=theme.canvas;}
        }
    }
    if(networkBackdrop_&&!theme.light&&scene.network!=L"none")rt_->DrawBitmap(networkBackdrop_.Get(),D2D1::RectF(0,0,width,height),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);else Fill({0,0,width,height},Ink(theme.canvas));
    cost(0);
    if(scene.network==L"none")return;
    auto radial=[&](ComPtr<ID2D1Bitmap>& bitmap,D2D1_COLOR_F color){
        if(bitmap)return;
        // Two small premultiplied sprites replace repeated software evaluation
        // of radial gradients. 32 KiB per surface at 96 DPI, capped at 128 KiB.
        UINT pixels=std::clamp(MulDiv(64,GetDpiForWindow(hwnd),96),64,128);
        ComPtr<ID2D1BitmapRenderTarget> target;
        if(FAILED(rt_->CreateCompatibleRenderTarget(D2D1::SizeF(64,64),D2D1::SizeU(pixels,pixels),D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&target)))return;
        D2D1_GRADIENT_STOP stops[]={{0,{color.r,color.g,color.b,.58f}},{.22f,{color.r,color.g,color.b,.58f*.56f}},{.6f,{color.r,color.g,color.b,.58f*.13f}},{1,{color.r,color.g,color.b,0}}};
        ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1RadialGradientBrush> brush;
        if(FAILED(target->CreateGradientStopCollection(stops,4,&collection))||FAILED(target->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(32,32),D2D1::Point2F(0,0),32,32),collection.Get(),&brush)))return;
        target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(32,32),32,32),brush.Get());if(SUCCEEDED(target->EndDraw()))target->GetBitmap(&bitmap);
    };
    // A cached gradient supplies depth without a wallpaper texture or animation.
    if(networkKey_.w!=width||networkKey_.h!=height||networkKey_.x!=panX_||networkKey_.y!=panY_||networkZoom_!=Zoom()||networkDensity_!=scene.networkDensity){
        network_.Build(width,height,panX_,panY_,Zoom(),scene.networkDensity);networkKey_={panX_,panY_,width,height};networkZoom_=Zoom();networkDensity_=scene.networkDensity;networkLastTime_=0;
    }
    float time=scene.networkMotion&&!scene.gaming?networkMotionTime_:0;
    if(time!=networkLastTime_){network_.Animate(time);networkLastTime_=time;}
    auto& nodes=network_.nodes;bool interactive=scene.network!=L"static"&&!scene.gaming&&!covered;
    float dpi=96.f/GetDpiForWindow(hwnd),px=networkPainted_.x*dpi,py=networkPainted_.y*dpi;
    int nearest=interactive?network_.Nearest(px,py):-1;bool links=scene.network==L"links";
    // One bounded pass replaces a neighbour scan for every node and repeated
    // distance calculations at both ends of each edge. No per-frame allocation.
    std::array<float,480> glowAt{};
    if(links&&nearest>=0){glowAt[nearest]=1;for(auto edge:network_.edges)if(edge.a==nearest||edge.b==nearest)glowAt[edge.a]=glowAt[edge.b]=1;}
    else if(!links&&interactive)for(size_t i=0;i<nodes.size();++i){float dx=nodes[i].x-px,dy=nodes[i].y-py;glowAt[i]=std::max(0.f,1.f-std::sqrt(dx*dx+dy*dy)/190.f);}
    const auto baseEdge=theme.light?Ink(theme.muted,.13f):Color(L"7396CB",.16f);
    const auto edgeBlue=Color(L"8799FF"),edgeViolet=Color(L"A894F5");
    const auto nodeLight=Color(L"526CA1"),nodeBlue=Color(L"729CDC"),nodeViolet=Color(L"8191CB");
    const auto activeBlue=Color(L"89B7FF"),activeViolet=Color(L"B8A0FF"),activeNearest=Color(L"E5C5FF");
    cost(1);
    for(const auto edge:network_.edges){auto a=nodes[edge.a],b=nodes[edge.b];float glow=0;
        if(!visible(std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)))continue;
        glow=links?((edge.a==nearest||edge.b==nearest)&&nearest>=0?1.f:0.f):std::min(glowAt[edge.a],glowAt[edge.b]);
        auto color=glow>0?((edge.a+edge.b)%2?edgeBlue:edgeViolet):baseEdge;if(glow>0)color.a=(theme.light?.38f:.65f)*glow;
        brush_->SetColor(color);rt_->DrawLine(D2D1::Point2F(a.x,a.y),D2D1::Point2F(b.x,b.y),brush_.Get(),glow>0?1.15f:.65f);
    }
    cost(2);
    for(int i=0;i<static_cast<int>(nodes.size());++i){auto node=nodes[i];if(node.x<-8||node.x>width+8||node.y<-8||node.y>height+8)continue;float glow=glowAt[i],extent=node.radius+(glow>0?25:0);
        if(!visible(node.x-extent,node.y-extent,node.x+extent,node.y+extent))continue;
        if(glow>0){bool violet=i==nearest||i%3==0;auto& halo=violet?networkHaloViolet_:networkHaloBlue_;radial(halo,Color(violet?L"B68AFF":L"649FFF"));
            if(halo){float radius=node.radius+25;rt_->DrawBitmap(halo.Get(),D2D1::RectF(node.x-radius,node.y-radius,node.x+radius,node.y+radius),glow*(theme.light?.55f:1),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}}
        auto color=glow>0?(i==nearest?activeNearest:i%3==0?activeViolet:activeBlue):theme.light?nodeLight:i%3==0?nodeViolet:nodeBlue;
        color.a=glow>0?.65f+glow*.35f:theme.light?(node.radius>5?.5f:.34f):(node.radius>5?.60f:.43f);
        brush_->SetColor(color);rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(node.x,node.y),node.radius,node.radius),brush_.Get());
        if(glow>0&&i==nearest){brush_->SetColor(Color(L"F1DAFF",.8f));rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(node.x,node.y),std::max(1.3f,node.radius*.42f),std::max(1.3f,node.radius*.42f)),brush_.Get());}
    }
    cost(3);
}
void Surface::SyncNetworkAnimation(bool active){
    if(bar||rail)return;const auto& scene=app_.Active();
    bool wanted=active&&scene.networkMotion&&scene.network!=L"none"&&!scene.gaming&&!covered&&IsWindowVisible(hwnd)&&!IsIconic(hwnd)&&!(app_.config.wallpaperEnabled&&!scene.wallpaper.empty());
    if(wanted==networkAnimating_)return;networkAnimating_=wanted;
    // Motion may use Direct2D acceleration; releasing the target on either edge
    // returns a static desktop to its configured renderer and releases GPU caches.
    Discard();if(wanted){networkFrameTick_=GetTickCount64();SetTimer(hwnd,82,networkInterval_,nullptr);}else KillTimer(hwnd,82);
}
void Surface::TickNetwork(){
    HWND foreground=GetForegroundWindow();bool canvas=false;
    for(auto* list:{&app_.boards,&app_.bars,&app_.rails,&app_.tools})for(auto& surface:*list)canvas|=foreground==surface->hwnd;
    SyncNetworkAnimation(canvas&&app_.config.enabled&&!app_.preview&&!app_.quitting&&!app_.launcher.Visible());
    if(!networkAnimating_)return;auto now=GetTickCount64();networkMotionTime_+=std::min(.15f,(now-networkFrameTick_)/1000.f)*app_.Active().networkSpeed;networkFrameTick_=now;
    networkPainted_=networkPointer_;InvalidateRect(hwnd,nullptr,FALSE);
}
void Surface::TrackNetwork(POINT point,bool leave){
    if(bar||rail)return;
    auto& scene=app_.Active();if(scene.network==L"static"||scene.network==L"none"||scene.gaming||covered||!IsWindowVisible(hwnd)){
        KillTimer(hwnd,81);networkPending_=false;networkPointer_=networkPainted_={-10000,-10000};return;
    }
    networkPointer_=leave?POINT{-10000,-10000}:point;
    if(networkPointer_.x==networkPainted_.x&&networkPointer_.y==networkPainted_.y)return;
    // The motion frame already samples the current pointer. Do not add a second
    // 30 Hz repaint source while the graph's own timer is active.
    if(networkAnimating_){KillTimer(hwnd,81);networkPending_=false;return;}
    if(scene.network==L"links"){
        float dpi=96.f/GetDpiForWindow(hwnd);
        if(network_.Nearest(networkPointer_.x*dpi,networkPointer_.y*dpi)==network_.Nearest(networkPainted_.x*dpi,networkPainted_.y*dpi)){
            KillTimer(hwnd,81);networkPending_=false;networkPainted_=networkPointer_;return;
        }
    }
    auto now=GetTickCount64();if(!leave&&now-networkTick_<34){if(!networkPending_){SetTimer(hwnd,81,static_cast<UINT>(34-(now-networkTick_)),nullptr);networkPending_=true;}return;}
    KillTimer(hwnd,81);networkPending_=false;networkTick_=now;float dpi=GetDpiForWindow(hwnd)/96.f;RECT dirty{LONG_MAX,LONG_MAX,LONG_MIN,LONG_MIN};
    auto include=[&](float x,float y,float radius){dirty.left=std::min(dirty.left,static_cast<LONG>(std::floor((x-radius)*dpi)));dirty.top=std::min(dirty.top,static_cast<LONG>(std::floor((y-radius)*dpi)));dirty.right=std::max(dirty.right,static_cast<LONG>(std::ceil((x+radius)*dpi)));dirty.bottom=std::max(dirty.bottom,static_cast<LONG>(std::ceil((y+radius)*dpi)));};
    for(POINT cursor:{networkPainted_,networkPointer_}){if(cursor.x<-1000)continue;float x=cursor.x/dpi,y=cursor.y/dpi;
        if(scene.network==L"near")include(x,y,248);else{int nearest=network_.Nearest(x,y);if(nearest>=0){auto n=network_.nodes[nearest];include(n.x,n.y,42);for(auto edge:network_.edges)if(edge.a==nearest||edge.b==nearest){n=network_.nodes[edge.a];include(n.x,n.y,42);n=network_.nodes[edge.b];include(n.x,n.y,42);}}}
    }
    networkPainted_=networkPointer_;RECT client;GetClientRect(hwnd,&client);if(IntersectRect(&dirty,&dirty,&client))InvalidateRect(hwnd,&dirty,FALSE);
}
}
