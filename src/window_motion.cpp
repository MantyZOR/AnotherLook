#include "window_motion.h"
#include "motion_scene.h"
#include "ui.h"
#include <commctrl.h>
#include <wrl/client.h>
#include <memory>

namespace gv {
namespace {
constexpr UINT_PTR SubclassId=0x47564d;
using Property=MotionScene::Property;
struct Reveal {
    HWND proxy{};MotionScene scene;RECT bounds{};bool layered=false;
    ~Reveal(){if(proxy)DestroyWindow(proxy);}
};
LRESULT CALLBACK RevealProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
LRESULT CALLBACK ProxyProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_NCHITTEST)return HTTRANSPARENT;
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT){PAINTSTRUCT p;BeginPaint(window,&p);EndPaint(window,&p);return 0;}
    return DefWindowProcW(window,message,w,l);
}
void Restore(HWND window,Reveal* state,bool redraw){
    KillTimer(window,WindowMotionTimer);RemoveWindowSubclass(window,RevealProc,SubclassId);
    // Restore the live HWND beneath the fully revealed proxy, then remove the
    // proxy. Never leave an invisible or non-interactive editor after a cancel.
    if(state->layered&&IsWindow(window)){
        SetLayeredWindowAttributes(window,0,255,LWA_ALPHA);
        SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)&~WS_EX_LAYERED);
        if(redraw&&IsWindowVisible(window))RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    }
    delete state;
}
bool Prepare(HWND window,Reveal& state){
    GetWindowRect(window,&state.bounds);int width=state.bounds.right-state.bounds.left,height=state.bounds.bottom-state.bounds.top;
    if(width<8||height<8||static_cast<size_t>(width)*height*4>12*1024*1024)return false;
    WNDCLASSW cls{};cls.lpfnWndProc=ProxyProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GoodView.Opening";RegisterClassW(&cls);
    DWORD ex=WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP;
    if(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)ex|=WS_EX_TOPMOST;
    state.proxy=CreateWindowExW(ex,cls.lpszClassName,L"",WS_POPUP,state.bounds.left,state.bounds.top,width,height,window,nullptr,cls.hInstance,nullptr);
    if(!state.proxy||!state.scene.Open(state.proxy,36*1024*1024))return false;
    PanelBitmap image;if(!image.Resize(width,height))return false;
    RECT rect{0,0,width,height};auto theme=UiTheme();HBRUSH background=CreateSolidBrush(theme.panel);FillRect(image.Dc(),&rect,background);DeleteObject(background);
    // Only our own window paints into this one-shot image. WM_PRINT also asks
    // native child controls and the widget preview to draw into the same DC.
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(image.Dc()),PRF_CLIENT|PRF_CHILDREN|PRF_NONCLIENT|PRF_ERASEBKGND);
    float dpi=GetDpiForWindow(window)/96.f;
    auto stored=reinterpret_cast<INT_PTR>(GetPropW(window,L"GoodView.Radius"));
    float radius=stored?static_cast<float>(stored-1)*dpi:0.f;image.RoundAlpha(radius);
    auto color=[](COLORREF c,float alpha=1.f){return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,alpha);};
    auto sheet=state.scene.Draw({width,height},[&](ID2D1RenderTarget* dc){
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;dc->CreateSolidColorBrush(color(theme.panel),&brush);
        if(brush)dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0,0,static_cast<float>(width),static_cast<float>(height)),radius,radius),brush.Get());
    });
    auto content=state.scene.Add(image);image.Reset();
    auto outline=state.scene.Draw({width,height},[&](ID2D1RenderTarget* dc){
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;dc->CreateSolidColorBrush(color(theme.accent),&brush);if(!brush)return;
        float inset=std::max(1.f,dpi),r=std::max(0.f,radius-inset);
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(inset,inset,width-inset,height-inset),r,r),brush.Get(),dpi);
        float y=std::min(height*.25f,76*dpi),margin=24*dpi;
        dc->DrawLine(D2D1::Point2F(margin,y),D2D1::Point2F(width-margin,y),brush.Get(),dpi*.6f);
    });
    if(sheet==MotionScene::None||content==MotionScene::None||outline==MotionScene::None)return false;
    for(auto node:{sheet,outline}){
        state.scene.Animate(node,Property::Right,1,static_cast<float>(width),0,.13);
        state.scene.Animate(node,Property::Bottom,std::max(2.f,3*dpi),static_cast<float>(height),.10,.26);
    }
    state.scene.Animate(content,Property::Opacity,0,1,.14,.28);
    state.scene.Animate(outline,Property::Opacity,.7f,0,.23,.19);
    return true;
}
LRESULT CALLBACK RevealProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto* state=reinterpret_cast<Reveal*>(data);
    if(message==WM_NCDESTROY){Restore(window,state,false);return DefSubclassProc(window,message,w,l);}
    // This is a one-shot cleanup timer, NOT a frame timer. Animation curves
    // continue on the compositor even while this UI thread is busy painting.
    if(message==WM_TIMER&&w==WindowMotionTimer){Restore(window,state,true);return 0;}
    bool interrupt=(message==WM_SHOWWINDOW&&!w)||(message==WM_COMMAND&&(HIWORD(w)==BN_CLICKED||HIWORD(w)==EN_CHANGE))||
        (message==WM_PARENTNOTIFY&&(LOWORD(w)==WM_LBUTTONDOWN||LOWORD(w)==WM_RBUTTONDOWN))||
        (message==WM_ACTIVATE&&LOWORD(w)==WA_INACTIVE)||message==WM_CLOSE||message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||
        message==WM_NCLBUTTONDOWN||message==WM_CANCELMODE||message==WM_DPICHANGED||message==WM_SETTINGCHANGE;
    if(message==WM_WINDOWPOSCHANGING){auto* pos=reinterpret_cast<WINDOWPOS*>(l);
        if(!(pos->flags&SWP_NOSIZE)&&(pos->cx!=state->bounds.right-state->bounds.left||pos->cy!=state->bounds.bottom-state->bounds.top))interrupt=true;
        if(!(pos->flags&SWP_NOMOVE)&&(pos->x!=state->bounds.left||pos->y!=state->bounds.top))interrupt=true;
    }
    if(interrupt)Restore(window,state,true);
    return DefSubclassProc(window,message,w,l);
}
}
bool ClientMotionAllowed(){BOOL animations=TRUE;return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0)&&animations&&!GetSystemMetrics(SM_REMOTESESSION);}
bool MenuMotionEnabled(){auto* app=CurrentApplication();return !app||app->config.ui.menuMotion!=L"none";}
bool WindowMotionActive(HWND window){DWORD_PTR data=0;return GetWindowSubclass(window,RevealProc,SubclassId,&data)!=FALSE;}
void FinishWindowMotion(HWND window){DWORD_PTR data=0;if(GetWindowSubclass(window,RevealProc,SubclassId,&data))Restore(window,reinterpret_cast<Reveal*>(data),true);}
void ShowBlueprintWindow(HWND window,int command){
    if(!window)return;auto* app=CurrentApplication();
    if(IsWindowVisible(window)||!ClientMotionAllowed()||(app&&!app->config.ui.windowMotion)||(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED)){
        FinishWindowMotion(window);ShowWindow(window,command);return;
    }
    auto state=std::make_unique<Reveal>();
    if(!Prepare(window,*state)||!SetWindowSubclass(window,RevealProc,SubclassId,reinterpret_cast<DWORD_PTR>(state.get()))){state.reset();ShowWindow(window,command);return;}
    auto* live=state.release();SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)|WS_EX_LAYERED);live->layered=true;
    // Alpha zero makes Windows pass mouse hits through the live HWND. One is
    // visually negligible but keeps its controls interactive under the proxy.
    if(!SetLayeredWindowAttributes(window,0,1,LWA_ALPHA)){Restore(window,live,false);ShowWindow(window,command);return;}
    ShowWindow(window,command);
    if(!WindowMotionActive(window))return;
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    ShowWindow(live->proxy,SW_SHOWNOACTIVATE);
    if(!live->scene.Commit()||!SetTimer(window,WindowMotionTimer,WindowMotionDuration+20,nullptr))FinishWindowMotion(window);
}
void EditMotionPreferences(HWND owner){
    auto* app=CurrentApplication();if(!app)return;auto& ui=app->config.ui;
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(ui.menuMotion==L"constellation"?MF_CHECKED:0),1,L"Меню: созвездие");
    AppendMenuW(menu,MF_STRING|(ui.menuMotion==L"petals"?MF_CHECKED:0),2,L"Меню: лепестки");
    AppendMenuW(menu,MF_STRING|(ui.menuMotion==L"none"?MF_CHECKED:0),3,L"Меню: без движения");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(ui.windowMotion?MF_CHECKED:0),4,L"Окна: чертёж в сборке");
    AppendMenuW(menu,MF_STRING|(!ui.windowMotion?MF_CHECKED:0),5,L"Окна: без движения");
    POINT point{};GetCursorPos(&point);auto result=StyledMenu(menu,owner,point);DestroyMenu(menu);
    if(result>=1&&result<=3)ui.menuMotion=result==1?L"constellation":result==2?L"petals":L"none";
    else if(result==4||result==5)ui.windowMotion=result==4;else return;
    app->SaveSoon();
}
}
