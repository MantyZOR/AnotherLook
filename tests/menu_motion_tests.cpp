#include "ui.h"
#include "branch_menu.h"
#include "window_motion.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <psapi.h>
#include <dwmapi.h>

using namespace gv;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
uint64_t Cpu(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;}
size_t PrivateBytes(){PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));return memory.PrivateUsage;}
unsigned Sources(bool requireCloaked=false){struct State{unsigned count=0;bool hidden=true;}state;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p){wchar_t name[100]{};GetClassNameW(h,name,100);if(wcscmp(name,L"GoodView.MotionSource")==0){auto& s=*reinterpret_cast<State*>(p);++s.count;DWORD cloak=0;s.hidden&=SUCCEEDED(DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloak,sizeof(cloak)))&&cloak!=0;}return TRUE;},reinterpret_cast<LPARAM>(&state));if(requireCloaked)Require(state.hidden,"source image leaked into the desktop");return state.count;}
void Pump(unsigned ms){double end=MotionNow()+ms/1000.;while(MotionNow()<end){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}}
unsigned snapshots=0,cleanupTimers=0;
LRESULT CALLBACK Fixture(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_TIMER&&w==WindowMotionTimer)++cleanupTimers;if(m==WM_PAINT||m==WM_PRINTCLIENT){if(m==WM_PRINTCLIENT)++snapshots;PAINTSTRUCT p{};HDC dc=m==WM_PAINT?BeginPaint(h,&p):reinterpret_cast<HDC>(w);RECT r;GetClientRect(h,&r);RoundFill(dc,r,UiTheme().panel,20,UiTheme().border);if(m==WM_PAINT)EndPaint(h,&p);return 0;}return DefWindowProcW(h,m,w,l);}
HMENU Menu(){HMENU menu=CreatePopupMenu();for(auto caption:{L"Заметка",L"Сайт",L"Приложение из каталога",L"Папка",L"Часы",L"Календарь",L"Нагрузка системы"})AppendMenuW(menu,MF_STRING,GetMenuItemCount(menu)+1,caption);return menu;}
void Alpha(const PanelBitmap& bitmap){auto size=bitmap.Size();bool any=false;for(size_t i=0;i<static_cast<size_t>(size.cx)*size.cy;++i){auto p=bitmap.Pixels()[i];unsigned a=p>>24;any|=a!=0;Require((p&255)<=a&&((p>>8)&255)<=a&&((p>>16)&255)<=a,"menu alpha is not premultiplied");}Require(any,"menu frame is empty");Require(bitmap.Pixels()[0]==0,"menu corner lost transparency");}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
    fs::path directory=fs::absolute(argv[1]);fs::create_directories(directory);Application app(directory,true);app.config=DefaultConfig();
    HWND foreground=GetForegroundWindow();json::JsonObject report;
    auto config=app.config;config.ui.menuMotion=L"petals";config.ui.windowMotion=false;Config restored;std::wstring error;
    Require(Deserialize(Serialize(config),restored,error)&&restored.ui.menuMotion==L"petals"&&!restored.ui.windowMotion,"motion preferences did not persist");
    auto legacy=json::JsonObject::Parse(Serialize(config));auto ui=legacy.GetNamedObject(L"interface");ui.Remove(L"menuMotion");ui.Remove(L"windowMotion");
    Require(Deserialize(std::wstring(legacy.Stringify()),restored,error)&&restored.ui.menuMotion==L"constellation"&&restored.ui.windowMotion,"old profiles did not receive motion defaults");
    ui.SetNamedValue(L"menuMotion",json::JsonValue::CreateStringValue(L"invalid"));Require(Deserialize(std::wstring(legacy.Stringify()),restored,error)&&restored.ui.menuMotion==L"constellation","invalid motion preference was not normalized");
    HMENU three=CreatePopupMenu();for(auto label:{L"Редактировать",L"Открыть",L"Удалить"})AppendMenuW(three,MF_STRING,GetMenuItemCount(three)+1,label);
    for(UINT dpi:{96u,120u,144u,192u}){BranchMenuRenderer renderer;Require(renderer.Build(three,dpi,{1920,1200},false,L"constellation"),"three actions do not fit");renderer.Render(1);auto hub=renderer.Hub();auto& targets=renderer.Targets();
        auto distance=[&](RECT r){return std::hypot(std::max({r.left-hub.x,0L,hub.x-r.right}),std::max({r.top-hub.y,0L,hub.y-r.bottom}));};
        Require(std::abs(distance(targets[0])-distance(targets[1]))<=1&&std::abs(distance(targets[0])-distance(targets[2]))<=1.5,"three constellation branches do not have equal clearances");
        if(dpi==96)SaveBitmapPng(reinterpret_cast<HBITMAP>(GetCurrentObject(renderer.Bitmap().Dc(),OBJ_BITMAP)),directory/L"three-actions.png");
    }DestroyMenu(three);
    HMENU menu=Menu();size_t maximumBytes=0;int images=0;
    for(auto theme:{L"dark",L"warm",L"cool"})for(UINT dpi:{96u,120u,144u,192u})for(auto mode:{L"constellation",L"petals"}){
        app.Active().appearance=theme;BranchMenuRenderer renderer;if(!renderer.Build(menu,dpi,{1920,1200},true,mode)){auto size=renderer.Size();std::cerr<<"DPI="<<dpi<<" size="<<size.cx<<"x"<<size.cy<<" bytes="<<renderer.CacheBytes()<<"\n";throw std::runtime_error("seven actions do not fit the menu preview");}
        for(float phase:{.3f,.65f,1.f}){Require(renderer.Render(phase,phase==1?2:-1),"cannot render menu motion");Alpha(renderer.Bitmap());auto size=renderer.Size();
            for(size_t i=0;i<renderer.Targets().size();++i){auto r=renderer.Targets()[i];if(IsRectEmpty(&r))continue;Require(r.left>=0&&r.top>=0&&r.right<=size.cx&&r.bottom<=size.cy,"animated menu target exceeds its image");POINT center{(r.left+r.right)/2,(r.top+r.bottom)/2};Require(renderer.Hit(center)==static_cast<int>(i),"drawn action and hit target disagree");}
        }
        maximumBytes=std::max(maximumBytes,renderer.CacheBytes());Require(maximumBytes<=6*1024*1024,"menu exceeded its memory budget");
        if(dpi==96){auto bitmap=reinterpret_cast<HBITMAP>(GetCurrentObject(renderer.Bitmap().Dc(),OBJ_BITMAP));Require(SaveBitmapPng(bitmap,directory/(std::wstring(theme)+L"-"+mode+L".png")),"cannot save menu preview");++images;}
    }
    app.Active().appearance=L"dark";BranchMenuRenderer cost;Require(cost.Build(menu,96,{1920,1200},false,L"constellation"),"cost fixture failed");cost.Render(1);auto cpu=Cpu();auto began=std::chrono::steady_clock::now();
    for(int i=0;i<480;++i)Require(cost.Render((i%18)/17.f),"animation frame failed");
    double cpuMs=(Cpu()-cpu)/10000.;report.SetNamedValue(L"menuCpuMsPerFrame",json::JsonValue::CreateNumberValue(cpuMs/480));report.SetNamedValue(L"menuWallMsPerFrame",json::JsonValue::CreateNumberValue(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()/480));report.SetNamedValue(L"menuCacheBytesAt96Dpi",json::JsonValue::CreateNumberValue(static_cast<double>(cost.CacheBytes())));report.SetNamedValue(L"maxCacheBytes96To192Dpi",json::JsonValue::CreateNumberValue(static_cast<double>(maximumBytes)));DestroyMenu(menu);
    auto stableBytes=cost.CacheBytes();cpu=Cpu();for(int i=0;i<240;++i)Require(cost.Render(1,i%7),"hover frame failed");
    report.SetNamedValue(L"menuHoverCpuMsPerFrame",json::JsonValue::CreateNumberValue((Cpu()-cpu)/10000./240));Require(cost.CacheBytes()==stableBytes,"hover allocated a new card cache");
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Fixture;cls.lpszClassName=L"GoodView.Motion.Offscreen";RegisterClassW(&cls);
    size_t beforeDevice=PrivateBytes();HWND composed=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"",WS_POPUP,-32000,-32000,600,500,nullptr,nullptr,cls.hInstance,nullptr);
    Require(composed!=nullptr,"cannot create composition fixture");
    {MotionScene scene;HMENU gpuMenu=Menu();BranchMenuRenderer renderer;Require(renderer.Build(gpuMenu,96,{1920,1200},false,L"constellation"),"GPU menu fixture failed");auto started=MotionNow();
        Require(scene.Open(composed,6*1024*1024)&&renderer.Compose(scene,true),"compositor menu did not commit");report.SetNamedValue(L"menuPrepareMs",json::JsonValue::CreateNumberValue((MotionNow()-started)*1000));report.SetNamedValue(L"menuGpuSurfaceBytes",json::JsonValue::CreateNumberValue(static_cast<double>(scene.Bytes())));
        Require(Sources(true)==9,"menu did not share its highlight image between actions");
        Require(scene.Commits()==1,"opening menu needed multiple commits");Pump(100);Require(scene.Commits()==1,"menu required CPU commits while animating");
        Require(renderer.Highlight(scene,1),"compositor hover failed");auto commits=scene.Commits();for(int i=0;i<100;++i)renderer.Highlight(scene,1);Require(scene.Commits()==commits,"unchanged hover kept committing frames");renderer.Complete(scene);DestroyMenu(gpuMenu);}
    DestroyWindow(composed);Require(MotionScene::ActiveScenes()==0&&Sources()==0,"menu retained its composition scene after closing");Pump(50);report.SetNamedValue(L"sharedDevicePrivateBytesDelta",json::JsonValue::CreateNumberValue(static_cast<double>(PrivateBytes())-beforeDevice));
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"",WS_POPUP,-32000,-32000,614,600,nullptr,nullptr,cls.hInstance,nullptr);Require(window!=nullptr,"cannot create offscreen fixture");
    RoundWindow(window,20);HRGN expected=CreateRectRgn(0,0,0,0);GetWindowRgn(window,expected);
    HWND child=CreateWindowW(L"EDIT",L"text",WS_CHILD|WS_VISIBLE,20,20,200,30,window,nullptr,cls.hInstance,nullptr);RECT before;GetWindowRect(child,&before);
    auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);bool animated=ClientMotionAllowed();report.SetNamedValue(L"systemAnimationsAllowed",json::JsonValue::CreateBooleanValue(animated));
    json::JsonArray openingCosts;
    for(int i=0;i<8;++i){auto started=MotionNow();ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);openingCosts.Append(json::JsonValue::CreateNumberValue((MotionNow()-started)*1000));Require(WindowMotionActive(window)==animated,"hardware motion unavailable on this test machine");ShowWindow(window,SW_HIDE);Require(!WindowMotionActive(window),"hiding left an animation active");Require(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"hiding left the real editor transparent");}
    report.SetNamedValue(L"windowShowCallMs",openingCosts);
    ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);RoundWindow(window,20);
    if(animated){BYTE alpha=0;DWORD flags=0;Require(GetLayeredWindowAttributes(window,nullptr,&alpha,&flags)&&alpha>0,"opening made the live controls click-through");}
    if(animated){HRGN full=CreateRectRgn(0,0,0,0);GetWindowRgn(window,full);Require(EqualRgn(expected,full)&&WindowMotionActive(window),"opening changed the real window's shape");DeleteObject(full);}
    Pump(80);auto shots=snapshots;auto cpuWhileMoving=Cpu();Pump(240);report.SetNamedValue(L"windowTransitionWaitCpuMs",json::JsonValue::CreateNumberValue((Cpu()-cpuWhileMoving)/10000.));Require(snapshots==shots,"window was captured again during motion");
    Pump(WindowMotionDuration+200-240);Require(!WindowMotionActive(window),"reveal did not stop after its duration");Require(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"completed editor is still a transparent layered window");Require(MotionScene::ActiveScenes()==0,"completed reveal retained its GPU resources");
    HRGN actual=CreateRectRgn(0,0,0,0);GetWindowRgn(window,actual);Require(EqualRgn(expected,actual),"original rounded region was not restored");RECT after;GetWindowRect(child,&after);Require(EqualRect(&before,&after),"reveal moved child controls");DeleteObject(actual);DeleteObject(expected);
    ShowWindow(window,SW_HIDE);ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);SendMessageW(window,WM_LBUTTONDOWN,0,0);Require(!WindowMotionActive(window),"interaction did not finish reveal immediately");
    ShowWindow(window,SW_HIDE);ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,nullptr,-32000,-32000,620,610,SWP_NOACTIVATE|SWP_NOZORDER);Require(!WindowMotionActive(window),"resize retained a stale clip");
    ShowWindow(window,SW_HIDE);ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,EN_SETFOCUS),0);Require(WindowMotionActive(window)==animated,"focusing an input cancelled the reveal");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),0);Require(!WindowMotionActive(window),"a button command left the reveal active");
    ShowWindow(window,SW_HIDE);ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,EN_CHANGE),0);Require(!WindowMotionActive(window),"typing did not finish the reveal immediately");
    ShowWindow(window,SW_HIDE);app.config.ui.windowMotion=false;ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);Require(!WindowMotionActive(window),"disabled motion started a timer");
    app.config.ui.windowMotion=true;ShowWindow(window,SW_HIDE);ShowBlueprintWindow(window,SW_SHOWNOACTIVATE);Pump(80);DestroyWindow(window);Pump(30);Require(!WindowMotionActive(window),"destroyed window retained state");
    Require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+4,"reveals leaked GDI resources");Require(MotionScene::ActiveScenes()==0&&Sources()==0,"destroying a window retained its composition scene");DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);Require(foregroundPid!=GetCurrentProcessId(),"offscreen tests activated a fixture");
    report.SetNamedValue(L"previewImages",json::JsonValue::CreateNumberValue(images));report.SetNamedValue(L"focusUnchanged",json::JsonValue::CreateBooleanValue(GetForegroundWindow()==foreground));report.SetNamedValue(L"offscreen",json::JsonValue::CreateBooleanValue(true));WriteAtomic(directory/L"motion-cost.json",std::wstring(report.Stringify()));
    std::cout<<"Menu motion, target geometry, alpha, settings, bounded reveal, region recovery and focus checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
