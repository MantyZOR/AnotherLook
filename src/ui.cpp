#include "ui.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <psapi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace gv {
namespace {
Application* currentApp=nullptr;
constexpr UINT WM_TRAY=WM_APP+1,WM_WINDOW_EVENT=WM_APP+2,WM_PANEL_SYNC=WM_APP+3,WM_WIN_TAP=WM_APP+4;
constexpr UINT_PTR TIMER_SAVE=1,TIMER_CLOCK=2,TIMER_LAYOUT=3,TIMER_DISPLAYS=4,TIMER_PANEL_GEOMETRY=5,TIMER_PROGRAM=6,TIMER_TRAY_MENU=7,TIMER_SYSTEM=8,TIMER_SECONDS=9;
constexpr UINT_PTR TIMER_SHELL_SETTLE=20,TIMER_SHELL_SETTLE_LAST=23;
bool Contains(Box b,float x,float y){return x>=b.x&&y>=b.y&&x<b.x+b.w&&y<b.y+b.h;}
D2D1_RECT_F R(Box b){return D2D1::RectF(b.x,b.y,b.x+b.w,b.y+b.h);}
std::wstring Truncate(std::wstring s,size_t max){if(s.size()>max)s=s.substr(0,max-1)+L"…";return s;}
std::wstring DateText(){SYSTEMTIME t;GetLocalTime(&t);const wchar_t* months[]={L"января",L"февраля",L"марта",L"апреля",L"мая",L"июня",L"июля",L"августа",L"сентября",L"октября",L"ноября",L"декабря"};return std::to_wstring(t.wDay)+L" "+months[t.wMonth-1];}
std::wstring TimeText(){SYSTEMTIME t;GetLocalTime(&t);wchar_t b[8];swprintf_s(b,L"%02u:%02u",t.wHour,t.wMinute);return b;}
int CursorMonitor(const std::vector<Monitor>& monitors){POINT point;GetCursorPos(&point);MONITORINFOEXW info{};info.cbSize=sizeof(info);GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTOPRIMARY),&info);for(size_t i=0;i<monitors.size();++i)if(monitors[i].label==info.szDevice)return static_cast<int>(i);return 0;}
void PlaceNewCard(Card& card,const Scene& scene,float width){
    card.box.w=std::min(card.box.w,std::max(220.f,width-32));
    for(float y=16;y<5800-card.box.h;y+=card.box.h+24)for(float x=16;x+card.box.w<=width;x+=card.box.w+24){Box candidate{x,y,card.box.w,card.box.h};bool free=true;for(const auto& other:scene.cards)if(other.monitor==card.monitor){auto b=other.box;if(candidate.x<b.x+b.w+12&&candidate.x+candidate.w+12>b.x&&candidate.y<b.y+b.h+12&&candidate.y+candidate.h+12>b.y){free=false;break;}}if(free){card.box=candidate;return;}}
}

}
D2D1_COLOR_F Color(std::wstring_view hex,float alpha){unsigned n=0;try{n=std::stoul(std::wstring(hex),nullptr,16);}catch(...){}return D2D1::ColorF(n,alpha);}
Application* CurrentApplication(){return currentApp;}
bool HandleExitShortcut(const MSG& message){if(!currentApp||message.message!=WM_KEYDOWN||message.wParam!='Q'||!(GetKeyState(VK_CONTROL)&0x8000))return false;currentApp->Quit();return true;}
Palette UiTheme(){
    // A conditional expression with Scene{} copied the entire live scene,
    // including every card, on each colour lookup. Cache only the three inputs
    // that PaletteFor actually uses; edits and theme switches remain immediate.
    static const Scene fallback;
    const Scene& scene=currentApp?currentApp->Active():fallback;
    struct Cache {bool ready=false;std::wstring accent,backdrop,appearance;Palette value;};
    thread_local Cache cache;
    if(!cache.ready||cache.accent!=scene.accent||cache.backdrop!=scene.backdrop||cache.appearance!=scene.appearance){
        cache.value=PaletteFor(scene);cache.accent=scene.accent;cache.backdrop=scene.backdrop;cache.appearance=scene.appearance;cache.ready=true;
    }
    return cache.value;
}
D2D1_COLOR_F Ink(COLORREF color,float alpha){return D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,alpha);}


Application::Application(fs::path dir,bool p):dataDir(std::move(dir)),preview(p),windows(dataDir,config),wallpaper(dataDir),web(dataDir,[this](){Refresh();}),chrome(dataDir),launcher(*this),notificationTray(*this),customization(*this),fileIndex(dataDir/L"file-index.sqlite"),fileSettings(*this){std::wstring error;config=LoadConfig(dataDir/L"settings.json",error);if(!error.empty())MessageBoxW(nullptr,error.c_str(),L"Настройки WindowsGoodView",MB_ICONINFORMATION);if(preview)config.enabled=false;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,d2d.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));if(!d2d||!dwrite)throw std::runtime_error("Direct2D initialization failed");typography.Initialize(dwrite.Get());auto stroke=D2D1::StrokeStyleProperties();stroke.startCap=stroke.endCap=stroke.dashCap=D2D1_CAP_STYLE_ROUND;stroke.lineJoin=D2D1_LINE_JOIN_ROUND;d2d->CreateStrokeStyle(stroke,nullptr,0,&iconStroke);icon=CreateAppIcon();currentApp=this;}
Application::~Application(){quitting=true;fileIndex.RequestStop();launcher.RequestStopIndex();SetHooks(false);for(auto* list:{&tools,&bars,&rails,&boards})for(auto& surface:*list){surface->Show(false);if(surface->bar||surface->rail)surface->Place(true);}chrome.Restore();windows.RestoreAll();if(events)wallpaper.Restore(true);ShutdownStage(L"file-index");fileIndex.Stop();ShutdownStage(L"app-catalog");launcher.StopIndex();ShutdownStage(L"windows");fileSettings.Close();customization.Close();notificationTray.Stop();SetHooks(false);launcher.Hide();web.CloseAll();tools.clear();bars.clear();rails.clear();boards.clear();chrome.Restore();windows.RestoreAll();if(events)wallpaper.Restore(true);if(events){NOTIFYICONDATAW n{sizeof(n)};n.hWnd=events;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);}if(guardianStop)SetEvent(guardianStop);if(guardianProcess){WaitForSingleObject(guardianProcess,1500);CloseHandle(guardianProcess);}if(guardianStop)CloseHandle(guardianStop);if(events)DestroyWindow(events);if(icon)DestroyIcon(icon);ShutdownStage(L"complete");currentApp=nullptr;}
Scene& Application::Active(){auto*s=FindScene(config,config.active);return s?*s:config.scenes.front();}
bool Application::RenderPreview(const fs::path& path,const std::wstring& scene){preview=true;config.enabled=false;if(FindScene(config,scene))config.active=scene;unsigned metrics=0;for(const auto& card:Active().cards)if(card.type==L"system")metrics|=card.appearance.metrics;if(metrics){systemLoad.Sample(metrics);Sleep(150);systemLoad.Sample(metrics);}monitors=GetMonitors();if(monitors.empty())return false;openWindows=ListAppWindows();auto surface=std::make_unique<Surface>(*this,false,0);surface->Create(true);bool result=surface->ExportPreview(path);return result;}
IDWriteTextFormat* Application::Font(float size,int weight,FontRole role){auto key=std::tuple{static_cast<int>(std::lround(size*10)),weight,role};auto it=fonts_.find(key);if(it!=fonts_.end())return it->second.Get();ComPtr<IDWriteTextFormat>f;if(FAILED(dwrite->CreateTextFormat(FontFamily(role),typography.Collection(),static_cast<DWRITE_FONT_WEIGHT>(weight),DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ru-ru",&f)))throw std::runtime_error("Text format creation failed");fonts_[key]=f;return f.Get();}
ID2D1PathGeometry* Application::IconPath(Glyph glyph){
    auto commands=GlyphPath(glyph);if(commands.empty())return nullptr;
    auto found=glyphPaths_.find(glyph);if(found!=glyphPaths_.end())return found->second.Get();
    ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
    if(FAILED(d2d->CreatePathGeometry(&path))||FAILED(path->Open(&sink)))return nullptr;
    bool open=false;
    for(auto p:commands){
        if(p.verb=='M'){if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);sink->BeginFigure(D2D1::Point2F(p.x1,p.y1),D2D1_FIGURE_BEGIN_HOLLOW);open=true;}
        else if(p.verb=='L')sink->AddLine(D2D1::Point2F(p.x1,p.y1));
        else if(p.verb=='C')sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(p.x1,p.y1),D2D1::Point2F(p.x2,p.y2),D2D1::Point2F(p.x3,p.y3)));
        else if(open){sink->EndFigure(p.verb=='Z'?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);open=false;}
    }
    if(open)sink->EndFigure(D2D1_FIGURE_END_OPEN);if(FAILED(sink->Close()))return nullptr;
    auto result=path.Get();glyphPaths_[glyph]=std::move(path);return result;
}
void Application::StartGuardian(){
    if(guardianProcess)return;auto suffix=std::to_wstring(GetCurrentProcessId());guardianStop=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\WindowsGoodView.Stop."+suffix).c_str());HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\WindowsGoodView.Ready."+suffix).c_str());if(!guardianStop||!ready){if(ready)CloseHandle(ready);return;}
    auto command=L"\""+ExePath().wstring()+L"\" --guardian "+suffix+L" --data \""+dataDir.wstring()+L"\"";STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process)){CloseHandle(process.hThread);if(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0)guardianProcess=process.hProcess;else{SetEvent(guardianStop);WaitForSingleObject(process.hProcess,1500);CloseHandle(process.hProcess);CloseHandle(guardianStop);guardianStop=nullptr;}}CloseHandle(ready);
}
int Application::Run(bool background){INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW c{};c.lpfnWndProc=EventProc;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"GoodView.Events";RegisterClassW(&c);events=CreateWindowExW(WS_EX_TOOLWINDOW,c.lpszClassName,dataDir.c_str(),WS_POPUP,0,0,0,0,nullptr,nullptr,c.hInstance,this);
    if(!preview&&windows.RepairSavedHelpers())Save();
    bool rescued=fs::exists(dataDir/L"rescue.flag");if(rescued){config.enabled=false;std::error_code error;fs::remove(dataDir/L"rescue.flag",error);}
    for(int i=0;i<4;++i)RegisterHotKey(events,10+i,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'1'+i);RegisterHotKey(events,20,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_SPACE);RegisterHotKey(events,22,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'A');RegisterHotKey(events,23,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'D');
    NOTIFYICONDATAW n{sizeof(n)};n.hWnd=events;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=WM_TRAY;n.hIcon=icon;wcscpy_s(n.szTip,L"WindowsGoodView — пространства");Shell_NotifyIconW(NIM_ADD,&n);n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);
    UpdateMonitors();RefreshWindows();if(!preview&&!rescued&&(config.enabled||!background))Enable();else if(!background){ApplyState(true);ShowDesktop();}
    if(showAppsOnStart)launcher.Show();
    if(showSettingsOnStart)customization.Show(boards.empty()?nullptr:boards.front()->hwnd);
    if(initializeFileSearch&&!config.files.initialized){config.files.initialized=true;config.files.enabled=true;config.files.allFixedDrives=true;SaveSoon();}if(enableFastOnStart){config.files.initialized=config.files.enabled=config.files.fastNtfs=true;SaveSoon();}if(config.files.enabled){ConfigureFiles();if(config.files.fastNtfs&&!config.files.paused&&(initializeFileSearch||enableFastOnStart))RequestIndexAccess(events,fastSetupOnStart);}launcher.PrepareIndex();for(const auto& scene:config.scenes)for(const auto& card:scene.cards)if(card.type==L"launcher")launcher.Learn({card.title,card.target});SyncSystemWidget();
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(HandleExitShortcut(msg))continue;if(launcher.Message(msg)||customization.Message(msg)||fileSettings.Message(msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);}
void Application::UpdateMonitors(){
    auto next=GetMonitors();if(next.empty())return;
    bool same=next.size()==monitors.size()&&boards.size()==next.size();for(size_t i=0;same&&i<next.size();++i)same=next[i].id==monitors[i].id;
    monitors=std::move(next);for(auto& scene:config.scenes)for(auto& card:scene.cards)if(card.monitorId.empty()&&card.monitor>=0&&card.monitor<static_cast<int>(monitors.size()))card.monitorId=monitors[card.monitor].id;
    if(same){for(size_t i=0;i<monitors.size();++i)monitors[i].dpi=GetDpiForWindow(boards[i]->hwnd);return;}
    std::vector<std::unique_ptr<Surface>> nextBoards;for(size_t i=0;i<monitors.size();++i){auto board=std::make_unique<Surface>(*this,false,static_cast<int>(i));board->Create(true);nextBoards.push_back(std::move(board));}
    // Reparent live pages before destroying their old hosts on monitor changes.
    web.HideEmbedded();for(auto& scene:config.scenes)for(auto& card:scene.cards)if(web.Exists(card.id)&&!web.Detached(card.id)){size_t index=0;for(size_t i=0;i<monitors.size();++i)if(card.monitorId==monitors[i].id)index=i;web.Position(card.id,nextBoards[index]->hwnd,{0,0,100,100},false);}
    notificationTray.Stop();tools.clear();bars.clear();rails.clear();boards=std::move(nextBoards);for(size_t i=0;i<monitors.size();++i)monitors[i].dpi=GetDpiForWindow(boards[i]->hwnd);for(size_t i=0;i<monitors.size();++i){auto bar=std::make_unique<Surface>(*this,true,static_cast<int>(i));bar->Create(true);bars.push_back(std::move(bar));}rails.clear();for(size_t i=0;i<monitors.size();++i){auto rail=std::make_unique<Surface>(*this,false,static_cast<int>(i),true);rail->Create(true);rails.push_back(std::move(rail));}
    for(size_t i=0;i<monitors.size();++i)for(auto kind:{UiElement::Search,UiElement::Settings,UiElement::Power}){auto tool=std::make_unique<Surface>(*this,true,static_cast<int>(i),false,kind);tool->Create(true);tools.push_back(std::move(tool));}
}
void Application::SetHooks(bool on){
    if(systemPrompt_)on=false;if(on)winKey.Start(events,WM_WIN_TAP);else winKey.Stop();
    for(auto hook:{&foregroundHook,&windowHook,&moveHook,&geometryHook,&menuHook})if(*hook){UnhookWinEvent(*hook);*hook=nullptr;}
    externalMenus_.clear();trayMenuPending_=false;if(!on)return;DWORD flags=WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS;
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,WinEvent,0,0,WINEVENT_OUTOFCONTEXT);
    windowHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE,nullptr,WinEvent,0,0,flags);
    moveHook=SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND,EVENT_SYSTEM_MOVESIZEEND,nullptr,WinEvent,0,0,flags);
    geometryHook=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,WinEvent,0,0,flags);
    menuHook=SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART,EVENT_SYSTEM_MENUPOPUPEND,nullptr,WinEvent,0,0,flags);
}
void Application::SwitchScene(const std::wstring& id,bool show){
    if(!FindScene(config,id))return;CancelProgramActivation();launcher.Hide();
    if(!config.enabled&&!preview)Enable();
    if(config.active==id){if(show)ShowDesktop();return;}
    if(config.enabled)windows.Capture();for(auto& board:boards)board->ResetView();
    config.active=id;web.SceneChanged(id);ApplyState(show,true);
    // Choosing a workspace reveals its desktop. App activation belongs to the rail.
    if(show)ShowDesktop();SaveSoon();
}
void Application::ApplyState(bool show,bool sceneOnly){if(applying_)return;applying_=true;auto&s=Active();bool enabled=config.enabled&&!preview;if(!sceneOnly)SetHooks(enabled);KillTimer(events,TIMER_CLOCK);if(enabled)for(size_t i=0;i<boards.size();++i)boards[i]->monitor=static_cast<int>(i);
    if(enabled){if(config.ui.showTray)notificationTray.Start();else notificationTray.Stop();windows.Switch(s.id);if(!sceneOnly){for(auto&b:boards){b->Place(false);b->Show(true);}for(auto&b:bars){b->Place(false);b->Show(true);}for(auto& rail:rails){rail->Place(false);rail->Show(config.ui.railPosition!=L"hidden");}for(auto& tool:tools){tool->Place(false);tool->Show(UiPosition(config.ui,tool->element)!=L"hidden"&&(tool->element!=UiElement::Power||(powerOpen_&&tool->monitor==powerMonitor_)));}}windows.Tile(monitors,76);}
    else{notificationTray.Stop();for(auto& tool:tools){tool->Show(false);tool->Place(true);}for(auto&rail:rails){rail->Show(false);rail->Place(true);}for(auto&b:bars)b->Show(false);for(size_t i=0;i<boards.size();++i){boards[i]->Place(true);boards[i]->Show(i==0);}SetTimer(events,TIMER_CLOCK,60000,nullptr);}
    if(enabled&&!launched_.contains(s.id)){launched_.insert(s.id);for(auto&card:s.cards)if(card.autoLaunch&&card.type==L"launcher"){auto all=ListAppWindows();bool running=false;for(auto&w:all)if(_wcsicmp(w.process.c_str(),card.target.c_str())==0){windows.Attach(w.hwnd,s.id);running=true;break;}if(!running){DWORD pid=0;if(LaunchTarget(card.target,card.arguments,&pid))windows.ExpectLaunch(pid,s.id);}}}
    if(enabled){for(auto& card:s.cards)if(card.autoLaunch&&(card.type==L"site"||card.type==L"link")&&!web.Exists(card.id)&&!boards.empty()){size_t screen=0;for(size_t i=0;i<monitors.size();++i)if(monitors[i].id==card.monitorId)screen=i;OpenCard(card.id,*boards[screen],card.type==L"link");}}
    for(UINT_PTR timer=TIMER_SHELL_SETTLE;timer<=TIMER_SHELL_SETTLE_LAST;++timer)KillTimer(events,timer);
    // Explorer may show secondary taskbars while appbars are registering.
    // Reapply after all registrations, then settle a bounded startup sequence.
    if(enabled){chrome.Refresh();SetTimer(events,TIMER_SHELL_SETTLE,100,nullptr);}
    SetTimer(events,TIMER_CLOCK,60000,nullptr);applying_=false;notificationTray.UpdateStyle();launcher.UpdateStyle();notificationTray.Sync(enabled&&config.ui.showTray);SyncPanels();RefreshWindows();Refresh();UpdateTray();(void)show;}
void Application::ShowOverview(){EnterWorkspace();}
void Application::EnterWorkspace(){if(!config.enabled)Enable();else ShowDesktop();}
void Application::RefreshWindows(){
    auto next=ListAppWindows();for(const auto& entry:windows.Entries())if(entry.scene==config.active&&IsWindow(entry.info.hwnd)&&IsWindowVisible(entry.info.hwnd)&&std::none_of(next.begin(),next.end(),[&](const WindowInfo& info){return info.hwnd==entry.info.hwnd;}))next.push_back(entry.info);
    // EnumWindows follows Z order. Keep existing slots when focus changes and append
    // newly opened windows so the task rail does not jump under the pointer.
    std::map<HWND,size_t> order;for(size_t i=0;i<openWindows.size();++i)order[openWindows[i].hwnd]=i;
    auto rank=[&](HWND window){auto found=order.find(window);return found==order.end()?openWindows.size():found->second;};
    std::stable_sort(next.begin(),next.end(),[&](const WindowInfo& a,const WindowInfo& b){return rank(a.hwnd)<rank(b.hwnd);});
    std::map<HWND,HMONITOR> monitorsNow;for(const auto& info:next)monitorsNow[info.hwnd]=MonitorFromWindow(info.hwnd,MONITOR_DEFAULTTONEAREST);
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    bool changed=foreground!=railForegroundPid_||monitorsNow!=windowMonitors_||next.size()!=openWindows.size()||!std::equal(next.begin(),next.end(),openWindows.begin(),[](const WindowInfo& a,const WindowInfo& b){return a.hwnd==b.hwnd&&a.pid==b.pid&&a.title==b.title&&a.process==b.process&&a.className==b.className&&a.imageName==b.imageName;});
    bool slotsChanged=monitorsNow!=windowMonitors_||next.size()!=openWindows.size()||!std::equal(next.begin(),next.end(),openWindows.begin(),[](const WindowInfo& a,const WindowInfo& b){return a.hwnd==b.hwnd;});
    openWindows=std::move(next);windowMonitors_=std::move(monitorsNow);railForegroundPid_=foreground;
    if(changed)for(auto& rail:rails){if(slotsChanged){rail->ResetInteraction();}rail->Place(!config.enabled||preview);rail->Invalidate();if(IsWindowVisible(rail->hwnd))UpdateWindow(rail->hwnd);}
}
void Application::ToggleOverview(){launcher.Toggle();SyncPanels(launcher.Visible());}
void Application::ShowDesktop(){
    if(boards.empty())return;launcher.Hide();
    if(config.enabled&&!preview)chrome.Refresh();
    if(config.enabled&&!preview)for(auto& board:boards){
        bool covered=board->covered;board->covered=false;board->Show(true);if(covered)board->Invalidate();
        SetWindowPos(board->hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    }
    else boards.front()->Show(true);
    FocusAppWindow(boards[CursorMonitor(monitors)]->hwnd);SyncPanels(true);RefreshWindows();
}
void Application::Enable(){StartGuardian();if(!guardianProcess||WaitForSingleObject(guardianProcess,0)==WAIT_OBJECT_0){config.enabled=false;MessageBoxW(boards.empty()?nullptr:boards.front()->hwnd,L"Не удалось включить независимый аварийный выход Ctrl + Alt + Shift + Space. Возможно, сочетание уже занято. Рабочий стол Windows остаётся доступен.",L"GoodView",MB_ICONINFORMATION);ApplyState(true);return;}if(!chrome.Begin()){MessageBoxW(nullptr,L"Не удалось сохранить состояние панели Windows. Окружение не включено.",L"GoodView",MB_ICONINFORMATION);config.enabled=false;return;}preview=false;config.enabled=true;windows.AdoptSaved();ApplyState(true);SaveSoon();ShowDesktop();}
void Application::Pause(){HidePowerPanel();CancelProgramActivation();launcher.SetStatus(L"");notificationTray.Stop();windows.Capture();config.enabled=false;panelSyncPending_=false;KillTimer(events,TIMER_PANEL_GEOMETRY);
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{wchar_t name[128]{};GetClassNameW(window,name,128);if(std::wstring_view(name)==L"GoodView.Edit"||std::wstring_view(name)==L"GoodView.Menu"||std::wstring_view(name)==L"GoodView.Notice")PostMessageW(window,WM_CLOSE,0,0);else if(std::wstring_view(name)==L"#32770")PostMessageW(window,WM_COMMAND,IDCANCEL,0);return TRUE;},0);launcher.Hide();SetHooks(false);web.HideEmbedded();for(auto& tool:tools){tool->Show(false);tool->Place(true);tool->Discard();}for(auto&rail:rails){rail->Show(false);rail->Place(true);}for(auto&b:boards){b->Show(false);b->Discard();}for(auto&b:bars){b->Show(false);b->Place(true);b->Discard();}chrome.Restore();windows.RestoreAll();wallpaper.Restore(true);KillTimer(events,TIMER_CLOCK);SaveSoon();Refresh();UpdateTray();}
void Application::SaveSoon(){if(!quitting){if(events)SetTimer(events,TIMER_SAVE,350,nullptr);SyncSystemWidget();}}
void Application::Save(){if(saving_)return;saving_=true;bool ok=WriteAtomic(dataDir/L"settings.json",Serialize(config));saving_=false;if(!ok&&!quitting)MessageBoxW(boards.empty()?nullptr:boards.front()->hwnd,L"Не удалось сохранить настройки. Проверь доступ к папке данных WindowsGoodView.",L"WindowsGoodView",MB_ICONERROR);}
void Application::Refresh(){if(quitting)return;for(auto&b:boards)b->Invalidate();for(auto&b:bars)b->Invalidate();for(auto&rail:rails)rail->Invalidate();for(auto& tool:tools)tool->Invalidate();}
void Application::ApplyInterface(){
    // A layout change must not switch scenes, retile user windows or close the editor.
    launcher.UpdateStyle();notificationTray.UpdateStyle();fileSettings.Refresh();bool enabled=config.enabled&&!preview;
    for(auto& tool:tools){tool->Place(!enabled);tool->Show(enabled&&UiPosition(config.ui,tool->element)!=L"hidden"&&(tool->element!=UiElement::Power||(powerOpen_&&tool->monitor==powerMonitor_)));}for(auto& bar:bars)bar->Place(!enabled);for(auto& rail:rails){rail->Place(!enabled);rail->Show(enabled&&config.ui.railPosition!=L"hidden");}
    if(enabled){if(config.ui.showTray)notificationTray.Start();else notificationTray.Stop();chrome.Refresh();SetTimer(events,TIMER_SHELL_SETTLE,100,nullptr);SyncPanels();}
    SaveSoon();Refresh();customization.Refresh();
}
std::vector<Application::WindowGroup> Application::WindowGroups(int monitor)const{
    std::vector<WindowGroup> groups;if(monitor<0||monitor>=static_cast<int>(monitors.size()))return groups;
    for(const auto& window:openWindows){if(!IsWindow(window.hwnd))continue;MONITORINFOEXW info{};info.cbSize=sizeof(info);GetMonitorInfoW(MonitorFromWindow(window.hwnd,MONITOR_DEFAULTTONEAREST),&info);if(monitors[monitor].label!=info.szDevice)continue;
        auto key=window.process.empty()||window.className==L"ConsoleWindowClass"?L"pid:"+std::to_wstring(window.pid):window.process;
        auto found=std::find_if(groups.begin(),groups.end(),[&](const auto& group){return _wcsicmp(group.key.c_str(),key.c_str())==0;});
        auto name=window.className==L"ConsoleWindowClass"?window.title:fs::path(window.imageName).stem().wstring();if(name.empty())name=window.title;
        if(found==groups.end())groups.push_back({key,name,{window}});else found->windows.push_back(window);
    }return groups;
}
Box Application::PanelBox(UiElement element,int monitor,Box area)const{
    if(area.w<=0||area.h<=0){if(monitor<0||monitor>=static_cast<int>(monitors.size()))return {};const auto& m=monitors[monitor];float dpi=m.dpi/96.f;area={0,0,(m.bounds.right-m.bounds.left)/dpi,(m.bounds.bottom-m.bounds.top)/dpi};}
    Box size;float scale=config.ui.textScale;
    if(element==UiElement::Windows&&config.ui.railOrbit)size={0,0,280*scale,280*scale};
    else if(element==UiElement::Windows)size=LayoutRail(static_cast<int>(WindowGroups(monitor).size()),config.ui.railVertical,config.ui.railLabels,{0,0,area.w-24,std::max(64.f,area.h-176*scale)},scale).size;
    if(element==UiElement::Navigation){bool vertical=config.ui.barVertical;float length=24+static_cast<float>(config.scenes.size())*112+48;size={0,0,(vertical?172:std::min(length,(area.w-24)/scale))*scale,(vertical?std::min(length*.46f,(area.h-24)/scale):60)*scale};}
    return InterfaceBox(config.ui,element,area,size);
}
void Application::BeginTrayMenu(DWORD pid){trayMenuPending_=true;trayMenuPid_=pid;if(events)SetTimer(events,TIMER_TRAY_MENU,1500,nullptr);SyncPanels();}
Card* Application::FindCard(const std::wstring&id){for(auto&s:config.scenes)for(auto&c:s.cards)if(c.id==id)return &c;return nullptr;}
void Application::EditCard(const std::wstring&id){auto*c=FindCard(id);if(!c||boards.empty())return;Card changed=*c;size_t screen=std::min(static_cast<size_t>(std::max(0,c->monitor)),boards.size()-1);web.HideEmbedded();if(EditDialog(boards[screen]->hwnd,changed,L"Редактировать карточку")){c=FindCard(id);if(!c){Refresh();return;}bool different=changed.target!=c->target;if(changed.type==L"launcher"&&changed.appearance.variant!=c->appearance.variant){if(changed.appearance.variant==L"row"){changed.box.w=std::max(340.f,changed.box.w);changed.box.h=std::max(100.f,changed.box.h);}else if(changed.appearance.variant==L"tile"){changed.box.w=std::max(220.f,changed.box.w);changed.box.h=std::max(180.f,changed.box.h);}}*c=changed;if(different)web.Close(id);web.SetPolicy(id,c->background);SaveSoon();}Refresh();}
void Application::AddCard(const std::wstring& type,int monitor){
    if(boards.empty())return;if(type==L"launcher"){launcher.Show();return;}if(Active().cards.size()>=128){ShowNotice(boards.front()->hwnd,L"Карточки",L"В одном пространстве можно сохранить до 128 карточек.");return;}
    monitor=std::clamp(monitor,0,static_cast<int>(boards.size())-1);Card card;card.id=NewId();card.type=type;if(type==L"system"){card.appearance.variant=L"rings";card.appearance.metrics=15;}card.monitor=monitor;card.monitorId=monitors[monitor].id;card.title=type==L"site"?L"Новый сайт":type==L"clock"?L"СЕЙЧАС":type==L"calendar"?L"КАЛЕНДАРЬ":type==L"system"?L"СИСТЕМА":L"Новая заметка";card.box={16,16,type==L"site"?560.f:type==L"system"?440.f:360.f,type==L"site"?420.f:type==L"calendar"?300.f:type==L"system"?340.f:220.f};if(type==L"clock"){card.appearance.variant=L"floating";card.appearance.showTitle=false;}if(type==L"site")card.target=L"https://www.google.com";
    web.HideEmbedded();bool add=type==L"clock"||type==L"calendar"||type==L"system"||EditDialog(boards[monitor]->hwnd,card,L"Добавить карточку");if(add){float width=(monitors[monitor].bounds.right-monitors[monitor].bounds.left)*96.f/GetDpiForWindow(boards[monitor]->hwnd)-280;PlaceNewCard(card,Active(),width);Active().cards.push_back(card);boards[monitor]->Reveal(card);SaveSoon();}Refresh();
}
void Application::AddFolder(int monitor){
    if(boards.empty())return;monitor=std::clamp(monitor,0,static_cast<int>(boards.size())-1);web.HideEmbedded();
    auto path=ChooseFolder(boards[monitor]->hwnd,L"Папка на полотне");if(path){auto name=fs::path(*path).filename().wstring();if(name.empty())name=*path;Program folder{name,*path};folder.kind=2;UseProgram(folder,true,monitor);}Refresh();
}
void Application::AddScene(){if(boards.empty())return;int screen=CursorMonitor(monitors);if(config.scenes.size()>=32){ShowNotice(boards[screen]->hwnd,L"Пространства",L"Можно сохранить до 32 пространств.");return;}Card card;card.title=L"Новое пространство";card.type=L"scene";web.HideEmbedded();if(EditDialog(boards[screen]->hwnd,card,L"Новое пространство")){Scene scene;scene.id=NewId();scene.name=card.title;scene.description=card.content;scene.accent=Active().accent;scene.backdrop=Active().backdrop;scene.appearance=Active().appearance;ChooseSpaceIcon(boards[screen]->hwnd,scene.icon);config.scenes.push_back(scene);SwitchScene(scene.id);}Refresh();}
void Application::OpenCard(const std::wstring& id,Surface& surface,bool separate){
    auto* card=FindCard(id);if(!card)return;
    if(card->type==L"site"||card->type==L"link"){
        if(card->type==L"link"&&!separate){card->type=L"site";card->box.w=std::max(480.f,card->box.w);card->box.h=std::max(360.f,card->box.h);SaveSoon();}
        if(separate)web.Open(*card,config.active,surface.hwnd,{0,0,1000,680},true);
        else{if(!web.Detached(id)){ShowDesktop();surface.Reveal(*card);}web.Open(*card,config.active,surface.hwnd,{0,0,100,100},false);surface.SyncWeb();surface.Invalidate();}
    }else if(card->type==L"launcher"){auto extension=fs::path(card->target).extension().wstring();for(auto& letter:extension)letter=static_cast<wchar_t>(towlower(letter));if(GetFileAttributesW(card->target.c_str())!=INVALID_FILE_ATTRIBUTES&&((GetFileAttributesW(card->target.c_str())&FILE_ATTRIBUTE_DIRECTORY)||extension!=L".exe"&&extension!=L".lnk"&&extension!=L".appref-ms"&&extension!=L".bat"&&extension!=L".cmd"))LaunchTarget(card->target,card->arguments);else LaunchProgram(card->target,card->arguments,card->title);}
    else EditCard(id);
    Refresh();
}
void Application::UseProgram(const Program& program,bool pin,int monitor){
    if(program.window&&!pin){LaunchProgram(program.target,L"",program.name,program.window);return;}
    if(pin&&program.target.empty()){launcher.SetStatus(L"Для закрепления выбери exe через «Действия» → «Выбрать программу».");return;}
    auto resolved=ResolveProgramTarget(program.target);if(!resolved.url.empty()&&resolved.url!=program.target){UseProgram({program.name,resolved.url},pin,monitor);return;}
    if(boards.empty())return;int screen=monitor>=0?std::clamp(monitor,0,static_cast<int>(boards.size())-1):CursorMonitor(monitors);
    if(pin||IsWebUrl(program.target)){
        if(!config.enabled&&!preview)Enable();auto& scene=Active();auto existing=std::find_if(scene.cards.begin(),scene.cards.end(),[&](const Card& card){bool sameMonitor=card.monitorId.empty()?card.monitor==screen:card.monitorId==monitors[screen].id;return card.target==program.target&&(!pin||sameMonitor);});
        if(existing!=scene.cards.end()){if(!pin)OpenCard(existing->id,*boards[std::clamp(existing->monitor,0,static_cast<int>(boards.size())-1)]);else{ShowDesktop();boards[screen]->Reveal(*existing);}return;}
        if(scene.cards.size()>=128)return;Card card;card.id=NewId();card.title=program.name;card.target=program.target;card.type=IsWebUrl(program.target)?L"site":L"launcher";card.monitor=screen;card.monitorId=monitors[screen].id;card.box={16,16,card.type==L"site"?640.f:180.f,card.type==L"site"?440.f:112.f};float width=(monitors[screen].bounds.right-monitors[screen].bounds.left)*96.f/GetDpiForWindow(boards[screen]->hwnd)-280;if(program.kind==2){card.appearance.variant=L"icon";card.box.w=112;card.box.h=104;}PlaceNewCard(card,scene,width);
        scene.cards.push_back(card);SaveSoon();ShowDesktop();boards[screen]->Reveal(card);if(!pin)OpenCard(card.id,*boards[screen]);Refresh();
    }else LaunchProgram(program.target,L"",program.name);
}
void Application::CancelProgramActivation(){pendingProgram_.reset();if(events)KillTimer(events,TIMER_PROGRAM);}
bool Application::ActivateWindow(HWND window){
    if(!IsWindow(window))return false;
    if(config.enabled&&windows.IsManaged(window)){windows.MoveToScene(window,config.active);windows.Switch(config.active);SaveSoon();}
    bool result=FocusAppWindow(window);SyncPanels();
    if(!result&&!activatingProgram_){
        CancelProgramActivation();LASTINPUTINFO input{sizeof(input)};GetLastInputInfo(&input);DWORD pid=0;GetWindowThreadProcessId(window,&pid);auto now=GetTickCount64();
        pendingProgram_=PendingProgram{{},L"Приложение",pid,input.dwTime,now,now+3000,window};if(events)SetTimer(events,TIMER_PROGRAM,100,nullptr);
    }
    return result;
}
void Application::LaunchProgram(const std::wstring& target,const std::wstring& arguments,const std::wstring& name,HWND existingWindow){
    CancelProgramActivation();auto resolved=ResolveProgramTarget(target,arguments);auto existing=FindProgramWindows(resolved);
    if(existingWindow&&IsWindow(existingWindow)){WindowInfo selected;selected.hwnd=existingWindow;GetWindowThreadProcessId(existingWindow,&selected.pid);existing.insert(existing.begin(),std::move(selected));}
    if(config.enabled&&resolved.arguments.empty()&&!existing.empty()&&IsWindowVisible(existing.front().hwnd)&&!windows.IsManaged(existing.front().hwnd)&&windows.Attach(existing.front().hwnd,config.active)){windows.Capture();SaveSoon();}
    if(resolved.arguments.empty()&&!existing.empty()&&ActivateWindow(existing.front().hwnd)){launcher.SetStatus(L"");launcher.Hide();return;}
    DWORD pid=0;bool reuse=resolved.arguments.empty()&&!existing.empty();if(reuse)pid=existing.front().pid;
    if(!reuse&&!LaunchTarget(target,arguments,&pid)){
        DWORD error=GetLastError();auto message=error==ERROR_CANCELLED?L"Запуск отменён.":L"Не удалось открыть программу. Проверь файл и права доступа.";
        launcher.SetStatus(message);if(!launcher.Visible()&&!boards.empty())ShowNotice(boards.front()->hwnd,L"Запуск",message);return;
    }
    if(config.enabled&&pid)windows.ExpectLaunch(pid,config.active);
    LASTINPUTINFO input{sizeof(input)};GetLastInputInfo(&input);auto now=GetTickCount64();pendingProgram_=PendingProgram{std::move(resolved),name.empty()?fs::path(target).stem().wstring():name,pid,input.dwTime,now,now+15000};
    if(existingWindow)pendingProgram_->preferred=existingWindow;
    launcher.SetStatus(L"Открываем "+pendingProgram_->name+L"…",true);SetTimer(events,TIMER_PROGRAM,250,nullptr);TryActivateProgram();
}
void Application::TryActivateProgram(){
    if(!pendingProgram_||activatingProgram_||quitting)return;activatingProgram_=true;
    auto request=*pendingProgram_;std::vector<WindowInfo> matches;
    if(request.preferred){DWORD pid=0;GetWindowThreadProcessId(request.preferred,&pid);if(IsWindow(request.preferred)&&pid==request.pid){WindowInfo window;window.hwnd=request.preferred;window.pid=pid;matches.push_back(window);}else{CancelProgramActivation();launcher.SetStatus(L"Окно уже закрыто.");activatingProgram_=false;return;}}
    else matches=FindProgramWindows(request.target,request.pid);auto now=GetTickCount64();
    LASTINPUTINFO input{sizeof(input)};GetLastInputInfo(&input);DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    bool targetForeground=std::any_of(matches.begin(),matches.end(),[&](const WindowInfo& window){return window.pid==foreground;});
    if(now-request.started>300&&input.dwTime!=request.inputTime&&foreground!=GetCurrentProcessId()&&!targetForeground){CancelProgramActivation();launcher.SetStatus(L"Запуск продолжается в фоне.");}
    else if(now>=request.until){CancelProgramActivation();launcher.SetStatus(L"Программа не показала окно. Возможно, она работает в трее.");}
    else if(!matches.empty()){
        HWND window=matches.front().hwnd;
        if(config.enabled){if(windows.IsManaged(window))windows.MoveToScene(window,config.active);else if(IsWindowVisible(window)&&windows.Attach(window,config.active)){windows.Capture();SaveSoon();}}
        if(ActivateWindow(window)){CancelProgramActivation();launcher.SetStatus(L"");launcher.Hide();RefreshWindows();}
    }
    activatingProgram_=false;
}
void Application::WindowSwitcher(HWND owner){
    HMENU menu=CreatePopupMenu();auto available=ListAppWindows();for(auto& entry:windows.Entries())if(entry.scene==config.active&&IsWindow(entry.info.hwnd)&&std::none_of(available.begin(),available.end(),[&](auto& window){return window.hwnd==entry.info.hwnd;}))available.push_back(entry.info);
    if(available.empty())AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"Открытых приложений пока нет");
    for(size_t i=0;i<available.size();++i){AppendMenuW(menu,MF_STRING,static_cast<UINT_PTR>(100+i),Truncate(available[i].title,90).c_str());MenuIcon(menu,100+static_cast<UINT>(i),icons.GetWindow(available[i].hwnd).get());}
    POINT point;GetCursorPos(&point);SetForegroundWindow(owner);UINT selected=StyledMenu(menu,owner,point);DestroyMenu(menu);
    if(selected>=100&&selected<100+available.size())ActivateWindow(available[selected-100].hwnd);
}
void Application::SetTheme(int i){const wchar_t*accents[]={L"B4CEAC",L"A5BED5",L"D6B998",L"B8ACD5",L"D9A49C",L"9BD2C5",L"E0C780",L"B9C7DB",L"A9B9A0",L"DBA6BF"};auto&s=Active();int theme=std::clamp(i,0,9);s.accent=accents[theme];ApplyInterface();}
void Application::ChangeWallpaper(){wchar_t path[32768]{};OPENFILENAMEW f{sizeof(f)};f.hwndOwner=boards.front()->hwnd;f.lpstrFile=path;f.nMaxFile=32768;f.lpstrFilter=L"Изображения\0*.jpg;*.jpeg;*.png;*.bmp\0Все файлы\0*.*\0";f.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&f)){Active().wallpaper=path;SaveSoon();Refresh();}}
void Application::ToggleStartup(){bool next=!IsAutoStartEnabled();if(SetAutoStart(next,dataDir)){config.autoStart=next;SaveSoon();}else MessageBoxW(boards.front()->hwnd,L"Не удалось изменить автозапуск.",L"Автозапуск",MB_ICONINFORMATION);}
std::wstring Application::ResourceText()const{SIZE_T bytes=0;PROCESS_MEMORY_COUNTERS_EX p{};if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))bytes+=p.PrivateUsage;if(guardianProcess&&GetProcessMemoryInfo(guardianProcess,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)))bytes+=p.PrivateUsage;return L"Оболочка "+std::to_wstring((bytes+1024*1024-1)/(1024*1024))+L" МБ"+(web.Count()?L" · сайты отдельно":L"");}
void Application::SyncSystemWidget(){
    SyncSeconds();
    if(!events)return;unsigned metrics=0;if(!quitting&&(config.enabled||preview))for(const auto& board:boards)metrics|=board->VisibleSystemMetrics();bool visible=metrics!=0;
    if(visible==systemTimer_&&(!visible||systemLoad.Metrics()==metrics))return;systemTimer_=visible;
    if(visible){systemLoad.Reset();systemLoad.Sample(metrics);SetTimer(events,TIMER_SYSTEM,2000,nullptr);}else{KillTimer(events,TIMER_SYSTEM);systemLoad.Reset();}
}
void Application::ConfigureFiles(bool rebuild){if(rebuild&&!fileIndex.Status().error.empty()){fileIndex.Stop();for(auto suffix:{L"",L"-wal",L"-shm"}){std::error_code error;auto path=dataDir/(std::wstring(L"file-index.sqlite")+suffix);if(fs::exists(path,error))fs::rename(path,path.wstring()+L".failed-"+Timestamp(),error);}}fileIndex.Start(config.files,events);fileIndex.Configure(config.files,rebuild);}
void Application::SyncSeconds(){
    if(!events||quitting)return;bool visible=false;if(config.enabled||preview)for(auto& board:boards)visible|=board->TickSeconds();
    if(visible==secondsTimer_)return;secondsTimer_=visible;if(visible)SetTimer(events,TIMER_SECONDS,1000,nullptr);else KillTimer(events,TIMER_SECONDS);
}
void Application::SampleSystemWidget(){
    SyncSystemWidget();if(!systemTimer_)return;systemLoad.Sample(systemLoad.Metrics());for(auto& board:boards)if(board->HasVisibleSystemWidget())board->Invalidate();
}
void Application::Quit(){if(quitting)return;windows.Capture();Save();json::JsonObject metrics;metrics.SetNamedValue(L"desktopPaints",json::JsonValue::CreateNumberValue(static_cast<double>(paints)));metrics.SetNamedValue(L"panelPaints",json::JsonValue::CreateNumberValue(static_cast<double>(panelPaints)));metrics.SetNamedValue(L"windowEvents",json::JsonValue::CreateNumberValue(static_cast<double>(windowEvents)));metrics.SetNamedValue(L"fullscreenNotices",json::JsonValue::CreateNumberValue(static_cast<double>(fullscreenNotices)));metrics.SetNamedValue(L"clockTicks",json::JsonValue::CreateNumberValue(static_cast<double>(clockTicks)));WriteAtomic(dataDir/L"diagnostics.json",std::wstring(metrics.Stringify()));quitting=true;CancelProgramActivation();fileIndex.RequestStop();launcher.RequestStopIndex();SetHooks(false);launcher.Hide();fileSettings.Close();customization.Close();notificationTray.Stop();for(auto* list:{&tools,&bars,&rails,&boards})for(auto& surface:*list){surface->Show(false);if(surface->bar||surface->rail)surface->Place(true);}chrome.Restore();windows.RestoreAll();if(events)wallpaper.Restore(true);ShutdownStage(L"desktop-restored");PostQuitMessage(0);}
void Application::ShutdownStage(std::wstring_view stage){if(!events)return;json::JsonObject record;record.SetNamedValue(L"stage",json::JsonValue::CreateStringValue(stage));record.SetNamedValue(L"tick",json::JsonValue::CreateNumberValue(static_cast<double>(GetTickCount64())));record.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(GetCurrentProcessId()));WriteAtomic(dataDir/L"shutdown.json",std::wstring(record.Stringify()));}
void Application::UpdateTray(){if(!events)return;NOTIFYICONDATAW n{sizeof(n)};n.hWnd=events;n.uID=1;n.uFlags=NIF_TIP;auto text=L"WindowsGoodView · "+Active().name+(config.enabled?L"":preview?L" · предпросмотр":L" · Windows");wcsncpy_s(n.szTip,text.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&n);}
void Application::ShowTrayMenu(){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Открыть пространства");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);for(size_t i=0;i<config.scenes.size();++i)AppendMenuW(menu,MF_STRING|(config.scenes[i].id==config.active?MF_CHECKED:0),static_cast<UINT_PTR>(100+i),config.scenes[i].name.c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,2,config.enabled?L"Приостановить окружение / вернуть окна":L"Включить окружение");AppendMenuW(menu,MF_STRING,3,L"Завершить и вернуть обычный рабочий стол");POINT p;GetCursorPos(&p);SetForegroundWindow(events);UINT id=StyledMenu(menu,events,p);DestroyMenu(menu);if(id==1)ShowOverview();else if(id==2){if(config.enabled)Pause();else Enable();}else if(id==3)Quit();else if(id>=100&&id<100+config.scenes.size())SwitchScene(config.scenes[id-100].id);PostMessageW(events,WM_NULL,0,0);}
void Application::SyncPanels(bool reveal){
    if(!config.enabled||preview||quitting||applying_||syncingPanels_||systemPrompt_)return;
    syncingPanels_=true;
    // Event HWNDs and global appbar notifications can be stale. Read focus now.
    HWND foreground=GetForegroundWindow();
    bool revealing=reveal;
    std::erase_if(externalMenus_,[](HWND menu){return menu&&!IsWindow(menu);});
    bool shellForeground=!foreground||foreground==GetDesktopWindow();
    for(auto* surfaces:{&boards,&bars,&rails,&tools})for(auto& surface:*surfaces)if(surface->hwnd==foreground)shellForeground=true;
    DWORD foregroundPid=0;GetWindowThreadProcessId(foreground,&foregroundPid);if(powerOpen_&&!shellForeground&&foregroundPid!=GetCurrentProcessId())HidePowerPanel();
    bool onTop=(shellForeground||revealing)&&!trayMenuPending_&&externalMenus_.empty()&&!launcher.Visible();
    DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);HWND behind=pid&&pid!=GetCurrentProcessId()?foreground:nullptr;
    size_t count=std::min(monitors.size(),std::min(boards.size(),bars.size()));
    for(size_t i=0;i<count;++i){
        bool full=!revealing&&IsFullscreenOnMonitor(foreground,monitors[i].bounds,GetCurrentProcessId());
        bars[i]->Show(!full,reveal&&onTop);bars[i]->SetPanelLayer(onTop,behind);if(i<rails.size()){rails[i]->Show(!full&&config.ui.railPosition!=L"hidden",reveal&&onTop);rails[i]->SetPanelLayer(onTop,behind);}
        for(auto& tool:tools)if(tool->monitor==static_cast<int>(i)){tool->Show(!full&&UiPosition(config.ui,tool->element)!=L"hidden"&&(tool->element!=UiElement::Power||(powerOpen_&&tool->monitor==powerMonitor_)),reveal&&onTop);tool->SetPanelLayer(onTop,behind);}
        if(boards[i]->covered!=full){boards[i]->covered=full;if(full)boards[i]->Discard();else boards[i]->Invalidate();}
        boards[i]->SyncNetworkAnimation(shellForeground&&!launcher.Visible());
    }
    if(!monitors.empty())notificationTray.Sync(config.ui.showTray&&(revealing||!IsFullscreenOnMonitor(foreground,monitors.front().bounds,GetCurrentProcessId())),onTop,behind);
    syncingPanels_=false;SyncSystemWidget();
}
void Application::SchedulePanelSync(bool geometryChange){
    if(!config.enabled||preview||quitting||applying_||syncingPanels_||systemPrompt_)return;
    if(!events){SyncPanels();return;}
    if(geometryChange){SetTimer(events,TIMER_PANEL_GEOMETRY,90,nullptr);return;}
    if(panelSyncPending_)return;panelSyncPending_=true;PostMessageW(events,WM_PANEL_SYNC,0,0);
}
void Application::HandleWindowEvent(DWORD event,HWND window){
    ++windowEvents;if(systemPrompt_)return;
    if(event==EVENT_SYSTEM_MENUPOPUPSTART||event==EVENT_SYSTEM_MENUPOPUPEND){
        if(event==EVENT_SYSTEM_MENUPOPUPSTART)externalMenus_.insert(window);else{externalMenus_.erase(window);trayMenuPending_=false;if(events)KillTimer(events,TIMER_TRAY_MENU);}
        SchedulePanelSync();return;
    }
    if(event==EVENT_SYSTEM_FOREGROUND||event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_HIDE||event==EVENT_OBJECT_DESTROY||event==EVENT_SYSTEM_MOVESIZEEND)RefreshWindows();
    if(pendingProgram_&&(event==EVENT_SYSTEM_FOREGROUND||event==EVENT_OBJECT_SHOW))TryActivateProgram();
    if(!config.enabled||preview||applying_)return;
    if(event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_CREATE||event==EVENT_OBJECT_LOCATIONCHANGE){
        wchar_t name[128]{};GetClassNameW(window,name,128);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid!=GetCurrentProcessId()&&(std::wstring_view(name)==L"Shell_TrayWnd"||std::wstring_view(name)==L"Shell_SecondaryTrayWnd")&&(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)){chrome.Refresh();SetTimer(events,TIMER_SHELL_SETTLE,90,nullptr);}
    }
    if(event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_CREATE){
        windows.HandleNewWindow(window);if(Active().autoTile&&windows.IsManaged(window))SetTimer(events,TIMER_LAYOUT,180,nullptr);
    }
    if(event==EVENT_SYSTEM_MOVESIZEEND&&windows.IsManaged(window)){windows.MarkFloating(window);windows.Capture();SaveSoon();Refresh();}
    if(event==EVENT_OBJECT_LOCATIONCHANGE)SchedulePanelSync(true);
    else if(event==EVENT_SYSTEM_FOREGROUND||event==EVENT_SYSTEM_MOVESIZEEND||event==EVENT_OBJECT_SHOW||event==EVENT_OBJECT_DESTROY)SchedulePanelSync();
}
void CALLBACK Application::WinEvent(HWINEVENTHOOK,DWORD event,HWND h,LONG obj,LONG child,DWORD,DWORD){if(!currentApp||(!h&&event!=EVENT_SYSTEM_MENUPOPUPEND))return;
    // Our tooltip/panel visibility cannot change the list of external apps.
    // In particular, hovering ring segments must not enumerate every process.
    if(event>=EVENT_OBJECT_CREATE){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId())return;}
    // A destroyed HWND cannot answer GetAncestor. Restrict that route to known
    // top-level windows so closing browser child controls does not rescan every app.
    if(event==EVENT_OBJECT_DESTROY&&!currentApp->windows.IsManaged(h)&&std::none_of(currentApp->openWindows.begin(),currentApp->openWindows.end(),[&](const auto& window){return window.hwnd==h;}))return;
    bool filtered=(event==EVENT_OBJECT_LOCATIONCHANGE&&h!=GetForegroundWindow())||(event>=EVENT_OBJECT_CREATE&&(obj!=OBJID_WINDOW||child!=CHILDID_SELF||(event!=EVENT_OBJECT_DESTROY&&GetAncestor(h,GA_ROOT)!=h)));if(filtered){wchar_t name[128]{};GetClassNameW(h,name,128);if(std::wstring_view(name)!=L"Shell_TrayWnd"&&std::wstring_view(name)!=L"Shell_SecondaryTrayWnd")return;}PostMessageW(currentApp->events,WM_WINDOW_EVENT,event,reinterpret_cast<LPARAM>(h));}
LRESULT CALLBACK Application::EventProc(HWND h,UINT m,WPARAM w,LPARAM l){auto*a=reinterpret_cast<Application*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){a=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}if(!a)return DefWindowProcW(h,m,w,l);
    if(m==WM_APP+86){a->config.files.initialized=a->config.files.enabled=a->config.files.fastNtfs=true;a->ConfigureFiles();a->SaveSoon();if(!a->fileIndex.Status().fastConnected)a->RequestIndexAccess(a->fileSettings.Window()?a->fileSettings.Window():h);return 0;}
    if(m==WM_APP+87&&w>=L'A'&&w<=L'Z'){a->config.files.initialized=a->config.files.enabled=a->config.files.fastNtfs=true;a->ConfigureFiles();a->SaveSoon();a->RequestIndexAccess(a->fileSettings.Window()?a->fileSettings.Window():h,std::wstring(1,static_cast<wchar_t>(w)));return 0;}
    if(m==WM_APP+85){auto status=a->fileIndex.Status();json::JsonObject report;auto number=[&](const wchar_t* key,uint64_t value){report.SetNamedValue(key,json::JsonValue::CreateNumberValue(static_cast<double>(value)));};number(L"entries",status.entries);number(L"folders",status.folders);number(L"skipped",status.skipped);number(L"bytes",status.bytes);number(L"elapsedMs",status.elapsedMs);number(L"queryMs",status.queryMs);number(L"scannedEntries",status.scannedEntries);number(L"currentEntries",status.currentEntries);number(L"fastEntries",status.fastEntries);number(L"mftReadMs",status.mftReadMs);number(L"mftBuildMs",status.mftBuildMs);number(L"mftRecords",status.mftRecords);number(L"mftIndexed",status.mftIndexed);number(L"mftCapacity",status.mftCapacity);number(L"remainingMs",status.remainingMs);number(L"usnChanges",status.usnChanges);report.SetNamedValue(L"fastConnected",json::JsonValue::CreateBooleanValue(status.fastConnected));report.SetNamedValue(L"fastCached",json::JsonValue::CreateBooleanValue(status.fastCached));report.SetNamedValue(L"provider",json::JsonValue::CreateStringValue(status.provider));report.SetNamedValue(L"note",json::JsonValue::CreateStringValue(status.note));report.SetNamedValue(L"journalMissing",json::JsonValue::CreateStringValue(status.journalMissing));report.SetNamedValue(L"busy",json::JsonValue::CreateBooleanValue(status.busy));report.SetNamedValue(L"paused",json::JsonValue::CreateBooleanValue(status.paused));report.SetNamedValue(L"trigram",json::JsonValue::CreateBooleanValue(status.trigram));report.SetNamedValue(L"error",json::JsonValue::CreateStringValue(status.error));WriteAtomic(a->dataDir/L"file-index-status.json",std::wstring(report.Stringify()));return 0;}
    if(m==WM_HOTKEY&&a->systemPrompt_)return 0;
    if(m==WM_WIN_TAP){
        if(!a->winKey.TakeTap(w))return 0;
        bool modal=std::any_of(a->boards.begin(),a->boards.end(),[](const auto& board){return !IsWindowEnabled(board->hwnd);});
        if(a->config.enabled&&!a->preview&&!a->systemPrompt_&&!a->quitting&&!modal){
            a->TogglePowerPanel();
        }
        if(!a->winKey.ReleasePending()){a->winReleaseRetries_=0;SetTimer(h,24,50,nullptr);}
        return 0;
    }
    if(m==WM_HOTKEY){int id=static_cast<int>(w);if(std::any_of(a->boards.begin(),a->boards.end(),[](const auto& b){return !IsWindowEnabled(b->hwnd);}))return 0;if(id>=10&&id<14&&id-10<static_cast<int>(a->config.scenes.size())){a->launcher.Hide();if(!a->config.enabled&&!a->preview)a->Enable();if(a->config.enabled||a->preview)a->SwitchScene(a->config.scenes[id-10].id);}else if(id==20)a->ToggleOverview();else if(id==23){if(!a->config.enabled&&!a->preview)a->Enable();else a->ShowDesktop();}else if(id==22&&a->config.enabled){auto target=GetForegroundWindow();if(a->windows.Attach(target,a->config.active)){a->windows.Capture();a->SaveSoon();a->windows.Tile(a->monitors,76);a->Refresh();}}return 0;}
    if(m==WM_TRAY){auto event=LOWORD(l);if(event==WM_CONTEXTMENU||event==WM_RBUTTONUP)a->ShowTrayMenu();else if(event==NIN_SELECT||event==WM_LBUTTONUP)a->EnterWorkspace();return 0;}
    if(m==WM_TIMER&&w==TIMER_TRAY_MENU){KillTimer(h,TIMER_TRAY_MENU);a->trayMenuPending_=false;a->SchedulePanelSync();return 0;}
    if(m==WM_APP+84){a->fileSettings.Refresh();a->launcher.FilesChanged();static bool previousBusy=false;bool busy=a->fileIndex.Status().busy;if(busy!=previousBusy){previousBusy=busy;for(auto& bar:a->bars)bar->Invalidate();for(auto& tool:a->tools)tool->Invalidate();}return 0;}if(m==WM_TIMER&&w==TIMER_SECONDS){a->SyncSeconds();for(auto& board:a->boards)board->TickSeconds(true);return 0;}if(m==WM_TIMER&&w==TIMER_SYSTEM){a->SampleSystemWidget();return 0;}
    if(m==WM_TIMER&&w==24){if(a->winKey.ReleasePending()||++a->winReleaseRetries_>=10)KillTimer(h,24);return 0;}
    if(m==WM_TIMER&&w>=TIMER_SHELL_SETTLE&&w<=TIMER_SHELL_SETTLE_LAST){KillTimer(h,w);if(a->config.enabled&&!a->preview&&!a->systemPrompt_){a->chrome.Refresh();if(w<TIMER_SHELL_SETTLE_LAST)SetTimer(h,w+1,200+static_cast<UINT>(w-TIMER_SHELL_SETTLE)*150,nullptr);}return 0;}
    if(m==WM_TIMER){if(w==TIMER_PROGRAM){a->TryActivateProgram();}else if(w==TIMER_PANEL_GEOMETRY){KillTimer(h,TIMER_PANEL_GEOMETRY);a->SchedulePanelSync();}else if(w==TIMER_SAVE){KillTimer(h,TIMER_SAVE);a->Save();}else if(w==TIMER_CLOCK){++a->clockTicks;a->RefreshWindows();a->Refresh();if(a->config.enabled)a->chrome.Refresh();}else if(w==TIMER_LAYOUT){KillTimer(h,TIMER_LAYOUT);if(GetTickCount64()-a->lastTile_>350){a->lastTile_=GetTickCount64();a->windows.Tile(a->monitors,76);}}else if(w==TIMER_DISPLAYS){if(a->systemPrompt_)return 0;KillTimer(h,TIMER_DISPLAYS);a->windows.Capture();a->UpdateMonitors();a->ApplyState(true);if(a->config.enabled)a->chrome.Refresh();}return 0;}
    if(m==WM_PANEL_SYNC){a->panelSyncPending_=false;a->SyncPanels();return 0;}if(m==WM_WINDOW_EVENT){a->HandleWindowEvent(static_cast<DWORD>(w),reinterpret_cast<HWND>(l));return 0;}if(m==WM_APP+30){a->ShowOverview();return 0;}if(m==WM_APP+31){a->Quit();return 0;}if(m==WM_APP+33){a->launcher.Show();return 0;}if(m==WM_APP+35){a->notificationTray.WriteDiagnostics(a->dataDir/L"tray-diagnostics.json");return 0;}if(m==WM_APP+34){a->customization.Show(a->boards.empty()?nullptr:a->boards.front()->hwnd);return 0;}if(m==WM_APP+32){a->Pause();std::error_code error;fs::remove(a->dataDir/L"rescue.flag",error);return 0;}
    if(m==WM_DEVICECHANGE&&a->config.files.enabled&&a->config.files.allFixedDrives){a->ConfigureFiles();}if(m==WM_DISPLAYCHANGE){SetTimer(h,TIMER_DISPLAYS,250,nullptr);return 0;}if(m==WM_QUERYENDSESSION){a->windows.Capture();a->Save();return TRUE;}if(m==WM_ENDSESSION&&w){a->Quit();return 0;}return DefWindowProcW(h,m,w,l);
}

}
