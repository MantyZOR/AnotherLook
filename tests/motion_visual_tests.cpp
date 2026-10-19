// Explicit manual run: a short, non-activating preview with a solid backdrop.
// Pixel copies are restricted to that backdrop and our own preview windows.
#include "ui.h"
#include "branch_menu.h"
#include "window_motion.h"
#include <dwmapi.h>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace gv;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Pump(unsigned ms){double end=MotionNow()+ms/1000.;while(MotionNow()<end){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,4,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}}
std::function<void()> tick;std::exception_ptr failure;
std::vector<HWND> Editors(){std::vector<HWND> result;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p){wchar_t name[100]{};GetClassNameW(h,name,100);if(wcscmp(name,L"GoodView.Edit")==0)reinterpret_cast<std::vector<HWND>*>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
void CloseEditors(){for(auto h:Editors())PostMessageW(h,WM_CLOSE,0,0);}
LRESULT CALLBACK PreviewProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_TIMER&&tick){try{tick();}catch(...){failure=std::current_exception();tick={};CloseEditors();}return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);RECT r;GetClientRect(h,&r);HBRUSH brush=CreateSolidBrush(RGB(12,19,31));FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(h,&p);return 0;}
    return DefWindowProcW(h,m,w,l);
}
struct Window {HWND h{};~Window(){if(IsWindow(h))DestroyWindow(h);}};
bool OwnArea(RECT bounds,HWND backdrop){
    struct Context{RECT bounds;HWND backdrop;DWORD process;bool safe=true,reached=false;}context{bounds,backdrop,GetCurrentProcessId()};
    EnumWindows([](HWND h,LPARAM value){auto& c=*reinterpret_cast<Context*>(value);if(h==c.backdrop){c.reached=true;return FALSE;}
        if(!IsWindowVisible(h))return TRUE;DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==c.process)return TRUE;DWORD cloaked=0;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
        RECT r{},intersection{};GetWindowRect(h,&r);if(IntersectRect(&intersection,&r,&c.bounds)){c.safe=false;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&context));return context.safe&&context.reached;
}
struct Image {SIZE size{};std::vector<uint32_t> pixels;};
Image Capture(RECT bounds,HWND backdrop){
    Require(OwnArea(bounds,backdrop),"preview was covered by another application; no pixels were saved");
    PanelBitmap image;Require(image.Resize(bounds.right-bounds.left,bounds.bottom-bounds.top),"cannot allocate preview image");HDC screen=GetDC(nullptr);
    bool copied=screen&&BitBlt(image.Dc(),0,0,image.Size().cx,image.Size().cy,screen,bounds.left,bounds.top,SRCCOPY|CAPTUREBLT);if(screen)ReleaseDC(nullptr,screen);Require(copied,"cannot copy own preview rectangle");GdiFlush();
    return {image.Size(),std::vector<uint32_t>(image.Pixels(),image.Pixels()+static_cast<size_t>(image.Size().cx)*image.Size().cy)};
}
void Save(const Image& source,const fs::path& path){PanelBitmap bitmap;Require(bitmap.Resize(source.size.cx,source.size.cy),"cannot save preview image");std::copy(source.pixels.begin(),source.pixels.end(),bitmap.Pixels());bitmap.RoundAlpha(0);Require(SaveBitmapPng(reinterpret_cast<HBITMAP>(GetCurrentObject(bitmap.Dc(),OBJ_BITMAP)),path),"cannot write preview PNG");}
void Solid(PanelBitmap& bitmap,int width,int height,COLORREF color){Require(bitmap.Resize(width,height),"cannot create stack fixture");RECT r{0,0,width,height};HBRUSH brush=CreateSolidBrush(color);FillRect(bitmap.Dc(),&r,brush);DeleteObject(brush);bitmap.RoundAlpha(0);}
bool ColorAt(const Image& image,int x,int y,COLORREF color){auto p=image.pixels[static_cast<size_t>(y)*image.size.cx+x];return std::abs(int((p>>16)&255)-GetRValue(color))<8&&std::abs(int((p>>8)&255)-GetGValue(color))<8&&std::abs(int(p&255)-GetBValue(color))<8;}
void CheckStack(HWND backdrop,const fs::path& output){
    RECT area{};GetWindowRect(backdrop,&area);Window host{CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"GoodView.Motion.Visible",L"",WS_POPUP,area.left+100,area.top+100,400,300,backdrop,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Require(host.h!=nullptr,"cannot create stack preview");MotionScene scene;Require(scene.Open(host.h),"composition unavailable");PanelBitmap back,front;Solid(back,400,300,RGB(170,44,62));Solid(front,120,90,RGB(47,174,210));
    Require(scene.Add(back)!=MotionScene::None&&scene.Add(front,{100,80})!=MotionScene::None&&scene.Commit(),"cannot compose stack preview");ShowWindow(host.h,SW_SHOWNOACTIVATE);Pump(80);DwmFlush();RECT bounds;GetWindowRect(host.h,&bounds);auto image=Capture(bounds,backdrop);Save(image,output/L"native-layer-order.png");
    Require(ColorAt(image,150,120,RGB(47,174,210)),"new visual is behind the previous sibling");Require(ColorAt(image,20,20,RGB(170,44,62)),"background or source rasterization is missing");
}
void CheckConstellation(HWND backdrop,const fs::path& output){
    HMENU menu=CreatePopupMenu();for(auto label:{L"Редактировать",L"Открыть",L"Удалить"})AppendMenuW(menu,MF_STRING,GetMenuItemCount(menu)+1,label);
    BranchMenuRenderer renderer;Require(renderer.Build(menu,96,{1100,800},false,L"constellation"),"cannot prepare constellation preview");DestroyMenu(menu);auto size=renderer.Size();RECT area{};GetWindowRect(backdrop,&area);
    Window host{CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"GoodView.Motion.Visible",L"",WS_POPUP,area.left+100,area.top+100,size.cx,size.cy,backdrop,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Require(host.h!=nullptr,"cannot create constellation preview");MotionScene scene;Require(scene.Open(host.h)&&renderer.Compose(scene,true),"cannot compose constellation preview");ShowWindow(host.h,SW_SHOWNOACTIVATE);Pump(170);RECT bounds{};GetWindowRect(host.h,&bounds);Save(Capture(bounds,backdrop),output/L"native-constellation-mid.png");Pump(MenuMotionDuration-170+70);auto image=Capture(bounds,backdrop);Save(image,output/L"native-constellation.png");renderer.Render(1);
    auto expected=renderer.Bitmap().Pixels();size_t total=0,matches=0;for(auto r:renderer.Targets())for(int y=r.top+16;y<r.bottom-16;++y)for(int x=r.left+16;x<r.right-16;++x){size_t i=static_cast<size_t>(y)*size.cx+x;auto p=expected[i];++total;if(ColorAt(image,x,y,RGB((p>>16)&255,(p>>8)&255,p&255)))++matches;}
    Require(total&&matches/static_cast<double>(total)>.98,"constellation links or layers cover the action cards");
}
struct DialogRun {
    Application& app;HWND backdrop;fs::path output;bool spam=false,spammed=false;double began=0;size_t next=0;unsigned repeated=0;
    std::vector<Image> frames;std::vector<double> times;
    void Tick(){
        auto editors=Editors();Require(editors.size()<=1,"a repeated action created a second editor");if(editors.empty())return;HWND editor=editors.front();
        if(!began)began=MotionNow();
        if(spam&&!spammed){spammed=true;for(int i=0;i<500;++i){app.AddScene();++repeated;Require(Editors().size()==1,"reentrant scene creation opened another editor");}}
        double elapsed=(MotionNow()-began)*1000;const double moments[]={70,220,380,560,790,980};
        if(next<std::size(moments)&&elapsed>=moments[next]){RECT rect{};GetWindowRect(editor,&rect);frames.push_back(Capture(rect,backdrop));times.push_back(elapsed);++next;}
        if(elapsed>1050)PostMessageW(editor,WM_CLOSE,0,0);
    }
    void Check(){
        Require(frames.size()==6,"missing native transition frames");for(size_t i=0;i<frames.size();++i)Save(frames[i],output/(L"frame-"+std::to_wstring(i)+L".png"));
        const auto& before=frames[4];const auto& after=frames[5];Require(before.size.cx==after.size.cx&&before.size.cy==after.size.cy,"editor moved at handoff");
        size_t bright=0,alreadyVisible=0;for(size_t i=0;i<after.pixels.size();++i){auto a=after.pixels[i],b=before.pixels[i];int ar=(a>>16)&255,ag=(a>>8)&255,ab=a&255;
            if(std::max({ar,ag,ab})<150)continue;++bright;if(std::abs(ar-int((b>>16)&255))<24&&std::abs(ag-int((b>>8)&255))<24&&std::abs(ab-int(b&255))<24)++alreadyVisible;}
        Require(bright>100,"editor preview has no readable content");double ratio=alreadyVisible/static_cast<double>(bright);
        json::JsonObject report;report.SetNamedValue(L"repeatedAddSceneCalls",json::JsonValue::CreateNumberValue(repeated));report.SetNamedValue(L"visibleContentBeforeHandoff",json::JsonValue::CreateNumberValue(ratio));json::JsonArray clock;for(double time:times)clock.Append(json::JsonValue::CreateNumberValue(time));report.SetNamedValue(L"captureTimesMs",clock);WriteAtomic(output/L"native-frames.json",std::wstring(report.Stringify()));
        Require(ratio>.95,"editor content appears only at handoff instead of during reveal");
    }
};
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3||std::wstring_view(argv[2])!=L"--visible")return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);fs::path output=fs::absolute(argv[1]);fs::create_directories(output/L"profile");
    Application app(output/L"profile",true);app.config=DefaultConfig();app.config.enabled=false;HWND foreground=GetForegroundWindow();app.monitors={GetMonitors().front()};auto work=app.monitors.front().work;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=PreviewProc;cls.lpszClassName=L"GoodView.Motion.Visible";RegisterClassW(&cls);
    Window backdrop{CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,cls.lpszClassName,L"Another Look · animation preview",WS_POPUP,work.left+(work.right-work.left-1100)/2,work.top+(work.bottom-work.top-800)/2,1100,800,nullptr,nullptr,cls.hInstance,nullptr)};Require(backdrop.h!=nullptr,"cannot create solid preview backdrop");ShowWindow(backdrop.h,SW_SHOWNOACTIVATE);UpdateWindow(backdrop.h);Pump(30);
    CheckStack(backdrop.h,output);CheckConstellation(backdrop.h,output);
    auto board=std::make_unique<Surface>(app,false,0);board->hwnd=backdrop.h;app.boards.push_back(std::move(board));
    for(int run=0;run<2;++run){auto folder=output/(run?L"scene-again":L"scene");fs::create_directories(folder);DialogRun state{app,backdrop.h,folder,true};tick=[&]{state.Tick();};SetTimer(backdrop.h,1,10,nullptr);size_t scenes=app.config.scenes.size();app.AddScene();KillTimer(backdrop.h,1);tick={};if(failure)std::rethrow_exception(failure);state.Check();Require(app.config.scenes.size()==scenes,"cancelled scene creation changed the profile");}
    {auto folder=output/L"calendar";fs::create_directories(folder);DialogRun state{app,backdrop.h,folder};tick=[&]{state.Tick();};SetTimer(backdrop.h,1,10,nullptr);Card card;card.type=L"calendar";card.title=L"Календарь";card.box={0,0,350,320};EditDialog(backdrop.h,card,L"Настроить календарь");KillTimer(backdrop.h,1);tick={};if(failure)std::rethrow_exception(failure);state.Check();}
    app.boards.front()->hwnd=nullptr;app.boards.clear();Require(GetForegroundWindow()==foreground,"preview changed the foreground window");std::cout<<"Native stacking, staged editor content, handoff and 1000 repeated scene actions passed; foreground unchanged\n";return 0;
}catch(const std::exception& error){CloseEditors();std::cerr<<error.what()<<'\n';return 1;}}
