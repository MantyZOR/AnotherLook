#include "tray.h"
#include "ui.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cstddef>
#include <uxtheme.h>
#include <cmath>

namespace gv {
namespace {
constexpr UINT ChangedMessage=WM_APP+65;
bool Same(const TrayIcon& icon,const traywire::IconData& data){
    if((data.flags&NIF_GUID)&&!IsEqualGUID(data.guid,GUID{}))return IsEqualGUID(icon.guid,data.guid);
    return icon.window==reinterpret_cast<HWND>(static_cast<ULONG_PTR>(data.window))&&icon.id==data.id;
}
HWND ExplorerTray(HWND except){
    HWND window=nullptr;wchar_t directory[MAX_PATH]{};GetWindowsDirectoryW(directory,MAX_PATH);auto expected=(fs::path(directory)/L"explorer.exe").wstring();
    while((window=FindWindowExW(nullptr,window,L"Shell_TrayWnd",nullptr))){
        if(window==except)continue;DWORD pid=0;GetWindowThreadProcessId(window,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)continue;
        wchar_t path[32768]{};DWORD size=32768;bool valid=QueryFullProcessImageNameW(process,0,path,&size)&&_wcsicmp(path,expected.c_str())==0;CloseHandle(process);if(valid)return window;
    }return nullptr;
}
void TrayToggle(HDC dc,RECT rect,bool expanded,COLORREF color){
    float scale=static_cast<float>(std::min(rect.right-rect.left,rect.bottom-rect.top))/24;auto X=[&](int value){return rect.left+static_cast<LONG>(std::lround(value*scale));};auto Y=[&](int value){return rect.top+static_cast<LONG>(std::lround(value*scale));};
    if(!expanded){for(int y:{5,14})for(int x:{5,14})RoundFill(dc,{X(x),Y(y),X(x+5),Y(y+5)},color,std::max(1,static_cast<int>(std::lround(scale))));return;}
    // One filled contour avoids doubled joins from overlapping line segments.
    POINT points[]={{X(4),Y(8)},{X(7),Y(5)},{X(12),Y(10)},{X(17),Y(5)},{X(20),Y(8)},{X(12),Y(16)}};auto brush=CreateSolidBrush(color);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,points,6);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
}
}
std::vector<TrayCommand> SystemTrayCommands(const GUID& guid){
    GUID family=guid;family.Data1=0;static constexpr GUID expected{0,0x23e3,0x4229,{0x82,0xc1,0xe4,0x1c,0xb6,0x7d,0x5b,0x9c}};
    if(!IsEqualGUID(family,expected))return {};wchar_t directory[MAX_PATH]{};if(!GetSystemDirectoryW(directory,MAX_PATH))return {};fs::path system(directory);
    switch(guid.Data1){
        case 0x7820ae73:return {{L"Микшер громкости",(system/L"SndVol.exe").wstring(),L""},{L"Настройки звука",L"ms-settings:sound",L""},{L"Устройства воспроизведения",(system/L"control.exe").wstring(),L"mmsys.cpl,,0"},{L"Устройства записи",(system/L"control.exe").wstring(),L"mmsys.cpl,,1"}};
        case 0x7820ae81:return {{L"Центр обновления Windows",L"ms-settings:windowsupdate",L""},{L"Журнал обновлений",L"ms-settings:windowsupdate-history",L""}};
        case 0x7820ae74:return {{L"Сеть и интернет",L"ms-settings:network-status",L""},{L"Сетевые подключения",(system/L"control.exe").wstring(),L"ncpa.cpl"}};
        case 0x7820ae75:return {{L"Питание и спящий режим",L"ms-settings:powersleep",L""},{L"Схемы электропитания",(system/L"control.exe").wstring(),L"powercfg.cpl"}};
        default:return {};
    }
}
namespace traywire {
std::optional<Update> Decode(const COPYDATASTRUCT& packet){
    constexpr size_t header=offsetof(Packet,icon),minimum=offsetof(IconData,tip);
    if(packet.dwData!=1||!packet.lpData||packet.cbData<header+minimum||packet.cbData>4096)return {};
    Packet copy{};memcpy(&copy,packet.lpData,std::min<size_t>(packet.cbData,sizeof(copy)));
    if(copy.operation>NIM_SETVERSION||copy.icon.size<minimum||copy.icon.size>sizeof(IconData)||header+copy.icon.size>packet.cbData)return {};
    if(!copy.icon.window&&(!(copy.icon.flags&NIF_GUID)||IsEqualGUID(copy.icon.guid,GUID{})))return {};copy.icon.tip[127]=0;return Update{copy.operation,copy.icon};
}
std::pair<WPARAM,LPARAM> Callback(const TrayIcon& icon,UINT event,POINT point){
    if(icon.version>=NOTIFYICON_VERSION_4)return {MAKEWPARAM(static_cast<WORD>(point.x),static_cast<WORD>(point.y)),MAKELPARAM(event,static_cast<WORD>(icon.id))};
    return {icon.id,event};
}
}
NotificationTray::~NotificationTray(){Stop();}
void NotificationTray::UpdateStyle(){auto theme=UiTheme();if(tooltip_){SendMessageW(tooltip_,TTM_SETTIPBKCOLOR,theme.panel,0);SendMessageW(tooltip_,TTM_SETTIPTEXTCOLOR,theme.text,0);}if(panel_)Layout(true);}
bool NotificationTray::Start(bool isolatedTest){
    if(proxy_)return true;isolated_=isolatedTest;expanded_=false;textScale_=0;
    if(!isolated_){explorer_=ExplorerTray(nullptr);if(!explorer_)return false;}
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=ProxyProc;cls.lpszClassName=isolated_?L"GoodView.TrayTest.Proxy":L"Shell_TrayWnd";RegisterClassW(&cls);
    proxy_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,cls.hInstance,this);if(!proxy_)return false;
    // Shell_NotifyIcon is also used by elevated clients. Only its marshalled message
    // is admitted; no process privileges or system policy are changed.
    ChangeWindowMessageFilterEx(proxy_,WM_COPYDATA,MSGFLT_ALLOW,nullptr);
    cls.lpfnWndProc=PanelProc;cls.style=CS_DBLCLKS;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.NotificationTray";RegisterClassW(&cls);
    panel_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,cls.lpszClassName,L"GoodView — фоновые приложения",WS_POPUP|WS_CLIPCHILDREN,0,0,300,52,app_.boards.empty()?nullptr:app_.boards.front()->hwnd,nullptr,cls.hInstance,this);
    if(!panel_){Stop();return false;}font_=UiFont(15,GetDpiForWindow(panel_));
    tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,panel_,nullptr,cls.hInstance,nullptr);SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,420);SetWindowTheme(tooltip_,L"",L"");SendMessageW(tooltip_,TTM_SETTIPBKCOLOR,RGB(19,28,30),0);SendMessageW(tooltip_,TTM_SETTIPTEXTCOLOR,RGB(218,233,222),0);SendMessageW(tooltip_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
    if(!isolated_)SendTaskbarCreated();return true;
}
void NotificationTray::SendTaskbarCreated(){UINT message=RegisterWindowMessageW(L"TaskbarCreated");if(message)SendNotifyMessageW(HWND_BROADCAST,message,0,0);}
void NotificationTray::Stop(){
    bool wasActive=proxy_!=nullptr,announce=wasActive&&!isolated_;if(tooltip_)DestroyWindow(tooltip_);tooltip_=nullptr;
    if(panel_)DestroyWindow(panel_);panel_=nullptr;if(proxy_)DestroyWindow(proxy_);proxy_=nullptr;
    if(font_)DeleteObject(font_);font_=nullptr;icons_.clear();hits_.clear();dirty_=false;explorer_=nullptr;
    panelBitmap_.Reset();if(announce)SendTaskbarCreated();
}
void NotificationTray::Sync(bool visible,bool topmost,HWND behind){
    if(!proxy_)return;
    if(!isolated_){
        HWND current=ExplorerTray(proxy_);bool restarted=current&&current!=explorer_;explorer_=current;
        // A hidden receiver is enough for Shell32; it has no frame to paint.
        if(FindWindowW(L"Shell_TrayWnd",nullptr)!=proxy_)SetWindowPos(proxy_,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        if(restarted)SendTaskbarCreated();
    }
    if(!panel_)return;
    topmost_=topmost;
    bool wasVisible=IsWindowVisible(panel_)!=FALSE;
    if(visible){Layout(false);if(!wasVisible){Paint();ShowWindow(panel_,SW_SHOWNOACTIVATE);}SetShellPanelLayer(panel_,topmost,behind);}
    else if(wasVisible)ShowWindow(panel_,SW_HIDE);
}
void NotificationTray::Expand(){if(!proxy_)return;expanded_=!expanded_;Layout();Sync(true,topmost_);app_.SyncPanels();}
void NotificationTray::Changed(){if(dirty_||!proxy_)return;dirty_=true;PostMessageW(proxy_,ChangedMessage,0,0);}
void NotificationTray::Update(const traywire::Update& update){
    const auto& data=update.data;auto found=std::find_if(icons_.begin(),icons_.end(),[&](const TrayIcon& icon){return Same(icon,data);});
    if(update.operation==NIM_DELETE){if(found!=icons_.end()){icons_.erase(found);Changed();}return;}
    HWND window=data.window?reinterpret_cast<HWND>(static_cast<ULONG_PTR>(data.window)):found!=icons_.end()?found->window:nullptr;DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(!pid||(pid==GetCurrentProcessId()&&!isolated_)||!IsWindow(window))return;
    if(found==icons_.end()){
        if((update.operation!=NIM_ADD&&update.operation!=NIM_MODIFY)||icons_.size()>=128)return;
        icons_.push_back({});found=icons_.end()-1;found->window=window;found->pid=pid;found->id=data.id;if(data.flags&NIF_GUID)found->guid=data.guid;
    }
    if(found->pid!=pid||found->window!=window){
        if(update.operation!=NIM_ADD&&update.operation!=NIM_MODIFY){icons_.erase(found);Changed();return;}
        auto guid=found->guid;*found={};found->window=window;found->pid=pid;found->id=data.id;found->guid=guid;
    }
    if(update.operation==NIM_SETVERSION)found->version=data.version;
    else{
        if(data.flags&NIF_MESSAGE)found->callback=data.callback;
        if(data.flags&NIF_TIP)found->tip=data.tip;
        if(data.flags&NIF_STATE)found->state=(found->state&~data.stateMask)|(data.state&data.stateMask);
        if(data.flags&NIF_ICON)found->image=MonoFromIcon(reinterpret_cast<HICON>(static_cast<ULONG_PTR>(data.icon)));
    }
    ++updates_;Changed();
}
void NotificationTray::Layout(bool refresh){
    if(!panel_||app_.monitors.empty())return;auto removed=std::erase_if(icons_,[](const TrayIcon& icon){DWORD pid=0;GetWindowThreadProcessId(icon.window,&pid);return !IsWindow(icon.window)||pid!=icon.pid;});refresh|=removed!=0;
    size_t count=std::count_if(icons_.begin(),icons_.end(),[](const TrayIcon& icon){return !(icon.state&NIS_HIDDEN);});UINT dpi=GetDpiForWindow(panel_);float scale=app_.config.ui.textScale;auto U=[&](int value){return static_cast<int>(std::lround(value*dpi*scale/96));};
    if(textScale_!=scale){refresh=true;textScale_=scale;if(font_)DeleteObject(font_);font_=UiFont(static_cast<int>(15*scale),dpi);if(tooltip_)SendMessageW(tooltip_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);}
    bool vertical=app_.config.ui.trayVertical;auto monitor=app_.monitors.front().bounds;int maxRows=std::max(1,static_cast<int>((monitor.bottom-monitor.top)*96/(dpi*scale)-80)/34);int maxColumns=vertical?1:expanded_?8:7,maxCells=vertical?5:7;if(expanded_)maxColumns=std::max(maxColumns,(static_cast<int>(count)+maxRows-1)/maxRows);
    int shown=static_cast<int>(expanded_?count:std::min<size_t>(maxCells,count)),columns=std::max(1,std::min(maxColumns,shown)),rows=std::max(1,(shown+columns-1)/columns);
    int width=U(62+columns*34),height=U(20+rows*34);auto bounds=app_.monitors.front().bounds;float dpiScale=dpi/96.f;
    columns_=columns;rows_=rows;
    auto box=InterfaceBox(app_.config.ui,UiElement::Tray,{0,0,(bounds.right-bounds.left)/dpiScale,(bounds.bottom-bounds.top)/dpiScale},{0,0,width/dpiScale,height/dpiScale});
    RECT now{};GetWindowRect(panel_,&now);RECT next{bounds.left+static_cast<LONG>(std::lround(box.x*dpiScale)),bounds.top+static_cast<LONG>(std::lround(box.y*dpiScale)),0,0};next.right=next.left+width;next.bottom=next.top+height;
    bool resized=now.right-now.left!=width||now.bottom-now.top!=height;
    if(!EqualRect(&now,&next))SetWindowPos(panel_,nullptr,next.left,next.top,width,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    // The alpha buffer defines smooth, symmetric corners without an HRGN clip.
    if(refresh||resized)InvalidateRect(panel_,nullptr,FALSE);
}
void NotificationTray::Tooltips(){
    if(!tooltip_)return;int count=static_cast<int>(SendMessageW(tooltip_,TTM_GETTOOLCOUNT,0,0));for(int i=count-1;i>=0;--i){TOOLINFOW tool{sizeof(tool)};if(SendMessageW(tooltip_,TTM_ENUMTOOLSW,i,reinterpret_cast<LPARAM>(&tool)))SendMessageW(tooltip_,TTM_DELTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    // Rebuilt after Paint establishes the current cells; strings live in icons_.
    for(const auto& [rect,index]:hits_)if(index<icons_.size()){TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_SUBCLASS;tool.hwnd=panel_;tool.uId=index+1;tool.rect=rect;tool.lpszText=icons_[index].tip.empty()?const_cast<wchar_t*>(L"Фоновое приложение"):icons_[index].tip.data();SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    TOOLINFOW more{sizeof(more)};more.uFlags=TTF_SUBCLASS;more.hwnd=panel_;more.uId=1000;more.rect=more_;more.lpszText=const_cast<wchar_t*>(expanded_?L"Свернуть фоновые приложения":L"Показать все фоновые приложения");SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&more));
}
void NotificationTray::Paint(){
    ++paints_;PAINTSTRUCT paint;BeginPaint(panel_,&paint);RECT client;GetClientRect(panel_,&client);if(panelBitmap_.Resize(client.right,client.bottom)){Draw(panelBitmap_.Dc(),client);panelBitmap_.RoundAlpha(UiRadius(app_.config.ui,UiElement::Tray)*app_.config.ui.textScale*GetDpiForWindow(panel_)/96.f);panelBitmap_.Present(panel_);}EndPaint(panel_,&paint);Tooltips();
}
void NotificationTray::Draw(HDC target,RECT client){
    // The caller already owns a retained or export bitmap. Draw into it directly.
    HDC memory=target;HBRUSH background=CreateSolidBrush(UiTheme().panel);FillRect(memory,&client,background);DeleteObject(background);
    UINT dpi=GetDpiForWindow(panel_);float scale=app_.config.ui.textScale;auto U=[&](int value){return static_cast<int>(std::lround(value*dpi*scale/96));};bool vertical=app_.config.ui.trayVertical;RoundFill(memory,client,UiTheme().panel,U(static_cast<int>(UiRadius(app_.config.ui,UiElement::Tray))),UiTheme().border);SetBkMode(memory,TRANSPARENT);auto oldFont=SelectObject(memory,font_);SetTextColor(memory,UiTheme().muted);
    hits_.clear();int cell=0,limit=expanded_?static_cast<int>(icons_.size()):(vertical?5:7);
    for(size_t index=0;index<icons_.size()&&cell<limit;++index){auto& icon=icons_[index];if(icon.state&NIS_HIDDEN)continue;
        int x=10+(cell%columns_)*34,y=10+(cell/columns_)*34;RECT rect{U(x),U(y),U(x+32),U(y+32)};hits_.push_back({rect,index});
        if(hover_==static_cast<int>(index))RoundFill(memory,rect,UiTheme().field,U(10));RECT image=rect;InflateRect(&image,-U(6),-U(6));
        if(icon.image&&!icon.image->pixels.empty())DrawMonoIcon(memory,*icon.image,image);else DrawGlyph(memory,Glyph::Apps,image,UiTheme().text);++cell;
    }
    int center=client.bottom/2;more_={client.right-U(44),center-U(16),client.right-U(12),center+U(16)};
    RoundFill(memory,more_,hover_==-2?UiTheme().field:UiTheme().card,U(9));RECT glyph=more_;InflateRect(&glyph,-U(4),-U(4));TrayToggle(memory,glyph,expanded_,UiTheme().text);
    if(!cell){RECT icon{U(16),center-U(10),U(36),center+U(10)};DrawGlyph(memory,Glyph::Tray,icon,UiTheme().muted);}
    SelectObject(memory,oldFont);
}
bool NotificationTray::ExportPreview(const fs::path& path){
    if(!panel_)return false;expanded_=true;Layout();RECT client;GetClientRect(panel_,&client);HDC memory=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),client.right,-client.bottom,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(memory);return false;}auto old=SelectObject(memory,bitmap);Draw(memory,client);bool saved=SaveBitmapPng(bitmap,path);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);return saved;
}
bool NotificationTray::Invoke(const TrayIcon& source,bool rightClick,POINT point){
    TrayIcon icon=source;
    DWORD pid=0;GetWindowThreadProcessId(icon.window,&pid);if(!IsWindow(icon.window)||pid!=icon.pid)return false;
    // Windows moves these icons between shell components across releases. Their
    // stable system GUIDs identify our fixed commands, even without a callback.
    auto commands=SystemTrayCommands(icon.guid);if(!commands.empty()&&(rightClick||icon.callback<WM_USER))return InvokeSystem(commands,rightClick,point);
    if(icon.callback<WM_USER)return false;
    AllowSetForegroundWindow(pid);auto send=[&](UINT event){auto [w,l]=traywire::Callback(icon,event,point);return PostMessageW(icon.window,icon.callback,w,l)!=FALSE;};
    if(rightClick){
        if(tooltip_)SendMessageW(tooltip_,TTM_POP,0,0);app_.BeginTrayMenu(pid);Sync(Visible(),false);
        // Explorer sends the mouse sequence as well as the semantic context event.
        // Version 3 uses legacy packing but already expects WM_CONTEXTMENU.
        if(icon.version>=NOTIFYICON_VERSION)return send(WM_CONTEXTMENU);
        send(WM_RBUTTONDOWN);return send(WM_RBUTTONUP);
    }
    if(icon.version>=NOTIFYICON_VERSION_4)return send(NIN_SELECT);
    send(WM_LBUTTONDOWN);return send(WM_LBUTTONUP);
}
bool NotificationTray::InvokeSystem(const std::vector<TrayCommand>& commands,bool rightClick,POINT point){
    if(commands.empty()||!panel_)return false;UINT command=1;HWND previous=GetForegroundWindow();
    if(rightClick){
        if(tooltip_)SendMessageW(tooltip_,TTM_POP,0,0);app_.BeginTrayMenu(GetCurrentProcessId());Sync(Visible(),false);
        HMENU menu=CreatePopupMenu();for(size_t i=0;i<commands.size();++i)AppendMenuW(menu,MF_STRING,i+1,commands[i].label.c_str());
        SetForegroundWindow(panel_);command=StyledMenu(menu,panel_,point);DestroyMenu(menu);PostMessageW(panel_,WM_NULL,0,0);
    }
    if(command&&command<=commands.size()){
        const auto& action=commands[command-1];if(!LaunchTarget(action.target,action.arguments))ShowNotice(panel_,L"Системная команда",L"Не удалось открыть системное окно. Проверь, доступен ли этот компонент Windows.");
    }else if(previous&&previous!=panel_&&GetForegroundWindow()==panel_)FocusAppWindow(previous);
    if(app_.config.enabled&&!app_.preview)app_.chrome.Refresh();app_.SyncPanels();return true;
}
bool NotificationTray::WriteDiagnostics(const fs::path& path)const{
    json::JsonObject report;json::JsonArray entries;
    for(const auto& icon:icons_){wchar_t cls[128]{},guid[40]{};GetClassNameW(icon.window,cls,128);StringFromGUID2(icon.guid,guid,40);json::JsonObject item;
        item.SetNamedValue(L"tip",json::JsonValue::CreateStringValue(icon.tip));item.SetNamedValue(L"class",json::JsonValue::CreateStringValue(cls));item.SetNamedValue(L"guid",json::JsonValue::CreateStringValue(guid));
        item.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(icon.pid));item.SetNamedValue(L"id",json::JsonValue::CreateNumberValue(icon.id));item.SetNamedValue(L"callback",json::JsonValue::CreateNumberValue(icon.callback));item.SetNamedValue(L"version",json::JsonValue::CreateNumberValue(icon.version));entries.Append(item);
    }report.SetNamedValue(L"active",json::JsonValue::CreateBooleanValue(Active()));report.SetNamedValue(L"icons",entries);return WriteAtomic(path,std::wstring(report.Stringify()));
}
LRESULT NotificationTray::Forward(HWND,UINT message,WPARAM w,LPARAM l){
    if(isolated_||forwarding_)return 0;if(!IsWindow(explorer_))explorer_=ExplorerTray(proxy_);if(!explorer_)return 0;
    if(message==WM_USER+372){PostMessageW(explorer_,message,w,l);return 0;}
    forwarding_=true;DWORD_PTR result=0;SendMessageTimeoutW(explorer_,message,w,l,SMTO_ABORTIFHUNG,200,&result);forwarding_=false;return static_cast<LRESULT>(result);
}
LRESULT CALLBACK NotificationTray::ProxyProc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<NotificationTray*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<NotificationTray*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,w,l);
    if(message==ChangedMessage){self->dirty_=false;self->Layout();return 0;}
    if(message==WM_COPYDATA&&l){
        auto& packet=*reinterpret_cast<COPYDATASTRUCT*>(l);
        if(packet.dwData==3&&packet.lpData&&packet.cbData>=sizeof(traywire::Identifier)&&packet.cbData<=128){
            traywire::Identifier id{};memcpy(&id,packet.lpData,sizeof(id));
            for(size_t index=0;index<self->icons_.size();++index){const auto& icon=self->icons_[index];bool same=!IsEqualGUID(id.guid,GUID{})?IsEqualGUID(id.guid,icon.guid):icon.window==reinterpret_cast<HWND>(static_cast<ULONG_PTR>(id.window))&&icon.id==id.id;if(!same)continue;
                RECT rect=self->more_;for(const auto& [cell,item]:self->hits_)if(item==index){rect=cell;break;}MapWindowPoints(self->panel_,nullptr,reinterpret_cast<POINT*>(&rect),2);
                if(id.operation==1)return MAKELPARAM(rect.left,rect.top);if(id.operation==2)return MAKELPARAM(rect.right,rect.bottom);
            }
        }
        auto update=traywire::Decode(packet);if(update)self->Update(*update);auto forwarded=self->Forward(window,message,w,l);return forwarded?forwarded:update?TRUE:FALSE;
    }
    if(message==WM_COMMAND||message==WM_ACTIVATEAPP||message>=WM_USER)return self->Forward(window,message,w,l);
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK NotificationTray::PanelProc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<NotificationTray*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<NotificationTray*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,w,l);
    if(message==WM_PAINT){self->Paint();return 0;}if(message==WM_ERASEBKGND)return 1;
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_MOUSEMOVE){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};int hover=PtInRect(&self->more_,point)?-2:-1;for(auto [rect,index]:self->hits_)if(PtInRect(&rect,point))hover=static_cast<int>(index);if(self->hover_!=hover){self->hover_=hover;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);return 0;}
    if(message==WM_MOUSELEAVE){self->hover_=-1;InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_LBUTTONDBLCLK){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};for(auto [rect,index]:self->hits_)if(index<self->icons_.size()&&PtInRect(&rect,point)){auto icon=self->icons_[index];DWORD pid=0;GetWindowThreadProcessId(icon.window,&pid);if(pid!=icon.pid||icon.callback<WM_USER)return 0;ClientToScreen(window,&point);auto [wParam,lParam]=traywire::Callback(icon,WM_LBUTTONDBLCLK,point);self->doubleRelease_=PostMessageW(icon.window,icon.callback,wParam,lParam)!=FALSE;return 0;}return 0;}
    if(message==WM_LBUTTONUP||message==WM_RBUTTONUP){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(PtInRect(&self->more_,point)){self->Expand();return 0;}for(auto [rect,index]:self->hits_)if(index<self->icons_.size()&&PtInRect(&rect,point)){auto icon=self->icons_[index];ClientToScreen(window,&point);if(message==WM_LBUTTONUP&&self->doubleRelease_){self->doubleRelease_=false;if(icon.version<4){auto [wParam,lParam]=traywire::Callback(icon,WM_LBUTTONUP,point);PostMessageW(icon.window,icon.callback,wParam,lParam);}}else self->Invoke(icon,message==WM_RBUTTONUP,point);return 0;}self->doubleRelease_=false;return 0;}
    if(message==WM_DPICHANGED){if(self->font_)DeleteObject(self->font_);self->font_=nullptr;self->textScale_=0;self->Layout();return 0;}
    if(message==WM_KEYDOWN&&w==VK_ESCAPE){self->expanded_=false;self->Layout();return 0;}
    if(message==WM_CLOSE){self->expanded_=false;self->Layout();return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
