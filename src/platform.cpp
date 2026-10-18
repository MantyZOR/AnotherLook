#include "platform.h"
#include <shellapi.h>
#include <dwmapi.h>
#include <psapi.h>
#include <fstream>
#include <algorithm>
#include <array>
#include <shlobj.h>
#include <shobjidl.h>
#include <set>
#include <tlhelp32.h>
#include <propkey.h>

namespace gv {
bool WinTapSequence::Key(UINT key,bool down,bool modifiers){
    bool win=key==VK_LWIN||key==VK_RWIN;
    if(!win){if(down&&(left||right))combined=true;return false;}
    auto& held=key==VK_LWIN?left:right;
    if(down){if(!held){if(!left&&!right)combined=modifiers;else combined=true;held=true;}return false;}
    bool mask=held&&!combined;held=false;
    if(left||right)mask=false;else combined=false;
    return mask;
}
thread_local WinKeyGuard* WinKeyGuard::current_=nullptr;
DWORD WINAPI WinKeyGuard::Run(void* value){
    auto& self=*static_cast<WinKeyGuard*>(value);current_=&self;MSG message;PeekMessageW(&message,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
    self.sequence_={};self.hook_=SetWindowsHookExW(WH_KEYBOARD_LL,Keyboard,GetModuleHandleW(nullptr),0);SetEvent(self.ready_);
    if(self.hook_)while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(self.hook_)UnhookWindowsHookEx(self.hook_);self.hook_=nullptr;current_=nullptr;return 0;
}
void WinKeyGuard::Start(HWND receiver,UINT message){
    receiver_=receiver;message_=message;if(thread_)return;
    ++generation_;tapQueued_=false;ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!ready_)return;
    thread_=CreateThread(nullptr,128*1024,Run,this,0,&threadId_);
    if(!thread_){CloseHandle(ready_);ready_=nullptr;return;}
    WaitForSingleObject(ready_,INFINITE);
}
void WinKeyGuard::Stop(){
    receiver_=nullptr;message_=0;++generation_;tapQueued_=false;
    if(thread_){PostThreadMessageW(threadId_,WM_QUIT,0,0);WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);thread_=nullptr;}
    if(ready_){CloseHandle(ready_);ready_=nullptr;}threadId_=0;
}
bool WinKeyGuard::TakeTap(WPARAM generation){if(generation!=generation_.load())return false;tapQueued_=false;return receiver_.load()!=nullptr;}
LRESULT CALLBACK WinKeyGuard::Keyboard(int code,WPARAM message,LPARAM value){
    constexpr ULONG_PTR marker=0x4756574b;
    if(code<0||!current_)return CallNextHookEx(nullptr,code,message,value);
    const auto& key=*reinterpret_cast<KBDLLHOOKSTRUCT*>(value);
    if(key.dwExtraInfo==marker)return CallNextHookEx(nullptr,code,message,value);
    bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
    bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
    bool modifiers=((GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_SHIFT))&0x8000)!=0;
    if((down||up)&&current_->sequence_.Key(key.vkCode,down,modifiers)){
        // Queue shell work outside the hook, once on release. The receiver checks
        // the shell state again, including a pause or system prompt in the meantime.
        auto receiver=current_->receiver_.load();auto notification=current_->message_.load();
        if(receiver&&notification&&!current_->tapQueued_.exchange(true)&&!PostMessageW(receiver,notification,current_->generation_.load(),0))current_->tapQueued_=false;
        // Keep physical Win+key shortcuts intact. An unused virtual key
        // consumes Start-menu activation before the real Win release is replayed.
        INPUT input[3]{};for(auto& item:input){item.type=INPUT_KEYBOARD;item.ki.dwExtraInfo=marker;}
        input[0].ki.wVk=input[1].ki.wVk=0xff;input[1].ki.dwFlags=KEYEVENTF_KEYUP;
        input[2].ki.wVk=static_cast<WORD>(key.vkCode);input[2].ki.dwFlags=KEYEVENTF_KEYUP|KEYEVENTF_EXTENDEDKEY;
        UINT sent=SendInput(3,input,sizeof(INPUT));
        if(sent==3)return 1;
        if(sent){INPUT release[2]={input[1],input[2]};SendInput(2,release,sizeof(INPUT));}
    }
    return CallNextHookEx(nullptr,code,message,value);
}
namespace {
std::wstring Text(HWND h){int n=GetWindowTextLengthW(h);if(n<=0||n>32768)return {};std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(wcslen(s.c_str()));return s;}
std::wstring ProcessPath(DWORD pid){HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return {};wchar_t b[32768];DWORD n=32768;bool ok=QueryFullProcessImageNameW(h,0,b,&n);CloseHandle(h);return ok?std::wstring(b,n):L"";}
struct ProcessRecord {DWORD parent=0;std::wstring name;};
using ProcessTable=std::map<DWORD,ProcessRecord>;
ProcessTable Processes(){ProcessTable table;HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return table;PROCESSENTRY32W entry{sizeof(entry)};if(Process32FirstW(snapshot,&entry))do{table[entry.th32ProcessID]={entry.th32ParentProcessID,entry.szExeFile};}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return table;}
WindowInfo Info(HWND h,const ProcessTable* known=nullptr){WindowInfo w;w.hwnd=h;GetWindowThreadProcessId(h,&w.pid);w.title=Text(h);w.process=ProcessPath(w.pid);w.imageName=fs::path(w.process).filename().wstring();if(w.imageName.empty()){auto own=known?ProcessTable{}:Processes();const auto& table=known?*known:own;if(auto process=table.find(w.pid);process!=table.end())w.imageName=process->second.name;}wchar_t c[256]{};GetClassNameW(h,c,256);w.className=c;return w;}
bool SamePath(std::wstring_view a,std::wstring_view b){return _wcsicmp(std::wstring(a).c_str(),std::wstring(b).c_str())==0;}
bool TrayMessageClass(std::wstring_view name){
    constexpr std::wstring_view suffix=L"TrayIconMessageWindowClass";
    if(name.size()<3||!name.starts_with(L"Qt")||name[2]<L'0'||name[2]>L'9')return false;
    auto marker=name.find(suffix,3);if(marker==std::wstring_view::npos)return false;
    // Qt prefixes include its version and optionally a debug/namespace tag; a
    // second Qt instance can append a GUID to the registered class name.
    auto tail=name.substr(marker+suffix.size());
    return tail.empty()||(tail.size()==38&&tail.front()==L'{'&&tail.back()==L'}');
}
bool TaskbarClass(std::wstring_view name){return name==L"Shell_TrayWnd"||name==L"Shell_SecondaryTrayWnd";}
bool ExplorerTaskbar(HWND window){
    wchar_t cls[128]{},directory[MAX_PATH]{};GetClassNameW(window,cls,128);
    if(!TaskbarClass(cls)||!GetWindowsDirectoryW(directory,MAX_PATH))return false;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    return SamePath(ProcessPath(pid),(fs::path(directory)/L"explorer.exe").wstring());
}
bool VisibleStyle(HWND window){return (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0;}
bool ShowTaskbar(HWND window){
    ShowWindowAsync(window,SW_SHOWNOACTIVATE);
    // Preserve owner-chain ordering while restoring this window's topmost band.
    return SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_ASYNCWINDOWPOS|SWP_NOOWNERZORDER)!=FALSE;
}
json::JsonValue S(std::wstring_view s){return json::JsonValue::CreateStringValue(winrt::hstring(s));}
json::JsonValue N(double n){return json::JsonValue::CreateNumberValue(n);}
std::wstring JStr(const json::JsonObject& o,const wchar_t* key){return std::wstring(o.GetNamedString(key,L""));}
bool AppWindow(HWND h,bool includeHidden=false){
    if(!IsWindow(h)||(!includeHidden&&!IsWindowVisible(h))||h==GetShellWindow()||GetAncestor(h,GA_ROOT)!=h)return false;
    auto style=GetWindowLongPtrW(h,GWL_EXSTYLE),frame=GetWindowLongPtrW(h,GWL_STYLE);
    if((style&WS_EX_NOACTIVATE)&&!(style&WS_EX_APPWINDOW))return false;
    if((style&WS_EX_TOOLWINDOW)&&!(style&WS_EX_APPWINDOW)&&!(includeHidden&&(frame&WS_CAPTION)))return false;
    RECT bounds{};if(IsIconic(h)){WINDOWPLACEMENT placement{sizeof(placement)};if(!GetWindowPlacement(h,&placement))return false;bounds=placement.rcNormalPosition;}else if(!GetWindowRect(h,&bounds))return false;
    // Framework sentinels can be visible, titled "app windows" while only 2x2 px.
    if(bounds.right-bounds.left<64||bounds.bottom-bounds.top<48)return false;
    if(includeHidden&&!IsWindowVisible(h)&&!(frame&(WS_CAPTION|WS_THICKFRAME))&&!(style&WS_EX_APPWINDOW))return false;
    HWND owner=GetWindow(h,GW_OWNER);if(owner&&IsWindowVisible(owner)&&!(style&WS_EX_APPWINDOW)&&!includeHidden)return false;
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId())return false;
    BOOL cloak=FALSE;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloak,sizeof(cloak));if(cloak)return false;
    if(Text(h).empty())return false;wchar_t name[256]{};GetClassNameW(h,name,256);std::wstring_view cls=name;
    if(TrayMessageClass(cls)||cls==L"Progman"||cls==L"WorkerW"||cls==L"Shell_TrayWnd"||cls==L"Shell_SecondaryTrayWnd"||cls==L"#32769"||cls==L"IME"||cls==L"MSCTFIME UI")return false;
    return true;
}
bool GenerateBackdrop(const fs::path& p,const Scene& s){
    const int w=1920,h=1080;BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),w,h,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);
    auto rgb=std::stoul(s.backdrop,nullptr,16);COLORREF bg=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);auto a=std::stoul(s.accent,nullptr,16);
    auto mix=[&](int x,int y,int k){return (x*(100-k)+y*k)/100;};
    HBRUSH brush=CreateSolidBrush(bg);RECT rc{0,0,w,h};FillRect(dc,&rc,brush);DeleteObject(brush);
    for(int i=0;i<18;++i){auto color=RGB(mix(GetRValue(bg),(a>>16)&255,7),mix(GetGValue(bg),(a>>8)&255,7),mix(GetBValue(bg),a&255,7));HPEN pen=CreatePen(PS_SOLID,1,color);auto op=SelectObject(dc,pen);
      POINT points[7]={{650+i*20,h},{550+i*25,900},{1250+i*20,920},{1100+i*28,600},{1000+i*20,280},{1510+i*16,350},{w+100,40+i*15}};
      PolyBezier(dc,points,7);SelectObject(dc,op);DeleteObject(pen);}
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(mix(GetRValue(bg),(a>>16)&255,30),mix(GetGValue(bg),(a>>8)&255,30),mix(GetBValue(bg),a&255,30)));
    HFONT font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto of=SelectObject(dc,font);
    TextOutW(dc,80,980,s.name.c_str(),static_cast<int>(s.name.size()));SelectObject(dc,of);DeleteObject(font);
    BITMAPFILEHEADER header{0x4d42,static_cast<DWORD>(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+w*h*4),0,0,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<char*>(&header),sizeof(header));f.write(reinterpret_cast<char*>(&bi.bmiHeader),sizeof(bi.bmiHeader));f.write(static_cast<char*>(pixels),w*h*4);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return static_cast<bool>(f);
}
void RestoreWallpaperSnapshot(const fs::path& dir,bool finish){
    auto raw=ReadUtf8(dir/L"wallpaper-original.json");if(!raw)return;
    try{auto o=json::JsonObject::Parse(*raw);ComPtr<IDesktopWallpaper> wall;if(FAILED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&wall))))return;
      auto a=o.GetNamedArray(L"monitors");for(auto v:a){auto j=v.GetObject();auto id=JStr(j,L"id"),path=JStr(j,L"path");wall->SetWallpaper(id.c_str(),path.c_str());}
      wall->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(static_cast<int>(o.GetNamedNumber(L"position",4))));wall->SetBackgroundColor(static_cast<COLORREF>(o.GetNamedNumber(L"color",0)));
      if(finish){std::error_code ec;fs::remove(dir/L"wallpaper-original.json",ec);}
    }catch(...){}
}
}
fs::path ExePath(){wchar_t b[32768];GetModuleFileNameW(nullptr,b,32768);return b;}
Box RectBox(RECT r){return {static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)};}
void SetShellPanelLayer(HWND window,bool topmost,HWND behind){
    if(!IsWindow(window))return;
    bool current=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER;
    if(current)SetWindowPos(window,HWND_NOTOPMOST,0,0,0,0,flags);
    // Visible shell panels are normal owned popups of their monitor's canvas.
    // Windows raises the owner group together; focus changes never cross the
    // topmost boundary, which caused compositor flashes on desktop activation.
    if(topmost){
        HWND owner=GetWindow(window,GW_OWNER);bool above=false;
        if(owner&&IsWindowVisible(owner)){for(HWND next=GetWindow(owner,GW_HWNDPREV);next;next=GetWindow(next,GW_HWNDPREV))if(next==window){above=true;break;}if(!above)SetWindowPos(window,HWND_TOP,0,0,0,0,flags);}
    }else if(behind&&behind!=window&&IsWindowVisible(behind)&&!(GetWindowLongPtrW(behind,GWL_EXSTYLE)&WS_EX_TOPMOST)){
        // Being anywhere below the foreground app is sufficient. Re-inserting each
        // panel directly behind it on every event repeatedly shuffles sibling panels.
        bool below=false;for(HWND next=GetWindow(behind,GW_HWNDNEXT);next;next=GetWindow(next,GW_HWNDNEXT))if(next==window){below=true;break;}
        if(!below)SetWindowPos(window,behind,0,0,0,0,flags);
    }
}
bool IsFullscreenOnMonitor(HWND window,const RECT& monitor,DWORD shellProcess){
    if(!window||!IsWindow(window)||!IsWindowVisible(window)||IsIconic(window)||GetAncestor(window,GA_ROOT)!=window||GetPropW(window,L"NonRudeHWND"))return false;
    DWORD process=0;GetWindowThreadProcessId(window,&process);if(!process||process==shellProcess)return false;
    auto style=GetWindowLongPtrW(window,GWL_STYLE),extended=GetWindowLongPtrW(window,GWL_EXSTYLE);
    // A framed or maximized application must leave shell navigation accessible.
    if((style&WS_CAPTION)||(style&WS_CHILD)||(extended&WS_EX_TOOLWINDOW))return false;
    wchar_t name[128]{};GetClassNameW(window,name,128);std::wstring_view cls=name;
    if(cls==L"Progman"||cls==L"WorkerW"||cls==L"Shell_TrayWnd"||cls==L"Shell_SecondaryTrayWnd"||cls==L"#32769")return false;
    BOOL cloaked=FALSE;if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return false;
    RECT frame{},client{};if(FAILED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame)))&&!GetWindowRect(window,&frame))return false;
    if(!GetClientRect(window,&client))return false;MapWindowPoints(window,nullptr,reinterpret_cast<POINT*>(&client),2);
    auto covers=[&](const RECT& rect){return rect.left<=monitor.left+2&&rect.top<=monitor.top+2&&rect.right>=monitor.right-2&&rect.bottom>=monitor.bottom-2;};
    return covers(frame)&&covers(client);
}
std::vector<Monitor> GetMonitors(){std::vector<Monitor> result;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL{auto& r=*reinterpret_cast<std::vector<Monitor>*>(p);MONITORINFOEXW i{};i.cbSize=sizeof(i);GetMonitorInfoW(h,&i);Monitor m;m.id=i.szDevice;m.bounds=i.rcMonitor;m.work=i.rcWork;m.primary=(i.dwFlags&MONITORINFOF_PRIMARY)!=0;DISPLAY_DEVICEW d{};d.cb=sizeof(d);if(EnumDisplayDevicesW(i.szDevice,0,&d,EDD_GET_DEVICE_INTERFACE_NAME)&&*d.DeviceID)m.id=d.DeviceID;m.label=i.szDevice;r.push_back(m);return TRUE;},reinterpret_cast<LPARAM>(&result));
    std::stable_sort(result.begin(),result.end(),[](const Monitor&a,const Monitor&b){if(a.primary!=b.primary)return a.primary;return a.bounds.left<b.bounds.left;});return result;
}
std::vector<WindowInfo> ListAppWindows(bool includeHidden){struct Context {bool hidden;ProcessTable processes;std::vector<WindowInfo> windows;} context{includeHidden,Processes(),{}};EnumWindows([](HWND h,LPARAM p)->BOOL{auto& context=*reinterpret_cast<Context*>(p);if(AppWindow(h,context.hidden))context.windows.push_back(Info(h,&context.processes));return TRUE;},reinterpret_cast<LPARAM>(&context));return context.windows;}
ProgramTarget ResolveProgramTarget(std::wstring_view target,std::wstring_view arguments){
    ProgramTarget result;result.arguments=arguments;std::wstring path(target);
    if(IsWebUrl(path)){result.url=NormalizeWebUrl(path);return result;}
    constexpr std::wstring_view apps=L"shell:AppsFolder\\";if(path.starts_with(apps)){result.appId=path.substr(apps.size());return result;}
    auto extension=fs::path(path).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
    if(extension==L".url"){wchar_t url[32768]{};GetPrivateProfileStringW(L"InternetShortcut",L"URL",L"",url,32768,path.c_str());if(IsWebUrl(url))result.url=NormalizeWebUrl(url);return result;}
    if(extension==L".lnk"){
        ComPtr<IShellLinkW> link;ComPtr<IPersistFile> file;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))&&SUCCEEDED(link.As(&file))&&SUCCEEDED(file->Load(path.c_str(),STGM_READ))){
            wchar_t executable[32768]{},directory[32768]{},parameters[32768]{};link->GetPath(executable,32768,nullptr,SLGP_RAWPATH);link->GetWorkingDirectory(directory,32768);link->GetArguments(parameters,32768);path=executable;result.workingDirectory=directory;
            if(*parameters)result.arguments=std::wstring(parameters)+(arguments.empty()?L"":L" "+std::wstring(arguments));
            ComPtr<IPropertyStore> properties;if(SUCCEEDED(link.As(&properties))){PROPVARIANT value{};if(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)result.appId=value.pwszVal;PropVariantClear(&value);}
        }
    }
    if(!path.empty()){wchar_t expanded[32768]{};if(ExpandEnvironmentStringsW(path.c_str(),expanded,32768)&&*expanded)path=expanded;std::error_code error;auto absolute=fs::absolute(path,error);result.executable=(error?fs::path(path):absolute).lexically_normal().wstring();result.imageName=fs::path(result.executable).filename().wstring();if(result.workingDirectory.empty())result.workingDirectory=fs::path(result.executable).parent_path().wstring();}
    return result;
}
bool MatchesProgram(const WindowInfo& window,const ProgramTarget& target){
    if(!target.appId.empty()&&!window.appId.empty()&&SamePath(target.appId,window.appId))return true;
    if(!target.executable.empty()&&!window.process.empty())return SamePath(target.executable,window.process);
    return !target.imageName.empty()&&!window.imageName.empty()&&SamePath(target.imageName,window.imageName);
}
std::vector<WindowInfo> FindProgramWindows(const ProgramTarget& target,DWORD launchedPid){
    auto processes=Processes();std::set<DWORD> candidates;if(launchedPid)candidates.insert(launchedPid);
    for(const auto& [pid,record]:processes)if(!target.imageName.empty()&&SamePath(record.name,target.imageName)){auto path=ProcessPath(pid);if(path.empty()||target.executable.empty()||SamePath(path,target.executable))candidates.insert(pid);}
    auto descends=[&](DWORD process,DWORD ancestor){for(int depth=0;process&&depth<32;++depth){if(process==ancestor)return true;auto found=processes.find(process);if(found==processes.end()||found->second.parent==process)break;process=found->second.parent;}return false;};
    std::vector<std::pair<int,WindowInfo>> ranked;
    for(auto window:ListAppWindows(true)){
        if(!target.appId.empty()){ComPtr<IPropertyStore> properties;if(SUCCEEDED(SHGetPropertyStoreForWindow(window.hwnd,IID_PPV_ARGS(&properties)))){PROPVARIANT value{};if(SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)window.appId=value.pwszVal;PropVariantClear(&value);}}
        int score=MatchesProgram(window,target)?100:candidates.contains(window.pid)?90:launchedPid&&descends(window.pid,launchedPid)?30:0;
        if(!score&&window.className==L"ConsoleWindowClass")for(DWORD process:candidates){
            if(descends(window.pid,process)||descends(process,window.pid)){score=20;break;}
            auto host=processes.find(window.pid),client=processes.find(process);if(host!=processes.end()&&client!=processes.end()&&host->second.parent&&host->second.parent==client->second.parent){auto parent=processes.find(host->second.parent);if(parent!=processes.end()&&(SamePath(parent->second.name,L"cmd.exe")||SamePath(parent->second.name,L"powershell.exe")||SamePath(parent->second.name,L"pwsh.exe"))){score=10;break;}}
        }
        if(score){score+=IsWindowVisible(window.hwnd)?5:0;score+=IsIconic(window.hwnd)?0:2;ranked.emplace_back(score,std::move(window));}
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first>b.first;});std::vector<WindowInfo> result;for(auto& item:ranked)result.push_back(std::move(item.second));return result;
}
bool FocusAppWindow(HWND window){
    if(!IsWindow(window))return false;
    wchar_t cls[256]{};GetClassNameW(window,cls,256);if(TrayMessageClass(cls))return false;
    DWORD requestedProcess=0;GetWindowThreadProcessId(window,&requestedProcess);
    bool ownCanvas=requestedProcess==GetCurrentProcessId()&&std::wstring_view(cls)==L"GoodView.Surface";
    if(IsIconic(window)){
        // The shell switch restores minimized/elevated windows. Do not queue an
        // additional activating SW_RESTORE which could run after a later click.
        SwitchToThisWindow(window,TRUE);
    }else if(!IsWindowVisible(window))ShowWindowAsync(window,SW_SHOWNA);
    if(IsIconic(window)||!IsWindowVisible(window))return false;
    HWND target=window;
    // Navigation and tray panels are owned popups, not modal app dialogs. A
    // workspace selection must focus its canvas rather than its last panel.
    if(!ownCanvas||!IsWindowEnabled(window))for(int i=0;i<8;++i){HWND popup=GetLastActivePopup(target);if(!popup||popup==target||!IsWindowVisible(popup))break;target=popup;}
    if(!IsWindowEnabled(target))return false;
    if(IsIconic(target)){if(target!=window)SwitchToThisWindow(target,TRUE);if(IsIconic(target))return false;}
    else if(target!=window&&!IsWindowVisible(target))ShowWindowAsync(target,SW_SHOWNA);
    DWORD process=0;GetWindowThreadProcessId(target,&process);AllowSetForegroundWindow(process);
    if(process!=GetCurrentProcessId())SetWindowPos(target,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_ASYNCWINDOWPOS);
    BOOL requested=SetForegroundWindow(target);
    // Explicit task selection uses the shell switch path if ordinary activation
    // is refused, including restored elevated utilities. Callers cancel delayed
    // attempts as soon as the user switches to unrelated work.
    // A successful cross-thread request can still be in the target's queue.
    // Sending another shell switch at that point can activate it a second time
    // after the user has already opened a different popup.
    if(!requested&&GetForegroundWindow()!=target&&process!=GetCurrentProcessId())SwitchToThisWindow(target,TRUE);
    return !IsIconic(target)&&IsWindowVisible(target)&&GetForegroundWindow()==target;
}
bool LaunchTarget(std::wstring_view target,std::wstring_view args,DWORD* pid){
    if(pid)*pid=0;if(target.empty())return false;auto resolved=ResolveProgramTarget(target,args);SHELLEXECUTEINFOW s{};s.cbSize=sizeof(s);s.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_ASYNCOK|SEE_MASK_FLAG_NO_UI;s.lpVerb=L"open";std::wstring t(target),a(args);s.lpFile=t.c_str();s.lpParameters=a.empty()?nullptr:a.c_str();s.lpDirectory=resolved.workingDirectory.empty()?nullptr:resolved.workingDirectory.c_str();s.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&s))return false;if(s.hProcess){if(pid)*pid=GetProcessId(s.hProcess);CloseHandle(s.hProcess);}return true;
}
bool SetAutoStart(bool enabled,const fs::path& dataDirectory){HKEY k=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return false;
    LONG result;if(enabled){auto cmd=L"\""+ExePath().wstring()+L"\" --background"+(dataDirectory.empty()?L"":L" --data \""+dataDirectory.wstring()+L"\"");result=RegSetValueExW(k,L"WindowsGoodView",0,REG_SZ,reinterpret_cast<const BYTE*>(cmd.c_str()),static_cast<DWORD>((cmd.size()+1)*sizeof(wchar_t)));}else{result=RegDeleteValueW(k,L"WindowsGoodView");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}RegCloseKey(k);return result==ERROR_SUCCESS;
}
void RemoveOwnedAutoStart(){
    wchar_t command[32768]{};DWORD bytes=sizeof(command);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"WindowsGoodView",RRF_RT_REG_SZ,nullptr,command,&bytes)!=ERROR_SUCCESS)return;
    int count=0;auto args=CommandLineToArgvW(command,&count);if(args){if(count>0&&SamePath(args[0],ExePath().wstring()))SetAutoStart(false);LocalFree(args);}
}
bool IsAutoStartEnabled(){wchar_t b[32768];DWORD n=sizeof(b);return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"WindowsGoodView",RRF_RT_REG_SZ,nullptr,b,&n)==ERROR_SUCCESS;}
HICON CreateAppIcon(){if(auto resource=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,0)))return resource;const int size=32;BITMAPV5HEADER b{};b.bV5Size=sizeof(b);b.bV5Width=size;b.bV5Height=-size;b.bV5Planes=1;b.bV5BitCount=32;b.bV5Compression=BI_BITFIELDS;b.bV5RedMask=0xff0000;b.bV5GreenMask=0xff00;b.bV5BlueMask=0xff;b.bV5AlphaMask=0xff000000;void* pixels=nullptr;HDC dc=GetDC(nullptr);HBITMAP color=CreateDIBSection(dc,reinterpret_cast<BITMAPINFO*>(&b),DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(nullptr,dc);if(!color)return CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));auto* p=static_cast<DWORD*>(pixels);std::fill(p,p+size*size,0xff18211f);for(int y=7;y<25;++y)for(int x=7;x<25;++x)if(x<14 || y<14)p[y*size+x]=0xffb4ceac;HBITMAP mask=CreateBitmap(size,size,1,1,nullptr);ICONINFO i{TRUE,0,0,mask,color};auto icon=CreateIconIndirect(&i);DeleteObject(color);DeleteObject(mask);return icon;}
WindowManager::WindowManager(fs::path d,Config& c):dir_(std::move(d)),config_(c),prop_(L"WindowsGoodView.Managed."+std::to_wstring(GetCurrentProcessId())){}
WindowManager::~WindowManager(){RestoreAll();}
bool WindowManager::Valid(const Entry& e)const{DWORD pid=0;GetWindowThreadProcessId(e.info.hwnd,&pid);return IsWindow(e.info.hwnd)&&pid==e.info.pid&&reinterpret_cast<ULONG_PTR>(GetPropW(e.info.hwnd,prop_.c_str()))==e.token;}
void WindowManager::Prune(){std::erase_if(entries_,[&](const Entry&e){return !Valid(e);});}
bool WindowManager::IsManaged(HWND h)const{return std::any_of(entries_.begin(),entries_.end(),[&](const Entry&e){return e.info.hwnd==h&&Valid(e);});}
bool WindowManager::WriteRecovery(){json::JsonObject o;o.SetNamedValue(L"property",S(prop_));json::JsonArray a;
    for(auto&e:entries_){if(!Valid(e))continue;json::JsonObject j;j.SetNamedValue(L"hwnd",S(std::to_wstring(reinterpret_cast<ULONG_PTR>(e.info.hwnd))));j.SetNamedValue(L"pid",N(e.info.pid));j.SetNamedValue(L"token",S(std::to_wstring(e.token)));auto&p=e.original;j.SetNamedValue(L"show",N(p.showCmd));j.SetNamedValue(L"left",N(p.rcNormalPosition.left));j.SetNamedValue(L"top",N(p.rcNormalPosition.top));j.SetNamedValue(L"right",N(p.rcNormalPosition.right));j.SetNamedValue(L"bottom",N(p.rcNormalPosition.bottom));a.Append(j);}o.SetNamedValue(L"windows",a);return WriteAtomic(dir_/L"recovery.json",std::wstring(o.Stringify()));
}
bool WindowManager::Attach(HWND h,const std::wstring& scene){Prune();if(IsManaged(h)){MoveToScene(h,scene);return true;}if(!AppWindow(h))return false;
    Entry e;e.info=Info(h);if(e.info.process.empty())return false;e.scene=scene;if(!GetWindowPlacement(h,&e.original))return false;e.placement=e.original;e.token=nextToken_++;e.floating=GetWindow(h,GW_OWNER)!=nullptr||e.info.className==L"#32770";
    if(!SetPropW(h,prop_.c_str(),reinterpret_cast<HANDLE>(e.token)))return false;entries_.push_back(e);
    if(!WriteRecovery()){RemovePropW(h,prop_.c_str());entries_.pop_back();return false;}return true;
}
void WindowManager::Detach(HWND h){for(auto it=entries_.begin();it!=entries_.end();++it)if(it->info.hwnd==h){if(auto*s=FindScene(config_,it->scene)){auto rule=std::find_if(s->windows.begin(),s->windows.end(),[&](const SavedWindow&w){return SamePath(w.process,it->info.process)&&w.className==it->info.className;});if(rule!=s->windows.end())s->windows.erase(rule);}if(Valid(*it)){if(it->hidden)ShowWindowAsync(h,SW_SHOWNOACTIVATE);RemovePropW(h,prop_.c_str());}entries_.erase(it);break;}WriteRecovery();Capture();}
void WindowManager::ApplyVisibility(Entry&e,bool visible){if(!Valid(e))return;if(visible){if(e.hidden){auto placement=e.placement;placement.flags|=WPF_ASYNCWINDOWPLACEMENT;SetWindowPlacement(e.info.hwnd,&placement);ShowWindowAsync(e.info.hwnd,e.placement.showCmd==SW_SHOWMINIMIZED?SW_SHOWMINNOACTIVE:SW_SHOWNOACTIVATE);e.hidden=false;}}else if(!e.hidden){GetWindowPlacement(e.info.hwnd,&e.placement);ShowWindowAsync(e.info.hwnd,SW_HIDE);e.hidden=true;}}
void WindowManager::Capture(){Prune();auto monitors=GetMonitors();
    for(auto&s:config_.scenes){std::vector<SavedWindow> saved;for(auto&e:entries_)if(e.scene==s.id&&Valid(e)){if(!e.hidden)GetWindowPlacement(e.info.hwnd,&e.placement);RECT r=e.placement.rcNormalPosition;HMONITOR mh=MonitorFromWindow(e.info.hwnd,MONITOR_DEFAULTTONEAREST);MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(mh,&mi);auto monitor=std::find_if(monitors.begin(),monitors.end(),[&](const Monitor&m){return m.label==mi.szDevice;});Box work=monitor!=monitors.end()?RectBox(monitor->work):RectBox(mi.rcWork);SavedWindow w;w.process=e.info.process;w.title=Text(e.info.hwnd);w.className=e.info.className;w.monitor=monitor!=monitors.end()?monitor->id:L"";w.floating=e.floating;w.normalized=Normalize(RectBox(r),work);saved.push_back(w);}if(!saved.empty())s.windows=std::move(saved);}
}
void WindowManager::Switch(const std::wstring& scene){Prune();for(auto&e:entries_)ApplyVisibility(e,e.scene==scene);}
void WindowManager::RestoreAll(){if(entries_.empty())return;for(auto&e:entries_)if(Valid(e)){auto placement=e.original;placement.flags|=WPF_ASYNCWINDOWPLACEMENT;SetWindowPlacement(e.info.hwnd,&placement);ShowWindowAsync(e.info.hwnd,e.original.showCmd==SW_SHOWMINIMIZED?SW_SHOWMINNOACTIVE:SW_SHOWNOACTIVATE);RemovePropW(e.info.hwnd,prop_.c_str());}entries_.clear();std::error_code ec;fs::remove(dir_/L"recovery.json",ec);}
void WindowManager::Tile(const std::vector<Monitor>& monitors,int topInset){Prune();auto*s=FindScene(config_,config_.active);if(!s||s->gaming||!s->autoTile)return;
    for(auto&m:monitors){std::vector<Entry*> group;for(auto&e:entries_)if(e.scene==s->id&&!e.floating&&!IsIconic(e.info.hwnd)){MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(e.info.hwnd,MONITOR_DEFAULTTONEAREST),&mi);if(m.label==mi.szDevice)group.push_back(&e);}MONITORINFO live{sizeof(live)};POINT center{m.bounds.left+(m.bounds.right-m.bounds.left)/2,m.bounds.top+(m.bounds.bottom-m.bounds.top)/2};bool current=GetMonitorInfoW(MonitorFromPoint(center,MONITOR_DEFAULTTONEAREST),&live)!=FALSE;Box a=RectBox(current?live.rcWork:m.work);if(a.y<m.bounds.top+topInset){float d=m.bounds.top+topInset-a.y;a.y+=d;a.h-=d;}auto boxes=TileLayout(a,static_cast<int>(group.size()),s->layout);
      for(size_t i=0;i<boxes.size();++i){auto b=boxes[i];ShowWindowAsync(group[i]->info.hwnd,SW_RESTORE);SetWindowPos(group[i]->info.hwnd,nullptr,static_cast<int>(b.x),static_cast<int>(b.y),static_cast<int>(b.w),static_cast<int>(b.h),SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);}}
}
bool WindowManager::RepairSavedHelpers(){
    std::vector<SavedWindow> helpers;for(const auto& scene:config_.scenes)for(const auto& saved:scene.windows)if(TrayMessageClass(saved.className))helpers.push_back(saved);
    if(helpers.empty())return false;
    // Only repair receivers previously saved by this profile, with both executable
    // and Qt's internal class matching. Hide their accidental frame; never close
    // the receiver, which must keep handling the application's tray callbacks.
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        wchar_t cls[256]{};GetClassNameW(window,cls,256);if(!TrayMessageClass(cls))return TRUE;
        const auto& saved=*reinterpret_cast<const std::vector<SavedWindow>*>(data);auto info=Info(window);
        if(std::any_of(saved.begin(),saved.end(),[&](const SavedWindow& item){return item.className==info.className&&SamePath(item.process,info.process);}))ShowWindowAsync(window,SW_HIDE);
        return TRUE;
    },reinterpret_cast<LPARAM>(&helpers));
    for(auto& scene:config_.scenes)std::erase_if(scene.windows,[](const SavedWindow& saved){return TrayMessageClass(saved.className);});
    return true;
}
void WindowManager::AdoptSaved(){auto all=ListAppWindows();std::set<HWND>used;auto monitors=GetMonitors();for(auto&s:config_.scenes)for(auto&w:s.windows){auto found=std::find_if(all.begin(),all.end(),[&](const WindowInfo&i){return !used.contains(i.hwnd)&&SamePath(i.process,w.process)&&i.className==w.className;});if(found==all.end())continue;if(!Attach(found->hwnd,s.id))continue;used.insert(found->hwnd);auto&e=entries_.back();e.floating=w.floating;auto monitor=std::find_if(monitors.begin(),monitors.end(),[&](const Monitor&m){return m.id==w.monitor;});if(monitor==monitors.end()&&!monitors.empty())monitor=monitors.begin();if(monitor!=monitors.end()){auto b=Denormalize(w.normalized,RectBox(monitor->work));SetWindowPos(e.info.hwnd,nullptr,static_cast<int>(b.x),static_cast<int>(b.y),static_cast<int>(b.w),static_cast<int>(b.h),SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);e.placement.rcNormalPosition={static_cast<LONG>(b.x),static_cast<LONG>(b.y),static_cast<LONG>(b.x+b.w),static_cast<LONG>(b.y+b.h)};}}}
void WindowManager::ExpectLaunch(DWORD pid,const std::wstring& scene){if(pid)pending_[pid]={scene,GetTickCount64()+15000};}
void WindowManager::HandleNewWindow(HWND h){std::erase_if(pending_,[](const auto& p){return p.second.second<GetTickCount64();});if(IsManaged(h)||!AppWindow(h))return;auto i=Info(h);auto it=pending_.find(i.pid);if(it!=pending_.end()){auto scene=it->second.first;Attach(h,scene);Switch(config_.active);return;}
    for(auto&s:config_.scenes)for(auto&w:s.windows)if(SamePath(w.process,i.process)&&w.className==i.className){size_t existing=0;for(auto&e:entries_)if(e.scene==s.id&&SamePath(e.info.process,w.process))++existing;size_t expected=std::count_if(s.windows.begin(),s.windows.end(),[&](const SavedWindow& sw){return SamePath(sw.process,w.process);});if(existing<expected){Attach(h,s.id);Switch(config_.active);return;}}std::erase_if(pending_,[](const auto&p){return p.second.second<GetTickCount64();});
}
void WindowManager::ToggleFloating(HWND h){for(auto&e:entries_)if(e.info.hwnd==h)e.floating=!e.floating;}
void WindowManager::MarkFloating(HWND h){for(auto&e:entries_)if(e.info.hwnd==h)e.floating=true;}
void WindowManager::MoveToScene(HWND h,const std::wstring& scene){for(auto&e:entries_)if(e.info.hwnd==h){if(e.scene==scene)return;if(auto*s=FindScene(config_,e.scene)){auto rule=std::find_if(s->windows.begin(),s->windows.end(),[&](const SavedWindow&w){return SamePath(w.process,e.info.process)&&w.className==e.info.className;});if(rule!=s->windows.end())s->windows.erase(rule);}e.scene=scene;ApplyVisibility(e,scene==config_.active);}Capture();}
size_t WindowManager::Count(const std::wstring& scene)const{return std::count_if(entries_.begin(),entries_.end(),[&](const Entry&e){return e.scene==scene&&Valid(e);});}
void RecoverSession(const fs::path& dir){ShellChrome::Recover(dir);auto raw=ReadUtf8(dir/L"recovery.json");if(raw)try{auto o=json::JsonObject::Parse(*raw);auto prop=JStr(o,L"property");if(prop.starts_with(L"WindowsGoodView.Managed.")){for(auto v:o.GetNamedArray(L"windows")){auto j=v.GetObject();auto hwnd=reinterpret_cast<HWND>(std::stoull(JStr(j,L"hwnd")));auto token=std::stoull(JStr(j,L"token"));DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(pid!=static_cast<DWORD>(j.GetNamedNumber(L"pid"))||reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,prop.c_str()))!=token)continue;WINDOWPLACEMENT p{sizeof(p)};p.flags=WPF_ASYNCWINDOWPLACEMENT;p.showCmd=static_cast<UINT>(j.GetNamedNumber(L"show",SW_SHOWNORMAL));p.rcNormalPosition={static_cast<LONG>(j.GetNamedNumber(L"left")),static_cast<LONG>(j.GetNamedNumber(L"top")),static_cast<LONG>(j.GetNamedNumber(L"right")),static_cast<LONG>(j.GetNamedNumber(L"bottom"))};SetWindowPlacement(hwnd,&p);ShowWindowAsync(hwnd,p.showCmd==SW_SHOWMINIMIZED?SW_SHOWMINNOACTIVE:SW_SHOWNOACTIVATE);RemovePropW(hwnd,prop.c_str());}}}catch(...){}std::error_code ec;fs::remove(dir/L"recovery.json",ec);RestoreWallpaperSnapshot(dir,true);}
void RequestRescue(DWORD parent){
    auto name=L"Local\\WindowsGoodView.Rescue."+std::to_wstring(parent);
    if(HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str())){SetEvent(event);CloseHandle(event);}
}
int RunGuardian(DWORD parent,const fs::path& dir){
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,parent);
    auto suffix=std::to_wstring(parent);
    HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\WindowsGoodView.Stop."+suffix).c_str());
    HANDLE rescue=CreateEventW(nullptr,FALSE,FALSE,(L"Local\\WindowsGoodView.Rescue."+suffix).c_str());
    bool hotkey=RegisterHotKey(nullptr,1,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_SPACE)!=FALSE;
    if(hotkey)if(HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\WindowsGoodView.Ready."+suffix).c_str())){SetEvent(ready);CloseHandle(ready);}
    auto restore=[&](){
        // This thread remains responsive even when the main UI is stuck in another app.
        EnumWindows([](HWND window,LPARAM value)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==static_cast<DWORD>(value))ShowWindowAsync(window,SW_HIDE);return TRUE;},parent);
        RecoverSession(dir);ShellChrome::RepairTaskbars();WriteAtomic(dir/L"rescue.flag",L"Return to Windows requested");
        if(HWND window=FindWindowW(L"GoodView.Events",dir.c_str()))PostMessageW(window,WM_APP+32,0,0);
    };
    if(!process){RecoverSession(dir);}else{
        std::vector<HANDLE> objects{process};if(stop)objects.push_back(stop);DWORD rescueIndex=static_cast<DWORD>(objects.size());if(rescue)objects.push_back(rescue);DWORD count=static_cast<DWORD>(objects.size());bool done=false;
        while(!done){DWORD result=MsgWaitForMultipleObjects(count,objects.data(),FALSE,INFINITE,QS_ALLINPUT);
            if(result==WAIT_OBJECT_0){RecoverSession(dir);done=true;}
            else if(stop&&result==WAIT_OBJECT_0+1)done=true;
            else if(rescue&&result==WAIT_OBJECT_0+rescueIndex)restore();
            else if(result==WAIT_OBJECT_0+count){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_HOTKEY)restore();}}
            else done=true;
        }
    }
    if(hotkey)UnregisterHotKey(nullptr,1);if(process)CloseHandle(process);if(stop)CloseHandle(stop);if(rescue)CloseHandle(rescue);return 0;
}

std::vector<HWND> ShellChrome::Discover(){
    std::vector<HWND> result;
    EnumWindows([](HWND window,LPARAM value)->BOOL{
        auto& out=*reinterpret_cast<std::vector<HWND>*>(value);wchar_t cls[128]{};GetClassNameW(window,cls,128);
        // The notification receiver uses the same class name. Only Explorer's
        // actual taskbars belong to the Windows chrome recovery journal.
        if(TaskbarClass(cls)&&ExplorerTaskbar(window))out.push_back(window);
        if(HWND view=FindWindowExW(window,nullptr,L"SHELLDLL_DefView",nullptr))if(HWND icons=FindWindowExW(view,nullptr,L"SysListView32",nullptr))out.push_back(icons);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));return result;
}
bool ShellChrome::Begin(){return Begin(Discover());}
bool ShellChrome::Begin(const std::vector<HWND>& windows){
    auto path=dir_/L"shell-original.json";json::JsonObject root;json::JsonArray entries;std::wstring property;
    try{if(auto raw=ReadUtf8(path)){root=json::JsonObject::Parse(*raw);entries=root.GetNamedArray(L"windows");property=JStr(root,L"property");}}catch(...){return false;}
    if(property.empty())property=L"WindowsGoodView.Chrome."+std::to_wstring(GetCurrentProcessId());
    std::vector<HWND> added;
    bool changed=false;
    for(HWND window:windows){
        if(!IsWindow(window))continue;auto handle=std::to_wstring(reinterpret_cast<ULONG_PTR>(window));bool known=false;DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t cls[128]{};GetClassNameW(window,cls,128);
        json::JsonArray retained;for(auto value:entries){auto entry=value.GetObject();if(JStr(entry,L"hwnd")!=handle){retained.Append(value);continue;}bool valid=static_cast<DWORD>(entry.GetNamedNumber(L"pid",0))==pid&&JStr(entry,L"class")==cls&&GetPropW(window,property.c_str());if(valid){known=true;retained.Append(value);}else changed=true;}entries=retained;if(known)continue;
        if(!SetPropW(window,property.c_str(),reinterpret_cast<HANDLE>(1)))continue;
        json::JsonObject entry;entry.SetNamedValue(L"hwnd",S(handle));entry.SetNamedValue(L"pid",N(pid));entry.SetNamedValue(L"class",S(cls));entry.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue((GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=0));entries.Append(entry);added.push_back(window);
    }
    root.SetNamedValue(L"property",S(property));root.SetNamedValue(L"windows",entries);
    if((!active_||!added.empty()||changed)&&!WriteAtomic(path,std::wstring(root.Stringify()))){for(HWND window:added)RemovePropW(window,property.c_str());return false;}
    active_=true;for(HWND window:windows)if(GetPropW(window,property.c_str())&&VisibleStyle(window)){ShowWindowAsync(window,SW_HIDE);SetWindowPos(window,nullptr,0,0,0,0,SWP_HIDEWINDOW|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS|SWP_NOOWNERZORDER);}return true;
}
void ShellChrome::Refresh(){if(active_)Begin();}
bool ShellChrome::RepairTaskbars(const fs::path& reportPath){
    struct State {HWND window;bool before,queued;DWORD error;};std::vector<State> taskbars;
    for(HWND window:Discover())if(ExplorerTaskbar(window)){bool visible=VisibleStyle(window);SetLastError(ERROR_SUCCESS);bool queued=ShowTaskbar(window);taskbars.push_back({window,visible,queued,queued?ERROR_SUCCESS:GetLastError()});}
    // Cross-thread requests are asynchronous. Verify Explorer has processed them.
    auto until=GetTickCount64()+1500;
    while(GetTickCount64()<until){bool ready=true;for(auto state:taskbars)ready&=IsWindowVisible(state.window)&&(GetWindowLongPtrW(state.window,GWL_EXSTYLE)&WS_EX_TOPMOST);if(ready)break;Sleep(15);}
    bool primary=false,passed=!taskbars.empty();json::JsonArray items;
    for(auto state:taskbars){
        wchar_t cls[128]{};GetClassNameW(state.window,cls,128);RECT rect{},overlap{};GetWindowRect(state.window,&rect);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(state.window,MONITOR_DEFAULTTONEAREST),&monitor);
        bool visible=IsWindowVisible(state.window)!=FALSE,topmost=(GetWindowLongPtrW(state.window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,onScreen=IntersectRect(&overlap,&rect,&monitor.rcMonitor)!=FALSE;
        bool isPrimary=std::wstring_view(cls)==L"Shell_TrayWnd";primary|=isPrimary;passed&=visible&&topmost&&onScreen;
        json::JsonObject item;item.SetNamedValue(L"class",S(cls));item.SetNamedValue(L"beforeVisible",json::JsonValue::CreateBooleanValue(state.before));item.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(visible));item.SetNamedValue(L"topmost",json::JsonValue::CreateBooleanValue(topmost));item.SetNamedValue(L"onScreen",json::JsonValue::CreateBooleanValue(onScreen));item.SetNamedValue(L"primaryMonitor",json::JsonValue::CreateBooleanValue((monitor.dwFlags&MONITORINFOF_PRIMARY)!=0));item.SetNamedValue(L"left",N(rect.left));item.SetNamedValue(L"top",N(rect.top));item.SetNamedValue(L"right",N(rect.right));item.SetNamedValue(L"bottom",N(rect.bottom));
        DWORD_PTR response=0;item.SetNamedValue(L"responsive",json::JsonValue::CreateBooleanValue(SendMessageTimeoutW(state.window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,250,&response)!=0));
        item.SetNamedValue(L"positionRequestQueued",json::JsonValue::CreateBooleanValue(state.queued));item.SetNamedValue(L"positionError",N(state.error));
        HWND foreground=GetForegroundWindow();bool aboveForeground=false;for(HWND window=GetTopWindow(nullptr);window;window=GetWindow(window,GW_HWNDNEXT)){if(window==state.window){aboveForeground=true;break;}if(window==foreground)break;}
        item.SetNamedValue(L"aboveForeground",json::JsonValue::CreateBooleanValue(aboveForeground));wchar_t foregroundClass[128]{};GetClassNameW(foreground,foregroundClass,128);item.SetNamedValue(L"foregroundClass",S(foregroundClass));
        item.SetNamedValue(L"foregroundFullscreen",json::JsonValue::CreateBooleanValue(IsFullscreenOnMonitor(GetForegroundWindow(),monitor.rcMonitor,GetCurrentProcessId())));items.Append(item);
    }
    passed&=primary;
    if(!reportPath.empty()){json::JsonObject report;report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed));report.SetNamedValue(L"taskbars",items);WriteAtomic(reportPath,std::wstring(report.Stringify()));}
    return passed;
}
void ShellChrome::Recover(const fs::path& directory){
    auto path=directory/L"shell-original.json";auto raw=ReadUtf8(path);if(!raw)return;
    try{auto root=json::JsonObject::Parse(*raw);auto property=JStr(root,L"property");if(!property.starts_with(L"WindowsGoodView.Chrome."))return;
        struct RestoreRequest {HWND window;bool visible,taskbar;json::JsonObject entry;};std::vector<RestoreRequest> requests;
        for(auto value:root.GetNamedArray(L"windows")){auto entry=value.GetObject();HWND window=reinterpret_cast<HWND>(std::stoull(JStr(entry,L"hwnd")));DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t cls[128]{};GetClassNameW(window,cls,128);
            if(pid!=static_cast<DWORD>(entry.GetNamedNumber(L"pid"))||JStr(entry,L"class")!=cls||!GetPropW(window,property.c_str()))continue;
            // Explorer can hide a taskbar transiently (fullscreen or an earlier failed exit).
            // Its WS_VISIBLE snapshot is not the user's auto-hide preference.
            bool taskbar=ExplorerTaskbar(window),visible=taskbar||entry.GetNamedBoolean(L"visible",false);
            requests.push_back({window,visible,taskbar,entry});
            if(taskbar)ShowTaskbar(window);else ShowWindowAsync(window,visible?SW_SHOWNOACTIVATE:SW_HIDE);
        }
        auto completed=[](const RestoreRequest& request){return !IsWindow(request.window)||(VisibleStyle(request.window)==request.visible&&(!request.taskbar||(GetWindowLongPtrW(request.window,GWL_EXSTYLE)&WS_EX_TOPMOST)));};
        auto until=GetTickCount64()+1500;
        while(GetTickCount64()<until){bool ready=true;for(const auto& request:requests)ready&=completed(request);if(ready)break;Sleep(15);}
        json::JsonArray pending;
        for(const auto& request:requests){if(completed(request))RemovePropW(request.window,property.c_str());else pending.Append(request.entry);}
        if(pending.Size()){root.SetNamedValue(L"windows",pending);WriteAtomic(path,std::wstring(root.Stringify()));}
        else {std::error_code ec;fs::remove(path,ec);}
    }catch(...){}
}
void ShellChrome::Restore(){if(active_||fs::exists(dir_/L"shell-original.json")){Recover(dir_);active_=false;}}

std::vector<ProgramFolder> ProgramFolders(){
    std::vector<ProgramFolder> folders;for(auto id:{FOLDERID_Programs,FOLDERID_CommonPrograms,FOLDERID_Desktop,FOLDERID_PublicDesktop}){PWSTR path=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&path)))folders.push_back({path,IsEqualGUID(id,FOLDERID_Programs)||IsEqualGUID(id,FOLDERID_CommonPrograms)});CoTaskMemFree(path);}return folders;
}
std::vector<Program> InstalledPrograms(){
    std::vector<Program> result;std::set<std::wstring> names;
    auto add=[&](std::wstring name,std::wstring target){if(name.empty()||target.empty()||result.size()>=4000)return;auto key=name;std::transform(key.begin(),key.end(),key.begin(),towlower);if(names.insert(key).second)result.push_back({std::move(name),std::move(target)});};
    for(const auto& folderPath:ProgramFolders()){
        {
            std::error_code ec;fs::recursive_directory_iterator iterator(folderPath.path,fs::directory_options::skip_permission_denied,ec),end;
            for(;iterator!=end&&result.size()<4000;iterator.increment(ec)){if(ec){ec.clear();continue;}auto extension=iterator->path().extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
                if(!folderPath.recursive)iterator.disable_recursion_pending();if(extension==L".lnk"||extension==L".appref-ms"||extension==L".url")add(iterator->path().stem().wstring(),iterator->path().wstring());
            }
        }
    }
    ComPtr<IShellItem> folder;ComPtr<IEnumShellItems> items;
    if(SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&folder)))&&SUCCEEDED(folder->BindToHandler(nullptr,BHID_EnumItems,IID_PPV_ARGS(&items)))){
        ComPtr<IShellItem> item;ULONG fetched=0;
        while(result.size()<4000&&items->Next(1,&item,&fetched)==S_OK){PWSTR name=nullptr,target=nullptr;item->GetDisplayName(SIGDN_NORMALDISPLAY,&name);item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING,&target);if(name&&target)add(name,L"shell:AppsFolder\\"+std::wstring(target));CoTaskMemFree(name);CoTaskMemFree(target);item.Reset();}
    }
    std::sort(result.begin(),result.end(),[](const Program& a,const Program& b){return CompareStringOrdinal(a.name.c_str(),-1,b.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;});return result;
}
bool Wallpaper::Apply(const Scene& s){if(s.gaming){Restore();return true;}ComPtr<IDesktopWallpaper>wall;if(FAILED(CoCreateInstance(CLSID_DesktopWallpaper,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&wall))))return false;
    if(!fs::exists(dir_/L"wallpaper-original.json")){json::JsonObject o;DESKTOP_WALLPAPER_POSITION pos=DWPOS_FILL;wall->GetPosition(&pos);COLORREF color=0;wall->GetBackgroundColor(&color);o.SetNamedValue(L"position",N(pos));o.SetNamedValue(L"color",N(color));json::JsonArray a;UINT count=0;wall->GetMonitorDevicePathCount(&count);for(UINT i=0;i<count;++i){LPWSTR id=nullptr,path=nullptr;if(SUCCEEDED(wall->GetMonitorDevicePathAt(i,&id))){wall->GetWallpaper(id,&path);json::JsonObject m;m.SetNamedValue(L"id",S(id));m.SetNamedValue(L"path",S(path?path:L""));a.Append(m);}CoTaskMemFree(id);CoTaskMemFree(path);}o.SetNamedValue(L"monitors",a);if(!WriteAtomic(dir_/L"wallpaper-original.json",std::wstring(o.Stringify())))return false;}
    fs::path target=s.wallpaper;if(target.empty()){target=dir_/(L"backdrop-"+s.accent+L"-"+s.backdrop+L".bmp");std::error_code ec;fs::create_directories(dir_,ec);if(!fs::exists(target)&&!GenerateBackdrop(target,s))return false;}
    if(!fs::exists(target))return false;wall->SetPosition(DWPOS_FILL);return SUCCEEDED(wall->SetWallpaper(nullptr,target.c_str()));
}
void Wallpaper::Restore(bool finish){RestoreWallpaperSnapshot(dir_,finish);}
}

