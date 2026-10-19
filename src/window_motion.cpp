#include "window_motion.h"
#include "ui.h"
#include <commctrl.h>
#include <memory>

namespace gv {
namespace {
constexpr UINT_PTR SubclassId=0x47564d;
struct Reveal {
    HRGN original{};bool hasOriginal=false;ULONGLONG began=0;int width=0,height=0,band=3;
    ~Reveal(){if(original)DeleteObject(original);}
};
LRESULT CALLBACK RevealProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void Restore(HWND window,Reveal* state,bool redraw){
    KillTimer(window,WindowMotionTimer);RemoveWindowSubclass(window,RevealProc,SubclassId);
    if(state->hasOriginal){if(SetWindowRgn(window,state->original,redraw))state->original=nullptr;}
    else SetWindowRgn(window,nullptr,redraw);
    delete state;
    // Async result/layout updates may request the same rounding while the
    // opening clip is active. Apply the latest radius only after restoring it.
    auto radius=reinterpret_cast<INT_PTR>(RemovePropW(window,L"GoodView.MotionRadius"));
    if(radius>0)RoundWindow(window,static_cast<int>(radius-1));
}
bool Clip(HWND window,Reveal& state,float progress){
    auto r=BlueprintClip(state.width,state.height,progress,state.band);
    HRGN region=CreateRectRgnIndirect(&r);if(!region)return false;
    if(state.hasOriginal)CombineRgn(region,region,state.original,RGN_AND);
    if(!SetWindowRgn(window,region,TRUE)){DeleteObject(region);return false;}return true;
}
LRESULT CALLBACK RevealProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto* state=reinterpret_cast<Reveal*>(data);
    if(message==WM_NCDESTROY){RemovePropW(window,L"GoodView.MotionRadius");KillTimer(window,WindowMotionTimer);RemoveWindowSubclass(window,RevealProc,SubclassId);delete state;return DefSubclassProc(window,message,w,l);}
    if(message==WM_TIMER&&w==WindowMotionTimer){
        float progress=(GetTickCount64()-state->began)/static_cast<float>(WindowMotionDuration);
        if(progress>=1||!IsWindowVisible(window)||!ClientMotionAllowed()||!Clip(window,*state,progress))Restore(window,state,true);
        return 0;
    }
    bool interrupt=(message==WM_SHOWWINDOW&&!w)||(message==WM_COMMAND&&(HIWORD(w)==BN_CLICKED||HIWORD(w)==EN_CHANGE))||message==WM_CLOSE||message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_CANCELMODE||message==WM_DPICHANGED||message==WM_SETTINGCHANGE;
    if(message==WM_WINDOWPOSCHANGING){auto* pos=reinterpret_cast<WINDOWPOS*>(l);if(!(pos->flags&SWP_NOSIZE)&&(pos->cx!=state->width||pos->cy!=state->height))interrupt=true;}
    if(interrupt)Restore(window,state,true);
    return DefSubclassProc(window,message,w,l);
}
}
bool ClientMotionAllowed(){BOOL animations=TRUE;return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0)&&animations&&!GetSystemMetrics(SM_REMOTESESSION);}
bool MenuMotionEnabled(){auto* app=CurrentApplication();return !app||app->config.ui.menuMotion!=L"none";}
bool WindowMotionActive(HWND window){DWORD_PTR data=0;return GetWindowSubclass(window,RevealProc,SubclassId,&data)!=FALSE;}
void FinishWindowMotion(HWND window){DWORD_PTR data=0;if(GetWindowSubclass(window,RevealProc,SubclassId,&data))Restore(window,reinterpret_cast<Reveal*>(data),true);}
void ShowBlueprintWindow(HWND window,int command){
    if(!window)return;
    auto* app=CurrentApplication();
    if(IsWindowVisible(window)||!ClientMotionAllowed()||(app&&!app->config.ui.windowMotion)||(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED)){
        FinishWindowMotion(window);ShowWindow(window,command);return;
    }
    auto state=std::make_unique<Reveal>();RECT bounds{};GetWindowRect(window,&bounds);state->width=bounds.right-bounds.left;state->height=bounds.bottom-bounds.top;
    // Only window regions are animated. No screenshots, overlay windows,
    // full-window bitmap cache, render thread, or timer after completion.
    if(state->width<8||state->height<8){ShowWindow(window,command);return;}
    state->band=std::max(2,MulDiv(3,GetDpiForWindow(window),96));state->original=CreateRectRgn(0,0,0,0);state->hasOriginal=state->original&&GetWindowRgn(window,state->original)!=ERROR;state->began=GetTickCount64();
    if(!SetWindowSubclass(window,RevealProc,SubclassId,reinterpret_cast<DWORD_PTR>(state.get()))){ShowWindow(window,command);return;}
    auto* live=state.release();
    if(!Clip(window,*live,0)){Restore(window,live,false);ShowWindow(window,command);return;}
    ShowWindow(window,command);
    if(WindowMotionActive(window)&&!SetTimer(window,WindowMotionTimer,16,nullptr))FinishWindowMotion(window);
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
