#include "ui.h"
#include <wincodec.h>
#include <psapi.h>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace gv {
// A separate offscreen benchmark: no hooks, taskbar replacement, visible
// fixtures, synthetic input, or changes to the user's running shell.
struct DesktopRegressionProbe {
    Application app;
    explicit DesktopRegressionProbe(const fs::path& dir):app(dir,true){
        app.config=DefaultConfig();app.monitors=GetMonitors();app.config.wallpaperEnabled=false;
        auto& scene=app.Active();scene.gaming=false;scene.cards.clear();scene.networkDensity=1;
        for(int i=0;i<12;++i){Card card;card.id=std::to_wstring(i);card.title=L"Рабочая заметка для проверки отрисовки";card.content=std::wstring(256,L'я');scene.cards.push_back(std::move(card));}
    }
    static uint64_t Cpu(){FILETIME created{},ended{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;}
    template<class F> static json::JsonObject Measure(int frames,F draw){
        auto start=std::chrono::steady_clock::now();auto cpu=Cpu();for(int i=0;i<frames;++i)draw(i);double used=(Cpu()-cpu)/10000.;
        json::JsonObject result;result.SetNamedValue(L"frames",json::JsonValue::CreateNumberValue(frames));result.SetNamedValue(L"cpuMs",json::JsonValue::CreateNumberValue(used));result.SetNamedValue(L"cpuMsPerFrame",json::JsonValue::CreateNumberValue(used/frames));result.SetNamedValue(L"wallMs",json::JsonValue::CreateNumberValue(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()));return result;
    }
    static void Save(IWICImagingFactory* factory,IWICBitmap* bitmap,const fs::path& path){ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;factory->CreateStream(&stream);winrt::check_hresult(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));encoder->CreateNewFrame(&frame,nullptr);frame->Initialize(nullptr);UINT width=0,height=0;bitmap->GetSize(&width,&height);frame->SetSize(width,height);WICPixelFormatGUID format=GUID_WICPixelFormat32bppPBGRA;frame->SetPixelFormat(&format);winrt::check_hresult(frame->WriteSource(bitmap,nullptr));frame->Commit();encoder->Commit();}
    static uint64_t Pixels(IWICBitmap* bitmap){
        ComPtr<IWICBitmapLock> lock;winrt::check_hresult(bitmap->Lock(nullptr,WICBitmapLockRead,&lock));UINT bytes=0,stride=0,width=0,height=0;BYTE* data=nullptr;lock->GetDataPointer(&bytes,&data);lock->GetStride(&stride);bitmap->GetSize(&width,&height);uint64_t hash=14695981039346656037ull;
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width*4;++x){hash^=data[static_cast<size_t>(y)*stride+x];hash*=1099511628211ull;}return hash;
    }
    json::JsonObject Run(const fs::path& dir){
        json::JsonObject report;report.SetNamedValue(L"theme10000",Measure(10000,[](int){auto palette=UiTheme();if(palette.text==0)throw std::runtime_error("invalid theme");}));
        Surface board(app,false,0);board.Create(true);ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICBitmap> bitmap;winrt::check_hresult(factory->CreateBitmap(1920,1200,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap));
        ComPtr<ID2D1RenderTarget> target;auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);winrt::check_hresult(app.d2d->CreateWicBitmapRenderTarget(bitmap.Get(),properties,&target));board.rt_=target;target->CreateSolidColorBrush(Ink(UiTheme().text),&board.brush_);
        for(const wchar_t* mode:{L"links",L"near"}){
            app.Active().network=mode;board.networkPainted_={-10000,-10000};target->BeginDraw();board.DrawNetwork(1920,1200);winrt::check_hresult(target->EndDraw());
            std::vector<POINT> points;for(auto node:board.network_.nodes)if(node.x>250&&node.y>250&&node.x<1670&&node.y<950)points.push_back({static_cast<LONG>(node.x),static_cast<LONG>(node.y)});
            if(points.empty())throw std::runtime_error("missing network points");board.profilePaint_=true;std::fill(std::begin(board.paintMs_),std::end(board.paintMs_),0);
            auto draw=[&](int i){POINT next=points[(i*7)%points.size()];RECT dirty{LONG_MAX,LONG_MAX,LONG_MIN,LONG_MIN};auto include=[&](float x,float y,float radius){dirty.left=std::min(dirty.left,static_cast<LONG>(std::floor(x-radius)));dirty.top=std::min(dirty.top,static_cast<LONG>(std::floor(y-radius)));dirty.right=std::max(dirty.right,static_cast<LONG>(std::ceil(x+radius)));dirty.bottom=std::max(dirty.bottom,static_cast<LONG>(std::ceil(y+radius)));};
                for(auto cursor:{board.networkPainted_,next}){if(cursor.x<-1000)continue;if(app.Active().network==L"near")include(static_cast<float>(cursor.x),static_cast<float>(cursor.y),248);else{int nearest=board.network_.Nearest(static_cast<float>(cursor.x),static_cast<float>(cursor.y));if(nearest>=0){auto node=board.network_.nodes[nearest];include(node.x,node.y,42);for(auto edge:board.network_.edges)if(edge.a==nearest||edge.b==nearest){auto a=board.network_.nodes[edge.a],b=board.network_.nodes[edge.b];include(a.x,a.y,42);include(b.x,b.y,42);}}}}
                board.networkPainted_=next;target->BeginDraw();target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(dirty.left),static_cast<float>(dirty.top),static_cast<float>(dirty.right),static_cast<float>(dirty.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);board.DrawNetwork(1920,1200,{static_cast<float>(dirty.left),static_cast<float>(dirty.top),static_cast<float>(dirty.right-dirty.left),static_cast<float>(dirty.bottom-dirty.top)});target->PopAxisAlignedClip();winrt::check_hresult(target->EndDraw());};
            auto result=Measure(120,draw);const wchar_t* names[]={L"backdropMs",L"graphMs",L"edgesMs",L"nodesMs"};for(int i=0;i<4;++i)result.SetNamedValue(names[i],json::JsonValue::CreateNumberValue(board.paintMs_[i]));report.SetNamedValue(mode,result);Save(factory.Get(),bitmap.Get(),dir/(std::wstring(mode)+L".png"));
            auto partial=Pixels(bitmap.Get());target->BeginDraw();board.DrawNetwork(1920,1200);winrt::check_hresult(target->EndDraw());bool identical=partial==Pixels(bitmap.Get());report.SetNamedValue(std::wstring(mode)+L"PartialMatchesFull",json::JsonValue::CreateBooleanValue(identical));if(!identical)throw std::runtime_error("partial drawing left stale pixels");
        }
        Surface rail(app,false,0,true);rail.Create(true);app.config.ui.railOrbit=true;rail.panelBitmap_.Resize(280,280);ComPtr<ID2D1DCRenderTarget> ring;winrt::check_hresult(app.d2d->CreateDCRenderTarget(&properties,&ring));RECT rect{0,0,280,280};winrt::check_hresult(ring->BindDC(rail.panelBitmap_.Dc(),&rect));rail.rt_=ring;ring->CreateSolidColorBrush(Ink(UiTheme().text),&rail.brush_);
        std::vector<HWND> fixtures;WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.hIcon=app.icon;cls.lpszClassName=L"GoodView.PointerCost.Hidden";RegisterClassW(&cls);
        for(int i=0;i<18;++i){auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"Hidden benchmark window",WS_POPUP,app.monitors[0].bounds.left+40,app.monitors[0].bounds.top+40,100,100,nullptr,nullptr,cls.hInstance,nullptr);fixtures.push_back(window);WindowInfo info;info.hwnd=window;info.pid=GetCurrentProcessId();info.title=L"Окно для проверки кольца";info.process=L"C:\\Benchmark\\Application.exe";app.openWindows.push_back(info);app.icons.GetWindow(window);}
        auto drawRing=[&](int i){uint64_t now=1000+i*16;rail.orbitHover_.Target((i/4)%9,now,true);rail.orbitHover_.Tick(now);rail.hits_.clear();ring->BeginDraw();ring->Clear(D2D1::ColorF(0,0.f));rail.DrawOrbit(280,280);winrt::check_hresult(ring->EndDraw());};
        drawRing(0);report.SetNamedValue(L"ring",Measure(240,drawRing));
        unsigned timerMessages=0,immediateFrames=0;ValidateRect(rail.hwnd,nullptr);for(int i=0;i<30;++i){rail.HoverOrbit(i%9);if(GetUpdateRect(rail.hwnd,nullptr,FALSE)){++immediateFrames;ValidateRect(rail.hwnd,nullptr);}Sleep(3);MSG message;while(PeekMessageW(&message,rail.hwnd,WM_TIMER,WM_TIMER,PM_REMOVE))if(message.wParam==83)++timerMessages;}KillTimer(rail.hwnd,83);report.SetNamedValue(L"hoverImmediateInvalidations",json::JsonValue::CreateNumberValue(immediateFrames));report.SetNamedValue(L"hoverTimerMessagesDuringRetarget",json::JsonValue::CreateNumberValue(timerMessages));SaveBitmapPng(rail.panelBitmap_.Size().cx?reinterpret_cast<HBITMAP>(GetCurrentObject(rail.panelBitmap_.Dc(),OBJ_BITMAP)):nullptr,dir/L"ring.png");
        for(auto window:fixtures)DestroyWindow(window);app.openWindows.clear();
        auto& scene=app.Active();auto saved=scene;
        auto same=[](Palette a,Palette b){return a.light==b.light&&a.canvas==b.canvas&&a.panel==b.panel&&a.card==b.card&&a.field==b.field&&a.text==b.text&&a.muted==b.muted&&a.border==b.border&&a.accent==b.accent;};
        for(const wchar_t* appearance:{L"dark",L"warm",L"cool"})for(const wchar_t* accent:{L"A5C7A3",L"E079B6",L"7396CB"}){scene.appearance=appearance;scene.accent=accent;scene.backdrop=L"213854";if(!same(UiTheme(),PaletteFor(scene)))throw std::runtime_error("theme cache failed to refresh");}scene=saved;report.SetNamedValue(L"themeChangesChecked",json::JsonValue::CreateNumberValue(9));
        HWND sink=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);app.events=sink;
        Application::WinEvent(nullptr,EVENT_OBJECT_SHOW,rail.hwnd,OBJID_WINDOW,CHILDID_SELF,0,0);MSG event;bool ownQueued=PeekMessageW(&event,sink,WM_APP+2,WM_APP+2,PM_REMOVE)!=FALSE;
        Application::WinEvent(nullptr,EVENT_SYSTEM_FOREGROUND,rail.hwnd,OBJID_WINDOW,CHILDID_SELF,0,0);bool focusQueued=PeekMessageW(&event,sink,WM_APP+2,WM_APP+2,PM_REMOVE)!=FALSE;app.events=nullptr;DestroyWindow(sink);
        if(ownQueued||!focusQueued)throw std::runtime_error("own window events should not rescan apps, but focus must pass");report.SetNamedValue(L"ownVisibilityFilteredAndFocusPreserved",json::JsonValue::CreateBooleanValue(true));
        PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));report.SetNamedValue(L"privateBytes",json::JsonValue::CreateNumberValue(static_cast<double>(memory.PrivateUsage)));report.SetNamedValue(L"offscreen",json::JsonValue::CreateBooleanValue(true));return report;
    }
};
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    gv::fs::path dir;
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=2){LocalFree(argv);return ERROR_INVALID_PARAMETER;}dir=gv::fs::absolute(argv[1]);LocalFree(argv);
        gv::fs::create_directories(dir);gv::DesktopRegressionProbe probe(dir);auto result=probe.Run(dir);return gv::WriteAtomic(dir/L"pointer-cost.json",std::wstring(result.Stringify()))?0:1;
    }catch(const std::exception& error){if(!dir.empty())gv::WriteAtomic(dir/L"error.txt",gv::Wide(error.what()));return 2;}catch(...){return 2;}
}
