#include "motion_scene.h"
#include <dcomp.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>

namespace gv {
namespace {
using Microsoft::WRL::ComPtr;
unsigned activeScenes=0;
struct Engine {
    ComPtr<ID2D1Factory> drawing;
    ComPtr<IWICImagingFactory> imaging;
    ComPtr<IDCompositionDesktopDevice> device;
    bool Open(){
        // HWND wrappers let DWM own the GPU surfaces. No private D3D driver
        // device, swap chain or per-process GPU command context is required.
        return SUCCEEDED(DCompositionCreateDevice2(nullptr,IID_PPV_ARGS(&device)))&&
            SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,drawing.GetAddressOf()))&&
            SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging)));
    }
    void Trim(){device->Commit();}
};
// This thread-local device is shared by short scenes. No textures or windows
// are retained in it, and it has no frame loop or idle timer.
thread_local std::shared_ptr<Engine> cachedEngine;
LRESULT CALLBACK SourceProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_NCHITTEST)return HTTRANSPARENT;if(m==WM_ERASEBKGND)return 1;return DefWindowProcW(h,m,w,l);}
struct Source {
    HWND window{};ComPtr<IUnknown> wrapper;SIZE size{};
    ~Source(){if(window&&IsWindow(window))DestroyWindow(window);}
};
}
struct MotionScene::Impl {
    struct Node {
        ComPtr<IDCompositionVisual2> visual;
        ComPtr<IDCompositionEffectGroup> effect;
        ComPtr<IDCompositionScaleTransform> scale;
        ComPtr<IDCompositionRectangleClip> clip;
        std::shared_ptr<Source> source;
        SIZE size{};
    };
    std::shared_ptr<Engine> engine;
    ComPtr<IDCompositionDesktopDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual2> root;
    std::vector<Node> nodes;
    size_t bytes=0,budget=0,commits=0;
    HWND window{};
    bool counted=false,failed=false;
    ~Impl(){nodes.clear();root.Reset();target.Reset();if(counted&&!--activeScenes)engine->Trim();}
    bool Check(HRESULT hr){if(FAILED(hr))failed=true;return SUCCEEDED(hr);}
    MotionScene::Node AddNode(std::shared_ptr<Source> source,POINT position,MotionScene::Node parent,SIZE groupSize={}){
        Node node;node.source=std::move(source);node.size=node.source?node.source->size:groupSize;auto size=node.size;
        if(!Check(device->CreateVisual(&node.visual))||!Check(device->CreateEffectGroup(&node.effect))||!Check(device->CreateScaleTransform(&node.scale))||!Check(device->CreateRectangleClip(&node.clip)))return MotionScene::None;
        if(!Check(node.clip->SetRight(static_cast<float>(size.cx)))||!Check(node.clip->SetBottom(static_cast<float>(size.cy)))||
           (node.source&&!Check(node.visual->SetContent(node.source->wrapper.Get())))||!Check(node.visual->SetEffect(node.effect.Get()))||!Check(node.visual->SetClip(node.clip.Get()))||!Check(node.visual->SetTransform(node.scale.Get()))||
           !Check(node.visual->SetOffsetX(static_cast<float>(position.x)))||!Check(node.visual->SetOffsetY(static_cast<float>(position.y))))return MotionScene::None;
        auto* ancestor=parent==MotionScene::None?root.Get():nodes.at(parent).visual.Get();
        // Calls arrive back-to-front. With a null reference, FALSE appends on
        // top; TRUE would put every new visual UNDER all existing siblings.
        if(!Check(ancestor->AddVisual(node.visual.Get(),FALSE,nullptr)))return MotionScene::None;
        nodes.push_back(std::move(node));return nodes.size()-1;
    }
};
MotionScene::MotionScene()=default;
MotionScene::~MotionScene()=default;
double MotionNow(){static LARGE_INTEGER frequency=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f;}();LARGE_INTEGER tick;QueryPerformanceCounter(&tick);return static_cast<double>(tick.QuadPart)/frequency.QuadPart;}
bool WarmMotionDevice(){
    if(!cachedEngine){auto engine=std::make_shared<Engine>();if(!engine->Open())return false;cachedEngine=std::move(engine);}
    return true;
}
bool MotionScene::Open(HWND window,size_t budget){
    impl_=std::make_unique<Impl>();auto& s=*impl_;s.budget=budget;s.window=window;
    if(!WarmMotionDevice())return false;
    s.engine=cachedEngine;s.device=s.engine->device;
    if(!s.Check(s.device->CreateTargetForHwnd(window,TRUE,&s.target))||!s.Check(s.device->CreateVisual(&s.root))||!s.Check(s.target->SetRoot(s.root.Get())))return false;
    s.counted=true;++activeScenes;return true;
}
MotionScene::Node MotionScene::Draw(SIZE size,const std::function<void(ID2D1RenderTarget*)>& draw,POINT position,Node parent){
    if(!impl_||impl_->failed||size.cx<=0||size.cy<=0)return None;auto& s=*impl_;
    size_t bytes=static_cast<size_t>(size.cx)*size.cy*4;if(bytes>s.budget-s.bytes){s.failed=true;return None;}
    PanelBitmap bitmap;if(!bitmap.Resize(size.cx,size.cy))return None;
    ComPtr<IWICBitmap> pixels;ComPtr<ID2D1RenderTarget> target;
    auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    if(!s.Check(s.engine->imaging->CreateBitmap(size.cx,size.cy,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&pixels))||
       !s.Check(s.engine->drawing->CreateWicBitmapRenderTarget(pixels.Get(),properties,&target)))return None;
    target->SetDpi(96,96);target->BeginDraw();target->Clear(D2D1::ColorF(0,0.f));draw(target.Get());
    if(!s.Check(target->EndDraw())||!s.Check(pixels->CopyPixels(nullptr,size.cx*4,static_cast<UINT>(bytes),reinterpret_cast<BYTE*>(bitmap.Pixels()))))return None;
    return Add(bitmap,position,parent);
}
MotionScene::Node MotionScene::Add(const PanelBitmap& bitmap,POINT position,Node parent){
    if(!impl_||impl_->failed)return None;auto& s=*impl_;auto size=bitmap.Size();size_t bytes=bitmap.Bytes();if(!bytes||bytes>s.budget-s.bytes){s.failed=true;return None;}
    auto source=std::make_shared<Source>();source->size=size;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=SourceProc;cls.lpszClassName=L"GoodView.MotionSource";RegisterClassW(&cls);RECT bounds{};GetWindowRect(s.window,&bounds);
    source->window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_LAYERED,cls.lpszClassName,L"",WS_POPUP,bounds.left,bounds.top,size.cx,size.cy,s.window,nullptr,cls.hInstance,nullptr);
    BOOL cloaked=TRUE;
    if(!source->window||!s.Check(DwmSetWindowAttribute(source->window,DWMWA_CLOAK,&cloaked,sizeof(cloaked)))||!bitmap.Present(source->window))return None;
    ShowWindow(source->window,SW_SHOWNOACTIVATE);
    if(!s.Check(s.device->CreateSurfaceFromHwnd(source->window,&source->wrapper)))return None;
    s.bytes+=bytes;return s.AddNode(std::move(source),position,parent);
}
MotionScene::Node MotionScene::Group(SIZE size,Node parent){if(!impl_||impl_->failed||size.cx<=0||size.cy<=0)return None;return impl_->AddNode(nullptr,{},parent,size);}
MotionScene::Node MotionScene::Clone(Node source,Node parent){if(!impl_||source>=impl_->nodes.size()||impl_->failed||!impl_->nodes[source].source)return None;auto copy=impl_->nodes[source].source;return impl_->AddNode(std::move(copy),{},parent);}
bool MotionScene::Set(Node id,Property property,float value){
    if(!impl_||impl_->failed||id>=impl_->nodes.size())return false;auto& n=impl_->nodes[id];HRESULT hr=E_FAIL;
    switch(property){case Property::X:hr=n.visual->SetOffsetX(value);break;case Property::Y:hr=n.visual->SetOffsetY(value);break;
    case Property::ScaleX:hr=n.scale->SetScaleX(value);break;case Property::ScaleY:hr=n.scale->SetScaleY(value);break;
    case Property::Opacity:hr=n.effect->SetOpacity(value);break;case Property::Left:hr=n.clip->SetLeft(value);break;case Property::Top:hr=n.clip->SetTop(value);break;case Property::Right:hr=n.clip->SetRight(value);break;case Property::Bottom:hr=n.clip->SetBottom(value);break;}
    return impl_->Check(hr);
}
bool MotionScene::Animate(Node id,Property property,float from,float to,double delay,double seconds){
    if(!impl_||impl_->failed||id>=impl_->nodes.size()||seconds<=0)return false;auto& s=*impl_;auto& n=s.nodes[id];
    ComPtr<IDCompositionAnimation> curve;if(!s.Check(s.device->CreateAnimation(&curve)))return false;
    float delta=to-from,t=static_cast<float>(seconds);
    // A continuous cubic ease-out, expressed in seconds for the compositor.
    if(delay>0&&!s.Check(curve->AddCubic(0,from,0,0,0)))return false;
    if(!s.Check(curve->AddCubic(delay,from,3*delta/t,-3*delta/(t*t),delta/(t*t*t)))||!s.Check(curve->End(delay+seconds,to)))return false;
    HRESULT hr=E_FAIL;switch(property){case Property::X:hr=n.visual->SetOffsetX(curve.Get());break;case Property::Y:hr=n.visual->SetOffsetY(curve.Get());break;
    case Property::ScaleX:hr=n.scale->SetScaleX(curve.Get());break;case Property::ScaleY:hr=n.scale->SetScaleY(curve.Get());break;
    case Property::Opacity:hr=n.effect->SetOpacity(curve.Get());break;case Property::Left:hr=n.clip->SetLeft(curve.Get());break;case Property::Top:hr=n.clip->SetTop(curve.Get());break;case Property::Right:hr=n.clip->SetRight(curve.Get());break;case Property::Bottom:hr=n.clip->SetBottom(curve.Get());break;}
    return s.Check(hr);
}
bool MotionScene::Commit(){if(!impl_||impl_->failed)return false;++impl_->commits;return impl_->Check(impl_->device->Commit());}
size_t MotionScene::Bytes()const{return impl_?impl_->bytes:0;}
size_t MotionScene::Commits()const{return impl_?impl_->commits:0;}
unsigned MotionScene::ActiveScenes(){return activeScenes;}
}
