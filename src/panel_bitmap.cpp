#include "panel_bitmap.h"
#include <algorithm>
#include <cmath>

namespace gv {
void PanelBitmap::Reset(){
    if(dc_&&original_)SelectObject(dc_,original_);
    if(bitmap_)DeleteObject(bitmap_);
    if(dc_)DeleteDC(dc_);
    dc_=nullptr;bitmap_=nullptr;original_=nullptr;pixels_=nullptr;size_={};
}
bool PanelBitmap::Resize(int width,int height){
    if(dc_&&size_.cx==width&&size_.cy==height)return true;
    Reset();if(width<=0||height<=0)return false;
    dc_=CreateCompatibleDC(nullptr);if(!dc_)return false;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};
    bitmap_=CreateDIBSection(dc_,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels_),nullptr,0);
    if(!bitmap_){Reset();return false;}original_=SelectObject(dc_,bitmap_);size_={width,height};return true;
}
void PanelBitmap::RoundAlpha(float radius){
    if(!pixels_)return;GdiFlush();
    // GDI text and icons do not preserve the alpha byte. The interior is opaque;
    // only the four corner tiles need coverage and premultiplication.
    for(size_t i=0,n=static_cast<size_t>(size_.cx)*size_.cy;i<n;++i)pixels_[i]|=0xff000000;
    radius=std::clamp(radius,0.f,std::min(size_.cx,size_.cy)/2.f);
    int extent=std::min(static_cast<int>(std::ceil(radius)),static_cast<int>((std::min(size_.cx,size_.cy)+1)/2));
    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x){
        float distance=std::hypot(radius-(x+.5f),radius-(y+.5f));
        unsigned alpha=static_cast<unsigned>(std::lround(std::clamp(radius+.5f-distance,0.f,1.f)*255));
        auto apply=[&](int px,int py){auto& pixel=pixels_[static_cast<size_t>(py)*size_.cx+px];
            unsigned b=pixel&255,g=(pixel>>8)&255,r=(pixel>>16)&255;
            pixel=(alpha<<24)|((r*alpha+127)/255<<16)|((g*alpha+127)/255<<8)|(b*alpha+127)/255;
        };
        apply(x,y);if(x!=size_.cx-1-x)apply(size_.cx-1-x,y);
        if(y!=size_.cy-1-y){apply(x,size_.cy-1-y);if(x!=size_.cx-1-x)apply(size_.cx-1-x,size_.cy-1-y);}
    }
}
bool PanelBitmap::Present(HWND window)const{
    if(!dc_)return false;POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};auto size=size_;
    return UpdateLayeredWindow(window,nullptr,nullptr,&size,dc_,&source,0,&blend,ULW_ALPHA)!=FALSE;
}
}
