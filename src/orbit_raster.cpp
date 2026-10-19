#include "ui.h"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <emmintrin.h>

namespace gv {
namespace {
uint32_t Over(uint32_t source,uint32_t destination){
    unsigned a=source>>24;if(!a)return destination;if(a==255)return source;unsigned inverse=255-a;
    auto channel=[&](int shift){return ((source>>shift)&255)+((((destination>>shift)&255)*inverse+127)/255);};
    return channel(0)|(channel(8)<<8)|(channel(16)<<16)|(channel(24)<<24);
}
// Bounded pixel-centred resampling; transparent icon pixels stay transparent.
void BlitCoverage(const std::vector<uint8_t>& alpha,int sourceWidth,int sourceHeight,float left,float top,float scale,int width,int height,uint32_t* pixels,const std::array<uint32_t,256>& colors){
    int x0=std::max(0,static_cast<int>(std::floor(left))),y0=std::max(0,static_cast<int>(std::floor(top)));
    int x1=std::min(width,static_cast<int>(std::ceil(left+sourceWidth*scale))),y1=std::min(height,static_cast<int>(std::ceil(top+sourceHeight*scale)));
    float inverse=1/scale;int start=static_cast<int>(std::floor(((x0+.5f-left)*inverse-.5f)*65536));int step=static_cast<int>(inverse*65536+.5f);
    for(int y=y0;y<y1;++y){
        float sy=(y+.5f-top)*inverse-.5f;int iy=static_cast<int>(std::floor(sy));unsigned wy=static_cast<unsigned>((sy-iy)*256+.5f);
        auto row0=alpha.data()+static_cast<size_t>(std::clamp(iy,0,sourceHeight-1))*sourceWidth;
        auto row1=alpha.data()+static_cast<size_t>(std::clamp(iy+1,0,sourceHeight-1))*sourceWidth;
        auto dest=pixels+static_cast<size_t>(y)*width;int fixed=start;
        for(int x=x0;x<x1;++x,fixed+=step){
            int ix=fixed>>16;unsigned wx=(static_cast<unsigned>(fixed)&65535)>>8;int sx=std::clamp(ix,0,sourceWidth-1),nx=std::clamp(ix+1,0,sourceWidth-1);
            unsigned a=row0[sx],b=row0[nx],c=row1[sx],d=row1[nx];
            if((a&b&c&d)==255){dest[x]=colors[255];continue;}
            if(!(a|b|c|d))continue;
            unsigned value=(((a*(256-wx)+b*wx)*(256-wy)+(c*(256-wx)+d*wx)*wy)+32768)>>16;
            dest[x]=Over(colors[value],dest[x]);
        }
    }
}
void BlitIcon(const MonoIcon& icon,float left,float top,float extent,float opacity,int width,int height,uint32_t* pixels){
    const int size=icon.size;float inverse=size/extent;
    int x0=std::max(0,static_cast<int>(std::floor(left))),y0=std::max(0,static_cast<int>(std::floor(top)));
    int x1=std::min(width,static_cast<int>(std::ceil(left+extent))),y1=std::min(height,static_cast<int>(std::ceil(top+extent)));
    int start=static_cast<int>(std::floor(((x0+.5f-left)*inverse-.5f)*65536)),step=static_cast<int>(inverse*65536+.5f);
    const auto zero=_mm_setzero_si128(),round=_mm_set1_epi16(128),amount=_mm_set1_epi16(static_cast<short>(opacity*256+.5f));
    auto unpack=[&](uint32_t value){return _mm_unpacklo_epi8(_mm_cvtsi32_si128(static_cast<int>(value)),zero);};
    auto mix=[&](__m128i a,__m128i b,int weight){return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(a,_mm_set1_epi16(static_cast<short>(256-weight))),_mm_mullo_epi16(b,_mm_set1_epi16(static_cast<short>(weight)))),round),8);};
    for(int y=y0;y<y1;++y){float sy=(y+.5f-top)*inverse-.5f;int iy=static_cast<int>(std::floor(sy)),wy=static_cast<int>((sy-iy)*256+.5f);int fixed=start;
        auto sample=[&](int x,int row){return x>=0&&row>=0&&x<size&&row<size?icon.pixels[static_cast<size_t>(row)*size+x]:0u;};
        for(int x=x0;x<x1;++x,fixed+=step){int ix=fixed>>16,wx=(static_cast<unsigned>(fixed)&65535)>>8;
            auto upper=mix(unpack(sample(ix,iy)),unpack(sample(ix+1,iy)),wx),lower=mix(unpack(sample(ix,iy+1)),unpack(sample(ix+1,iy+1)),wx);
            auto value=mix(upper,lower,wy);if(opacity<1)value=_mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(value,amount),round),8);
            uint32_t source=static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_packus_epi16(value,zero)));auto& dest=pixels[static_cast<size_t>(y)*width+x];dest=Over(source,dest);
        }
    }
}
}
bool Surface::PaintOrbitRaster(int width,int height,UINT dpi){
    auto stamp=profilePaint_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    auto cost=[&](int part){if(profilePaint_){auto now=std::chrono::steady_clock::now();paintMs_[part]+=std::chrono::duration<double,std::milli>(now-stamp).count();stamp=now;}};
    // Bounded, per-ring storage, with vector rendering as the fallback for very
    // large/custom DPI settings. Never allocate a full-monitor animation cache.
    if(width<=0||height<=0||static_cast<size_t>(width)*height>1024*1024)return false;
    auto windows=OrbitWindows();int count=static_cast<int>(windows.size());orbit_.Limit(count);auto layout=LayoutOrbit(count,orbit_.Position());
    std::array<WindowInfo,OrbitCapacity*2> visible;
    for(int i=0;i<layout.visible;++i)for(int j=0;j<2;++j){auto& dest=visible[i*2+j];const auto& source=*windows[j?layout.slots[i].after:layout.slots[i].before];dest.hwnd=source.hwnd;dest.pid=source.pid;dest.title=source.title;}
    auto theme=UiTheme();int page=1+static_cast<int>(std::ceil(orbit_.Position()/OrbitCapacity)),pages=(count+OrbitCapacity-1)/OrbitCapacity;
    OrbitRasterKey key{width,height,dpi,app_.config.ui.textScale,layout.visible,page,pages,theme.panel,theme.border,theme.text,theme.muted,theme.accent};
    auto pixels=panelBitmap_.Pixels();size_t area=static_cast<size_t>(width)*height;float pixelScale=std::min(width,height)/280.f;
    if(!orbitRasterKey_||*orbitRasterKey_!=key){
        orbitRasterKey_.reset();orbitCoverage_={};orbitBase_.clear();
        auto hover=orbitHover_;int pointer=hover_;orbitHover_.Reset();float uiScale=app_.config.ui.textScale;
        auto render=[&](int part){orbitRasterPart_=part;scaledUi_=true;rt_->BeginDraw();rt_->Clear(D2D1::ColorF(0,0.f));rt_->SetTransform(D2D1::Matrix3x2F::Scale(uiScale,uiScale));DrawOrbit(width*96.f/dpi/uiScale,height*96.f/dpi/uiScale);rt_->SetTransform(D2D1::Matrix3x2F::Identity());auto result=rt_->EndDraw();scaledUi_=false;orbitRasterPart_=0;return SUCCEEDED(result);};
        bool ready=render(1);
        if(ready){
            GdiFlush();int segments=std::max(1,layout.visible);size_t cacheBytes=area*4;
            for(int i=0;i<segments;++i){
                D2D1_RECT_F bounds{};auto transform=D2D1::Matrix3x2F::Rotation(-i*360.f/segments)*D2D1::Matrix3x2F::Scale(pixelScale,pixelScale)*D2D1::Matrix3x2F::Translation(width/2.f,height/2.f);
                if(!orbitArc_||FAILED(orbitArc_->GetBounds(transform,&bounds))){ready=false;break;}
                auto& coverage=orbitCoverage_[i];coverage.x=std::max(0,static_cast<int>(std::floor(bounds.left))-1);coverage.y=std::max(0,static_cast<int>(std::floor(bounds.top))-1);
                coverage.w=std::min(width,static_cast<int>(std::ceil(bounds.right))+1)-coverage.x;coverage.h=std::min(height,static_cast<int>(std::ceil(bounds.bottom))+1)-coverage.y;
                cacheBytes+=static_cast<size_t>(coverage.w)*coverage.h;if(cacheBytes>4*1024*1024){ready=false;break;}
                coverage.alpha.resize(static_cast<size_t>(coverage.w)*coverage.h);
            }
            // All segments were rasterized in one pass. Assign coverage by its
            // angular slot, keeping the original antialiasing at both arc edges.
            for(int y=0;ready&&y<height;++y)for(int x=0;x<width;++x)if(unsigned alpha=pixels[static_cast<size_t>(y)*width+x]>>24){
                float angle=std::fmod(-OrbitTau/4-std::atan2(y+.5f-height/2.f,x+.5f-width/2.f)+2*OrbitTau,OrbitTau);
                int slot=std::min(segments-1,static_cast<int>(angle*segments/OrbitTau));auto& coverage=orbitCoverage_[slot];
                if(x>=coverage.x&&y>=coverage.y&&x<coverage.x+coverage.w&&y<coverage.y+coverage.h)coverage.alpha[static_cast<size_t>(y-coverage.y)*coverage.w+x-coverage.x]=static_cast<uint8_t>(alpha);
            }
        }
        if(ready)ready=render(2);orbitHover_=hover;hover_=pointer;SyncOrbitTimer();
        if(!ready){orbitCoverage_={};std::vector<uint32_t>{}.swap(orbitBase_);return false;}
        GdiFlush();orbitBase_.assign(pixels,pixels+area);orbitRasterKey_=key;
        if(tooltip_){TOOLINFOW info{sizeof(info)};info.hwnd=hwnd;info.uId=1;info.rect={0,0,width,height};SendMessageW(tooltip_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&info));}
    }
    GdiFlush();std::copy(orbitBase_.begin(),orbitBase_.end(),pixels);hits_.clear();cards_.clear();
    orbitFrame_={0,0,width*96.f/dpi,height*96.f/dpi};orbitVisible_=layout.visible;
    if(hover_>=layout.visible){hover_=-1;orbitHover_.Reset();}SyncOrbitTimer();
    int segments=std::max(1,layout.visible);
    cost(0);
    auto color=[&](float amount){float tint=.065f*amount;auto c=[&](unsigned a,unsigned b){return static_cast<unsigned>(std::lround(a+(static_cast<float>(b)-a)*tint));};return c(GetBValue(theme.panel),GetBValue(theme.accent))|(c(GetGValue(theme.panel),GetGValue(theme.accent))<<8)|(c(GetRValue(theme.panel),GetRValue(theme.accent))<<16);};
    auto solid=[](uint32_t rgb,unsigned alpha){return (alpha<<24)|((((rgb>>16)&255)*alpha+127)/255<<16)|((((rgb>>8)&255)*alpha+127)/255<<8)|((rgb&255)*alpha+127)/255;};
    const auto restColor=color(0);const uint32_t footprintColors[]{0,solid(restColor,1),solid(restColor,2)};
    for(int i=0;i<segments;++i){
        auto& coverage=orbitCoverage_[i];float amount=orbitHover_.Value(i);auto lift=LiftOrbitSegment(i,segments,amount);auto rgb=color(amount);std::array<uint32_t,256> colors;for(unsigned a=0;a<256;++a)colors[a]=solid(rgb,a);
        if(amount==0){for(int y=0;y<coverage.h;++y)for(int x=0;x<coverage.w;++x){unsigned a=coverage.alpha[static_cast<size_t>(y)*coverage.w+x];if(a){auto& dest=pixels[static_cast<size_t>(y+coverage.y)*width+x+coverage.x];dest=Over(colors[a],dest);}}continue;}
        if(amount>0)for(int y=0;y<coverage.h;++y)for(int x=0;x<coverage.w;++x){unsigned a=(coverage.alpha[static_cast<size_t>(y)*coverage.w+x]*2+127)/255;if(a){auto& dest=pixels[static_cast<size_t>(y+coverage.y)*width+x+coverage.x];dest=Over(footprintColors[a],dest);}}
        float left=width/2.f+(coverage.x-width/2.f)*lift.scale+lift.x*pixelScale,top=height/2.f+(coverage.y-height/2.f)*lift.scale+lift.y*pixelScale;
        BlitCoverage(coverage.alpha,coverage.w,coverage.h,left,top,lift.scale,width,height,pixels,colors);
    }
    cost(1);HWND foreground=GetForegroundWindow();bool iconsReady=true;
    for(int i=0;i<layout.visible;++i){
        auto cell=layout.slots[i];auto lift=LiftOrbitSegment(i,layout.visible,orbitHover_.Value(i));float size=pixelScale*lift.scale;
        float x=width/2.f+lift.x*pixelScale+(cell.x-140)*size,y=height/2.f+lift.y*pixelScale+(cell.y-140)*size;
        const auto& before=visible[i*2];const auto& after=visible[i*2+1];const auto& selected=cell.blend>=.5f?after:before;HWND handle=selected.hwnd;DWORD pid=selected.pid;
        if(handle==foreground){float radius=17*size;uint32_t rgb=GetBValue(theme.accent)|(GetGValue(theme.accent)<<8)|(GetRValue(theme.accent)<<16);for(int py=std::max(0,static_cast<int>(y-radius-1));py<std::min(height,static_cast<int>(y+radius+1));++py)for(int px=std::max(0,static_cast<int>(x-radius-1));px<std::min(width,static_cast<int>(x+radius+1));++px){unsigned a=static_cast<unsigned>(std::lround(std::clamp(radius+.5f-std::hypot(px+.5f-x,py+.5f-y),0.f,1.f)*255*.16f));auto& dest=pixels[static_cast<size_t>(py)*width+px];dest=Over(solid(rgb,a),dest);}}
        float dip=96.f/dpi;hits_.push_back({{(x-18*size)*dip,(y-18*size)*dip,36*size*dip,36*size*dip},[this,handle,pid]{DWORD owner=0;GetWindowThreadProcessId(handle,&owner);if(!IsWindow(handle)||owner!=pid)return;if(handle==GetForegroundWindow())ShowWindowAsync(handle,SW_MINIMIZE);else app_.ActivateWindow(handle);},false});
        hits_.back().context=[this,handle,pid]{for(const auto& window:app_.openWindows)if(window.hwnd==handle&&window.pid==pid){OpenWindowMenu({window});break;}};hits_.back().tooltip=selected.title;hits_.back().orbitSlot=i;
        auto icon=[&](HWND window,float opacity){
            if(opacity<=0)return;auto source=app_.icons.GetWindow(window);if(!source||source->size<=0||source->pixels.empty()){iconsReady=false;return;}float extent=std::min(26*size,static_cast<float>(source->size));
            BlitIcon(*source,x-extent/2,y-extent/2,extent,opacity,width,height,pixels);
        };
        if(cell.before==cell.after)icon(before.hwnd,1);else{icon(before.hwnd,1-cell.blend);icon(after.hwnd,cell.blend);}
    }
    cost(2);bool presented=iconsReady&&panelBitmap_.Present(hwnd);cost(3);return presented;
}
}
