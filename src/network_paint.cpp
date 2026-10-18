#include "ui.h"
#include <algorithm>
#include <cmath>
#include <chrono>
namespace gv {
void Surface::DrawNetwork(float width,float height){
    auto began=profilePaint_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    auto cost=[&](int part){if(profilePaint_){auto now=std::chrono::steady_clock::now();paintMs_[part]+=std::chrono::duration<double,std::milli>(now-began).count();began=now;}};
    auto& scene=app_.Active();
    auto cacheSize=D2D1::SizeU(320,200);
    if(networkBackdrop_&&(networkBackdropBase_!=UiTheme().canvas||networkBackdrop_->GetPixelSize().width!=cacheSize.width||networkBackdrop_->GetPixelSize().height!=cacheSize.height))networkBackdrop_.Reset();
    // A bounded 256 KiB texture provides the full-screen gradient. Moving scenes
    // let Direct2D accelerate its scaling instead of retaining screen-sized copies.
    if(!UiTheme().light&&scene.network!=L"none"&&!networkBackdrop_){
        ComPtr<ID2D1BitmapRenderTarget> backdrop;
        auto size=D2D1::SizeF(320,200);
        if(SUCCEEDED(rt_->CreateCompatibleRenderTarget(size,cacheSize,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,&backdrop))){
            backdrop->BeginDraw();backdrop->Clear(Ink(UiTheme().canvas));
            D2D1_GRADIENT_STOP stops[]={{0,Color(L"527CB7",.23f)},{.22f,Color(L"527CB7",.129f)},{.6f,Color(L"527CB7",.03f)},{1,Color(L"527CB7",0)}};
            ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1RadialGradientBrush> ambient;
            if(SUCCEEDED(backdrop->CreateGradientStopCollection(stops,4,&collection))&&SUCCEEDED(backdrop->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(size.width*.53f,size.height*.48f),D2D1::Point2F(0,0),size.width*.72f,size.height*.94f),collection.Get(),&ambient)))backdrop->FillRectangle(D2D1::RectF(0,0,size.width,size.height),ambient.Get());
            if(SUCCEEDED(backdrop->EndDraw())){backdrop->GetBitmap(&networkBackdrop_);networkBackdropBase_=UiTheme().canvas;}
        }
    }
    if(networkBackdrop_&&!UiTheme().light&&scene.network!=L"none")rt_->DrawBitmap(networkBackdrop_.Get(),D2D1::RectF(0,0,width,height),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);else Fill({0,0,width,height},Ink(UiTheme().canvas));
    cost(0);
    if(scene.network==L"none")return;
    auto radial=[&](ComPtr<ID2D1RadialGradientBrush>& brush,D2D1_COLOR_F color,float opacity){
        if(brush)return;D2D1_GRADIENT_STOP stops[]={{0,{color.r,color.g,color.b,opacity}},{.22f,{color.r,color.g,color.b,opacity*.56f}},{.6f,{color.r,color.g,color.b,opacity*.13f}},{1,{color.r,color.g,color.b,0}}};
        ComPtr<ID2D1GradientStopCollection> collection;if(SUCCEEDED(rt_->CreateGradientStopCollection(stops,4,&collection)))rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(0,0),1,1),collection.Get(),&brush);
    };
    // A cached gradient supplies depth without a wallpaper texture or animation.
    if(networkKey_.w!=width||networkKey_.h!=height||networkKey_.x!=panX_||networkKey_.y!=panY_||networkZoom_!=Zoom()||networkDensity_!=scene.networkDensity){
        network_.Build(width,height,panX_,panY_,Zoom(),scene.networkDensity);networkKey_={panX_,panY_,width,height};networkZoom_=Zoom();networkDensity_=scene.networkDensity;
    }
    network_.Animate(scene.networkMotion&&!scene.gaming?networkMotionTime_:0);
    auto& nodes=network_.nodes;bool interactive=scene.network!=L"static"&&!scene.gaming&&!covered;
    float dpi=96.f/GetDpiForWindow(hwnd),px=networkPainted_.x*dpi,py=networkPainted_.y*dpi;
    int nearest=interactive?network_.Nearest(px,py):-1;bool links=scene.network==L"links";
    auto related=[&](int index){if(index==nearest)return true;return nearest>=0&&std::any_of(network_.edges.begin(),network_.edges.end(),[&](auto edge){return (edge.a==nearest&&edge.b==index)||(edge.b==nearest&&edge.a==index);});};
    auto strength=[&](const NetworkNode& node){float dx=node.x-px,dy=node.y-py;return interactive?std::max(0.f,1.f-std::sqrt(dx*dx+dy*dy)/190.f):0.f;};
    cost(1);
    for(const auto edge:network_.edges){auto a=nodes[edge.a],b=nodes[edge.b];float glow=links?((edge.a==nearest||edge.b==nearest)&&nearest>=0?1.f:0.f):std::min(strength(a),strength(b));
        auto color=glow>0?Color((edge.a+edge.b)%2?L"8799FF":L"A894F5",(UiTheme().light?.38f:.65f)*glow):UiTheme().light?Ink(UiTheme().muted,.13f):Color(L"7396CB",.16f);
        brush_->SetColor(color);rt_->DrawLine(D2D1::Point2F(a.x,a.y),D2D1::Point2F(b.x,b.y),brush_.Get(),glow>0?1.15f:.65f);
    }
    cost(2);
    for(int i=0;i<static_cast<int>(nodes.size());++i){auto node=nodes[i];if(node.x<-8||node.x>width+8||node.y<-8||node.y>height+8)continue;float glow=links?(nearest>=0&&related(i)?1.f:0.f):strength(node);
        if(glow>0){bool violet=i==nearest||i%3==0;auto& halo=violet?networkHaloViolet_:networkHaloBlue_;radial(halo,Color(violet?L"B68AFF":L"649FFF"),.58f);
            if(halo){float radius=node.radius+25;halo->SetCenter(D2D1::Point2F(node.x,node.y));halo->SetRadiusX(radius);halo->SetRadiusY(radius);halo->SetOpacity(glow*(UiTheme().light?.55f:1));rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(node.x,node.y),radius,radius),halo.Get());}}
        auto color=glow>0?Color(i==nearest?L"E5C5FF":i%3==0?L"B8A0FF":L"89B7FF",.65f+glow*.35f):UiTheme().light?Color(L"526CA1",node.radius>5?.5f:.34f):Color(i%3==0?L"8191CB":L"729CDC",node.radius>5?.60f:.43f);
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
