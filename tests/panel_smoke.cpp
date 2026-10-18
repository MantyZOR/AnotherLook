#include "ui.h"
#include <algorithm>
#include <shellapi.h>
#include <commctrl.h>

namespace gv {
int RunPanelProbe(const fs::path& directory){
    // An isolated profile and only our own windows; never hide Explorer or adopt apps.
    if(fs::exists(directory/L"settings.json")||fs::exists(directory/L"shell-original.json"))return 2;
    Application app(directory,true);app.monitors=GetMonitors();if(app.monitors.empty())return 2;
    app.config=DefaultConfig();app.config.enabled=true;app.preview=false;
    WNDCLASSW eventsClass{};eventsClass.hInstance=GetModuleHandleW(nullptr);eventsClass.lpfnWndProc=Application::EventProc;eventsClass.lpszClassName=L"GoodView.PanelProbeEvents";RegisterClassW(&eventsClass);
    app.events=CreateWindowExW(0,eventsClass.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,eventsClass.hInstance,&app);if(!app.events)return 2;
    json::JsonArray checks;bool passed=true;
    auto check=[&](bool ok,const wchar_t* name){json::JsonObject item;item.SetNamedValue(L"name",json::JsonValue::CreateStringValue(name));item.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(ok));if(!ok){json::JsonArray states;for(auto* list:{&app.bars,&app.rails,&app.tools,&app.boards})for(auto& surface:*list){json::JsonObject state;state.SetNamedValue(L"bar",json::JsonValue::CreateBooleanValue(surface->bar));state.SetNamedValue(L"rail",json::JsonValue::CreateBooleanValue(surface->rail));state.SetNamedValue(L"monitor",json::JsonValue::CreateNumberValue(surface->monitor));state.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(IsWindowVisible(surface->hwnd)!=FALSE));state.SetNamedValue(L"topmost",json::JsonValue::CreateBooleanValue((GetWindowLongPtrW(surface->hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0));states.Append(state);}item.SetNamedValue(L"surfaces",states);}checks.Append(item);passed&=ok;};
    auto topmost=[](HWND window){return (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;};
    auto injectTopmost=[](HWND window){SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);};
    auto above=[](HWND upper,HWND lower){for(HWND window=GetTopWindow(nullptr);window;window=GetWindow(window,GW_HWNDNEXT)){if(window==upper)return true;if(window==lower)return false;}return false;};
    auto wait=[](const std::function<bool()>& ready){auto until=GetTickCount64()+500;while(!ready()&&GetTickCount64()<until){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(2);}return ready();};
    auto pump=[](){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}};
    for(size_t i=0;i<app.monitors.size();++i){
        auto board=std::make_unique<Surface>(app,false,static_cast<int>(i));board->Create(false);board->Show(true);app.boards.push_back(std::move(board));
        auto bar=std::make_unique<Surface>(app,true,static_cast<int>(i));bar->Create(true);bar->Show(true);app.bars.push_back(std::move(bar));
        auto rail=std::make_unique<Surface>(app,false,static_cast<int>(i),true);rail->Create(true);rail->Show(true);app.rails.push_back(std::move(rail));
        for(auto kind:{UiElement::Search,UiElement::Settings}){auto tool=std::make_unique<Surface>(app,true,static_cast<int>(i),false,kind);tool->Create(true);tool->Show(true);app.tools.push_back(std::move(tool));}
    }
    // A synthetic test has no user's click to grant foreground permission.
    // Activate its own fixture once before testing normal canvas transitions.
    DWORD initialForeground=0;GetWindowThreadProcessId(GetForegroundWindow(),&initialForeground);
    if(initialForeground!=GetCurrentProcessId())SwitchToThisWindow(app.boards.front()->hwnd,TRUE);
    wait([&]{DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();});
    app.ShowDesktop();HWND primary=app.bars.front()->hwnd;
    check(app.tools.size()==app.monitors.size()*2&&std::all_of(app.tools.begin(),app.tools.end(),[&](auto& tool){return GetWindow(tool->hwnd,GW_OWNER)==app.boards[tool->monitor]->hwnd;}),L"search and settings are separate owned native modules on every monitor");
    std::vector<json::JsonObject> positionTrace;
    auto traceProc=+[](HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{
        auto result=DefSubclassProc(window,message,w,l);
        if(message==WM_WINDOWPOSCHANGING||message==WM_WINDOWPOSCHANGED){auto* position=reinterpret_cast<WINDOWPOS*>(l);auto& trace=*reinterpret_cast<std::vector<json::JsonObject>*>(data);json::JsonObject entry;entry.SetNamedValue(L"message",json::JsonValue::CreateNumberValue(message));entry.SetNamedValue(L"after",json::JsonValue::CreateStringValue(std::to_wstring(reinterpret_cast<INT_PTR>(position->hwndInsertAfter))));entry.SetNamedValue(L"flags",json::JsonValue::CreateNumberValue(position->flags));entry.SetNamedValue(L"topmost",json::JsonValue::CreateBooleanValue((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0));wchar_t afterClass[128]{};GetClassNameW(position->hwndInsertAfter,afterClass,128);entry.SetNamedValue(L"afterClass",json::JsonValue::CreateStringValue(afterClass));entry.SetNamedValue(L"afterIsSelf",json::JsonValue::CreateBooleanValue(position->hwndInsertAfter==window));entry.SetNamedValue(L"afterOwnedBySelf",json::JsonValue::CreateBooleanValue(GetWindow(position->hwndInsertAfter,GW_OWNER)==window));trace.push_back(entry);if(trace.size()>32)trace.erase(trace.begin());}return result;
    };
    SetWindowSubclass(primary,traceProc,1,reinterpret_cast<DWORD_PTR>(&positionTrace));
    check(std::all_of(app.boards.begin(),app.boards.end(),[](const auto& surface){return GetPropW(surface->hwnd,L"NonRudeHWND")!=nullptr;}),L"our desktop canvases opt out of Windows fullscreen heuristics before showing");
    check(IsWindowVisible(primary)&&!topmost(primary)&&GetWindow(primary,GW_OWNER)==app.boards[0]->hwnd,L"primary navigation is a normal owned panel of its desktop");
    injectTopmost(primary);check(IsWindowVisible(primary)&&topmost(primary),L"reproduce an accidental topmost panel without hiding it");
    app.bars.front()->Show(true);check(IsWindowVisible(primary)&&!topmost(primary),L"showing a panel repairs its layer without a visibility toggle");
    injectTopmost(primary);check(wait([&]{return IsWindowVisible(primary)&&!topmost(primary);}),L"unexpected topmost styling is repaired through the event queue");
    SendMessageW(primary,WM_SYSCOMMAND,SC_MINIMIZE,0);check(!IsIconic(primary),L"system minimize commands cannot remove shell navigation");
    app.bars.front()->Show(false);injectTopmost(primary);app.bars.front()->Show(true);
    check(IsWindowVisible(primary)&&!topmost(primary),L"showing hidden navigation restores the normal desktop layer");
    injectTopmost(primary);injectTopmost(app.rails.front()->hwnd);app.ShowDesktop();
    check(wait([&]{return IsWindowVisible(primary)&&!topmost(primary)&&!topmost(app.rails.front()->hwnd);}),L"return to canvas recovers both panels without using another monitor");
    check(above(primary,app.boards.front()->hwnd),L"primary panel is above the opaque desktop, not merely marked visible");
    for(auto& bar:app.bars)SendMessageW(bar->hwnd,WM_APP+10,ABN_FULLSCREENAPP,TRUE);
    check(IsWindowVisible(primary)&&!topmost(primary),L"stale fullscreen notices preserve the normal navigation layer");
    app.HandleWindowEvent(EVENT_SYSTEM_FOREGROUND,GetDesktopWindow());
    check(IsWindowVisible(primary)&&!topmost(primary),L"queued foreground event uses current focus rather than its stale window handle");
    for(int i=0;i<24;++i){app.ShowDesktop();app.SyncPanels();}
    check(wait([&]{return IsWindowVisible(primary)&&!topmost(primary)&&above(primary,app.boards.front()->hwnd);}),L"repeated canvas transitions keep navigation above its owner without topmost cycling");
    bool secondary=true;for(size_t i=1;i<app.bars.size();++i)secondary&=IsWindowVisible(app.bars[i]->hwnd)&&!topmost(app.bars[i]->hwnd)&&above(app.bars[i]->hwnd,app.boards[i]->hwnd);
    check(secondary,L"primary recovery preserves panels on other monitors");
    app.notificationTray.Start(true);app.SyncPanels();pump();
    struct Traffic {unsigned positions=0,paints=0,frames=0,shows=0,bands=0;} traffic;
    auto trafficProc=+[](HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{
        auto& traffic=*reinterpret_cast<Traffic*>(data);if(message==WM_WINDOWPOSCHANGING)++traffic.positions;if(message==WM_PAINT)++traffic.paints;if(message==WM_NCPAINT)++traffic.frames;if(message==WM_SHOWWINDOW)++traffic.shows;if(message==WM_STYLECHANGED&&static_cast<int>(w)==GWL_EXSTYLE){auto change=reinterpret_cast<STYLESTRUCT*>(l);if((change->styleOld^change->styleNew)&WS_EX_TOPMOST)++traffic.bands;}return DefSubclassProc(window,message,w,l);
    };
    std::vector<HWND> observed;for(auto* list:{&app.bars,&app.rails,&app.tools})for(auto& surface:*list)observed.push_back(surface->hwnd);observed.push_back(app.notificationTray.Window());
    for(HWND window:observed){UpdateWindow(window);SetWindowSubclass(window,trafficProc,2,reinterpret_cast<DWORD_PTR>(&traffic));}
    traffic={};for(int i=0;i<32;++i){app.SyncPanels();for(HWND window:observed)UpdateWindow(window);}
    check(!traffic.positions&&!traffic.frames&&!traffic.shows,L"settled foreground synchronization does not reposition reframe or reshow any panel");
    check(!traffic.paints,L"settled foreground synchronization does not repaint navigation rail or tray");
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION fixtureProcess{};auto fixtureCommand=L"\""+ExePath().wstring()+L"\" --fixture --fixture-parent "+std::to_wstring(GetCurrentProcessId());
    bool started=CreateProcessW(nullptr,fixtureCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&fixtureProcess)!=FALSE;
    check(started,L"start an isolated foreign application for overlap regression");
    if(started){
        CloseHandle(fixtureProcess.hThread);HWND fixture=nullptr;auto until=GetTickCount64()+2500;
        while(!fixture&&GetTickCount64()<until){for(auto& window:FindProgramWindows(ResolveProgramTarget(ExePath().wstring())))if(window.pid==fixtureProcess.dwProcessId&&GetPropW(window.hwnd,L"GoodView.FixtureReady"))fixture=window.hwnd;if(!fixture){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}}
        if(fixture){
            auto bounds=app.monitors[0].bounds;SetWindowPos(fixture,HWND_TOP,bounds.left,bounds.top,bounds.right-bounds.left-2,bounds.bottom-bounds.top-2,SWP_SHOWWINDOW);FocusAppWindow(fixture);
            check(wait([&]{return GetForegroundWindow()==fixture;}),L"foreign framed app becomes active for overlap regression");app.SyncPanels();
            check(IsWindowVisible(primary)&&!topmost(primary)&&!topmost(app.rails[0]->hwnd)&&above(fixture,primary)&&above(fixture,app.rails[0]->hwnd),L"active normal app is above navigation and open-window panels");
            check(std::all_of(app.tools.begin(),app.tools.end(),[&](auto& tool){return !topmost(tool->hwnd)&&above(fixture,tool->hwnd);}),L"separate search and settings yield to a foreground application");
            RECT panelRect;GetWindowRect(primary,&panelRect);POINT point{(panelRect.left+panelRect.right)/2,(panelRect.top+panelRect.bottom)/2};HWND hit=WindowFromPoint(point);check(hit==fixture||IsChild(fixture,hit),L"the app can receive mouse input where it overlaps a shell panel");
            app.notificationTray.Start(true);app.SyncPanels();check(!topmost(app.notificationTray.Window())&&above(fixture,app.notificationTray.Window()),L"custom tray also yields to the active application");
            app.HandleWindowEvent(EVENT_SYSTEM_FOREGROUND,app.boards[0]->hwnd);app.SyncPanels();check(!topmost(primary)&&above(fixture,primary),L"a stale shell event cannot lift panels over the current app");
            pump();for(HWND window:observed)UpdateWindow(window);traffic={};for(int i=0;i<32;++i){app.SyncPanels();for(HWND window:observed)UpdateWindow(window);}
            check(!traffic.positions&&!traffic.frames&&!traffic.shows&&!traffic.paints,L"settled external focus does not reshuffle or redraw panels behind the application");
            STARTUPINFOW secondStartup{sizeof(secondStartup)};PROCESS_INFORMATION secondProcess{};bool secondStarted=CreateProcessW(nullptr,fixtureCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&secondStartup,&secondProcess)!=FALSE;
            check(secondStarted,L"start a second isolated app to check stable icon slots");
            if(secondStarted){
                CloseHandle(secondProcess.hThread);HWND second=nullptr;auto end=GetTickCount64()+2500;
                while(!second&&GetTickCount64()<end){for(const auto& window:FindProgramWindows(ResolveProgramTarget(ExePath().wstring())))if(window.pid==secondProcess.dwProcessId&&GetPropW(window.hwnd,L"GoodView.FixtureReady"))second=window.hwnd;if(!second){pump();Sleep(10);}}
                if(second){
                    SetWindowPos(fixture,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);app.RefreshWindows();
                    auto slots=[&](){std::vector<HWND> result;for(const auto& info:app.openWindows)if(info.hwnd==fixture||info.hwnd==second)result.push_back(info.hwnd);return result;};auto before=slots();
                    SetWindowPos(second,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);app.RefreshWindows();
                    check(before.size()==2&&slots()==before,L"changing native application Z order preserves the slots of both existing icons");
                    SetWindowPos(fixture,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);app.RefreshWindows();check(slots()==before,L"reversing native application Z order preserves icon slots again");
                    PostMessageW(second,WM_CLOSE,0,0);
                }else check(false,L"second application has a discoverable window");
                if(WaitForSingleObject(secondProcess.hProcess,2000)==WAIT_TIMEOUT){TerminateProcess(secondProcess.hProcess,0);WaitForSingleObject(secondProcess.hProcess,1000);}CloseHandle(secondProcess.hProcess);
            }
            FocusAppWindow(fixture);wait([&]{return GetForegroundWindow()==fixture;});app.SyncPanels();pump();for(HWND window:observed)UpdateWindow(window);traffic={};app.ShowDesktop();pump();
            check(!traffic.bands&&!traffic.shows,L"real application-to-desktop return never toggles panel visibility or topmost bands");
            check(GetWindow(app.notificationTray.Window(),GW_OWNER)==app.boards[0]->hwnd&&std::all_of(app.rails.begin(),app.rails.end(),[&](const auto& rail){return GetWindow(rail->hwnd,GW_OWNER)==app.boards[rail->monitor]->hwnd;}),L"tray and task rails share the correct desktop owner on every monitor");
            PostMessageW(fixture,WM_CLOSE,0,0);
        }else check(false,L"foreign fixture has a discoverable window");
        if(WaitForSingleObject(fixtureProcess.hProcess,2000)==WAIT_TIMEOUT){TerminateProcess(fixtureProcess.hProcess,0);WaitForSingleObject(fixtureProcess.hProcess,1000);}CloseHandle(fixtureProcess.hProcess);
    }
    app.ShowDesktop();check(wait([&]{return !topmost(primary)&&above(primary,app.boards[0]->hwnd);}),L"returning to desktop restores navigation without entering the topmost band");
    app.HandleWindowEvent(EVENT_SYSTEM_MENUPOPUPSTART,primary);app.SyncPanels();check(!topmost(primary),L"a native popup session keeps shell panels below context menus");
    app.HandleWindowEvent(EVENT_SYSTEM_MENUPOPUPEND,primary);app.SyncPanels();check(!topmost(primary)&&above(primary,app.boards[0]->hwnd),L"closing a menu preserves normal desktop owner order");
    for(HWND window:observed)RemoveWindowSubclass(window,trafficProc,2);app.notificationTray.Stop();
    RemoveWindowSubclass(primary,traceProc,1);
    for(auto& surface:app.tools)surface->Show(false);for(auto& surface:app.bars)surface->Show(false);for(auto& surface:app.rails)surface->Show(false);for(auto& surface:app.boards)surface->Show(false);
    json::JsonObject report;report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed));report.SetNamedValue(L"checks",checks);report.SetNamedValue(L"monitorCount",json::JsonValue::CreateNumberValue(static_cast<double>(app.monitors.size())));
    if(!passed){json::JsonArray trace;for(auto& entry:positionTrace)trace.Append(entry);report.SetNamedValue(L"positionTrace",trace);}
    if(!WriteAtomic(directory/L"panel-smoke.json",std::wstring(report.Stringify())))return 2;return passed?0:1;
}
}
