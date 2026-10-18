#include "ui.h"
#include <wincodec.h>
#include <algorithm>
namespace gv {
bool Surface::RenderCardPreview(HWND host,HDC target,RECT bounds,const Card& source){
    int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;if(width<=0||height<=0||!host)return false;
    UINT bitmapWidth=0,bitmapHeight=0;if(previewBitmap_)previewBitmap_->GetSize(&bitmapWidth,&bitmapHeight);
    UINT dpi=GetDpiForWindow(host);
    if(!rt_||!brush_||!previewBitmap_||bitmapWidth!=static_cast<UINT>(width)||bitmapHeight!=static_cast<UINT>(height)){
        Discard();previewBitmap_.Reset();ComPtr<IWICImagingFactory> factory;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&previewBitmap_)))return false;
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
        if(FAILED(app_.d2d->CreateWicBitmapRenderTarget(previewBitmap_.Get(),properties,&rt_))||FAILED(rt_->CreateSolidColorBrush(Ink(UiTheme().text),&brush_)))return false;
    }
    // Reuse the canvas renderer and a draft card. No HWND, browser or config is changed.
    HWND previous=hwnd;hwnd=host;cardPreview_=true;hits_.clear();cards_.clear();Card draft=source;
    float w=width*96.f/dpi,h=height*96.f/dpi,z=Zoom(),cardWidth=std::max(1.f,draft.box.w*z),cardHeight=std::max(1.f,draft.box.h*z);
    float fit=std::min({1.f,(w-24)/cardWidth,(h-24)/cardHeight});fit=std::max(.05f,fit);
    rt_->BeginDraw();rt_->SetTransform(D2D1::Matrix3x2F::Identity());rt_->Clear(Ink(UiTheme().canvas));
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(fit,fit)*D2D1::Matrix3x2F::Translation((w-cardWidth*fit)/2,(h-cardHeight*fit)/2));
    DrawCard(draft,{0,0,cardWidth,cardHeight});rt_->SetTransform(D2D1::Matrix3x2F::Identity());auto result=rt_->EndDraw();hwnd=previous;cardPreview_=false;
    if(FAILED(result)){Discard();previewBitmap_.Reset();return false;}
    previewPixels_.resize(static_cast<size_t>(width)*height);if(FAILED(previewBitmap_->CopyPixels(nullptr,width*4,static_cast<UINT>(previewPixels_.size()*4),reinterpret_cast<BYTE*>(previewPixels_.data()))))return false;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};
    return SetDIBitsToDevice(target,bounds.left,bounds.top,width,height,0,0,0,height,previewPixels_.data(),&info,DIB_RGB_COLORS)!=0;
}
}
