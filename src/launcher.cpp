#include "launcher.h"
#include "ui.h"
#include "app_index.h"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <windowsx.h>

namespace gv {
namespace {
constexpr int SearchId=100,ListId=101,ActionsId=102,BrowseId=103,PinId=105,RefreshId=106;
constexpr UINT NavigateResults=WM_APP+2;
std::wstring ReadField(HWND field){int length=GetWindowTextLengthW(field);std::wstring text(length+1,L'\0');GetWindowTextW(field,text.data(),length+1);text.resize(length);return text;}
std::wstring Lower(std::wstring value){return AppIndex::Key(std::move(value));}
std::wstring Trim(std::wstring value){while(!value.empty()&&iswspace(value.front()))value.erase(value.begin());while(!value.empty()&&iswspace(value.back()))value.pop_back();if(value.size()>1&&value.front()==L'"'&&value.back()==L'"')value=value.substr(1,value.size()-2);return value;}
RECT Pixels(Box box,UINT dpi){auto p=[&](float value){return static_cast<LONG>(std::round(value*dpi/96.f));};return {p(box.x),p(box.y),p(box.x+box.w),p(box.y+box.h)};}
LRESULT CALLBACK LauncherInput(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR search){
    if(m==WM_GETDLGCODE){auto result=DefSubclassProc(h,m,w,l)|DLGC_WANTARROWS;auto* message=reinterpret_cast<const MSG*>(l);if(message&&(message->wParam==VK_RETURN||message->wParam==VK_ESCAPE))result|=DLGC_WANTMESSAGE;return result;}
    // Child controls must also work in a nested Win32/COM message loop, where
    // Application::Run cannot pretranslate the Enter key for the launcher.
    if(m==WM_KEYDOWN&&(w==VK_RETURN||w==VK_ESCAPE)){SendMessageW(GetParent(h),WM_COMMAND,w==VK_RETURN?IDOK:IDCANCEL,reinterpret_cast<LPARAM>(h));return 0;}
    if(m==WM_CHAR&&(w==L'\r'||w==L'\n'||w==VK_ESCAPE))return 0;
    if(m==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN||(!search&&(w==VK_LEFT||w==VK_RIGHT)))){SendMessageW(GetParent(h),NavigateResults,w,l);return 0;}
    if(m==WM_MOUSEWHEEL&&!search){int current=static_cast<int>(SendMessageW(h,LB_GETTOPINDEX,0,0));SendMessageW(h,LB_SETTOPINDEX,std::max(0,current+(GET_WHEEL_DELTA_WPARAM(w)>0?-3:3)),0);return 0;}
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,LauncherInput,1);
    return DefSubclassProc(h,m,w,l);
}
}

Launcher::~Launcher(){
    StopIndex();if(window_)DestroyWindow(window_);
    if(font_)DeleteObject(font_);if(large_)DeleteObject(large_);if(small_)DeleteObject(small_);if(badge_)DeleteObject(badge_);
    if(background_)DeleteObject(background_);if(field_)DeleteObject(field_);
}
void Launcher::Fonts(UINT dpi){
    if(font_)DeleteObject(font_);if(large_)DeleteObject(large_);if(small_)DeleteObject(small_);if(badge_)DeleteObject(badge_);
    float scale=app_.config.ui.textScale;font_=UiFont(static_cast<int>(18*scale),dpi);large_=UiFont(static_cast<int>(28*scale),dpi,FW_SEMIBOLD);small_=UiFont(static_cast<int>(15*scale),dpi,FW_NORMAL,FontRole::Body);badge_=UiFont(static_cast<int>(13*scale),dpi,FW_SEMIBOLD);
    for(int id:{SearchId,ListId,ActionsId,IDOK,IDCANCEL})if(HWND child=GetDlgItem(window_,id))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
    SendMessageW(list_,LB_SETITEMHEIGHT,0,static_cast<int>(48*dpi*scale/96));
}
void Launcher::UpdateStyle(){if(window_){if(background_)DeleteObject(background_);if(field_)DeleteObject(field_);background_=CreateSolidBrush(UiTheme().panel);field_=CreateSolidBrush(UiTheme().field);for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){wchar_t cls[32]{};GetClassNameW(child,cls,32);if(std::wstring_view(cls)==L"Button")StyleUiButton(child,UiTheme().panel);}Fonts(GetDpiForWindow(window_));Layout();RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}}
void Launcher::Create(bool index){
    if(window_)return;
    WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hIcon=app_.icon;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.Launcher";RegisterClassW(&cls);
    background_=CreateSolidBrush(UiTheme().panel);field_=CreateSolidBrush(UiTheme().field);
    window_=CreateWindowExW((app_.preview?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST,cls.lpszClassName,L"GoodView — приложения",WS_POPUP|WS_CLIPCHILDREN,0,0,820,620,nullptr,nullptr,cls.hInstance,this);
    auto child=[&](const wchar_t* type,const wchar_t* title,DWORD style,int id){
        HWND control=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);
        if(std::wstring_view(type)==L"BUTTON")StyleUiButton(control,UiTheme().panel);
        return control;
    };
    search_=child(L"EDIT",L"",ES_AUTOHSCROLL,SearchId);SetWindowSubclass(search_,LauncherInput,1,TRUE);
    SendMessageW(search_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Приложение, сайт или путь к файлу"));SendMessageW(search_,EM_SETLIMITTEXT,32760,0);
    list_=child(L"LISTBOX",L"Программы и открытые окна",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,ListId);SetWindowSubclass(list_,LauncherInput,1,FALSE);
    child(L"BUTTON",L"Открыть",BS_OWNERDRAW,IDOK);child(L"BUTTON",L"Действия",BS_OWNERDRAW,ActionsId);child(L"BUTTON",L"Закрыть",BS_OWNERDRAW,IDCANCEL);
    Fonts(GetDpiForWindow(window_));Layout();BOOL dark=TRUE;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));
    if(index)PrepareIndex();
}
void Launcher::PrepareIndex(){
    if(!window_){Create();return;}if(indexStop_)return;
    indexStop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);indexWake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!indexStop_||!indexWake_){if(indexStop_)CloseHandle(indexStop_);if(indexWake_)CloseHandle(indexWake_);indexStop_=indexWake_=nullptr;return;}
    worker_=std::thread([this]{IndexLoop();});
}
void Launcher::RequestStopIndex(){if(indexStop_)SetEvent(indexStop_);}
void Launcher::StopIndex(){
    RequestStopIndex();if(worker_.joinable())worker_.join();
    if(indexStop_)CloseHandle(indexStop_);if(indexWake_)CloseHandle(indexWake_);indexStop_=indexWake_=nullptr;
}
void Launcher::PublishIndex(const std::vector<Program>& programs){
    {std::lock_guard lock(mutex_);programs_=programs;loaded_=true;}if(window_)PostMessageW(window_,WM_APP+1,0,0);
}
void Launcher::Learn(const Program& program){
    if(program.kind||!indexWake_||program.target.empty()||IsWebUrl(program.target))return;
    {std::lock_guard lock(mutex_);if(pendingLearn_.size()<256)pendingLearn_.push_back(program);}SetEvent(indexWake_);
}
void Launcher::RequestRefresh(){if(!indexWake_)return;{std::lock_guard lock(mutex_);fullRefresh_=true;}SetEvent(indexWake_);}
void Launcher::IndexLoop(){
    std::vector<HANDLE> watches;
    try{
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        AppIndex index(app_.dataDir/L"program-index.json");bool cached=index.Load(),refresh=!cached||index.RefreshDue();if(cached)PublishIndex(index.Entries());
        for(const auto& folder:ProgramFolders()){HANDLE watch=FindFirstChangeNotificationW(folder.path.c_str(),folder.recursive,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME);if(watch!=INVALID_HANDLE_VALUE)watches.push_back(watch);}
        std::vector<HANDLE> waits{indexStop_,indexWake_};waits.insert(waits.end(),watches.begin(),watches.end());
        for(;;){
            if(WaitForSingleObject(indexStop_,0)==WAIT_OBJECT_0)break;
            std::vector<Program> learned;std::wstring query;
            {std::lock_guard lock(mutex_);refresh|=fullRefresh_;fullRefresh_=false;learned.swap(pendingLearn_);query.swap(pendingQuery_);}
            bool changed=false;
            if(refresh){indexing_=true;PostMessageW(window_,WM_APP+1,0,0);auto catalogue=InstalledPrograms();if(WaitForSingleObject(indexStop_,0)==WAIT_OBJECT_0)break;index.ReplaceCatalogue(std::move(catalogue));changed=true;refresh=false;}
            for(const auto& program:learned)changed|=index.Learn(program);
            if(!query.empty())if(auto program=AppIndex::Discover(query))changed|=index.Learn(*program);
            if(changed){indexWriteFailed_=!index.Save();PublishIndex(index.Entries());}indexing_=false;PostMessageW(window_,WM_APP+1,0,0);
            DWORD event=WaitForMultipleObjects(static_cast<DWORD>(waits.size()),waits.data(),FALSE,INFINITE);
            if(event==WAIT_OBJECT_0||event==WAIT_FAILED)break;
            if(event>=WAIT_OBJECT_0+2&&event<WAIT_OBJECT_0+waits.size()){
                // Coalesce installer bursts. No periodic polling or disk-wide scans.
                if(WaitForSingleObject(indexStop_,500)==WAIT_OBJECT_0)break;
                for(HANDLE watch:watches)if(WaitForSingleObject(watch,0)==WAIT_OBJECT_0)FindNextChangeNotification(watch);refresh=true;
            }else refresh|=index.RefreshDue();
        }
        winrt::uninit_apartment();
    }catch(...){indexing_=false;indexWriteFailed_=true;PostMessageW(window_,WM_APP+1,0,0);}
    for(HANDLE watch:watches)FindCloseChangeNotification(watch);
}
void Launcher::Layout(){
    if(!search_||!list_)return;
    RECT client;GetClientRect(window_,&client);UINT dpi=GetDpiForWindow(window_);auto layout=LayoutLauncher(client.right*96.f/dpi,client.bottom*96.f/dpi);
    auto place=[&](HWND child,Box box){RECT r=Pixels(box,dpi);SetWindowPos(child,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);};
    place(search_,{layout.search.x+48,layout.search.y+13,layout.search.w-68,26});
    place(list_,layout.results);place(GetDlgItem(window_,IDOK),layout.open);place(GetDlgItem(window_,ActionsId),layout.actions);place(GetDlgItem(window_,IDCANCEL),layout.close);
    RoundWindow(window_,static_cast<int>(UiRadius(app_.config.ui,UiElement::Search)));InvalidateRect(window_,nullptr,FALSE);
}
DWORD Launcher::OpeningAnimation()const{
    const auto& ui=app_.config.ui;auto position=ui.searchPosition;
    if(position==L"custom"){
        float x=ui.searchX-.5f,y=ui.searchY-.5f;
        if(std::abs(x)<.08f&&std::abs(y)<.08f)return AW_BLEND;
        position=std::abs(y)>=std::abs(x)?(y<0?L"top":L"bottom"):(x<0?L"left":L"right");
    }
    if(position==L"top")return AW_SLIDE|AW_VER_POSITIVE;
    if(position==L"bottom")return AW_SLIDE|AW_VER_NEGATIVE;
    if(position==L"left")return AW_SLIDE|AW_HOR_POSITIVE;
    if(position==L"right")return AW_SLIDE|AW_HOR_NEGATIVE;
    return AW_BLEND;
}
void Launcher::Show(bool addToDesktop,int monitor){
    bool opening=!Visible();
    addToDesktop_=addToDesktop;addMonitor_=monitor;Create();UpdateStyle();app_.CancelProgramActivation();status_.clear();launching_=false;running_=ListAppWindows(true);
    lastQuery_.clear();for(const auto& window:running_)if(!window.process.empty())Learn({fs::path(window.imageName).stem().wstring(),window.process});if(indexWake_)SetEvent(indexWake_);
    SetWindowTextW(window_,addToDesktop_?L"GoodView — добавить приложение":L"GoodView — приложения");SetWindowTextW(GetDlgItem(window_,IDOK),addToDesktop_?L"Добавить":L"Открыть");
    POINT point;GetCursorPos(&point);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTOPRIMARY),&info);
    UINT dpi=GetDpiForWindow(window_);auto b=info.rcMonitor;float scale=dpi/96.f;auto box=LauncherBox(app_.config.ui,{0,0,(b.right-b.left)/scale,(b.bottom-b.top)/scale});int x=b.left+static_cast<int>(box.x*scale),y=b.top+static_cast<int>(box.y*scale),width=static_cast<int>(box.w*scale),height=static_cast<int>(box.h*scale);Fonts(dpi);
    SetWindowPos(window_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    SetWindowTextW(search_,L"");Filter();Layout();
    BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    // Windows animates this one native popup, including its child controls.
    // No render thread, repeating timer or retained screenshot is needed.
    if(!opening||!animations||GetSystemMetrics(SM_REMOTESESSION)||!AnimateWindow(window_,160,OpeningAnimation()|AW_ACTIVATE))ShowWindow(window_,SW_SHOW);
    Layout();SetForegroundWindow(window_);SetFocus(search_);
}
void Launcher::Hide(){app_.fileIndex.Query(L"");fileResults_.clear();if(window_)ShowWindow(window_,SW_HIDE);}
void Launcher::Toggle(){if(Visible())Hide();else Show();}
void Launcher::SetStatus(std::wstring status,bool busy){status_=std::move(status);launching_=busy;if(window_){EnableWindow(GetDlgItem(window_,IDOK),!busy&&Selected().has_value());InvalidateRect(window_,nullptr,FALSE);}}
bool Launcher::Message(const MSG& message){
    if(!Visible()||(message.hwnd!=window_&&!IsChild(window_,message.hwnd)))return false;
    if(message.message==WM_KEYDOWN){
        if(message.wParam==VK_ESCAPE){app_.CancelProgramActivation();Hide();return true;}
        if(message.wParam==VK_F5){RequestRefresh();return true;}
        if((GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='K'){Actions();return true;}
        if((GetKeyState(VK_CONTROL)&0x8000)&&message.wParam=='O'){Browse();return true;}
        if(message.wParam==VK_RETURN){if(message.hwnd==GetDlgItem(window_,ActionsId))Actions();else if(message.hwnd==GetDlgItem(window_,IDCANCEL))Hide();else Execute((GetKeyState(VK_CONTROL)&0x8000)!=0);return true;}
        if((message.hwnd==search_||message.hwnd==list_)&&(message.wParam==VK_UP||message.wParam==VK_DOWN||message.wParam==VK_LEFT||message.wParam==VK_RIGHT))return false;
    }
    return IsDialogMessageW(window_,const_cast<MSG*>(&message))!=FALSE;
}
void Launcher::FilesChanged(){
    if(!Visible()||addToDesktop_)return;auto next=app_.fileIndex.Results(Trim(ReadField(search_)));bool same=next.size()==fileResults_.size()&&std::equal(next.begin(),next.end(),fileResults_.begin(),[](const FileHit& a,const FileHit& b){return a.path==b.path&&a.directory==b.directory;});if(!same){fileResults_=std::move(next);Filter();}
}
void Launcher::Filter(){
    auto query=Lower(Trim(ReadField(search_))),appQuery=query;if(appQuery.ends_with(L".exe")&&appQuery.find_first_of(L"\\/:")==std::wstring::npos)appQuery.resize(appQuery.size()-4);
    auto previous=Selected();int topIndex=static_cast<int>(SendMessageW(list_,LB_GETTOPINDEX,0,0));SendMessageW(list_,WM_SETREDRAW,FALSE,0);SendMessageW(list_,LB_RESETCONTENT,0,0);filtered_.clear();std::set<DWORD> shown;std::set<std::wstring> shownNames;
    auto append=[&](Program program){shownNames.insert(program.searchKey.empty()?Lower(program.name):program.searchKey);filtered_.push_back(std::move(program));SendMessageW(list_,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(filtered_.back().name.c_str()));};
    for(const auto& window:running_){
        if(!IsWindow(window.hwnd)||shown.contains(window.pid))continue;
        if(addToDesktop_&&window.process.empty())continue;
        if(!IsWindowVisible(window.hwnd)&&!(GetWindowLongPtrW(window.hwnd,GWL_STYLE)&WS_CAPTION)&&!(GetWindowLongPtrW(window.hwnd,GWL_EXSTYLE)&WS_EX_APPWINDOW))continue;
        auto name=fs::path(window.imageName).stem().wstring();if(window.className==L"ConsoleWindowClass"||name.empty())name=window.title;
        auto haystack=Lower(name+L" "+window.title+L" "+window.imageName);
        if(query.empty()||haystack.find(appQuery)!=std::wstring::npos){append({name,window.process,window.hwnd});shown.insert(window.pid);}
    }
    {std::lock_guard lock(mutex_);for(const auto& program:programs_){
        if(!query.empty()&&(program.searchKey.empty()?Lower(program.name):program.searchKey).find(appQuery)==std::wstring::npos)continue;
        bool duplicate=shownNames.contains(program.searchKey.empty()?Lower(program.name):program.searchKey);
        if(!duplicate)append(program);if(filtered_.size()>=250)break;
    }}
    if(!addToDesktop_&&!query.empty()){for(const auto& hit:app_.fileIndex.Results(query)){Program file{hit.name,hit.path};file.kind=hit.directory?2:1;append(std::move(file));}}if(!filtered_.empty()){size_t selected=0;if(previous)for(size_t i=0;i<filtered_.size();++i)if(filtered_[i].target==previous->target){selected=i;break;}SendMessageW(list_,LB_SETCURSEL,selected,0);SendMessageW(list_,LB_SETTOPINDEX,std::max(0,topIndex),0);}
    EnableWindow(GetDlgItem(window_,IDOK),!launching_&&Selected().has_value());SendMessageW(list_,WM_SETREDRAW,TRUE,0);InvalidateRect(list_,nullptr,FALSE);InvalidateRect(window_,nullptr,FALSE);
    if(query.empty()||addToDesktop_)app_.fileIndex.Query(L"");KillTimer(window_,31);if(indexWake_&&!query.empty()&&query!=lastQuery_)SetTimer(window_,31,180,nullptr);
}
std::optional<Program> Launcher::Selected()const{
    auto query=Trim(ReadField(search_));std::error_code error;
    if(IsWebUrl(query))return Program{query,NormalizeWebUrl(query)};
    if(!query.empty()&&fs::exists(query,error)){Program result{fs::path(query).filename().wstring(),query};auto extension=Lower(fs::path(query).extension().wstring());if(extension!=L".exe"&&extension!=L".lnk"&&extension!=L".appref-ms")result.kind=fs::is_directory(query,error)?2:1;return result;}
    int index=static_cast<int>(SendMessageW(list_,LB_GETCURSEL,0,0));if(IsLikelyWebAddress(query)&&(index<0||index>=static_cast<int>(filtered_.size())||(filtered_[index].kind==0&&Lower(filtered_[index].name)!=Lower(query))))return Program{query,NormalizeWebUrl(query)};
    if(index>=0&&index<static_cast<int>(filtered_.size())){auto result=filtered_[index];if(result.window&&!IsWindow(result.window)&&result.target.empty())return {};return result;}
    if(IsLikelyWebAddress(query))return Program{query,NormalizeWebUrl(query)};
    return {};
}
void Launcher::Execute(bool pin){
    if(launching_)return;
    pin=pin||addToDesktop_;int monitor=addToDesktop_?addMonitor_:-1;
    auto selected=Selected();if(!selected){SetStatus(L"Выбери программу или введи адрес сайта.");return;}
    if(selected->kind&&!pin){auto extension=Lower(fs::path(selected->target).extension().wstring());if(extension==L".exe"||extension==L".lnk"||extension==L".appref-ms"||extension==L".bat"||extension==L".cmd"){selected->kind=0;Learn(*selected);app_.UseProgram(*selected,false,monitor);return;}if(GetFileAttributesW(selected->target.c_str())==INVALID_FILE_ATTRIBUTES){SetStatus(L"Файл недоступен. Возможно, он перемещён или удалён.");return;}Hide();LaunchTarget(selected->target,L"");return;}Learn(*selected);
    if((pin&&!selected->target.empty())||IsWebUrl(selected->target))Hide();
    app_.UseProgram(*selected,pin,monitor);
}
void Launcher::Browse(){
    wchar_t path[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window_;dialog.lpstrFile=path;dialog.nMaxFile=32768;
    dialog.lpstrFilter=L"Программы, ярлыки и сайты\0*.exe;*.lnk;*.appref-ms;*.url\0Все файлы\0*.*\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    busy_=true;bool chosen=GetOpenFileNameW(&dialog)!=FALSE;busy_=false;
    if(chosen){int monitor=addToDesktop_?addMonitor_:-1;Program program{fs::path(path).stem().wstring(),path};Learn(program);app_.CancelProgramActivation();Hide();app_.UseProgram(program,true,monitor);}
}
void Launcher::Actions(){
    auto selected=Selected();HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(selected&&(!selected->target.empty())?0:MF_GRAYED),PinId,(L"Закрепить в "+app_.Active().name+L"\tCtrl+Enter").c_str());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,BrowseId,L"Выбрать программу или ярлык…\tCtrl+O");
    AppendMenuW(menu,MF_STRING,RefreshId,L"Обновить индекс программ\tF5");
    AppendMenuW(menu,MF_STRING,107,L"Настройки файлового поиска");RECT button;GetWindowRect(GetDlgItem(window_,ActionsId),&button);busy_=true;UINT action=StyledMenu(menu,window_,{button.left,button.top});busy_=false;DestroyMenu(menu);
    if(action==PinId){app_.CancelProgramActivation();launching_=false;Execute(true);}else if(action==BrowseId)Browse();else if(action==RefreshId)RequestRefresh();else if(action==107){Hide();app_.fileSettings.Show();}
    else if(Visible()){SetForegroundWindow(window_);SetFocus(search_);}
}
void Launcher::DrawResult(HDC dc,RECT row,const Program& program,bool selected){
    FillRect(dc,&row,background_);UINT dpi=GetDpiForWindow(window_);auto U=[&](int value){return static_cast<int>(std::round(value*dpi*app_.config.ui.textScale/96.f));};
    RECT body=row;InflateRect(&body,-U(2),-U(2));if(selected)RoundFill(dc,body,UiTheme().field,U(10));
    SetBkMode(dc,TRANSPARENT);int textRight=body.right-U(16);
    if(program.kind||program.window){const wchar_t* label=program.kind==2?L"Папка":program.kind?L"Файл":L"Открыто";auto old=SelectObject(dc,badge_);SIZE size{};GetTextExtentPoint32W(dc,label,static_cast<int>(wcslen(label)),&size);int height=std::max(U(24),static_cast<int>(size.cy)+U(6)),right=body.right-U(12),top=body.top+(body.bottom-body.top-height)/2;RECT badge{right-size.cx-U(20),top,right,top+height};RoundFill(dc,badge,UiTheme().panel,height/2,UiTheme().border);SetTextColor(dc,UiTheme().muted);DrawTextW(dc,label,-1,&badge,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);textRight=badge.left-U(12);}
    if(program.kind){auto old=SelectObject(dc,font_);SetTextColor(dc,UiTheme().text);RECT title{body.left+U(14),body.top,textRight,body.top+U(25)};DrawTextW(dc,program.name.c_str(),-1,&title,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,small_);SetTextColor(dc,UiTheme().muted);RECT path{body.left+U(14),body.top+U(25),textRight,body.bottom};DrawTextW(dc,program.target.c_str(),-1,&path,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,old);return;}auto icon=program.window?app_.icons.GetWindow(program.window):app_.icons.Get(program.target);DrawMonoIcon(dc,*icon,{body.left+U(12),body.top+U(8),body.left+U(40),body.top+U(36)});
    SetBkMode(dc,TRANSPARENT);auto old=SelectObject(dc,font_);SetTextColor(dc,UiTheme().text);RECT text{body.left+U(56),body.top,textRight,body.bottom};
    DrawTextW(dc,program.name.c_str(),-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,old);
}
void Launcher::DrawCommand(const DRAWITEMSTRUCT& item){
    if(item.CtlID!=IDCANCEL){DrawUiButton(item,font_,item.CtlID==IDOK,UiTheme().panel);return;}
    FillRect(item.hDC,&item.rcItem,background_);UINT dpi=GetDpiForWindow(window_);RECT rect=item.rcItem;
    if(item.itemState&(ODS_SELECTED|ODS_FOCUS)||GetPropW(item.hwndItem,L"GoodView.Hover"))RoundFill(item.hDC,rect,UiTheme().field,MulDiv(10,dpi,96));
    InflateRect(&rect,-MulDiv(8,dpi,96),-MulDiv(8,dpi,96));DrawGlyph(item.hDC,Glyph::Close,rect,UiTheme().muted);
}
void Launcher::PaintFrame(HDC dc,RECT client){
    FillRect(dc,&client,background_);UINT dpi=GetDpiForWindow(window_);auto U=[&](int value){return MulDiv(value,dpi,96);};auto layout=LayoutLauncher(client.right*96.f/dpi,client.bottom*96.f/dpi);
    RoundFill(dc,client,UiTheme().panel,U(static_cast<int>(UiRadius(app_.config.ui,UiElement::Search))),UiTheme().border);SetBkMode(dc,TRANSPARENT);
    auto old=SelectObject(dc,large_);SetTextColor(dc,UiTheme().text);RECT title=Pixels(layout.title,dpi);DrawTextW(dc,addToDesktop_?L"Добавить приложение":L"Приложения",-1,&title,DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,small_);SetTextColor(dc,UiTheme().muted);RECT subtitle=Pixels(layout.subtitle,dpi);DrawTextW(dc,addToDesktop_?L"Выбери программу — ярлык появится на полотне":L"Программы, файлы, папки и сайты",-1,&subtitle,DT_SINGLELINE|DT_VCENTER);
    RECT search=Pixels(layout.search,dpi);RoundFill(dc,search,UiTheme().field,U(12),UiTheme().border);
    DrawGlyph(dc,Glyph::Search,{search.left+U(16),search.top+U(15),search.left+U(38),search.top+U(37)},UiTheme().accent);
    HBRUSH divider=CreateSolidBrush(UiTheme().border);RECT line{U(24),Pixels(layout.info,dpi).top-U(16),client.right-U(24),Pixels(layout.info,dpi).top-U(15)};FillRect(dc,&line,divider);DeleteObject(divider);
    RECT info=Pixels(layout.info,dpi);SelectObject(dc,small_);SetTextColor(dc,launching_?UiTheme().accent:UiTheme().muted);
    auto status=status_.empty()?app_.Active().name+L" · "+std::to_wstring(filtered_.size())+L" результатов"+(indexing_?L" · обновление индекса":indexWriteFailed_?L" · индекс временно в памяти":L""):status_;DrawTextW(dc,status.c_str(),-1,&info,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    if(filtered_.empty()){RECT empty=Pixels(layout.results,dpi);InflateRect(&empty,-U(16),-U(24));bool loaded;{std::lock_guard lock(mutex_);loaded=loaded_;}SelectObject(dc,font_);DrawTextW(dc,loaded?L"Ничего не найдено.\nВведи адрес сайта или выбери файл в «Действиях».":L"Собираем каталог приложений…",-1,&empty,DT_WORDBREAK);}
    SelectObject(dc,old);
}
bool Launcher::ExportPreview(const fs::path& path){
    Create(false);programs_=InstalledPrograms();loaded_=true;Filter();RECT client;GetClientRect(window_,&client);
    HDC memory=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),client.right,-client.bottom,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(memory);return false;}
    auto previous=SelectObject(memory,bitmap);PaintFrame(memory,client);UINT dpi=GetDpiForWindow(window_);auto layout=LayoutLauncher(client.right*96.f/dpi,client.bottom*96.f/dpi);RECT search=Pixels({layout.search.x+48,layout.search.y+13,layout.search.w-68,26},dpi);auto font=SelectObject(memory,font_);SetBkMode(memory,TRANSPARENT);SetTextColor(memory,RGB(148,169,157));DrawTextW(memory,L"Приложение, сайт или путь к файлу",-1,&search,DT_SINGLELINE|DT_VCENTER);SelectObject(memory,font);
    RECT list=Pixels(layout.results,dpi);int rowHeight=MulDiv(48,dpi,96);for(size_t i=0;i<filtered_.size()&&list.top+rowHeight<=list.bottom;++i){RECT row{list.left,list.top,list.right,list.top+rowHeight};DrawResult(memory,row,filtered_[i],i==0);list.top+=rowHeight;}
    for(auto [id,box]:{std::pair<int,Box>{IDCANCEL,layout.close},{ActionsId,layout.actions},{IDOK,layout.open}}){DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=id;item.hwndItem=GetDlgItem(window_,id);item.hDC=memory;item.rcItem=Pixels(box,dpi);DrawCommand(item);}
    bool saved=SaveBitmapPng(bitmap,path);SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);return saved;
}
LRESULT CALLBACK Launcher::Proc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<Launcher*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Launcher*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,w,l);
    if(message==WM_APP+1){self->Filter();return 0;}
    if(message==NavigateResults){
        if(w==VK_LEFT||w==VK_RIGHT){SetFocus(self->search_);SendMessageW(self->search_,WM_KEYDOWN,w,l);return 0;}
        int count=static_cast<int>(SendMessageW(self->list_,LB_GETCOUNT,0,0));if(count<=0)return 0;
        int previous=static_cast<int>(SendMessageW(self->list_,LB_GETCURSEL,0,0));int next=previous==LB_ERR?(w==VK_DOWN?0:count-1):std::clamp(previous+(w==VK_DOWN?1:-1),0,count-1);
        if(next!=previous){SendMessageW(self->list_,LB_SETCURSEL,next,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ListId,LBN_SELCHANGE),reinterpret_cast<LPARAM>(self->list_));}return 0;
    }
    if(message==WM_TIMER&&w==31){KillTimer(window,31);auto query=Trim(ReadField(self->search_));self->lastQuery_=Lower(query);{std::lock_guard lock(self->mutex_);self->pendingQuery_=query;}if(!self->addToDesktop_)self->app_.fileIndex.Query(query);if(self->indexWake_)SetEvent(self->indexWake_);return 0;}
    if(message==WM_COMMAND){int id=LOWORD(w);if(id==SearchId&&HIWORD(w)==EN_CHANGE){self->app_.CancelProgramActivation();self->status_.clear();self->launching_=false;self->Filter();}
        else if(id==IDOK||(id==ListId&&HIWORD(w)==LBN_DBLCLK))self->Execute(false);else if(id==ActionsId)self->Actions();else if(id==ListId&&HIWORD(w)==LBN_SELCHANGE){self->app_.CancelProgramActivation();self->SetStatus(L"");}else if(id==IDCANCEL){self->app_.CancelProgramActivation();self->Hide();}return 0;}
    if(message==WM_CLOSE){self->app_.CancelProgramActivation();self->Hide();return 0;}
    if(message==WM_ACTIVATE&&LOWORD(w)==WA_INACTIVE&&!self->busy_){HWND next=reinterpret_cast<HWND>(l);if(!next||GetAncestor(next,GA_ROOTOWNER)!=window)self->Hide();return 0;}
    if(message==WM_SIZE){self->Layout();return 0;}
    if(message==WM_DPICHANGED){auto* rect=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);self->Fonts(HIWORD(w));self->Layout();return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_CTLCOLOREDIT){SetTextColor(reinterpret_cast<HDC>(w),UiTheme().text);SetBkColor(reinterpret_cast<HDC>(w),UiTheme().field);return reinterpret_cast<LRESULT>(self->field_);}
    if(message==WM_CTLCOLORLISTBOX||message==WM_CTLCOLORBTN){SetBkColor(reinterpret_cast<HDC>(w),UiTheme().panel);return reinterpret_cast<LRESULT>(self->background_);}
    if(message==WM_MEASUREITEM){reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=MulDiv(48,GetDpiForWindow(window),96);return TRUE;}
    if(message==WM_DRAWITEM){auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType==ODT_BUTTON){self->DrawCommand(*item);return TRUE;}if(item->itemID<self->filtered_.size())self->DrawResult(item->hDC,item->rcItem,self->filtered_[item->itemID],(item->itemState&(ODS_SELECTED|ODS_FOCUS))!=0);return TRUE;}
    if(message==WM_PAINT){PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);RECT client;GetClientRect(window,&client);self->PaintFrame(dc,client);EndPaint(window,&paint);return 0;}
    if(message==WM_PRINTCLIENT){RECT client;GetClientRect(window,&client);self->PaintFrame(reinterpret_cast<HDC>(w),client);return 0;}
    if(message==WM_NCHITTEST){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(window,&point);UINT dpi=GetDpiForWindow(window);RECT client;GetClientRect(window,&client);auto layout=LayoutLauncher(client.right*96.f/dpi,client.bottom*96.f/dpi);RECT close=Pixels(layout.close,dpi);if(point.y<MulDiv(88,dpi,96)&&!PtInRect(&close,point))return HTCAPTION;}
    if(message==WM_EXITSIZEMOVE){RECT r;GetWindowRect(window,&r);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);auto b=monitor.rcMonitor;auto& ui=self->app_.config.ui;ui.searchPosition=L"custom";ui.searchX=std::clamp(static_cast<float>(r.left-b.left-MulDiv(24,GetDpiForWindow(window),96))/std::max(1L,b.right-b.left-(r.right-r.left)-MulDiv(48,GetDpiForWindow(window),96)),0.f,1.f);ui.searchY=std::clamp(static_cast<float>(r.top-b.top-MulDiv(24,GetDpiForWindow(window),96))/std::max(1L,b.bottom-b.top-(r.bottom-r.top)-MulDiv(48,GetDpiForWindow(window),96)),0.f,1.f);self->app_.SaveSoon();return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
