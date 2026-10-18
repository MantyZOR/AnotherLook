#include "ui.h"
#include <algorithm>
#include <windowsx.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <thread>

namespace gv {
struct DesktopRegressionProbe {
    Application app;json::JsonArray checks;bool passed=true,performanceBlocked=false;
    inline static unsigned motionTimers=0;
    static LRESULT CALLBACK MotionTimerTrace(int code,WPARAM w,LPARAM l){if(code>=0){auto& message=*reinterpret_cast<CWPSTRUCT*>(l);if(message.message==WM_TIMER&&message.wParam==82)++motionTimers;}return CallNextHookEx(nullptr,code,w,l);}
    explicit DesktopRegressionProbe(const fs::path& path):app(path,true){}
    void Check(bool value,std::wstring_view name){json::JsonObject item;item.SetNamedValue(L"name",json::JsonValue::CreateStringValue(name));item.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(value));checks.Append(item);passed&=value;}
    static void Pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}}
    static bool WaitForeground(HWND window){auto until=GetTickCount64()+1000;do{Pump();if(GetForegroundWindow()==window)return true;Sleep(10);}while(GetTickCount64()<until);return false;}
    static void PumpFor(ULONGLONG milliseconds){auto until=GetTickCount64()+milliseconds;do{Pump();Sleep(5);}while(GetTickCount64()<until);}
    static unsigned ExecutableInstances(){
        unsigned count=0;auto executable=ExePath();HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return 0;
        PROCESSENTRY32W entry{sizeof(entry)};
        if(Process32FirstW(snapshot,&entry))do{
            if(_wcsicmp(entry.szExeFile,executable.filename().c_str())!=0)continue;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);if(!process)continue;
            wchar_t path[32768]{};DWORD size=32768;if(QueryFullProcessImageNameW(process,0,path,&size)&&_wcsicmp(path,executable.c_str())==0)++count;CloseHandle(process);
        }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return count;
    }
    void WinKeyChecks(){
        for(UINT win:{VK_LWIN,VK_RWIN}){
            WinTapSequence sequence;sequence.Key(win,true);sequence.Key(win,true);Check(sequence.Key(win,false),L"a solitary repeated Win press is masked on release");
            for(UINT key:std::initializer_list<UINT>{'E','L','R','D',VK_TAB}){sequence={};sequence.Key(win,true);sequence.Key(key,true);sequence.Key(key,false);Check(!sequence.Key(win,false),L"Win shortcut is passed through without injected replacement keys");}
            sequence={};sequence.Key(win,true,true);Check(!sequence.Key(win,false),L"a modifier held before Win remains a native combination");
            sequence.Key(win,true);Check(sequence.Key(win,false),L"a completed shortcut never disables the next solitary Win press");
        }
    }
    void SearchOpeningChecks(){
        auto saved=app.config.ui;auto& launcher=app.launcher;launcher.Create(false);
        struct Placement {const wchar_t* position;float x,y;DWORD direction;};
        for(auto placement:{Placement{L"top",.5f,0,AW_VER_POSITIVE},Placement{L"bottom",.5f,1,AW_VER_NEGATIVE},Placement{L"left",0,.5f,AW_HOR_POSITIVE},Placement{L"right",1,.5f,AW_HOR_NEGATIVE},Placement{L"custom",.5f,.1f,AW_VER_POSITIVE},Placement{L"custom",.5f,.9f,AW_VER_NEGATIVE},Placement{L"custom",.05f,.4f,AW_HOR_POSITIVE},Placement{L"custom",.95f,.6f,AW_HOR_NEGATIVE}}){
            auto& ui=app.config.ui;ui.searchPosition=placement.position;ui.searchX=placement.x;ui.searchY=placement.y;
            Check(launcher.OpeningAnimation()==(AW_SLIDE|placement.direction),L"search slides from its configured edge, including manually placed blocks");
            launcher.Hide();auto openingStarted=GetTickCount64();launcher.Show();auto openingElapsed=GetTickCount64()-openingStarted;
            BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
            if(animations&&!GetSystemMetrics(SM_REMOTESESSION))Check(openingElapsed>=100,L"the real native opening animation runs instead of silently falling back to an instant show");
            RECT actual{};GetWindowRect(launcher.window_,&actual);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(launcher.window_,MONITOR_DEFAULTTONEAREST),&monitor);
            float scale=GetDpiForWindow(launcher.window_)/96.f;auto bounds=monitor.rcMonitor;auto target=LauncherBox(ui,{0,0,(bounds.right-bounds.left)/scale,(bounds.bottom-bounds.top)/scale});
            Check(launcher.Visible()&&GetFocus()==launcher.search_&&actual.left==bounds.left+static_cast<int>(target.x*scale)&&actual.top==bounds.top+static_cast<int>(target.y*scale),L"opening animation ends at the saved position with the search field ready for typing");
            HRGN region=CreateRectRgn(0,0,0,0);RECT clip{};GetWindowRgn(launcher.window_,region);GetRgnBox(region,&clip);DeleteObject(region);
            Check(clip.right>=actual.right-actual.left-1&&clip.bottom>=actual.bottom-actual.top-1,L"a completed slide leaves the entire rounded search window visible");
        }
        app.config.ui.searchPosition=L"center";Check(launcher.OpeningAnimation()==AW_BLEND,L"a centered search uses a neutral fade");
        launcher.Hide();app.config.ui=saved;launcher.UpdateStyle();
    }
    void WinReturnChecks(HWND fixture){
        auto active=app.config.active;auto& launcher=app.launcher;
        for(UINT win:{VK_LWIN,VK_RWIN}){
            launcher.Hide();FocusAppWindow(fixture);
            bool focused=WaitForeground(fixture);Check(focused,L"the ordinary fixture app is active before a real Win tap");if(!focused)continue;
            if(win==VK_RWIN){launcher.Show();Check(WaitForeground(launcher.window_),L"search is active before returning home with Win");}
            app.winKey.Start(app.events,WM_APP+4);
            INPUT keys[2]{};for(auto& key:keys){key.type=INPUT_KEYBOARD;key.ki.wVk=static_cast<WORD>(win);key.ki.dwFlags=KEYEVENTF_EXTENDEDKEY;}
            keys[1].ki.dwFlags|=KEYEVENTF_KEYUP;
            Check(SendInput(2,keys,sizeof(INPUT))==2,L"native Win down and up reach the installed keyboard hook");
            POINT pointer{};GetCursorPos(&pointer);HWND home=app.boards.front()->hwnd;auto monitor=MonitorFromPoint(pointer,MONITOR_DEFAULTTOPRIMARY);
            for(const auto& board:app.boards)if(MonitorFromWindow(board->hwnd,MONITOR_DEFAULTTOPRIMARY)==monitor)home=board->hwnd;
            bool returned=WaitForeground(home);Pump();app.winKey.Stop();
            Check(returned&&app.config.active==active&&!launcher.Visible()&&PanelsVisible(),L"a real solitary Win tap returns to the same workspace and closes search");
        }
        launcher.Hide();FocusAppWindow(fixture);WaitForeground(fixture);
        app.config.enabled=false;SendMessageW(app.events,WM_APP+4,0,0);Pump();
        Check(!app.config.enabled&&GetForegroundWindow()==fixture,L"a queued Win tap cannot resume a paused shell");app.config.enabled=true;
        app.systemPrompt_=true;SendMessageW(app.events,WM_APP+4,0,0);Pump();
        Check(GetForegroundWindow()==fixture,L"a queued Win tap cannot cover a system consent dialog");app.systemPrompt_=false;
        app.ShowDesktop();Pump();
    }
    void BurstWinChecks(HWND fixture){
        ShowWindow(fixture,SW_RESTORE);FocusAppWindow(fixture);if(!WaitForeground(fixture)){Check(false,L"fixture is active before the Win burst");return;}
        app.winKey.Start(app.events,WM_APP+4);bool sent=false,keptForeground=false;
        std::thread sender([&]{Sleep(50);INPUT input[80]{};for(int i=0;i<80;++i){input[i].type=INPUT_KEYBOARD;input[i].ki.wVk=(i/2)%2?VK_RWIN:VK_LWIN;input[i].ki.dwFlags=KEYEVENTF_EXTENDEDKEY|((i%2)?KEYEVENTF_KEYUP:0);}sent=SendInput(80,input,sizeof(INPUT))==80;Sleep(120);keptForeground=GetForegroundWindow()==fixture;});
        // A slow paint must not remove the keyboard hook or let Start escape.
        Sleep(1400);sender.join();Check(sent&&keptForeground,L"rapid Win taps stay intercepted while the UI thread is busy for longer than the hook timeout");
        Pump();bool home=false;for(const auto& board:app.boards)home|=GetForegroundWindow()==board->hwnd;
        Check(home&&!app.launcher.Visible(),L"a coalesced Win burst returns to a complete workspace");
        Check(!(GetAsyncKeyState(VK_LWIN)&0x8000)&&!(GetAsyncKeyState(VK_RWIN)&0x8000),L"burst interception leaves neither Windows key stuck");app.winKey.Stop();
    }
    void OrbitChecks(){
        auto saved=app.config.ui;auto savedWindows=app.openWindows;std::vector<HWND> fixtures;std::vector<HICON> fixtureIcons;
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GoodView.Orbit.Fixture";RegisterClassW(&cls);
        app.openWindows.clear();for(int i=0;i<24;++i){HWND window=CreateWindowW(cls.lpszClassName,(L"Папка "+std::to_wstring(i+1)).c_str(),WS_OVERLAPPEDWINDOW,app.monitors[0].bounds.left+100,app.monitors[0].bounds.top+120,160,120,nullptr,nullptr,cls.hInstance,nullptr);fixtures.push_back(window);WindowInfo info;info.hwnd=window;info.pid=GetCurrentProcessId();info.title=L"Папка "+std::to_wstring(i+1);info.process=ExePath().wstring();info.imageName=L"WindowsGoodView.exe";app.openWindows.push_back(info);
            HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bitmapInfo{};bitmapInfo.bmiHeader={sizeof(BITMAPINFOHEADER),32,-32,1,32,BI_RGB,0,0,0,0,0};void* data=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bitmapInfo,DIB_RGB_COLORS,&data,nullptr,0);auto* pixels=static_cast<DWORD*>(data);std::fill(pixels,pixels+1024,0);auto previous=SelectObject(dc,bitmap);auto font=UiFont(23,96,600);auto previousFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));RECT iconRect{0,0,32,32};auto number=std::to_wstring(i+1);DrawTextW(dc,number.c_str(),-1,&iconRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);for(int pixel=0;pixel<1024;++pixel){auto coverage=std::max({pixels[pixel]&255,(pixels[pixel]>>8)&255,(pixels[pixel]>>16)&255});pixels[pixel]=coverage*0x01010101u;}SelectObject(dc,previousFont);DeleteObject(font);SelectObject(dc,previous);DeleteDC(dc);BYTE maskBits[128]{};auto mask=CreateBitmap(32,32,1,1,maskBits);ICONINFO iconInfo{TRUE,0,0,mask,bitmap};auto icon=CreateIconIndirect(&iconInfo);DeleteObject(mask);DeleteObject(bitmap);fixtureIcons.push_back(icon);SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));
        }
        app.config.ui.railOrbit=true;app.config.ui.textScale=1;auto& rail=*app.rails[0];rail.orbit_.Set(0,24);rail.Place(true);rail.Show(true);rail.Invalidate();UpdateWindow(rail.hwnd);
        HRGN region=CreateRectRgn(0,0,0,0);RECT client{};GetClientRect(rail.hwnd,&client);
        Check(GetWindowRgn(rail.hwnd,region)==ERROR&&(GetWindowLongPtrW(rail.hwnd,GWL_EXSTYLE)&WS_EX_LAYERED),L"the ring uses pixel alpha without a hard region clipping its contours");DeleteObject(region);
        Check(rail.panelBitmap_.Present(rail.hwnd),L"Windows accepts the native premultiplied ring buffer");
        auto size=rail.panelBitmap_.Size();auto pixels=rail.panelBitmap_.Pixels();
        bool alpha=pixels&&size.cx>0&&size.cy>0;size_t partial=0;if(alpha)for(size_t i=0,n=static_cast<size_t>(size.cx)*size.cy;i<n;++i){unsigned a=pixels[i]>>24;if(a>0&&a<255)++partial;}
        Check(alpha&&pixels[0]==0&&pixels[static_cast<size_t>(size.cy/2)*size.cx+size.cx/2-40]==0&&partial>40,L"the native ring preserves a transparent hole and antialiased inner and outer edges");
        auto segmentsSeparated=[&](int visible){
            auto dimensions=rail.panelBitmap_.Size();auto buffer=rail.panelBitmap_.Pixels();if(!buffer||dimensions.cx<=0||dimensions.cy<=0)return false;
            float scale=std::min(dimensions.cx,dimensions.cy)/280.f,cx=dimensions.cx/2.f,cy=dimensions.cy/2.f;
            auto coverage=[&](float x,float y){int px=static_cast<int>(std::lround(x)),py=static_cast<int>(std::lround(y));return buffer[static_cast<size_t>(py)*dimensions.cx+px]>>24;};
            for(int i=0;i<visible;++i){float gap=-1.5707963268f-i*6.2831853072f/visible,middle=gap-3.1415926536f/visible;
                if(coverage(cx+126*scale*std::cos(gap),cy+126*scale*std::sin(gap))!=0||coverage(cx+126*scale*std::cos(middle),cy+126*scale*std::sin(middle))!=255)return false;
            }
            // The entire icon square fits inside its arc, including its corners.
            float dpiScale=GetDpiForWindow(rail.hwnd)/96.f;
            for(const auto& hit:rail.hits_){float x=(hit.box.x+hit.box.w/2)*dpiScale,y=(hit.box.y+hit.box.h/2)*dpiScale;
                for(int dx:{-13,13})for(int dy:{-13,13})if(coverage(x+dx*scale,y+dy*scale)!=255)return false;
            }
            return true;
        };
        Check(segmentsSeparated(9),L"nine native arc segments have transparent gaps and enclose every icon corner");
        Check(rail.hits_.size()==9&&rail.hits_[0].tooltip==L"Папка 1"&&rail.hits_[8].tooltip==L"Папка 9",L"the first lap has nine evenly arranged individual windows");auto initialHits=rail.hits_;
        Check(rail.ExportPreview(app.dataDir/L"ring-first.png"),L"the actual native renderer exports the first lap with numbered fixture icons");
        SendMessageW(rail.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);UpdateWindow(rail.hwnd);Check(rail.orbit_.Target()==1,L"one wheel notch queues exactly one replacement");PumpFor(260);UpdateWindow(rail.hwnd);
        Check(!rail.orbit_.Running()&&rail.orbit_.Position()==1&&rail.hits_[0].tooltip==L"Папка 10"&&rail.hits_[8].tooltip==L"Папка 9",L"the tenth window replaces the first while the ninth stays in its slot");
        rail.orbit_.Set(.2f,24);Check(rail.ExportPreview(app.dataDir/L"ring-incoming.png"),L"the renderer exports the faint incoming tenth icon");
        rail.orbit_.Set(3,24);rail.Invalidate();UpdateWindow(rail.hwnd);bool fixed=rail.hits_.size()==9;for(size_t i=0;i<rail.hits_.size()&&i<initialHits.size();++i)fixed&=rail.hits_[i].box.x==initialHits[i].box.x&&rail.hits_[i].box.y==initialHits[i].box.y;
        Check(fixed&&rail.hits_[0].tooltip==L"Папка 10"&&rail.hits_[1].tooltip==L"Папка 11"&&rail.hits_[2].tooltip==L"Папка 12"&&rail.hits_[6].tooltip==L"Папка 7",L"10 11 and 12 replace the first three slots exactly as drawn without shifting the others");Check(rail.ExportPreview(app.dataDir/L"ring-next.png"),L"the updated partial lap exports as a native preview");
        SendMessageW(rail.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-40*WHEEL_DELTA)),0);PumpFor(260);UpdateWindow(rail.hwnd);
        Check(rail.orbit_.Position()==15&&rail.hits_[5].tooltip==L"Папка 24"&&rail.hits_[6].tooltip==L"Папка 16",L"fast scrolling reaches the incomplete last lap without dropping a window");
        auto paints=app.panelPaints;PumpFor(220);Check(!rail.orbit_.Running()&&paints==app.panelPaints,L"the replacement timer stops completely when the wheel settles");
        SendMessageW(rail.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(WHEEL_DELTA)),0);PumpFor(260);UpdateWindow(rail.hwnd);Check(rail.orbit_.Position()==14&&rail.hits_[5].tooltip==L"Папка 15",L"reverse wheel scrolling brings the overwritten window back into the same slot");
        app.config.ui.textScale=1.3f;rail.Place(true);rail.Invalidate();UpdateWindow(rail.hwnd);
        Check(segmentsSeparated(9),L"130 percent interface scale preserves transparent segment gaps and icon enclosure");
        Check(rail.ExportPreview(app.dataDir/L"ring-130.png"),L"the native segmented ring exports at 130 percent interface scale");
        app.config.ui.textScale=1;rail.Place(true);
        for(size_t i=4;i<fixtures.size();++i)DestroyWindow(fixtures[i]);app.openWindows.resize(4);rail.Invalidate();UpdateWindow(rail.hwnd);Check(rail.hits_.size()==4&&rail.orbit_.Position()==0&&!rail.orbit_.Running(),L"closing windows clamps the last lap and leaves no empty hit targets");
        Check(segmentsSeparated(4),L"four windows get four equally spaced arc segments without icon clipping");
        for(size_t i=1;i<4;++i)DestroyWindow(fixtures[i]);app.openWindows.resize(1);rail.Invalidate();UpdateWindow(rail.hwnd);
        Check(rail.hits_.size()==1&&segmentsSeparated(1),L"one window gets a continuous long arc with one transparent seam");
        Check(rail.ExportPreview(app.dataDir/L"ring-single.png"),L"the almost-complete single-window arc exports correctly");
        DestroyWindow(fixtures[0]);app.openWindows.clear();rail.Invalidate();UpdateWindow(rail.hwnd);
        Check(rail.hits_.empty()&&segmentsSeparated(1),L"the empty ring has no stale window hit targets and preserves its transparent seam");
        app.config.ui.railOrbit=false;rail.Place(true);rail.Invalidate();UpdateWindow(rail.hwnd);Check(rail.panelBitmap_.Pixels()&&(rail.panelBitmap_.Pixels()[static_cast<size_t>(rail.panelBitmap_.Size().cy/2)*rail.panelBitmap_.Size().cx+rail.panelBitmap_.Size().cx/2]>>24)==255,L"switching to the ordinary panel restores an opaque clickable centre");
        for(HWND window:fixtures)if(IsWindow(window))DestroyWindow(window);for(HICON icon:fixtureIcons)if(icon)DestroyIcon(icon);app.openWindows=savedWindows;app.config.ui=saved;rail.orbit_.Set(0,static_cast<int>(savedWindows.size()));rail.Place(true);rail.Invalidate();
    }
    void EnterChecks(HWND fixture){
        auto& launcher=app.launcher;launcher.Create(false);
        for(int minimized:{0,1}){
            ShowWindow(fixture,minimized?SW_MINIMIZE:SW_RESTORE);FocusAppWindow(fixture);WaitForeground(fixture);
            if(minimized)ShowWindow(fixture,SW_MINIMIZE);
            SendMessageW(fixture,WM_ACTIVATE,WA_ACTIVE,0);launcher.Show();Check(WaitForeground(launcher.window_),L"search receives foreground before testing Enter");
            WindowInfo info;info.hwnd=fixture;GetWindowThreadProcessId(fixture,&info.pid);info.process=ExePath().wstring();info.imageName=L"WindowsGoodView.exe";info.title=L"Unique existing fixture";info.className=L"GoodView.Fixture";
            launcher.running_={info};launcher.programs_.clear();SetWindowTextW(launcher.search_,L"Unique existing fixture");launcher.Filter();
            Check(launcher.Selected()&&launcher.Selected()->window==fixture,L"an existing application is the selected search result");
            MSG key{};key.hwnd=launcher.search_;key.message=WM_KEYDOWN;key.wParam=VK_RETURN;
            Check((SendMessageW(launcher.search_,WM_GETDLGCODE,VK_RETURN,reinterpret_cast<LPARAM>(&key))&DLGC_WANTMESSAGE)!=0,L"search retains Enter in a nested dialog loop");
            // Translate and dispatch exactly as a modal/COM loop does, without
            // Application::Run calling Launcher::Message first.
            TranslateMessage(&key);DispatchMessageW(&key);PumpFor(120);
            Check(WaitForeground(fixture)&&!IsIconic(fixture)&&!launcher.Visible(),minimized?L"Enter restores an already minimized app through the child input handler":L"Enter focuses an already visible app through the child input handler");
            Check(SendMessageW(launcher.search_,WM_CHAR,L'\r',0)==0,L"the translated Enter character is consumed without an edit-control beep");
            Check(ExecutableInstances()==2,L"Enter reuses the selected app instead of starting another instance");
        }
        app.CancelProgramActivation();launcher.Hide();
        auto motion=app.Active().networkMotion,gaming=app.Active().gaming,wallpaper=app.config.wallpaperEnabled;auto mode=app.Active().network;
        app.Active().networkMotion=true;app.Active().gaming=false;app.Active().network=L"links";app.config.wallpaperEnabled=false;app.ShowDesktop();PumpFor(80);app.SyncPanels(true);
        Check(std::any_of(app.boards.begin(),app.boards.end(),[](const auto& board){return board->networkAnimating_;}),L"optional motion starts only while the canvas is active");
        FocusAppWindow(fixture);WaitForeground(fixture);app.SyncPanels();
        Check(std::none_of(app.boards.begin(),app.boards.end(),[](const auto& board){return board->networkAnimating_;}),L"selecting an application stops motion on every monitor");
        app.Active().networkMotion=motion;app.Active().gaming=gaming;app.Active().network=mode;app.config.wallpaperEnabled=wallpaper;
    }
    void PromptAndRemovalChecks(HWND fixture){
        app.StartGuardian();Check(app.guardianProcess!=nullptr,L"independent recovery is ready before testing a shell handoff");if(!app.guardianProcess)return;
        SendMessageW(fixture,WM_ACTIVATE,WA_ACTIVE,0);app.customization.Show(app.boards[0]->hwnd,true);Pump();auto owner=app.customization.Window();
        auto active=app.config.active;auto cards=app.Active().cards.size();
        bool result=app.WithSystemPrompt(owner,[&]{
            bool hidden=true;for(auto* list:{&app.boards,&app.bars,&app.rails,&app.tools})for(auto& surface:*list)hidden&=!IsWindowVisible(surface->hwnd);
            Check(hidden&&app.systemPrompt_&&!app.chrome.Active(),L"system elevation handoff removes all opaque shell surfaces before invoking Windows");
            app.SyncPanels(true);Check(!IsWindowVisible(app.boards[0]->hwnd),L"queued focus events cannot raise the canvas over a system prompt");
            return false;
        });
        Pump();Check(!result&&!app.systemPrompt_&&app.config.enabled&&app.config.active==active&&app.Active().cards.size()==cards&&PanelsVisible(),L"cancelled system consent automatically returns to the same space and panels");
        result=app.WithSystemPrompt(owner,[&]{
            PumpFor(200);bool visible=true;int taskbars=0;
            for(HWND window:ShellChrome::Discover()){wchar_t name[128]{};GetClassNameW(window,name,128);if(std::wstring_view(name)==L"Shell_TrayWnd"||std::wstring_view(name)==L"Shell_SecondaryTrayWnd"){++taskbars;visible&=IsWindowVisible(window)!=FALSE;}}
            Check(taskbars>0&&visible&&!app.chrome.Active(),L"a system prompt restores the actual Windows taskbars on all monitors");return true;
        });
        Check(result&&app.chrome.Active()&&PanelsVisible()&&app.config.active==active,L"confirmed system consent restores the shell and its saved space automatically");
        app.config.ui.windowRadius=0;app.customization.Refresh();Pump();HRGN region=CreateRectRgn(0,0,0,0);GetWindowRgn(owner,region);Check(PtInRegion(region,0,0)!=FALSE,L"zero rounding gives the settings window square corners");DeleteObject(region);
        app.config.ui.windowRadius=32;app.customization.Refresh();Pump();region=CreateRectRgn(0,0,0,0);GetWindowRgn(owner,region);Check(!PtInRegion(region,0,0),L"settings-window rounding updates its real native region immediately");DeleteObject(region);
        Check(app.customization.ExportPreview(app.dataDir/L"appearance-0.8.5.png",true),L"new appearance controls export from the real settings window");app.customization.Close();
        Scene remove;remove.id=L"remove-fixture";remove.name=L"Удаляемое";Card note;note.id=L"removed-note";note.content=L"Preserved in the backup";remove.cards.push_back(note);app.config.scenes.push_back(remove);app.SwitchScene(remove.id);
        if(app.windows.IsManaged(fixture))app.windows.MoveToScene(fixture,remove.id);else app.windows.Attach(fixture,remove.id);
        Check(app.RemoveScene(app.config.active)&&!FindScene(app.config,remove.id)&&IsWindow(fixture)&&app.windows.IsManaged(fixture),L"deleting an active space keeps its running application alive and managed");
        Check(std::any_of(app.windows.Entries().begin(),app.windows.Entries().end(),[&](const auto& entry){return entry.info.hwnd==fixture&&entry.scene==app.config.active;}),L"a removed space's application moves to the remaining active space");
        bool backup=false;for(const auto& entry:fs::directory_iterator(app.dataDir))if(entry.path().filename().wstring().starts_with(L"settings.before-delete-")){auto raw=ReadUtf8(entry.path());Config saved;std::wstring error;if(raw&&Deserialize(*raw,saved,error)&&FindScene(saved,remove.id))backup=true;}
        Check(backup,L"space removal writes a recoverable profile before deleting cards");
        auto scenes=app.config.scenes;auto current=app.config.active;app.config.scenes={app.Active()};Check(!app.RemoveScene(current)&&app.config.scenes.size()==1,L"the last remaining space cannot be deleted");app.config.scenes=std::move(scenes);
    }
    void VisualChecks(){
        auto& scene=app.Active();scene.cards.clear();scene.gaming=false;scene.network=L"links";scene.networkMotion=false;
        app.config.ui.dockPosition=L"custom";app.config.ui.dockX=0;app.config.ui.dockY=1;app.config.ui.dockVertical=false;app.config.wallpaperEnabled=false;
        Card network;network.id=L"network-layout";network.type=L"system";network.title=L"";network.appearance.metrics=8;network.appearance.variant=L"rings";network.appearance.showTitle=false;network.box={180,200,160,170};scene.cards.push_back(network);
        app.systemLoad.download=102400.;app.systemLoad.upload=98304.;app.systemLoad.network=10.f;
        for(float scale:{1.f,1.3f}){app.config.ui.textScale=scale;app.ApplyInterface();app.systemLoad.download=102400.;app.systemLoad.upload=98304.;app.systemLoad.network=10.f;Check(app.boards[0]->ExportPreview(app.dataDir/(scale==1?L"dock-network-100.png":L"dock-network-130.png")),L"dock labels and a 100 KB/s network ring render at supported text scales");}
        app.config.ui.textScale=1;scene.cards.clear();app.SyncSystemWidget();KillTimer(app.events,8);KillTimer(app.events,9);
    }
    void AnimationChecks(){
        auto& scene=app.Active();scene.gaming=false;scene.network=L"links";scene.networkDensity=2;scene.networkMotion=false;
        app.ShowDesktop();PumpFor(200);for(auto& board:app.boards)UpdateWindow(board->hwnd);motionTimers=0;auto timerTrace=SetWindowsHookExW(WH_CALLWNDPROC,MotionTimerTrace,nullptr,GetCurrentThreadId());PumpFor(250);if(timerTrace)UnhookWindowsHookEx(timerTrace);Check(timerTrace&&motionTimers==0&&std::none_of(app.boards.begin(),app.boards.end(),[](const auto& board){return board->networkAnimating_;}),L"default static backdrop adds no repeating paint timer");
        PROCESS_MEMORY_COUNTERS_EX staticMemory{sizeof(staticMemory)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&staticMemory),sizeof(staticMemory));scene.networkMotion=true;app.SyncPanels(true);PumpFor(2000);
        auto cpu=[](){FILETIME created{},ended{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user);ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return k.QuadPart+u.QuadPart;};
        for(auto& board:app.boards){board->profilePaint_=true;std::fill(std::begin(board->paintMs_),std::end(board->paintMs_),0);}
        PROCESS_MEMORY_COUNTERS_EX before{sizeof(before)},after{sizeof(after)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));auto start=GetTickCount64(),startCpu=cpu(),paints=app.paints;bool activeThroughout=true,foregroundHeld=true;auto until=start+5000;do{Pump();DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);foregroundHeld&=foregroundPid==GetCurrentProcessId()&&!app.launcher.Visible();activeThroughout&=std::all_of(app.boards.begin(),app.boards.end(),[](const auto& board){return board->networkAnimating_;});Sleep(5);}while(GetTickCount64()<until);auto elapsed=GetTickCount64()-start,used=cpu()-startCpu;GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
        double oneCore=used/10000./elapsed*100;SYSTEM_INFO system{};GetSystemInfo(&system);json::JsonObject report;
        report.SetNamedValue(L"cpuOneCorePercent",json::JsonValue::CreateNumberValue(oneCore));report.SetNamedValue(L"cpuMachinePercent",json::JsonValue::CreateNumberValue(oneCore/system.dwNumberOfProcessors));report.SetNamedValue(L"privateBytesBefore",json::JsonValue::CreateNumberValue(static_cast<double>(before.PrivateUsage)));report.SetNamedValue(L"privateBytesAfter",json::JsonValue::CreateNumberValue(static_cast<double>(after.PrivateUsage)));report.SetNamedValue(L"canvasPaints",json::JsonValue::CreateNumberValue(static_cast<double>(app.paints-paints)));report.SetNamedValue(L"elapsedMs",json::JsonValue::CreateNumberValue(static_cast<double>(elapsed)));report.SetNamedValue(L"monitors",json::JsonValue::CreateNumberValue(static_cast<double>(app.boards.size())));WriteAtomic(app.dataDir/L"animation-cost.json",std::wstring(report.Stringify()));
        json::JsonObject timings;const wchar_t* stages[]={L"backdropMs",L"graphMs",L"edgesMs",L"nodesMs",L"presentMs"};for(int i=0;i<5;++i){double total=0;for(auto& board:app.boards)total+=board->paintMs_[i];timings.SetNamedValue(stages[i],json::JsonValue::CreateNumberValue(total));}for(auto& board:app.boards)board->profilePaint_=false;WriteAtomic(app.dataDir/L"paint-cost.json",std::wstring(timings.Stringify()));
        report.SetNamedValue(L"activeThroughout",json::JsonValue::CreateBooleanValue(activeThroughout));
        performanceBlocked=!foregroundHeld;
        if(foregroundHeld){
            Check(activeThroughout,L"motion stays enabled while the measured desktop retains the foreground");
        Check(app.paints>paints&&oneCore<20,L"optional two-monitor motion remains within its bounded drawing budget");Check((app.paints-paints)*1000./elapsed/app.boards.size()>=14,L"motion keeps a smooth cadence instead of reducing the update rate to one frame per second");
        Check(after.PrivateUsage<=before.PrivateUsage+2097152,L"optional motion has no growing per-frame memory allocation");
        }else report.SetNamedValue(L"blockedReason",json::JsonValue::CreateStringValue(L"Another application held the foreground; animation correctly paused, so its cadence cannot be measured."));
        scene.gaming=true;app.SyncPanels(true);Check(std::none_of(app.boards.begin(),app.boards.end(),[](const auto& board){return board->networkAnimating_;}),L"game spaces turn off every motion timer");
        scene.gaming=false;scene.networkMotion=false;app.SyncPanels(true);
        for(auto& board:app.boards){board->Invalidate();UpdateWindow(board->hwnd);}PumpFor(200);PROCESS_MEMORY_COUNTERS_EX stopped{sizeof(stopped)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&stopped),sizeof(stopped));
        report.SetNamedValue(L"staticPrivateBytes",json::JsonValue::CreateNumberValue(static_cast<double>(staticMemory.PrivateUsage)));report.SetNamedValue(L"staticWorkingSetBytes",json::JsonValue::CreateNumberValue(static_cast<double>(staticMemory.WorkingSetSize)));report.SetNamedValue(L"motionWorkingSetBytes",json::JsonValue::CreateNumberValue(static_cast<double>(after.WorkingSetSize)));report.SetNamedValue(L"stoppedPrivateBytes",json::JsonValue::CreateNumberValue(static_cast<double>(stopped.PrivateUsage)));report.SetNamedValue(L"stoppedWorkingSetBytes",json::JsonValue::CreateNumberValue(static_cast<double>(stopped.WorkingSetSize)));WriteAtomic(app.dataDir/L"animation-cost.json",std::wstring(report.Stringify()));
    }
    static bool Above(HWND upper,HWND lower){for(HWND h=GetTopWindow(nullptr);h;h=GetWindow(h,GW_HWNDNEXT)){if(h==upper)return true;if(h==lower)return false;}return false;}
    bool PanelsVisible(){bool visible=true;for(auto* list:{&app.bars,&app.rails,&app.tools})for(auto& surface:*list)visible&=IsWindowVisible(surface->hwnd)&&Above(surface->hwnd,app.boards[surface->monitor]->hwnd);return visible;}
    void Snapshot(const wchar_t* name,HWND fixture){json::JsonObject report;json::JsonArray windows;DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);report.SetNamedValue(L"foregroundIsFixture",json::JsonValue::CreateBooleanValue(GetForegroundWindow()==fixture));report.SetNamedValue(L"foregroundIsSelf",json::JsonValue::CreateBooleanValue(foregroundPid==GetCurrentProcessId()));for(auto* list:{&app.boards,&app.bars,&app.rails,&app.tools})for(auto& surface:*list){json::JsonObject item;item.SetNamedValue(L"kind",json::JsonValue::CreateStringValue(surface->bar?L"bar":surface->rail?L"rail":L"canvas"));item.SetNamedValue(L"monitor",json::JsonValue::CreateNumberValue(surface->monitor));item.SetNamedValue(L"visible",json::JsonValue::CreateBooleanValue(IsWindowVisible(surface->hwnd)!=FALSE));item.SetNamedValue(L"aboveFixture",json::JsonValue::CreateBooleanValue(Above(surface->hwnd,fixture)));item.SetNamedValue(L"aboveCanvas",json::JsonValue::CreateBooleanValue(Above(surface->hwnd,app.boards[surface->monitor]->hwnd)));windows.Append(item);}report.SetNamedValue(L"windows",windows);WriteAtomic(app.dataDir/(std::wstring(name)+L".json"),std::wstring(report.Stringify()));}
    void DragChecks(){
        auto& board=*app.boards[0];app.config.active=app.config.scenes[0].id;app.config.ui.railPosition=L"hidden";
        int launches=0;
        for(auto variant:{L"icon",L"tile",L"row"}){
            auto& scene=app.Active();scene.cards.clear();Card card;card.id=L"drag-fixture";card.type=L"launcher";card.title=L"Тест приложения";card.target=ExePath().wstring();card.monitor=0;card.appearance.variant=variant;card.box={120,144,308,236};scene.cards.push_back(card);
            board.Invalidate();UpdateWindow(board.hwnd);auto found=std::find_if(board.hits_.begin(),board.hits_.end(),[](const Hit& hit){return !hit.draggable.empty();});
            Check(found!=board.hits_.end(),std::wstring(variant)+L" application exposes a draggable launch target");if(found==board.hits_.end())continue;
            found->action=[&]{++launches;};float dpi=GetDpiForWindow(board.hwnd)/96.f;POINT down{static_cast<LONG>((found->box.x+found->box.w/2)*dpi),static_cast<LONG>((found->box.y+found->box.h/2)*dpi)};auto before=scene.cards[0].box;
            board.MouseDown(down);board.MouseUp(down);Check(launches==1,std::wstring(variant)+L" plain click still launches exactly once");launches=0;
            board.Invalidate();UpdateWindow(board.hwnd);for(auto& hit:board.hits_)if(!hit.draggable.empty())hit.action=[&]{++launches;};
            board.MouseDown(down);POINT moved{down.x+static_cast<LONG>(80*dpi),down.y+static_cast<LONG>(48*dpi)};board.MouseMove(moved,MK_LBUTTON);board.MouseUp(moved);
            Check(scene.cards[0].box.x==before.x+80&&scene.cards[0].box.y==before.y+48,std::wstring(variant)+L" drag moves the application card");Check(!launches,std::wstring(variant)+L" dragging never launches the application");
            board.Invalidate();UpdateWindow(board.hwnd);auto menu=std::find_if(board.hits_.rbegin(),board.hits_.rend(),[](const Hit& hit){return hit.canvas&&hit.draggable.empty();});
            if(menu!=board.hits_.rend()){bool clicked=false;menu->action=[&]{clicked=true;};POINT point{static_cast<LONG>((menu->box.x+menu->box.w/2)*dpi),static_cast<LONG>((menu->box.y+menu->box.h/2)*dpi)};board.MouseDown(point);board.MouseMove({point.x+8,point.y},MK_LBUTTON);board.MouseUp(point);Check(clicked&&board.drag_.empty(),std::wstring(variant)+L" card menu remains a button");}
        }
        app.config.ui.railPosition=L"left";for(auto& rail:app.rails){rail->Place(true);rail->Show(true);}
    }
    void GlyphChecks(){
        int width=640,height=320;HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto previous=SelectObject(dc,bitmap);RECT all{0,0,width,height};RoundFill(dc,all,RGB(24,39,61),0);auto font=UiFont(15,96);auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(236,242,253));
        int column=0;for(auto [glyph,label]:{std::pair{Glyph::Game,L"Игры"},std::pair{Glyph::Rocket,L"Новый старт"},std::pair{Glyph::Compass,L"Исследовать"},std::pair{Glyph::Moon,L"Ночь"}}){auto path=app.IconPath(glyph);D2D1_RECT_F bounds{};if(path)path->GetWidenedBounds(1.65f,app.iconStroke.Get(),nullptr,&bounds);Check(path&&bounds.left>=1&&bounds.top>=1&&bounds.right<=23&&bounds.bottom<=23,std::wstring(label)+L" vector stays inside its 24 px grid");Check(path&&path==app.IconPath(glyph),std::wstring(label)+L" Direct2D path is cached");
            int x=column++*160;for(auto [size,y]:{std::pair{20,38},std::pair{24,90},std::pair{48,150},std::pair{72,224}})DrawGlyph(dc,glyph,{x+(160-size)/2,y,x+(160+size)/2,y+size},RGB(236,242,253));RECT text{x,6,x+160,30};DrawTextW(dc,label,-1,&text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        Check(SaveBitmapPng(bitmap,app.dataDir/L"glyphs.png"),L"new glyphs export at actual toolbar and large preview sizes");SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    }
    int Run(){
        fs::create_directories(app.dataDir);
        auto file=app.dataDir/L"fixture-settings.json";if(fs::exists(file)){std::wstring error;app.config=LoadConfig(file,error);if(!error.empty())return 2;}
        WinKeyChecks();app.monitors=GetMonitors();if(app.monitors.empty())return 2;
        app.config.enabled=true;app.preview=false;for(auto& scene:app.config.scenes){scene.windows.clear();scene.autoTile=false;for(auto& card:scene.cards)card.autoLaunch=false;}
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Application::EventProc;cls.lpszClassName=L"GoodView.DesktopRegression.Events";RegisterClassW(&cls);app.events=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,&app);
        for(size_t i=0;i<app.monitors.size();++i){auto board=std::make_unique<Surface>(app,false,static_cast<int>(i));board->Create(false);board->Show(true);app.boards.push_back(std::move(board));auto bar=std::make_unique<Surface>(app,true,static_cast<int>(i));bar->Create(true);bar->Show(true);app.bars.push_back(std::move(bar));auto rail=std::make_unique<Surface>(app,false,static_cast<int>(i),true);rail->Create(true);rail->Show(true);app.rails.push_back(std::move(rail));for(auto kind:{UiElement::Search,UiElement::Settings}){auto tool=std::make_unique<Surface>(app,true,static_cast<int>(i),false,kind);tool->Create(true);tool->Show(true);app.tools.push_back(std::move(tool));}}
        app.notificationTray.Start(true);
        // The test starts without a user's click. Establish its initial foreground
        // once, then exercise normal app and workspace activation throughout.
        wchar_t orbitOnly[2]{};bool onlyOrbit=GetEnvironmentVariableW(L"GOODVIEW_TEST_ORBIT_ONLY",orbitOnly,2)!=0;
        if(!onlyOrbit){SwitchToThisWindow(app.boards.front()->hwnd,TRUE);WaitForeground(app.boards.front()->hwnd);app.ShowDesktop();}Pump();
        if(onlyOrbit){
            OrbitChecks();json::JsonObject report;report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed));report.SetNamedValue(L"checks",checks);WriteAtomic(app.dataDir/L"desktop-regression.json",std::wstring(report.Stringify()));return passed?0:1;
        }
        wchar_t motionOnly[2]{};if(GetEnvironmentVariableW(L"GOODVIEW_TEST_MOTION_ONLY",motionOnly,2)){
            app.Active().cards.clear();app.config.wallpaperEnabled=false;app.config.ui.textScale=1;AnimationChecks();json::JsonObject report;report.SetNamedValue(L"skipped",json::JsonValue::CreateBooleanValue(performanceBlocked));report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed&&!performanceBlocked));report.SetNamedValue(L"checks",checks);WriteAtomic(app.dataDir/L"desktop-regression.json",std::wstring(report.Stringify()));return !passed?1:performanceBlocked?77:0;
        }
        for(int cycle=0;cycle<3;++cycle)for(size_t i=0;i<app.config.scenes.size();++i){auto& bar=*app.bars[0];bar.Invalidate();UpdateWindow(bar.hwnd);if(i>=bar.hits_.size())continue;auto box=bar.hits_[i].box;float dpi=GetDpiForWindow(bar.hwnd)/96.f;POINT point{static_cast<LONG>((box.x+box.w/2)*dpi),static_cast<LONG>((box.y+box.h/2)*dpi)};bar.MouseDown(point);bar.MouseUp(point);Pump();Check(app.config.active==app.config.scenes[i].id&&PanelsVisible(),L"clicking a workspace keeps every monitor's panels above its canvas");}
        auto sceneId=app.config.active;STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        auto command=L"\""+ExePath().wstring()+L"\" --fixture --fixture-parent "+std::to_wstring(GetCurrentProcessId());
        bool started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;Check(started,L"start a separate fixture app for scene focus");
        if(started){CloseHandle(process.hThread);std::pair<DWORD,HWND> found{process.dwProcessId,nullptr};auto until=GetTickCount64()+2000;
            while(!found.second&&GetTickCount64()<until){EnumWindows(+[](HWND window,LPARAM parameter)->BOOL{auto& result=*reinterpret_cast<std::pair<DWORD,HWND>*>(parameter);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==result.first&&GetPropW(window,L"GoodView.FixtureReady")){result.second=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&found));Pump();if(!found.second)Sleep(10);}
            bool attached=found.second&&app.windows.Attach(found.second,sceneId);Check(attached,L"fixture app attaches to a workspace");
            if(attached){FocusAppWindow(found.second);Check(WaitForeground(found.second),L"fixture receives foreground before the workspace click");SendMessageW(found.second,WM_ACTIVATE,WA_ACTIVE,0);FocusAppWindow(app.bars[0]->hwnd);Check(WaitForeground(app.bars[0]->hwnd),L"workspace navigation receives the same foreground as a real click");app.SwitchScene(sceneId);Snapshot(L"before-pump",found.second);Pump();Snapshot(L"after-pump",found.second);Check(Above(app.boards[0]->hwnd,found.second)&&PanelsVisible(),L"choosing current workspace reveals canvas instead of obscuring it with an app");app.ActivateWindow(found.second);Pump();Check(Above(found.second,app.bars[0]->hwnd),L"opening an app releases the desktop reveal and puts the app above navigation");SendMessageW(found.second,WM_ACTIVATE,WA_ACTIVE,0);FocusAppWindow(app.bars[0]->hwnd);Check(WaitForeground(app.bars[0]->hwnd),L"navigation is active before switching back to a workspace");app.SwitchScene(app.config.scenes[0].id);app.SwitchScene(sceneId);Pump();Check(Above(app.boards[0]->hwnd,found.second)&&PanelsVisible(),L"returning to a workspace keeps its canvas and navigation accessible");SearchOpeningChecks();WinReturnChecks(found.second);BurstWinChecks(found.second);EnterChecks(found.second);PromptAndRemovalChecks(found.second);app.windows.Detach(found.second);}
            if(found.second){ShowWindowAsync(found.second,SW_SHOWNOACTIVATE);PumpFor(100);app.RefreshWindows();bool listed=std::any_of(app.openWindows.begin(),app.openWindows.end(),[&](const auto& info){return info.hwnd==found.second;});Check(listed,L"the closing fixture is present in the rail before its destroy event");for(auto& rail:app.rails)rail->hover_=0;auto paints=app.panelPaints;
                PostMessageW(found.second,WM_CLOSE,0,0);if(WaitForSingleObject(process.hProcess,2000)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,0);
                // The HWND is already invalid when the out-of-context event arrives.
                // Exercise the actual event filter and queued UI route, without a mouse move.
                Application::WinEvent(nullptr,EVENT_OBJECT_DESTROY,found.second,OBJID_WINDOW,CHILDID_SELF,0,0);Pump();
                Check(std::none_of(app.openWindows.begin(),app.openWindows.end(),[&](const auto& info){return info.hwnd==found.second;})&&app.panelPaints>paints&&std::all_of(app.rails.begin(),app.rails.end(),[](const auto& rail){return rail->hover_==-1;}),L"destroying a window immediately rebuilds and repaints the rail without moving the pointer");
            }CloseHandle(process.hProcess);
        }
        // Ring fixtures own an artificial window list. Their dedicated render
        // suite runs without the foreground/event hooks installed by this suite.
        VisualChecks();DragChecks();GlyphChecks();
        const wchar_t* icons[]={L"play",L"rocket",L"compass",L"moon"};for(size_t i=0;i<app.config.scenes.size()&&i<4;++i)app.config.scenes[i].icon=icons[i];
        Check(app.bars[0]->ExportPreview(app.dataDir/L"glyphs-d2d.png"),L"curved icons render through the real Direct2D toolbar");
        BYTE original[256]{},keyboard[256]{};GetKeyboardState(original);std::copy(std::begin(original),std::end(original),std::begin(keyboard));keyboard[VK_CONTROL]|=0x80;
        MSG shortcut{};shortcut.hwnd=app.bars[0]->hwnd;shortcut.message=WM_KEYDOWN;shortcut.wParam='Q';SetKeyboardState(keyboard);bool handled=HandleExitShortcut(shortcut);SetKeyboardState(original);
        Check(handled&&app.quitting,L"application exit shortcut is handled before child control dialog navigation");
        SendMessageW(app.bars[0]->hwnd,WM_CLOSE,0,0);Check(app.quitting&&!IsWindowVisible(app.boards[0]->hwnd)&&!IsWindowVisible(app.bars[0]->hwnd),L"close request hides the entire shell before worker cleanup");Pump();
        json::JsonObject report;report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed));report.SetNamedValue(L"checks",checks);WriteAtomic(app.dataDir/L"desktop-regression.json",std::wstring(report.Stringify()));return passed?0:1;
    }
};
int RunDesktopRegression(const fs::path& path){DesktopRegressionProbe probe(path);return probe.Run();}
}
