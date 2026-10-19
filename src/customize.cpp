#include "window_motion.h"
#include "ui.h"
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

namespace gv {
namespace {

const wchar_t* ElementName(UiElement element){switch(element){case UiElement::Navigation:return L"Пространства";case UiElement::Windows:return L"Открытые окна";case UiElement::Dock:return L"Действия на полотне";case UiElement::Tray:return L"Фоновые приложения";case UiElement::Settings:return L"Настройки";case UiElement::Power:return L"Питание";default:return L"Поиск";}}
bool IsShown(const Config::Interface& ui,UiElement e){return UiPosition(ui,e)!=L"hidden"&&(e!=UiElement::Tray||ui.showTray);}
}
Customization::~Customization(){Close();}
void Customization::Close(){EndDrag(true);if(window_)DestroyWindow(window_);windowIcons_.clear();trayIcons_.clear();appIcon_.reset();}
bool Customization::Message(const MSG& message){return window_&&(message.hwnd==window_||IsChild(window_,message.hwnd))&&IsDialogMessageW(window_,const_cast<MSG*>(&message));}
void Customization::Show(HWND owner,bool appearance){
    appearance_=appearance;
    if(!window_){
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Proc;cls.hIcon=app_.icon;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.Customization";RegisterClassW(&cls);
        RECT rect=CenteredWindow(owner,MulDiv(1020,owner?GetDpiForWindow(owner):GetDpiForSystem(),96),MulDiv(724,owner?GetDpiForWindow(owner):GetDpiForSystem(),96));
        window_=CreateWindowExW(app_.preview?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW,cls.lpszClassName,L"GoodView — настройка интерфейса",WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,cls.hInstance,this);
        if(!window_)return;UINT dpi=GetDpiForWindow(window_);font_=UiFont(15,dpi);title_=UiFont(24,dpi,FW_SEMIBOLD);small_=UiFont(14,dpi,FW_NORMAL,FontRole::Body);mini_=UiFont(12,dpi);background_=CreateSolidBrush(UiTheme().panel);
        auto button=[&](int id,const wchar_t* caption){HWND child=CreateWindowW(L"BUTTON",caption,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);StyleUiButton(child,UiTheme().panel);};
        button(360,L"Движение интерфейса");button(IDCANCEL,L"Закрыть");button(10,L"Расположение");button(11,L"Оформление");button(30,L"Экран 1");
        button(100,L"Среда");button(101,L"Окна");button(102,L"Действия");button(103,L"Фоновые");button(104,L"Поиск");button(105,L"Настройки");button(106,L"Питание");
        button(200,L"Горизонталь");button(201,L"Вертикаль");button(210,L"Сверху");button(211,L"Снизу");button(212,L"Слева");button(213,L"Справа");button(214,L"В центре");button(220,L"Показывать элемент");
        button(228,L"Вид панели окон");button(225,L"Подписи открытых программ");button(226,L"Компактные ярлыки");button(227,L"Индикатор сайтов");
        button(230,L"Уменьшить");button(231,L"Увеличить");button(240,L"Сбросить расположение");button(241,L"Готовые расположения");
        button(320,L"Сумерки");button(321,L"Тёплая светлая");button(322,L"Холодная светлая");button(300,L"Выбрать обои");button(301,L"Название пространства");button(302,L"Показывать обои");button(303,L"Значок пространства");button(304,L"Удалить пространство");button(340,L"Плотность точек");button(350,L"Движение выключено");
        const wchar_t* networkModes[]={L"Вокруг курсора",L"Связи узла",L"Статично",L"Без сети"};for(int i=0;i<4;++i)button(330+i,networkModes[i]);
        const wchar_t* corners[]={L"0",L"8",L"16",L"24",L"32"};for(int i=0;i<5;++i){button(250+i,corners[i]);button(270+i,corners[i]);button(280+i,corners[i]);}
        const wchar_t* themes[]={L"Шалфей",L"Синий час",L"Песок",L"Сирень",L"Терракота",L"Мята",L"Янтарь",L"Полночь",L"Лес",L"Роза"};for(int i=0;i<10;++i)button(310+i,themes[i]);
        BOOL dark=!UiTheme().light;DwmSetWindowAttribute(window_,20,&dark,sizeof(dark));DWMNCRENDERINGPOLICY nc=DWMNCRP_DISABLED;DwmSetWindowAttribute(window_,DWMWA_NCRENDERING_POLICY,&nc,sizeof(nc));
    }
    Layout();if(!appearance_)CapturePreviewIcons();ShowBlueprintWindow(window_,SW_SHOWNORMAL);SetForegroundWindow(window_);SetFocus(GetDlgItem(window_,appearance_?11:10));app_.SyncPanels();
}
void Customization::CapturePreviewIcons(){
    // Reuse cached images only while this editor is open; no preview polling.
    windowIcons_.clear();trayIcons_.clear();if(!appIcon_)appIcon_=MonoFromIcon(app_.icon);
    std::set<DWORD> seen;
    for(const auto& window:app_.openWindows){
        if(!IsWindow(window.hwnd))continue;MONITORINFOEXW info{};info.cbSize=sizeof(info);GetMonitorInfoW(MonitorFromWindow(window.hwnd,MONITOR_DEFAULTTONEAREST),&info);
        if(!app_.monitors.empty()&&app_.monitors[monitor_].label!=info.szDevice)continue;
        if(!app_.config.ui.railOrbit&&!seen.insert(window.pid).second)continue;
        auto icon=app_.icons.GetWindow(window.hwnd);if(icon&&!icon->pixels.empty())windowIcons_.push_back(std::move(icon));if(windowIcons_.size()==(app_.config.ui.railOrbit?9u:4u))break;
    }
    for(const auto& item:app_.notificationTray.Items())if(!(item.state&NIS_HIDDEN)&&item.image&&!item.image->pixels.empty()){trayIcons_.push_back(item.image);if(trayIcons_.size()==4)break;}
    if(windowIcons_.empty())windowIcons_.push_back(appIcon_);if(trayIcons_.empty())trayIcons_.push_back(appIcon_);
}
void Customization::DrawMiniPanel(HDC dc,UiElement element,RECT rect,float factor){
    auto U=[&](float value){return static_cast<int>(std::lround(value*factor));};int saved=SaveDC(dc);IntersectClipRect(dc,rect.left+1,rect.top+1,rect.right-1,rect.bottom-1);
    auto text=[&](std::wstring_view value,RECT box,COLORREF color,UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto old=SelectObject(dc,mini_);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.data(),static_cast<int>(value.size()),&box,flags|DT_NOPREFIX);SelectObject(dc,old);};
    const auto theme=UiTheme();bool vertical=UiVertical(app_.config.ui,element);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    if(element==UiElement::Power){const Glyph glyphs[]={Glyph::Power,Glyph::Restart,Glyph::Moon};for(int i=0;i<3;++i){int size=std::max(8,std::min(width,height)/2),x=vertical?rect.left+(width-size)/2:rect.left+width*(2*i+1)/6-size/2,y=vertical?rect.top+height*(2*i+1)/6-size/2:rect.top+(height-size)/2;DrawGlyph(dc,glyphs[i],{x,y,x+size,y+size},theme.text);}RestoreDC(dc,saved);return;}
    if(element==UiElement::Windows&&app_.config.ui.railOrbit){
        DrawRingFrame(dc,rect,element==selected_?theme.field:theme.card,element==selected_?theme.accent:theme.border,static_cast<int>(windowIcons_.size()));
        auto layout=LayoutOrbit(static_cast<int>(windowIcons_.size()),0);int size=std::max(8,U(14));
        for(int i=0;i<layout.visible;++i){int x=rect.left+static_cast<int>(layout.slots[i].x*width/280)-size/2,y=rect.top+static_cast<int>(layout.slots[i].y*height/280)-size/2;if(windowIcons_[i])DrawMonoIcon(dc,*windowIcons_[i],{x,y,x+size,y+size});}
        RestoreDC(dc,saved);return;
    }
    if(element==UiElement::Windows||element==UiElement::Tray){
        const auto& icons=element==UiElement::Windows?windowIcons_:trayIcons_;int size=std::max(1,std::min({U(16),width-U(6),height-U(6)})),step=size+U(7);int available=(vertical?height:width)-U(8);int count=std::min(static_cast<int>(icons.size()),std::max(0,(available+U(7))/std::max(1,step)));
        for(int i=0;i<count;++i){int x=vertical?rect.left+(width-size)/2:rect.left+U(5)+i*step,y=vertical?rect.top+U(5)+i*step:rect.top+(height-size)/2;if(icons[i])DrawMonoIcon(dc,*icons[i],{x,y,x+size,y+size});}
    }else if(element==UiElement::Navigation){
        int size=std::max(10,std::min(U(17),height-U(8))),cursor=vertical?rect.top+U(6):rect.left+U(7);
        for(const auto& scene:app_.config.scenes){RECT item=vertical?RECT{rect.left+(width-size)/2,cursor,rect.left+(width+size)/2,cursor+size}:RECT{cursor,rect.top+(height-size)/2,cursor+size,rect.top+(height+size)/2};if(item.right>rect.right-U(2)||item.bottom>rect.bottom-U(2))break;DrawGlyph(dc,SceneGlyph(scene),item,scene.id==app_.config.active?theme.accent:theme.muted);cursor+=size+U(9);}
    }else if(element==UiElement::Dock){
        for(int i=0;i<3;++i){RECT item=vertical?RECT{rect.left+U(3),rect.top+height*i/3,rect.right-U(3),rect.top+height*(i+1)/3}:RECT{rect.left+width*i/3,rect.top+U(2),rect.left+width*(i+1)/3,rect.bottom-U(2)};if(i==2)text(L"100%",item,theme.muted);else{int size=std::max(10,std::min(U(15),std::min(static_cast<int>(item.right-item.left),static_cast<int>(item.bottom-item.top))-U(4)));int x=(item.left+item.right-size)/2,y=(item.top+item.bottom-size)/2;DrawGlyph(dc,i==0?Glyph::Add:Glyph::Layout,{x,y,x+size,y+size},i==0?theme.accent:theme.muted);}}
    }else if(element==UiElement::Settings){int size=std::max(10,std::min({U(18),width-U(8),height-U(8)}));DrawGlyph(dc,Glyph::Settings,{rect.left+(width-size)/2,rect.top+(height-size)/2,rect.left+(width+size)/2,rect.top+(height+size)/2},theme.text);
    }else{
        int size=std::max(10,std::min(U(17),height-U(8)));DrawGlyph(dc,Glyph::Search,{rect.left+U(8),rect.top+(height-size)/2,rect.left+U(8)+size,rect.top+(height+size)/2},theme.muted);
    }
    RestoreDC(dc,saved);
}
void Customization::Refresh(){if(window_){Layout();InvalidateRect(window_,nullptr,FALSE);}}
void Customization::Layout(){
    if(!window_)return;RECT client;GetClientRect(window_,&client);float factor=std::min(client.right/1100.f,client.bottom/780.f);auto U=[&](auto value){return static_cast<int>(std::lround(static_cast<float>(value)*factor));};
    auto place=[&](int id,int x,int y,int w,int h,bool visible=true){HWND child=GetDlgItem(window_,id);ShowWindow(child,visible?SW_SHOWNOACTIVATE:SW_HIDE);SetWindowPos(child,nullptr,U(x),U(y),U(w),U(h),SWP_NOZORDER|SWP_NOACTIVATE);};
    place(360,696,28,228,42);place(IDCANCEL,936,28,132,42);place(304,732,104,336,44,appearance_);place(10,32,104,172,44);place(11,216,104,172,44);place(30,552,104,140,44,!appearance_&&app_.monitors.size()>1);
    for(int i=0;i<7;++i)place(100+i,32+i*95,166,89,38,!appearance_);
    place(200,732,250,160,38,!appearance_);place(201,902,250,166,38,!appearance_);
    for(int i=0;i<4;++i)place(210+i,732+(i%2)*172,332+(i/2)*48,164,38,!appearance_);
    place(214,732,428,336,38,!appearance_);place(220,732,480,336,38,!appearance_);place(228,368,698,324,38,!appearance_&&selected_==UiElement::Windows);
    for(int i=0;i<5;++i){place(250+i,732+i*68,564,62,36,!appearance_);place(270+i,732+i*68,534,62,36,appearance_);place(280+i,732+i*68,396,62,36,appearance_);}
    place(225,32,650,324,38);place(226,368,650,324,38);place(227,32,698,324,38);place(241,368,698,324,38,appearance_||selected_!=UiElement::Windows);
    place(230,732,638,126,38);place(231,942,638,126,38);place(240,732,698,336,38);
    place(300,32,512,208,38,appearance_);place(301,252,512,248,38,appearance_);place(302,512,512,180,38,appearance_);place(303,732,446,336,38,appearance_);for(int i=0;i<4;++i)place(330+i,32+i*168,592,156,38,appearance_);
    for(int i=0;i<3;++i)place(320+i,32+i*224,260,212,62,appearance_);for(int i=0;i<10;++i)place(310+i,32+(i%5)*134,372+(i/5)*60,124,44,appearance_);
    place(340,732,260,336,38,appearance_);place(350,732,308,336,38,appearance_);
    SetWindowTextW(GetDlgItem(window_,340),(L"Плотность: "+std::to_wstring(static_cast<int>(std::lround(app_.Active().networkDensity*100)))+L"%").c_str());
    SetWindowTextW(GetDlgItem(window_,228),app_.config.ui.railOrbit?L"Окна: кольцо":L"Окна: панель");SetWindowTextW(GetDlgItem(window_,350),app_.Active().gaming?L"В играх фон неподвижен":!app_.Active().networkMotion?L"Движение выключено":app_.Active().networkSpeed<.8f?L"Движение: спокойное":app_.Active().networkSpeed>1.3f?L"Движение: быстрое":L"Движение: обычное");EnableWindow(GetDlgItem(window_,350),!app_.Active().gaming);
    EnableWindow(GetDlgItem(window_,304),app_.config.scenes.size()>1);
    if(!app_.monitors.empty())monitor_=std::clamp(monitor_,0,static_cast<int>(app_.monitors.size())-1);
    SetWindowTextW(GetDlgItem(window_,30),(L"Экран "+std::to_wstring(monitor_+1)).c_str());
    float aspect=1.6f;if(!app_.monitors.empty()){auto bounds=app_.monitors[monitor_].bounds;aspect=static_cast<float>(bounds.right-bounds.left)/std::max(1L,bounds.bottom-bounds.top);}
    float w=660,h=w/aspect;if(h>380){h=380;w=h*aspect;}preview_={U(32+(660-w)/2),U(226+(380-h)/2),U(32+(660+w)/2),U(226+(380+h)/2)};
    bool hideAllowed=selected_!=UiElement::Navigation&&selected_!=UiElement::Search;EnableWindow(GetDlgItem(window_,220),hideAllowed);
    SetWindowTextW(GetDlgItem(window_,220),IsShown(app_.config.ui,selected_)?L"Элемент показан":L"Элемент скрыт");
    for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){StyleUiButton(child,UiTheme().panel);InvalidateRect(child,nullptr,FALSE);}RoundWindow(window_,static_cast<int>(app_.config.ui.windowRadius));InvalidateRect(window_,nullptr,FALSE);
}
void Customization::Draw(HDC dc,RECT client){
    FillRect(dc,&client,background_);RoundFill(dc,client,UiTheme().panel,MulDiv(static_cast<int>(app_.config.ui.windowRadius),GetDpiForWindow(window_),96),UiTheme().border);float factor=std::min(client.right/1100.f,client.bottom/780.f);auto U=[&](float value){return static_cast<LONG>(std::lround(value*factor));};
    auto text=[&](const std::wstring& value,RECT rect,HFONT font,COLORREF color,UINT flags=DT_SINGLELINE){auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&rect,flags);SelectObject(dc,old);};
    text(L"Настройка интерфейса",{U(32),U(24),U(680),U(66)},title_,UiTheme().text);
    text(L"Твоё пространство. Изменения сохраняются автоматически.",{U(34),U(70),U(680),U(96)},font_,UiTheme().muted);
    RoundFill(dc,{U(712),U(164),U(1080),U(744)},UiTheme().card,U(16));
    if(!appearance_){
        text(ElementName(selected_),{U(732),U(184),U(1068),U(214)},font_,UiTheme().text);
        text(L"Направление панели",{U(732),U(220),U(1068),U(246)},small_,UiTheme().muted);
        text(L"Привязка к краю",{U(732),U(310),U(1068),U(336)},small_,UiTheme().muted);
        text(L"Скругление · "+std::to_wstring(static_cast<int>(UiRadius(app_.config.ui,selected_)))+L" px",{U(732),U(532),U(1068),U(558)},small_,UiTheme().muted);
        RoundFill(dc,preview_,UiTheme().canvas,U(10),UiTheme().border);elements_.clear();
        auto bounds=app_.monitors.empty()?RECT{0,0,1920,1200}:app_.monitors[monitor_].bounds;float width=static_cast<float>(bounds.right-bounds.left),height=static_cast<float>(bounds.bottom-bounds.top),sx=(preview_.right-preview_.left)/width,sy=(preview_.bottom-preview_.top)/height;
        RECT example{preview_.left+static_cast<LONG>(width*.30f*sx),preview_.top+static_cast<LONG>(height*.28f*sy),preview_.left+static_cast<LONG>(width*.76f*sx),preview_.top+static_cast<LONG>(height*.72f*sy)};RoundFill(dc,example,UiTheme().card,U(5),UiTheme().border);if(appIcon_)DrawMonoIcon(dc,*appIcon_,{example.left+U(12),example.top+U(13),example.left+U(30),example.top+U(31)});text(L"Окно приложения",{example.left+U(38),example.top+U(10),example.right-U(8),example.top+U(36)},small_,UiTheme().muted);
        UINT monitorDpi=app_.monitors.empty()?96:app_.monitors[monitor_].dpi;float dpiScale=monitorDpi/96.f;Box area{0,0,width/dpiScale,height/dpiScale};
        auto drawElement=[&](UiElement e){
            auto box=app_.PanelBox(e,monitor_,area);RECT rect{preview_.left+static_cast<LONG>(box.x*dpiScale*sx),preview_.top+static_cast<LONG>(box.y*dpiScale*sy),preview_.left+static_cast<LONG>((box.x+box.w)*dpiScale*sx),preview_.top+static_cast<LONG>((box.y+box.h)*dpiScale*sy)};
            if(rect.bottom-rect.top<U(24))rect.bottom=rect.top+U(24);if(rect.right-rect.left<U(24))rect.right=rect.left+U(24);
            if(rect.right>preview_.right-U(2))OffsetRect(&rect,preview_.right-U(2)-rect.right,0);if(rect.bottom>preview_.bottom-U(2))OffsetRect(&rect,0,preview_.bottom-U(2)-rect.bottom);
            bool shown=IsShown(app_.config.ui,e);if(e!=UiElement::Windows||!app_.config.ui.railOrbit)RoundFill(dc,rect,!shown?UiTheme().field:e==selected_?UiTheme().field:UiTheme().card,static_cast<int>(UiRadius(app_.config.ui,e)*dpiScale*sx),e==selected_?UiTheme().accent:UiTheme().border);
            DrawMiniPanel(dc,e,rect,factor);
            elements_.push_back({rect,e});
        };
        for(auto e:{UiElement::Navigation,UiElement::Windows,UiElement::Dock,UiElement::Tray,UiElement::Search,UiElement::Settings,UiElement::Power})if(e!=selected_)drawElement(e);drawElement(selected_);
        text(L"Перетащи выбранную панель. Расположение общее для всех мониторов.",{U(34),U(608),U(692),U(630)},small_,UiTheme().muted);
    }else{
        text(L"Оформление пространства «"+app_.Active().name+L"»",{U(34),U(182),U(692),U(216)},font_,UiTheme().text);
        text(L"Тема",{U(34),U(224),U(692),U(250)},small_,UiTheme().muted);text(L"Акцент",{U(34),U(344),U(692),U(368)},small_,UiTheme().muted);
        text(L"Цвет сохраняется для этого пространства.",{U(34),U(480),U(692),U(508)},small_,UiTheme().muted);text(L"Фон со связями",{U(34),U(558),U(692),U(586)},small_,UiTheme().muted);
        text(L"Фон и виджеты",{U(732),U(188),U(1068),U(220)},font_,UiTheme().text);
        text(L"Плотность точек",{U(732),U(224),U(1068),U(250)},small_,UiTheme().muted);
        text(L"Скругление окна настроек",{U(732),U(366),U(1068),U(392)},small_,UiTheme().muted);
        text(L"Движение только на открытом полотне. В играх фон неподвижен.",{U(34),U(632),U(692),U(649)},mini_,UiTheme().muted);
        text(L"Скругление карточек",{U(732),U(500),U(1068),U(528)},small_,UiTheme().muted);

    }
    text(L"Размер текста",{U(732),U(608),U(1068),U(632)},small_,UiTheme().muted);
    text(std::to_wstring(static_cast<int>(std::lround(app_.config.ui.textScale*100)))+L"%",{U(860),U(638),U(940),U(676)},font_,UiTheme().text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    text(L"Изменения сохраняются автоматически",{U(34),U(748),U(1068),U(772)},small_,UiTheme().muted);
}
void Customization::Apply(){app_.ApplyInterface();}
void Customization::Command(int id){
    auto& ui=app_.config.ui;
    if(id==360){EditMotionPreferences(window_);return;}
    if(id==IDCANCEL){Close();return;}if(id==10||id==11){appearance_=id==11;if(!appearance_&&windowIcons_.empty())CapturePreviewIcons();Refresh();return;}
    if(id==30){monitor_=(monitor_+1)%std::max(1,static_cast<int>(app_.monitors.size()));CapturePreviewIcons();Refresh();return;}
    if(id>=100&&id<=106){selected_=static_cast<UiElement>(id-100);Refresh();return;}
    if(id==200||id==201)UiVertical(ui,selected_)=id==201;
    else if(id>=210&&id<=214){const wchar_t* positions[]={L"top",L"bottom",L"left",L"right",L"center"};UiPosition(ui,selected_)=positions[id-210];}
    else if(id==220){if(selected_==UiElement::Tray)ui.showTray=!ui.showTray;else if(selected_!=UiElement::Navigation&&selected_!=UiElement::Search)UiPosition(ui,selected_)=UiPosition(ui,selected_)==L"hidden"?L"custom":L"hidden";}
    else if(id==225)ui.railLabels=!ui.railLabels;else if(id==226)ui.compactLaunchers=!ui.compactLaunchers;else if(id==227)ui.showActivity=!ui.showActivity;
    else if(id==230||id==231)ui.textScale=std::clamp(std::round((ui.textScale+(id==231?.05f:-.05f))*100)/100,0.9f,1.3f);
    else if(id>=250&&id<=254){const float radii[]={0,8,16,24,32};UiRadius(ui,selected_)=radii[id-250];}
    else if(id>=270&&id<=274){const float radii[]={0,8,16,24,32};ui.cardRadius=radii[id-270];}
    else if(id>=280&&id<=284){const float radii[]={0,8,16,24,32};ui.windowRadius=radii[id-280];}
    else if(id==241){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Поиск сверху");AppendMenuW(menu,MF_STRING,2,L"Поиск снизу");AppendMenuW(menu,MF_STRING,3,L"Боковой поиск");RECT rect;GetWindowRect(GetDlgItem(window_,241),&rect);auto choice=StyledMenu(menu,window_,{rect.left,rect.bottom});DestroyMenu(menu);if(!choice)return;ui.barPosition=L"top";ui.barVertical=false;ui.railPosition=choice==3?L"right":L"left";ui.railVertical=true;ui.dockPosition=L"custom";ui.dockX=0;ui.dockY=1;ui.dockVertical=false;ui.trayPosition=L"right";ui.trayVertical=false;ui.settingsPosition=L"custom";ui.settingsX=1;ui.settingsY=0;ui.searchPosition=choice==2?L"bottom":L"custom";ui.searchVertical=false;ui.searchX=choice==3?0:.5f;ui.searchY=.09f;}
    else if(id==240){auto old=ui;ui=Config::Interface{};ui.textScale=old.textScale;ui.railLabels=old.railLabels;ui.railOrbit=old.railOrbit;ui.compactLaunchers=old.compactLaunchers;ui.cardRadius=old.cardRadius;ui.windowRadius=old.windowRadius;ui.menuMotion=old.menuMotion;ui.windowMotion=old.windowMotion;}
    else if(id>=320&&id<=322){const wchar_t* modes[]={L"dark",L"warm",L"cool"};app_.Active().appearance=modes[id-320];if(background_)DeleteObject(background_);background_=CreateSolidBrush(UiTheme().panel);Apply();return;}else if(id>=310&&id<320){app_.SetTheme(id-310);Refresh();return;}
    else if(id==300){app_.ChangeWallpaper();Refresh();return;}
    else if(id==301){Card card;card.type=L"scene";card.title=app_.Active().name;card.content=app_.Active().description;if(EditDialog(window_,card,L"Пространство")){app_.Active().name=card.title;app_.Active().description=card.content;app_.SaveSoon();app_.Refresh();}Refresh();return;}
    else if(id==302)app_.config.wallpaperEnabled=!app_.config.wallpaperEnabled;
    else if(id==303){if(ChooseSpaceIcon(window_,app_.Active().icon)){app_.SaveSoon();app_.Refresh();}Refresh();return;}
    else if(id==304){
        const auto& scene=app_.Active();HMENU menu=CreatePopupMenu();
        AppendMenuW(menu,MF_GRAYED,0,(L"Карточек: "+std::to_wstring(scene.cards.size())+L". Сайты закроются.").c_str());
        AppendMenuW(menu,MF_GRAYED,0,L"Открытые приложения сохранятся.");
        AppendMenuW(menu,MF_STRING,1,(L"Удалить «"+scene.name+L"»").c_str());AppendMenuW(menu,MF_STRING,2,L"Отмена");
        RECT rect;GetWindowRect(GetDlgItem(window_,304),&rect);auto result=StyledMenu(menu,window_,{rect.left,rect.bottom});DestroyMenu(menu);
        if(result==1){auto sceneId=scene.id;app_.RemoveScene(sceneId);}Refresh();return;
    }
    else if(id==340){HMENU menu=CreatePopupMenu();const int values[]={50,75,100,150,200};for(int i=0;i<5;++i)AppendMenuW(menu,MF_STRING|(static_cast<int>(std::lround(app_.Active().networkDensity*100))==values[i]?MF_CHECKED:0),i+1,(std::to_wstring(values[i])+L"%").c_str());RECT rect;GetWindowRect(GetDlgItem(window_,340),&rect);auto result=StyledMenu(menu,window_,{rect.left,rect.bottom});DestroyMenu(menu);if(result>=1&&result<=5)app_.Active().networkDensity=values[result-1]/100.f;else return;}
    else if(id==228){ui.railOrbit=!ui.railOrbit;CapturePreviewIcons();}
    else if(id==350){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Без движения");AppendMenuW(menu,MF_STRING,2,L"Спокойное");AppendMenuW(menu,MF_STRING,3,L"Обычное");AppendMenuW(menu,MF_STRING,4,L"Быстрое");RECT rect;GetWindowRect(GetDlgItem(window_,350),&rect);auto choice=StyledMenu(menu,window_,{rect.left,rect.bottom});DestroyMenu(menu);if(!choice)return;app_.Active().networkMotion=choice!=1;const float speeds[]={1,.5f,1,1.8f};app_.Active().networkSpeed=speeds[choice-1];}
    else if(id>=330&&id<=333){const wchar_t* modes[]={L"near",L"links",L"static",L"none"};app_.Active().network=modes[id-330];}
    else return;Apply();
}
void Customization::MoveElement(POINT point){
    if(!dragging_)return;auto& ui=app_.config.ui;auto bounds=app_.monitors.empty()?RECT{0,0,1920,1200}:app_.monitors[monitor_].bounds;UINT dpi=app_.monitors.empty()?96:app_.monitors[monitor_].dpi;float scale=dpi/96.f;
    float w=(bounds.right-bounds.left)/scale,h=(bounds.bottom-bounds.top)/scale,sx=(preview_.right-preview_.left)/w,sy=(preview_.bottom-preview_.top)/h;auto box=app_.PanelBox(selected_,monitor_,{0,0,w,h});auto [x,y]=UiAnchor(ui,selected_);
    x=std::clamp(((point.x-dragOffset_.x-preview_.left)/sx-12)/std::max(1.f,w-box.w-24),0.f,1.f);y=std::clamp(((point.y-dragOffset_.y-preview_.top)/sy-12)/std::max(1.f,h-box.h-24),0.f,1.f);InvalidateRect(window_,nullptr,FALSE);
}
void Customization::EndDrag(bool cancel){if(!dragging_)return;dragging_=false;if(cancel&&beforeDrag_)app_.config.ui=*beforeDrag_;beforeDrag_.reset();if(GetCapture()==window_)ReleaseCapture();if(!cancel)Apply();else Refresh();}
bool Customization::ExportPreview(const fs::path& path,bool appearance){
    Show(nullptr,appearance);RECT client;GetClientRect(window_,&client);HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),client.right,-client.bottom,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);Draw(dc,client);
    for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(child);item.hwndItem=child;item.hDC=dc;GetWindowRect(child,&item.rcItem);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&item.rcItem),2);SendMessageW(window_,WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));}
    bool saved=SaveBitmapPng(bitmap,path);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return saved;
}
LRESULT CALLBACK Customization::Proc(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<Customization*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Customization*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(window,message,w,l);
    if(message==WM_ERASEBKGND)return 1;
    // Keep native resize/snap behavior while drawing the whole frame ourselves.
    if(message==WM_NCCALCSIZE)return 0;
    if(message==WM_NCPAINT)return 0;
    if(message==WM_NCACTIVATE)return TRUE;
    if(message==WM_PRINTCLIENT){RECT client;GetClientRect(window,&client);self->Draw(reinterpret_cast<HDC>(w),client);return 0;}
    if(message==WM_PAINT){PAINTSTRUCT paint;HDC target=BeginPaint(window,&paint);RECT client;GetClientRect(window,&client);HDC memory=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,client.right,client.bottom);auto old=SelectObject(memory,bitmap);self->Draw(memory,client);BitBlt(target,0,0,client.right,client.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(window,&paint);return 0;}
    if(message==WM_COMMAND&&HIWORD(w)==BN_CLICKED){self->Command(LOWORD(w));return 0;}
    if(message==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);auto& ui=self->app_.config.ui;bool active=item.CtlID==10?!self->appearance_:item.CtlID==11?self->appearance_:item.CtlID>=100&&item.CtlID<=106?static_cast<UINT>(self->selected_)==item.CtlID-100:item.CtlID==200?!UiVertical(ui,self->selected_):item.CtlID==201?UiVertical(ui,self->selected_):item.CtlID==220?IsShown(ui,self->selected_):item.CtlID==225?ui.railLabels:item.CtlID==226?ui.compactLaunchers:item.CtlID==227?ui.showActivity:item.CtlID==302?self->app_.config.wallpaperEnabled:false;
        if(item.CtlID>=210&&item.CtlID<=214){const wchar_t* positions[]={L"top",L"bottom",L"left",L"right",L"center"};active=UiPosition(ui,self->selected_)==positions[item.CtlID-210];}if(item.CtlID>=320&&item.CtlID<=322){const wchar_t* modes[]={L"dark",L"warm",L"cool"};active=self->app_.Active().appearance==modes[item.CtlID-320];}if(item.CtlID>=310&&item.CtlID<320){const wchar_t* accents[]={L"B4CEAC",L"A5BED5",L"D6B998",L"B8ACD5",L"D9A49C",L"9BD2C5",L"E0C780",L"B9C7DB",L"A9B9A0",L"DBA6BF"};active=_wcsicmp(self->app_.Active().accent.c_str(),accents[item.CtlID-310])==0;}if(item.CtlID>=250&&item.CtlID<=254){const float radii[]={0,8,16,24,32};active=UiRadius(ui,self->selected_)==radii[item.CtlID-250];}if(item.CtlID>=270&&item.CtlID<=274){const float radii[]={0,8,16,24,32};active=ui.cardRadius==radii[item.CtlID-270];}if(item.CtlID>=330&&item.CtlID<=333){const wchar_t* modes[]={L"near",L"links",L"static",L"none"};active=self->app_.Active().network==modes[item.CtlID-330];}if(item.CtlID>=280&&item.CtlID<=284){const float radii[]={0,8,16,24,32};active=ui.windowRadius==radii[item.CtlID-280];}if(item.CtlID==350)active=self->app_.Active().networkMotion;if(item.CtlID<310||item.CtlID>=320)DrawUiButton(item,self->font_,active,UiTheme().panel);
        if(item.CtlID>=310&&item.CtlID<320){const COLORREF colors[]={RGB(180,206,172),RGB(165,190,213),RGB(214,185,152),RGB(184,172,213),RGB(217,164,156),RGB(155,210,197),RGB(224,199,128),RGB(185,199,219),RGB(169,185,160),RGB(219,166,191)};COLORREF color=colors[item.CtlID-310];if(UiTheme().light){Scene sample=self->app_.Active();const wchar_t* hexes[]={L"B4CEAC",L"A5BED5",L"D6B998",L"B8ACD5",L"D9A49C",L"9BD2C5",L"E0C780",L"B9C7DB",L"A9B9A0",L"DBA6BF"};sample.accent=hexes[item.CtlID-310];color=PaletteFor(sample).accent;}DrawAccentChoice(item,self->font_,color,active);}
        return TRUE;}
    if(message==WM_SIZE){self->Layout();return 0;}
    if(message==WM_LBUTTONDOWN&&!self->appearance_){
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};std::optional<std::pair<RECT,UiElement>> hit;
        if(!hit)for(auto it=self->elements_.rbegin();it!=self->elements_.rend();++it)if(PtInRect(&it->first,point)){hit=*it;break;}
        if(hit){self->selected_=hit->second;self->beforeDrag_=self->app_.config.ui;self->dragging_=true;self->dragOffset_={point.x-hit->first.left,point.y-hit->first.top};UiPosition(self->app_.config.ui,self->selected_)=L"custom";SetCapture(window);self->MoveElement(point);self->Refresh();return 0;}
    }
    if(message==WM_MOUSEMOVE&&self->dragging_){self->MoveElement({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;}
    if(message==WM_LBUTTONUP&&self->dragging_){self->MoveElement({GET_X_LPARAM(l),GET_Y_LPARAM(l)});self->EndDrag(false);return 0;}
    if(message==WM_CANCELMODE||(message==WM_CAPTURECHANGED&&reinterpret_cast<HWND>(l)!=window)){self->EndDrag(true);return 0;}
    if(message==WM_KEYDOWN&&w==VK_ESCAPE){if(self->dragging_)self->EndDrag(true);else self->Close();return 0;}
    if(message==WM_NCHITTEST){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};RECT bounds;GetWindowRect(window,&bounds);int grip=MulDiv(7,GetDpiForWindow(window),96);bool left=point.x<bounds.left+grip,right=point.x>=bounds.right-grip,top=point.y<bounds.top+grip,bottom=point.y>=bounds.bottom-grip;if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;if(left)return HTLEFT;if(right)return HTRIGHT;ScreenToClient(window,&point);if(point.y<MulDiv(90,GetDpiForWindow(window),96))return HTCAPTION;return HTCLIENT;}
    if(message==WM_GETMINMAXINFO){auto* info=reinterpret_cast<MINMAXINFO*>(l);UINT dpi=GetDpiForWindow(window);info->ptMinTrackSize={MulDiv(950,dpi,96),MulDiv(680,dpi,96)};return 0;}
    if(message==WM_CLOSE){self->EndDrag(true);DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){self->window_=nullptr;for(auto font:{self->font_,self->title_,self->small_,self->mini_})if(font)DeleteObject(font);if(self->background_)DeleteObject(self->background_);self->font_=self->title_=self->small_=self->mini_=nullptr;self->background_=nullptr;self->windowIcons_.clear();self->trayIcons_.clear();self->appIcon_.reset();self->elements_.clear();return 0;}
    if(message==WM_DPICHANGED){auto* rect=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);for(auto font:{&self->font_,&self->title_,&self->small_,&self->mini_})if(*font)DeleteObject(*font);self->font_=UiFont(15,HIWORD(w));self->title_=UiFont(24,HIWORD(w),FW_SEMIBOLD);self->small_=UiFont(14,HIWORD(w),FW_NORMAL,FontRole::Body);self->mini_=UiFont(12,HIWORD(w));self->Refresh();return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
