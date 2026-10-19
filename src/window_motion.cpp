#include "window_motion.h"
#include "motion_scene.h"
#include "ui.h"
#include <commctrl.h>
#include <wrl/client.h>
#include <memory>

namespace gv {
namespace {
constexpr UINT_PTR SubclassId=0x47564d;
constexpr UINT StartMotionMessage=WM_APP+0x5d1;
using Property=MotionScene::Property;
struct Reveal {
    HWND proxy{};MotionScene scene;RECT bounds{};bool layered=false,initializing=false,cancelled=false,destroyed=false;UINT_PTR sequence=0;
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
    if(!state.proxy||!state.scene.Open(state.proxy,24*1024*1024))return false;
    PanelBitmap sheet,image;if(!sheet.Resize(width,height)||!image.Resize(width,height))return false;
    RECT rect{0,0,width,height};auto theme=UiTheme();HBRUSH background=CreateSolidBrush(theme.panel);FillRect(sheet.Dc(),&rect,background);DeleteObject(background);
    // Preserve the exact frame and parent painting. Native child controls are
    // captured separately into one shared image, never re-painted per frame.
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(sheet.Dc()),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);
    BitBlt(image.Dc(),0,0,width,height,sheet.Dc(),0,0,SRCCOPY);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(image.Dc()),PRF_CLIENT|PRF_CHILDREN|PRF_NONCLIENT|PRF_ERASEBKGND);
    float dpi=GetDpiForWindow(window)/96.f;
    auto stored=reinterpret_cast<INT_PTR>(GetPropW(window,L"GoodView.Radius"));
    float radius=stored?static_cast<float>(stored-1)*dpi:0.f;sheet.RoundAlpha(radius);image.RoundAlpha(radius);
    auto reveal=state.scene.Group({width,height});if(reveal==MotionScene::None)return false;
    if(state.scene.Add(sheet,{},reveal)==MotionScene::None)return false;sheet.Reset();
    // Same structure as the chosen draft reference: a 3%-height top strip
    // opens across, then the whole group unfolds; its children fade in during
    // that unfolding. No opaque sheet can cover the content siblings.
    state.scene.Animate(reveal,Property::Right,1,static_cast<float>(width),0,.1984);
    state.scene.Animate(reveal,Property::Bottom,std::max(2.f,height*.03f),static_cast<float>(height),.1984,.4216);
    state.scene.Animate(reveal,Property::Opacity,.2f,1,0,.1984);
    // Prefer row gaps for slice boundaries, so labels and buttons do not fade
    // in separate halves. Tall previews may span several rows.
    std::vector<RECT> fields;
    struct FieldData{HWND owner;std::vector<RECT>* fields;int maximumHeight;} fieldData{window,&fields,static_cast<int>(96*dpi)};
    EnumChildWindows(window,[](HWND child,LPARAM value){auto& data=*reinterpret_cast<FieldData*>(value);if(GetParent(child)!=data.owner||!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))return TRUE;
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,data.owner,reinterpret_cast<POINT*>(&r),2);if(r.bottom-r.top<=data.maximumHeight){r.top-=2;r.bottom+=2;data.fields->push_back(r);}return TRUE;},reinterpret_cast<LPARAM>(&fieldData));
    std::sort(fields.begin(),fields.end(),[](RECT a,RECT b){return a.top<b.top;});
    std::vector<int> gaps;int bottom=0;for(auto r:fields){if(r.top>bottom+4)gaps.push_back((bottom+r.top)/2);bottom=std::max(bottom,static_cast<int>(r.bottom));}if(bottom<height-4)gaps.push_back((bottom+height)/2);
    std::vector<int> cuts{0};for(int i=1;i<5;++i){int desired=height*i/5,cut=desired,best=height/8+1;for(int gap:gaps)if(gap>cuts.back()+20*dpi&&gap<height-20*dpi&&std::abs(gap-desired)<best){cut=gap;best=std::abs(gap-desired);}
        // A distant gap must not collapse all the rows into one reveal.
        if(std::abs(cut-desired)>height/8)cut=desired;if(cut>cuts.back()+8&&cut<height-8)cuts.push_back(cut);
    }cuts.push_back(height);
    MotionScene::Node source=MotionScene::None;
    for(size_t i=0;i+1<cuts.size();++i){auto node=source==MotionScene::None?state.scene.Add(image,{},reveal):state.scene.Clone(source,reveal);if(node==MotionScene::None)return false;if(source==MotionScene::None)source=node;
        state.scene.Set(node,Property::Top,static_cast<float>(cuts[i]));state.scene.Set(node,Property::Bottom,static_cast<float>(cuts[i+1]));
        state.scene.Animate(node,Property::Opacity,0,1,.22+i*.055,.4);
    }
    return true;
}
LRESULT CALLBACK RevealProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto* state=reinterpret_cast<Reveal*>(data);
    if(message==WM_NCDESTROY){if(state->initializing){state->destroyed=state->cancelled=true;KillTimer(window,WindowMotionTimer);RemoveWindowSubclass(window,RevealProc,SubclassId);}else Restore(window,state,false);return DefSubclassProc(window,message,w,l);}
    if(message==StartMotionMessage){
        if(w!=state->sequence||state->initializing||state->proxy)return 0;
        // The caller has now selected and focused its actual inputs. Standard
        // EDIT controls must be visible before WM_PRINT can render their text.
        state->initializing=true;bool ready=false;
        try{
            RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
            ready=Prepare(window,*state);
            if(ready&&!state->cancelled){ShowWindow(state->proxy,SW_SHOWNOACTIVATE);ready=state->scene.Commit()&&SetTimer(window,WindowMotionTimer,WindowMotionDuration+20,nullptr);}
        }catch(...){ready=false;}
        state->initializing=false;
        if(state->destroyed)delete state;else if(!ready||state->cancelled)Restore(window,state,true);
        return 0;
    }
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
    if(interrupt){if(state->initializing)state->cancelled=true;else Restore(window,state,true);}
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
    auto state=std::make_unique<Reveal>();GetWindowRect(window,&state->bounds);static UINT_PTR sequence=0;state->sequence=++sequence;
    if(!SetWindowSubclass(window,RevealProc,SubclassId,reinterpret_cast<DWORD_PTR>(state.get()))){ShowWindow(window,command);return;}
    auto* live=state.release();SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)|WS_EX_LAYERED);live->layered=true;
    // Alpha zero makes Windows pass mouse hits through the live HWND. One is
    // visually negligible but keeps its controls interactive under the proxy.
    if(!SetLayeredWindowAttributes(window,0,1,LWA_ALPHA)){Restore(window,live,false);ShowWindow(window,command);return;}
    ShowWindow(window,command);
    if(!WindowMotionActive(window))return;
    if(!PostMessageW(window,StartMotionMessage,live->sequence,0))FinishWindowMotion(window);
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
