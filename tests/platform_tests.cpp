#include "platform.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <propkey.h>
using namespace gv;
namespace {
int checks=0;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
struct Child {HANDLE process{};DWORD pid{};Child()=default;Child(const Child&)=delete;Child(Child&&o)noexcept:process(o.process),pid(o.pid){o.process=nullptr;}~Child(){if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,0);WaitForSingleObject(process,2000);CloseHandle(process);}}};
Child Start(std::wstring command){STARTUPINFOW s{sizeof(s)};s.dwFlags=STARTF_USESHOWWINDOW;s.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION p{};Check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&s,&p)!=FALSE,"launch test child");CloseHandle(p.hThread);Child child;child.process=p.hProcess;child.pid=p.dwProcessId;return child;}
bool Wait(const std::function<bool()>&predicate,int ms=5000){auto until=GetTickCount64()+ms;do{if(predicate())return true;Sleep(20);}while(GetTickCount64()<until);return predicate();}
HWND FixtureWindow(DWORD pid){HWND found=nullptr;struct Query{DWORD pid;HWND*result;}q{pid,&found};EnumWindows([](HWND h,LPARAM p)->BOOL{auto*q=reinterpret_cast<Query*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t name[128]{};GetClassNameW(h,name,128);std::wstring_view cls=name;if(pid==q->pid&&(cls==L"GoodView.Fixture"||cls==L"Qt51519TrayIconMessageWindowClass"||cls==L"Qt680dGoodViewQWindowIcon")&&GetPropW(h,L"GoodView.FixtureReady")){*q->result=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&q));return found;}
}
int wmain(int argc,wchar_t**argv){try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    if(argc>=6&&std::wstring_view(argv[1])==L"--crash-owner"){
      HWND fixture=reinterpret_cast<HWND>(std::stoull(argv[2]));fs::path dir=argv[3],app=argv[4];Config c=DefaultConfig();WindowManager manager(dir,c);
      if(!manager.Attach(fixture,L"work"))return 3;
      ShellChrome chrome(dir);if(!chrome.Begin({fixture}))return 4;
      auto guardian=Start(L"\""+app.wstring()+L"\" --guardian "+std::to_wstring(GetCurrentProcessId())+L" --data \""+dir.wstring()+L"\"");
      manager.Switch(L"play");WriteAtomic(dir/L"ready",L"ready");Sleep(INFINITE);return 0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--inspect-target"){
      auto target=ResolveProgramTarget(argv[2]);auto matches=FindProgramWindows(target);json::JsonArray windows;
      for(const auto& window:matches){json::JsonObject item;item.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(window.pid));item.SetNamedValue(L"title",json::JsonValue::CreateStringValue(window.title));item.SetNamedValue(L"class",json::JsonValue::CreateStringValue(window.className));item.SetNamedValue(L"imageName",json::JsonValue::CreateStringValue(window.imageName));item.SetNamedValue(L"pathReadable",json::JsonValue::CreateBooleanValue(!window.process.empty()));
        item.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(IsWindowVisible(window.hwnd)!=FALSE));item.SetNamedValue(L"enabled",json::JsonValue::CreateBooleanValue(IsWindowEnabled(window.hwnd)!=FALSE));item.SetNamedValue(L"minimized",json::JsonValue::CreateBooleanValue(IsIconic(window.hwnd)!=FALSE));item.SetNamedValue(L"style",json::JsonValue::CreateNumberValue(static_cast<DWORD>(GetWindowLongPtrW(window.hwnd,GWL_STYLE))));item.SetNamedValue(L"extendedStyle",json::JsonValue::CreateNumberValue(static_cast<DWORD>(GetWindowLongPtrW(window.hwnd,GWL_EXSTYLE))));
        RECT bounds{};GetWindowRect(window.hwnd,&bounds);json::JsonArray rectangle;for(auto value:{bounds.left,bounds.top,bounds.right,bounds.bottom})rectangle.Append(json::JsonValue::CreateNumberValue(value));item.SetNamedValue(L"bounds",rectangle);HWND owner=GetWindow(window.hwnd,GW_OWNER),popup=GetLastActivePopup(window.hwnd);item.SetNamedValue(L"hasOwner",json::JsonValue::CreateBooleanValue(owner!=nullptr));item.SetNamedValue(L"ownerVisible",json::JsonValue::CreateBooleanValue(IsWindowVisible(owner)!=FALSE));item.SetNamedValue(L"hasActivePopup",json::JsonValue::CreateBooleanValue(popup&&popup!=window.hwnd));item.SetNamedValue(L"popupVisible",json::JsonValue::CreateBooleanValue(IsWindowVisible(popup)!=FALSE));windows.Append(item);}
      std::cout<<Utf8(std::wstring(windows.Stringify()))<<"\n";return matches.empty()?1:0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--inspect-window-class"){
      struct Query {std::wstring_view cls;json::JsonArray windows;} query{argv[2],{}};
      EnumWindows([](HWND window,LPARAM data)->BOOL{auto& query=*reinterpret_cast<Query*>(data);wchar_t cls[256]{};GetClassNameW(window,cls,256);if(query.cls!=cls)return TRUE;DWORD pid=0;GetWindowThreadProcessId(window,&pid);json::JsonObject item;item.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(pid));item.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(IsWindowVisible(window)!=FALSE));item.SetNamedValue(L"class",json::JsonValue::CreateStringValue(cls));query.windows.Append(item);return TRUE;},reinterpret_cast<LPARAM>(&query));
      std::cout<<Utf8(std::wstring(query.windows.Stringify()))<<"\n";return 0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--inspect-shell"){
      std::wstring property;if(auto raw=ReadUtf8(fs::path(argv[2])/L"shell-original.json"))try{property=std::wstring(json::JsonObject::Parse(*raw).GetNamedString(L"property",L""));}catch(...){}
      json::JsonArray windows;for(HWND window:ShellChrome::Discover()){wchar_t cls[128]{};GetClassNameW(window,cls,128);DWORD pid=0;GetWindowThreadProcessId(window,&pid);RECT rect{};GetWindowRect(window,&rect);MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);json::JsonObject item;item.SetNamedValue(L"class",json::JsonValue::CreateStringValue(cls));item.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(pid));item.SetNamedValue(L"hwnd",json::JsonValue::CreateStringValue(std::to_wstring(reinterpret_cast<ULONG_PTR>(window))));item.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(IsWindowVisible(window)!=FALSE));item.SetNamedValue(L"visibleStyle",json::JsonValue::CreateBooleanValue((GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0));item.SetNamedValue(L"journalMarker",json::JsonValue::CreateBooleanValue(!property.empty()&&GetPropW(window,property.c_str())));item.SetNamedValue(L"rootIsSelf",json::JsonValue::CreateBooleanValue(GetAncestor(window,GA_ROOT)==window));item.SetNamedValue(L"monitor",json::JsonValue::CreateStringValue(monitor.szDevice));json::JsonArray bounds;for(LONG value:{rect.left,rect.top,rect.right,rect.bottom})bounds.Append(json::JsonValue::CreateNumberValue(value));item.SetNamedValue(L"bounds",bounds);windows.Append(item);}std::cout<<Utf8(std::wstring(windows.Stringify()))<<"\n";return 0;
    }
    // The fixture grants its own test driver foreground permission on activation.
    // This keeps setup independent of whichever terminal launched CTest.
    Check(argc>=2,"application path provided");fs::path app=argv[1],dir=fs::temp_directory_path()/(L"GoodView-platform-"+NewId());fs::create_directories(dir);auto fixture=Start(L"\""+app.wstring()+L"\" --fixture --fixture-parent "+std::to_wstring(GetCurrentProcessId()));HWND hwnd=nullptr;
    Check(Wait([&]{hwnd=FixtureWindow(fixture.pid);return hwnd&&IsWindowVisible(hwnd);}),"fixture window visible");RECT original{};GetWindowRect(hwnd,&original);
    {
      auto target=ResolveProgramTarget(app.wstring());auto matches=FindProgramWindows(target);Check(std::any_of(matches.begin(),matches.end(),[&](const auto& window){return window.hwnd==hwnd;}),"find already running application by resolved executable");
      WindowInfo limited;limited.imageName=target.imageName;Check(MatchesProgram(limited,target),"elevated window can match by image name when full path is unreadable");limited.process=(dir/app.filename()).wstring();Check(!MatchesProgram(limited,target),"readable different installation does not match by filename alone");
      limited.appId=L"Example.App!Main";ProgramTarget packaged;packaged.appId=L"example.app!main";Check(MatchesProgram(limited,packaged),"packaged app identity comparison");
      auto shortcut=dir/L"fixture.lnk";ComPtr<IShellLinkW> link;ComPtr<IPersistFile> persisted;
      Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"create fixture shortcut");link->SetPath(app.c_str());link->SetArguments(L"--fixture");link->SetWorkingDirectory(dir.c_str());Check(SUCCEEDED(link.As(&persisted))&&SUCCEEDED(persisted->Save(shortcut.c_str(),TRUE)),"save fixture shortcut");
      auto resolved=ResolveProgramTarget(shortcut.wstring(),L"extra");Check(fs::equivalent(resolved.executable,app)&&resolved.arguments==L"--fixture extra"&&fs::equivalent(resolved.workingDirectory,dir),"shortcut resolves executable arguments and working directory");
      auto url=dir/L"fixture.url";Check(WriteAtomic(url,L"[InternetShortcut]\r\nURL=https://example.org/fixture\r\n"),"write fixture internet shortcut");Check(ResolveProgramTarget(url.wstring()).url==L"https://example.org/fixture","internet shortcut resolves as a site");
      ShowWindowAsync(hwnd,SW_MAXIMIZE);Check(Wait([&]{return IsZoomed(hwnd)!=FALSE;}),"fixture maximized before activation");FocusAppWindow(hwnd);Check(IsZoomed(hwnd)!=FALSE,"activating an already maximized window preserves its size");
      ShowWindowAsync(hwnd,SW_MINIMIZE);Check(Wait([&]{return IsIconic(hwnd)!=FALSE;}),"fixture minimized before activation");FocusAppWindow(hwnd);Check(Wait([&]{return !IsIconic(hwnd)&&IsWindowVisible(hwnd);}),"activation restores a minimized application");
      ShowWindowAsync(hwnd,SW_HIDE);Check(Wait([&]{return !IsWindowVisible(hwnd);}),"fixture hidden like a tray application");matches=FindProgramWindows(target);Check(std::any_of(matches.begin(),matches.end(),[&](const auto& window){return window.hwnd==hwnd;}),"hidden application remains discoverable for activation");FocusAppWindow(hwnd);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"activation shows an existing hidden application");
      ShowWindowAsync(hwnd,SW_RESTORE);SetWindowPos(hwnd,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);Check(Wait([&]{RECT rect;GetWindowRect(hwnd,&rect);return EqualRect(&rect,&original)!=FALSE;}),"fixture restored before lifecycle tests");fs::remove(shortcut);fs::remove(url);
    }
    {
      auto owned=Start(L"\""+app.wstring()+L"\" --fixture --fixture-owned");HWND ownedWindow=nullptr;
      Check(Wait([&]{ownedWindow=FixtureWindow(owned.pid);return ownedWindow&&IsWindowVisible(ownedWindow);}),"utility-style fixture with hidden owner is visible");
      Check(GetWindow(ownedWindow,GW_OWNER)&&!IsWindowVisible(GetWindow(ownedWindow,GW_OWNER)),"utility fixture really has a hidden owner window");
      auto available=ListAppWindows();Check(std::any_of(available.begin(),available.end(),[&](const auto& window){return window.hwnd==ownedWindow;}),"a real utility window with a hidden owner is listed among applications");
      auto matches=FindProgramWindows(ResolveProgramTarget(app.wstring()),owned.pid);Check(std::any_of(matches.begin(),matches.end(),[&](const auto& window){return window.hwnd==ownedWindow;}),"program activation discovers an owned main window");
      PostMessageW(ownedWindow,WM_CLOSE,0,0);Check(WaitForSingleObject(owned.process,2000)==WAIT_OBJECT_0,"owned fixture exits cleanly");
    }
    {
      auto helper=Start(L"\""+app.wstring()+L"\" --fixture --fixture-tiny");HWND helperWindow=nullptr;
      Check(Wait([&]{helperWindow=FixtureWindow(helper.pid);if(!helperWindow)return false;RECT rect;GetWindowRect(helperWindow,&rect);return rect.right-rect.left==2&&rect.bottom-rect.top==2;}),"reproduce a visible titled 2x2 framework helper");
      auto visible=ListAppWindows();Check(std::none_of(visible.begin(),visible.end(),[&](const auto& window){return window.hwnd==helperWindow;}),"tiny service window is absent from the task rail");
      auto all=ListAppWindows(true);Check(std::none_of(all.begin(),all.end(),[&](const auto& window){return window.hwnd==helperWindow;}),"launch discovery does not confuse a helper for an app window");PostMessageW(helperWindow,WM_CLOSE,0,0);Check(WaitForSingleObject(helper.process,2000)==WAIT_OBJECT_0,"helper fixture exits cleanly");
    }
    {
      auto helper=Start(L"\""+app.wstring()+L"\" --fixture-tray-helper");HWND receiver=nullptr;
      Check(Wait([&]{receiver=FixtureWindow(helper.pid);return receiver&&IsWindowVisible(receiver);}),"reproduce a full-sized visible Qt tray receiver");
      auto all=ListAppWindows(true);Check(std::none_of(all.begin(),all.end(),[&](const auto& info){return info.hwnd==receiver;}),"visible Qt tray receiver is excluded from launch discovery");
      auto available=ListAppWindows();Check(std::none_of(available.begin(),available.end(),[&](const auto& info){return info.hwnd==receiver;}),"visible Qt tray receiver is excluded from the task rail");
      auto target=ResolveProgramTarget(app.wstring());auto matches=FindProgramWindows(target,helper.pid);Check(std::none_of(matches.begin(),matches.end(),[&](const auto& info){return info.hwnd==receiver;}),"matching executable and launch PID cannot select a tray receiver");
      Config repair=DefaultConfig();WindowManager manager(dir,repair);Check(!manager.Attach(receiver,L"work"),"tray receivers cannot be attached or tiled");
      ShowWindowAsync(receiver,SW_HIDE);Check(Wait([&]{return !IsWindowVisible(receiver);}),"hide tray receiver before explicit activation regression");
      Check(!FocusAppWindow(receiver)&&!IsWindowVisible(receiver),"explicit activation never reveals a Qt tray receiver");
      ShowWindowAsync(receiver,SW_SHOWNOACTIVATE);Check(Wait([&]{return IsWindowVisible(receiver)!=FALSE;}),"reproduce a previously saved accidental receiver frame");
      SavedWindow saved;saved.process=(dir/L"unrelated.exe").wstring();saved.className=L"Qt51519TrayIconMessageWindowClass";repair.scenes[1].windows.push_back(saved);
      Check(manager.RepairSavedHelpers()&&IsWindowVisible(receiver),"repair never hides a receiver belonging to a different executable");
      saved.process=fs::absolute(app).make_preferred().wstring();repair.scenes[1].windows.push_back(saved);SavedWindow mainRule;mainRule.process=app.wstring();mainRule.className=L"GoodView.Fixture";repair.scenes[1].windows.push_back(mainRule);
      Check(manager.RepairSavedHelpers()&&Wait([&]{return !IsWindowVisible(receiver);}),"repair hides the saved receiver without destroying it");
      Check(IsWindow(receiver)&&WaitForSingleObject(helper.process,0)==WAIT_TIMEOUT,"the background application and callback receiver stay alive");
      Check(repair.scenes[1].windows.size()==1&&repair.scenes[1].windows[0].className==L"GoodView.Fixture","repair removes only internal tray window rules");
      Check(!manager.RepairSavedHelpers(),"saved receiver cleanup is idempotent");
      PostMessageW(receiver,WM_CLOSE,0,0);Check(WaitForSingleObject(helper.process,2000)==WAIT_OBJECT_0,"Qt receiver fixture exits cleanly");
      auto qt=Start(L"\""+app.wstring()+L"\" --fixture-qt --fixture-parent "+std::to_wstring(GetCurrentProcessId()));HWND mainWindow=nullptr;
      Check(Wait([&]{mainWindow=FixtureWindow(qt.pid);return mainWindow&&IsWindowVisible(mainWindow);}),"real Qt-style application window is visible");
      matches=FindProgramWindows(target,qt.pid);Check(std::any_of(matches.begin(),matches.end(),[&](const auto& info){return info.hwnd==mainWindow;}),"a real Qt window is supported even with the same title as a tray receiver");
      ShowWindowAsync(mainWindow,SW_HIDE);Check(Wait([&]{return !IsWindowVisible(mainWindow);}),"hide real Qt window to model a background application");
      matches=FindProgramWindows(target,qt.pid);Check(std::any_of(matches.begin(),matches.end(),[&](const auto& info){return info.hwnd==mainWindow;}),"hidden main Qt windows remain discoverable");
      FocusAppWindow(mainWindow);Check(Wait([&]{return IsWindowVisible(mainWindow)!=FALSE;}),"hidden main Qt windows still restore instead of being excluded as helpers");
      PostMessageW(mainWindow,WM_CLOSE,0,0);Check(WaitForSingleObject(qt.process,2000)==WAIT_OBJECT_0,"real Qt fixture exits cleanly");
    }
    Config c=DefaultConfig();{
      WindowManager manager(dir,c);Check(manager.Attach(hwnd,L"work"),"attach external fixture");Check(manager.IsManaged(hwnd),"managed identity token");Check(fs::exists(dir/L"recovery.json"),"journal written before hiding");
      manager.Switch(L"play");Check(Wait([&]{return !IsWindowVisible(hwnd);}),"inactive scene hides managed window");manager.Switch(L"work");Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"active scene shows managed window");
      manager.Capture();Check(c.scenes[1].windows.size()==1,"window rule persisted");Check(fs::equivalent(c.scenes[1].windows.front().process,app),"correct executable identity");manager.ToggleFloating(hwnd);manager.Capture();Check(c.scenes[1].windows.front().floating,"floating preference persisted");
      manager.Switch(L"play");Check(Wait([&]{return !IsWindowVisible(hwnd);}),"hidden before simulated recovery");RecoverSession(dir);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"recovery restores visibility");Check(!manager.IsManaged(hwnd),"recovery removes ownership token");Check(!fs::exists(dir/L"recovery.json"),"recovery clears journal");
    }
    {
      WindowManager manager(dir,c);Check(manager.Attach(hwnd,L"work"),"reattach after recovery");manager.Switch(L"play");Check(Wait([&]{return !IsWindowVisible(hwnd);}),"hide before regular shutdown");manager.RestoreAll();Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"regular shutdown restores window");RECT after{};GetWindowRect(hwnd,&after);Check(EqualRect(&original,&after)!=FALSE,"original geometry restored");
    }
    {
      WindowManager manager(dir,c);Check(manager.Attach(hwnd,L"work"),"attach for cross-scene move");manager.Capture();manager.MoveToScene(hwnd,L"web");Check(c.scenes[1].windows.empty()&&c.scenes[2].windows.size()==1,"moving removes stale source rule");manager.Detach(hwnd);Check(c.scenes[2].windows.empty(),"detaching removes persistent rule");Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"detaching shows hidden window");
    }
    {
      ShellChrome chrome(dir);Check(chrome.Begin({hwnd}),"chrome writes visibility snapshot");Check(fs::exists(dir/L"shell-original.json"),"chrome journal exists before hiding");Check(Wait([&]{return !IsWindowVisible(hwnd);}),"chrome hides only selected fixture");
      ShowWindowAsync(hwnd,SW_SHOWNOACTIVATE);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"simulate a shell window resurfacing after registration");Check(chrome.Begin({hwnd})&&Wait([&]{return !IsWindowVisible(hwnd);}),"refresh hides an already journaled window again");
      {auto raw=ReadUtf8(dir/L"shell-original.json");auto journal=json::JsonObject::Parse(*raw);auto entries=journal.GetNamedArray(L"windows");Check(entries.Size()==1&&entries.GetObjectAt(0).GetNamedBoolean(L"visible"),"repeated suppression preserves the original visible state");}
      chrome.Restore();Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"chrome restores original visible state");Check(!fs::exists(dir/L"shell-original.json"),"normal return removes chrome journal");
      ShowWindowAsync(hwnd,SW_HIDE);Check(Wait([&]{return !IsWindowVisible(hwnd);}),"fixture starts hidden for visibility preservation");Check(chrome.Begin({hwnd}),"record initially hidden fixture");chrome.Restore();Sleep(50);Check(!IsWindowVisible(hwnd),"restore does not show originally hidden windows");ShowWindowAsync(hwnd,SW_SHOWNOACTIVATE);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"fixture ready after visibility preservation");
      Check(chrome.Begin({hwnd})&&Wait([&]{return !IsWindowVisible(hwnd);}),"record chrome identity before recreation regression");ShowWindowAsync(hwnd,SW_SHOWNOACTIVATE);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"recreated fixture is visible");
      {auto raw=ReadUtf8(dir/L"shell-original.json");auto journal=json::JsonObject::Parse(*raw);journal.GetNamedArray(L"windows").GetObjectAt(0).SetNamedValue(L"class",json::JsonValue::CreateStringValue(L"StaleWindowClass"));Check(WriteAtomic(dir/L"shell-original.json",std::wstring(journal.Stringify())),"simulate a stale window identity in the recovery journal");}
      Check(chrome.Begin({hwnd})&&Wait([&]{return !IsWindowVisible(hwnd);}),"stale handle identity is re-registered before suppression");chrome.Restore();Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;})&&!fs::exists(dir/L"shell-original.json"),"re-registered chrome window still restores cleanly");
    }
    {WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"Shell_SecondaryTrayWnd";RegisterClassW(&cls);HWND fake=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Isolated tray-class fixture",WS_POPUP,0,0,80,30,nullptr,nullptr,cls.hInstance,nullptr);Check(fake!=nullptr,"create an isolated non-Explorer taskbar-class window");auto chrome=ShellChrome::Discover();Check(std::find(chrome.begin(),chrome.end(),fake)==chrome.end(),"taskbar discovery excludes non-Explorer receivers with matching class names");DestroyWindow(fake);}
    {
      auto monitors=GetMonitors();Check(!monitors.empty(),"monitor available for fullscreen classification");auto bounds=monitors.front().bounds;
      LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_STYLE),extended=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
      SetWindowLongPtrW(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);
      SetWindowPos(hwnd,nullptr,bounds.left-8,bounds.top-8,bounds.right-bounds.left+16,bounds.bottom-bounds.top+16,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);
      Check(Wait([&]{RECT r;GetWindowRect(hwnd,&r);return r.left<=bounds.left&&r.right>=bounds.right;}),"framed fixture spans the monitor like a maximized app");
      Check(!IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId()),"framed applications never hide shell panels");
      SetWindowLongPtrW(hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,extended&~WS_EX_TOOLWINDOW);
      SetWindowPos(hwnd,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);
      Check(Wait([&]{return IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId());}),"borderless fullscreen fixture is detected");
      SetPropW(hwnd,L"NonRudeHWND",reinterpret_cast<HANDLE>(TRUE));Check(!IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId()),"declared desktop-style windows opt out of fullscreen suppression");RemovePropW(hwnd,L"NonRudeHWND");
      Check(!IsFullscreenOnMonitor(hwnd,bounds,fixture.pid),"own shell windows cannot suppress shell panels");
      if(monitors.size()>1)Check(!IsFullscreenOnMonitor(hwnd,monitors[1].bounds,GetCurrentProcessId()),"fullscreen suppresses only the monitor it covers");
      SetWindowLongPtrW(hwnd,GWL_EXSTYLE,extended|WS_EX_TOOLWINDOW);Check(!IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId()),"tool windows cannot suppress shell panels");SetWindowLongPtrW(hwnd,GWL_EXSTYLE,extended);
      ShowWindowAsync(hwnd,SW_MINIMIZE);Check(Wait([&]{return IsIconic(hwnd)!=FALSE;}),"fullscreen fixture minimized");Check(!IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId()),"minimized fullscreen window cannot hide navigation");
      ShowWindowAsync(hwnd,SW_RESTORE);Check(Wait([&]{return !IsIconic(hwnd);}),"fixture restored after fullscreen tests");ShowWindowAsync(hwnd,SW_HIDE);Check(Wait([&]{return !IsWindowVisible(hwnd);}),"fullscreen fixture hidden");Check(!IsFullscreenOnMonitor(hwnd,bounds,GetCurrentProcessId()),"hidden fullscreen window cannot hide navigation");
      SetWindowLongPtrW(hwnd,GWL_STYLE,style);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,extended);SetWindowPos(hwnd,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_ASYNCWINDOWPOS);ShowWindowAsync(hwnd,SW_SHOWNOACTIVATE);Check(Wait([&]{return IsWindowVisible(hwnd)!=FALSE;}),"fixture ready after fullscreen classification tests");
    }
    // Kill only a test owner process. Its independent guardian must rescue the fixture.
    auto owner=Start(L"\""+ExePath().wstring()+L"\" --crash-owner "+std::to_wstring(reinterpret_cast<ULONG_PTR>(hwnd))+L" \""+dir.wstring()+L"\" \""+app.wstring()+L"\" sentinel");
    Check(Wait([&]{return fs::exists(dir/L"ready")&&!IsWindowVisible(hwnd);}),"crash owner and guardian ready");TerminateProcess(owner.process,17);WaitForSingleObject(owner.process,2000);Check(Wait([&]{return IsWindowVisible(hwnd)&&!fs::exists(dir/L"recovery.json")&&!fs::exists(dir/L"shell-original.json");}),"independent guardian restores windows and chrome after crash");
    fs::remove(dir/L"ready");auto blocked=Start(L"\""+ExePath().wstring()+L"\" --crash-owner "+std::to_wstring(reinterpret_cast<ULONG_PTR>(hwnd))+L" \""+dir.wstring()+L"\" \""+app.wstring()+L"\" sentinel");
    Check(Wait([&]{return fs::exists(dir/L"ready")&&!IsWindowVisible(hwnd);}),"nonresponsive owner ready");
    auto eventName=L"Local\\WindowsGoodView.Rescue."+std::to_wstring(blocked.pid);Check(Wait([&]{HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,eventName.c_str());if(event)CloseHandle(event);return event!=nullptr;}),"independent rescue event ready");RequestRescue(blocked.pid);
    Check(Wait([&]{return IsWindowVisible(hwnd)&&!fs::exists(dir/L"shell-original.json");}),"rescue restores desktop while main owner cannot process messages");Check(WaitForSingleObject(blocked.process,0)==WAIT_TIMEOUT,"rescue does not depend on owner exiting");TerminateProcess(blocked.process,17);WaitForSingleObject(blocked.process,2000);
    PostMessageW(hwnd,WM_CLOSE,0,0);Check(WaitForSingleObject(fixture.process,3000)==WAIT_OBJECT_0,"fixture exits cleanly");std::error_code ec;fs::remove(dir/L"ready",ec);fs::remove(dir/L"rescue.flag",ec);fs::remove(dir,ec);std::cout<<checks<<" native lifecycle checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}catch(const winrt::hresult_error&e){std::wcerr<<e.message().c_str()<<L"\n";return 1;}}
