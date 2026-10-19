#include "branch_menu.h"
#include "window_motion.h"
#include "live_probe.h"
#include "design.h"
#include "orbit_layout.h"
#include <uxtheme.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <gdiplus.h>
#include <propkey.h>
#include <propsys.h>

namespace gv {
std::vector<Segment> GlyphLines(Glyph g){
    using G=Glyph;
    switch(g){
    case G::Apps:return {{3,3,9,3},{9,3,9,9},{9,9,3,9},{3,9,3,3},{15,3,21,3},{21,3,21,9},{21,9,15,9},{15,9,15,3},{3,15,9,15},{9,15,9,21},{9,21,3,21},{3,21,3,15},{15,15,21,15},{21,15,21,21},{21,21,15,21},{15,21,15,15}};
    case G::Desktop:return {{3,4,21,4},{21,4,21,17},{21,17,3,17},{3,17,3,4},{12,17,12,21},{7,21,17,21}};
    case G::Windows:case G::Layout:return {{3,4,21,4},{21,4,21,20},{21,20,3,20},{3,20,3,4},{10,4,10,20},{10,11,21,11}};
    case G::Add:return {{5,12,19,12},{12,5,12,19}};
    case G::Minus:return {{5,12,19,12}};
    case G::Close:return {{6,6,18,18},{18,6,6,18}};
    case G::Back:return {{14,5,7,12},{7,12,14,19}};
    case G::Forward:case G::Chevron:return {{9,5,16,12},{16,12,9,19}};
    case G::Expand:return {{5,15,12,8},{12,8,19,15}};
    case G::Collapse:return {{5,8,12,15},{12,15,19,8}};
    case G::Exit:return {{10,4,4,4},{4,4,4,20},{4,20,10,20},{10,12,21,12},{16,7,21,12},{21,12,16,17}};
    case G::Reload:return {{18,6,21,6},{21,6,21,2},{19,7,16,4},{16,4,8,4},{8,4,4,8},{4,8,4,16},{4,16,8,20},{8,20,16,20},{16,20,20,16}};
    case G::Snapshot:return {{3,7,8,7},{8,7,10,4},{10,4,14,4},{14,4,16,7},{16,7,21,7},{21,7,21,20},{21,20,3,20},{3,20,3,7}};
    case G::Note:return {{5,3,19,3},{19,3,19,17},{19,17,15,21},{15,21,5,21},{5,21,5,3},{8,8,16,8},{8,12,16,12},{8,16,12,16}};
    case G::Globe:return {{3,12,21,12},{12,3,8,8},{8,8,8,16},{8,16,12,21},{12,3,16,8},{16,8,16,16},{16,16,12,21}};
    case G::Clock:return {{12,6,12,12},{12,12,16,14}};
    case G::Calendar:return {{4,5,20,5},{20,5,20,21},{20,21,4,21},{4,21,4,5},{4,10,20,10},{8,2,8,7},{16,2,16,7},{8,14,10,14},{14,14,16,14},{8,18,10,18}};
    case G::Folder:return {{3,6,10,6},{10,6,12,9},{12,9,21,9},{21,9,21,20},{21,20,3,20},{3,20,3,6}};
    case G::Check:return {{5,12,10,17},{10,17,20,7}};
    case G::Resize:return {{8,20,20,8},{14,20,20,14}};
    case G::Pin:return {{8,3,16,3},{16,3,15,10},{15,10,19,14},{19,14,5,14},{5,14,9,10},{9,10,8,3},{12,14,12,22}};
    case G::Tray:return {{4,8,4,20},{4,20,20,20},{20,20,20,8},{8,9,12,5},{12,5,16,9},{12,5,12,15}};
    case G::Search:return {{16,16,22,22}};
    case G::Horizontal:return {{3,7,21,7},{21,7,21,17},{21,17,3,17},{3,17,3,7},{8,10,8,14},{12,10,12,14},{16,10,16,14}};
    case G::Vertical:return {{7,3,17,3},{17,3,17,21},{17,21,7,21},{7,21,7,3},{10,8,14,8},{10,12,14,12},{10,16,14,16}};
    case G::Sun:return {{12,1,12,4},{12,20,12,23},{1,12,4,12},{20,12,23,12},{4,4,6,6},{18,18,20,20},{4,20,6,18},{18,6,20,4}};
    case G::Moon:return {{15,3,9,4},{9,4,5,9},{5,9,5,15},{5,15,9,20},{9,20,15,21},{15,21,20,17},{20,17,14,17},{14,17,10,13},{10,13,10,8},{10,8,15,3}};
    case G::Cpu:return {{6,6,18,6},{18,6,18,18},{18,18,6,18},{6,18,6,6},{9,1,9,6},{15,1,15,6},{9,18,9,23},{15,18,15,23},{1,9,6,9},{1,15,6,15},{18,9,23,9},{18,15,23,15}};
    case G::Memory:return {{3,7,21,7},{21,7,21,17},{21,17,3,17},{3,17,3,7},{7,10,7,14},{12,10,12,14},{17,10,17,14},{7,17,7,21},{12,17,12,21},{17,17,17,21}};
    case G::Disk:return {{5,3,19,3},{19,3,22,19},{22,19,2,19},{2,19,5,3},{3,14,21,14},{16,17,18,17}};
    case G::Network:return {{8,3,8,19},{4,7,8,3},{8,3,12,7},{16,5,16,21},{12,17,16,21},{16,21,20,17}};
    case G::Settings:return {{12,2,12,5},{12,19,12,22},{2,12,5,12},{19,12,22,12},{5,5,7,7},{17,17,19,19},{5,19,7,17},{17,7,19,5}};
    case G::Link:return {{8,16,16,8},{4,13,2,15},{2,15,2,19},{2,19,5,22},{5,22,9,22},{9,22,12,19},{12,5,15,2},{15,2,19,2},{19,2,22,5},{22,5,22,9},{22,9,19,12}};
    case G::Briefcase:return {{3,7,21,7},{21,7,21,20},{21,20,3,20},{3,20,3,7},{8,7,8,3},{8,3,16,3},{16,3,16,7},{3,12,21,12},{10,12,10,15},{10,15,14,15},{14,15,14,12}};
    case G::Game:return {{7,7,17,7},{17,7,20,10},{20,10,22,19},{22,19,19,21},{19,21,15,17},{15,17,9,17},{9,17,5,21},{5,21,2,19},{2,19,4,10},{4,10,7,7},{6,12,12,12},{9,9,9,15}};
    case G::Book:return {{3,4,9,4},{9,4,12,6},{12,6,15,4},{15,4,21,4},{21,4,21,20},{21,20,15,20},{15,20,12,22},{12,22,9,20},{9,20,3,20},{3,20,3,4},{12,6,12,22}};
    case G::Code:return {{7,6,2,12},{2,12,7,18},{17,6,22,12},{22,12,17,18},{14,3,10,21}};
    case G::Music:return {{9,18,9,5},{9,5,20,3},{20,3,20,16},{9,9,20,7}};
    case G::Headphones:return {{3,13,3,9},{3,9,6,4},{6,4,12,2},{12,2,18,4},{18,4,21,9},{21,9,21,13},{3,13,7,13},{7,13,7,21},{7,21,3,21},{3,21,3,13},{17,13,21,13},{21,13,21,21},{21,21,17,21},{17,21,17,13}};
    case G::Photo:return {{3,3,21,3},{21,3,21,21},{21,21,3,21},{3,21,3,3},{3,17,9,11},{9,11,16,18},{14,16,18,12},{18,12,21,15}};
    case G::Film:return {{3,3,21,3},{21,3,21,21},{21,21,3,21},{3,21,3,3},{7,3,7,21},{17,3,17,21},{3,9,7,9},{3,15,7,15},{17,9,21,9},{17,15,21,15}};
    case G::Mountain:return {{2,20,9,6},{9,6,14,15},{12,12,16,5},{16,5,23,20},{23,20,2,20},{6,12,9,14},{9,14,12,12}};
    case G::Coffee:return {{3,6,17,6},{17,6,17,15},{17,15,14,19},{14,19,6,19},{6,19,3,15},{3,15,3,6},{17,7,21,7},{21,7,22,10},{22,10,21,13},{21,13,17,13},{2,22,20,22},{7,1,7,3},{12,1,12,3}};
    case G::Star:return {{12,2,15,8},{15,8,22,9},{22,9,17,14},{17,14,18,21},{18,21,12,18},{12,18,6,21},{6,21,7,14},{7,14,2,9},{2,9,9,8},{9,8,12,2}};
    case G::Heart:return {{12,21,3,12},{3,12,2,7},{2,7,5,3},{5,3,9,3},{9,3,12,6},{12,6,15,3},{15,3,19,3},{19,3,22,7},{22,7,21,12},{21,12,12,21}};
    case G::Leaf:return {{3,21,14,10},{5,17,4,11},{4,11,8,5},{8,5,21,2},{21,2,20,13},{20,13,15,19},{15,19,9,20},{9,20,5,17},{10,14,10,9},{10,14,15,14}};
    case G::Compass:return {{15,8,13,14},{13,14,7,17},{7,17,9,11},{9,11,15,8}};
    case G::Rocket:return {{9,15,9,9},{9,9,14,4},{14,4,21,2},{21,2,20,9},{20,9,15,15},{15,15,9,15},{9,8,4,9},{4,9,2,15},{2,15,9,15},{15,15,15,22},{15,22,21,19},{21,19,20,12},{6,18,3,21}};
    case G::Layers:return {{12,2,22,7},{22,7,12,12},{12,12,2,7},{2,7,12,2},{2,12,12,17},{12,17,22,12},{2,17,12,22},{12,22,22,17}};
    default:return {};
    }
}
std::vector<Circle> GlyphCircles(Glyph g){
    switch(g){case Glyph::Game:return {{16,10,1},{19,13,1}};case Glyph::Music:return {{6,18,3},{17,16,3}};case Glyph::Photo:return {{16,7,2}};case Glyph::Rocket:return {{16,7,1.5f}};case Glyph::Compass:return {{12,12,10}};case Glyph::Sun:return {{12,12,5}};case Glyph::More:return {{5,12,1},{12,12,1},{19,12,1}};case Glyph::Clock:case Glyph::Globe:case Glyph::Activity:return {{12,12,9}};case Glyph::Snapshot:return {{12,13,4}};case Glyph::Search:return {{10,10,7}};case Glyph::Settings:return {{12,12,7},{12,12,2}};default:return {};}
}
namespace {void EnsureGraphics(){struct GraphicsRuntime {ULONG_PTR token{};GraphicsRuntime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~GraphicsRuntime(){if(token)Gdiplus::GdiplusShutdown(token);}};static GraphicsRuntime runtime;}}
void DrawGlyph(HDC dc,Glyph glyph,RECT r,COLORREF color){
    EnsureGraphics();
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    float sx=(r.right-r.left)/24.f,sy=(r.bottom-r.top)/24.f;Gdiplus::Color ink(255,GetRValue(color),GetGValue(color),GetBValue(color));Gdiplus::Pen pen(ink,std::max(1.15f,sx*1.65f));pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);Gdiplus::SolidBrush brush(ink);
    if(auto commands=GlyphPath(glyph);!commands.empty()){
        static std::map<Glyph,std::unique_ptr<Gdiplus::GraphicsPath>> paths;
        auto& path=paths[glyph];if(!path){path=std::make_unique<Gdiplus::GraphicsPath>();float x=0,y=0;
            for(auto p:commands){if(p.verb=='M'){path->StartFigure();x=p.x1;y=p.y1;}
                else if(p.verb=='L'){path->AddLine(x,y,p.x1,p.y1);x=p.x1;y=p.y1;}
                else if(p.verb=='C'){path->AddBezier(x,y,p.x1,p.y1,p.x2,p.y2,p.x3,p.y3);x=p.x3;y=p.y3;}
                else if(p.verb=='Z')path->CloseFigure();else path->StartFigure();}
        }
        graphics.TranslateTransform(static_cast<float>(r.left),static_cast<float>(r.top));graphics.ScaleTransform(sx,sy);pen.SetWidth(1.65f);graphics.DrawPath(&pen,path.get());return;
    }
    for(auto p:GlyphLines(glyph))graphics.DrawLine(&pen,r.left+p.x1*sx,r.top+p.y1*sy,r.left+p.x2*sx,r.top+p.y2*sy);
    for(auto p:GlyphCircles(glyph)){Gdiplus::RectF rect(r.left+(p.x-p.r)*sx,r.top+(p.y-p.r)*sy,p.r*2*sx,p.r*2*sy);if(p.r<=1.1f)graphics.FillEllipse(&brush,rect);else graphics.DrawEllipse(&pen,rect);}
}
void DrawAccentChoice(const DRAWITEMSTRUCT& item,HFONT font,COLORREF color,bool active){
    EnsureGraphics();auto theme=UiTheme();UINT dpi=GetDpiForWindow(item.hwndItem);float scale=dpi/96.f;HBRUSH background=CreateSolidBrush(theme.panel);FillRect(item.hDC,&item.rcItem,background);DeleteObject(background);
    Gdiplus::Graphics graphics(item.hDC);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);auto ink=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
    auto outline=[&](float inset,Gdiplus::Color fill,Gdiplus::Color border,float stroke){float x=item.rcItem.left+inset,y=item.rcItem.top+inset,w=item.rcItem.right-item.rcItem.left-2*inset,h=item.rcItem.bottom-item.rcItem.top-2*inset,r=h/2;Gdiplus::GraphicsPath path;path.AddArc(x,y,r*2,r*2,90,180);path.AddArc(x+w-r*2,y,r*2,r*2,270,180);path.CloseFigure();Gdiplus::SolidBrush brush(fill);graphics.FillPath(&brush,&path);Gdiplus::Pen pen(border,stroke);graphics.DrawPath(&pen,&path);};
    bool hot=GetPropW(item.hwndItem,L"GoodView.Hover")||(item.itemState&ODS_SELECTED);outline(1*scale,ink(theme.panel),ink(theme.border),1*scale);outline(5*scale,ink(active||hot?theme.field:theme.panel),ink(color),(active?2.3f:1.15f)*scale);
    auto old=SelectObject(item.hDC,font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,theme.text);wchar_t text[100]{};GetWindowTextW(item.hwndItem,text,100);RECT rect=item.rcItem;InflateRect(&rect,-static_cast<int>(8*scale),0);DrawTextW(item.hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(item.hDC,old);
}
void RoundFill(HDC dc,RECT r,COLORREF fill,int radius,COLORREF border){
    if(r.right<=r.left||r.bottom<=r.top)return;EnsureGraphics();
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    float x=r.left+.5f,y=r.top+.5f,w=r.right-r.left-1.f,h=r.bottom-r.top-1.f;
    float curve=std::clamp(radius-.5f,0.f,std::min(w,h)/2),diameter=curve*2;
    Gdiplus::GraphicsPath path;
    if(curve>0){path.AddArc(x,y,diameter,diameter,180,90);path.AddArc(x+w-diameter,y,diameter,diameter,270,90);path.AddArc(x+w-diameter,y+h-diameter,diameter,diameter,0,90);path.AddArc(x,y+h-diameter,diameter,diameter,90,90);path.CloseFigure();}
    else path.AddRectangle(Gdiplus::RectF(x,y,w,h));
    auto ink=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
    Gdiplus::SolidBrush brush(ink(fill));graphics.FillPath(&brush,&path);
    Gdiplus::Pen pen(ink(border==CLR_INVALID?fill:border),1);graphics.DrawPath(&pen,&path);
}
void RoundWindow(HWND window,int radius){
    SetPropW(window,L"GoodView.Radius",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(radius)+1));
    RECT r{};if(!GetClientRect(window,&r))return;UINT dpi=GetDpiForWindow(window);int round=MulDiv(radius*2,dpi,96);
    HRGN next=radius>0?CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,round,round):CreateRectRgn(0,0,r.right,r.bottom);if(!next)return;
    HRGN current=CreateRectRgn(0,0,0,0);bool same=current&&GetWindowRgn(window,current)!=ERROR&&EqualRgn(current,next);if(current)DeleteObject(current);
    // SetWindowRgn repaints the non-client frame even if its shape is unchanged.
    if(same){DeleteObject(next);return;}
    FinishWindowMotion(window);
    if(!SetWindowRgn(window,next,TRUE))DeleteObject(next);
}
void DrawRingFrame(HDC dc,RECT rect,COLORREF fill,COLORREF border,int count){
    EnsureGraphics();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float scale=std::min(rect.right-rect.left,rect.bottom-rect.top)/280.f,cx=(rect.left+rect.right)/2.f,cy=(rect.top+rect.bottom)/2.f;
    auto color=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
    Gdiplus::Pen band(color(fill),OrbitBandWidth*scale),line(color(border),1);
    int segments=std::clamp(count,1,OrbitCapacity);float radius=OrbitRadius*scale;
    for(int i=0;i<segments;++i){auto arc=OrbitSegment(i,segments);g.DrawArc(&band,cx-radius,cy-radius,2*radius,2*radius,arc.start*360/OrbitTau,arc.sweep*360/OrbitTau);}
    for(int i=0;i<segments;++i){float a=-OrbitTau/4-i*OrbitTau/segments,x=std::cos(a),y=std::sin(a);g.DrawLine(&line,cx+x*100*scale,cy+y*100*scale,cx+x*120*scale,cy+y*120*scale);}
}
HFONT UiFont(int size,UINT dpi,int weight,FontRole role){return CreateFontW(-MulDiv(size,dpi,96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,GdiFontFamily(role,weight));}
RECT CenteredWindow(HWND owner,int width,int height){POINT cursor;GetCursorPos(&cursor);MONITORINFO info{sizeof(info)};HMONITOR monitor=owner?MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY):MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY);GetMonitorInfoW(monitor,&info);auto a=info.rcWork;width=std::min(width,static_cast<int>(a.right-a.left-32));height=std::min(height,static_cast<int>(a.bottom-a.top-32));return {a.left+(a.right-a.left-width)/2,a.top+(a.bottom-a.top-height)/2,a.left+(a.right-a.left+width)/2,a.top+(a.bottom-a.top+height)/2};}
void DrawUiButton(const DRAWITEMSTRUCT& item,HFONT font,bool active,COLORREF backgroundColor){
    auto theme=UiTheme();if(backgroundColor==CLR_INVALID)backgroundColor=theme.panel;
    HBRUSH background=CreateSolidBrush(backgroundColor);FillRect(item.hDC,&item.rcItem,background);DeleteObject(background);
    bool down=(item.itemState&ODS_SELECTED)!=0,focus=(item.itemState&ODS_FOCUS)!=0,disabled=(item.itemState&ODS_DISABLED)!=0,hot=GetPropW(item.hwndItem,L"GoodView.Hover")!=nullptr;UINT dpi=GetDpiForWindow(item.hwndItem);
    bool keyboardFocus=focus&&!(SendMessageW(item.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS);
    COLORREF fill=active?theme.field:(hot||down)?theme.field:backgroundColor,ink=disabled?theme.muted:active?theme.accent:theme.text;
    RoundFill(item.hDC,item.rcItem,fill,MulDiv(9,dpi,96),keyboardFocus?theme.accent:active?theme.accent:theme.border);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);auto old=SelectObject(item.hDC,font);wchar_t text[300]{};GetWindowTextW(item.hwndItem,text,300);RECT r=item.rcItem;InflateRect(&r,-MulDiv(8,dpi,96),0);
    if(auto glyph=reinterpret_cast<INT_PTR>(GetPropW(item.hwndItem,L"GoodView.Glyph"))){int iconSize=MulDiv(22,dpi,96),gap=MulDiv(10,dpi,96);SIZE measure{};GetTextExtentPoint32W(item.hDC,text,static_cast<int>(wcslen(text)),&measure);int textWidth=text[0]?std::min(static_cast<int>(measure.cx),std::max(0,static_cast<int>(r.right-r.left)-iconSize-gap)):0;int width=iconSize+(textWidth?textWidth+gap:0),left=(r.left+r.right-width)/2;RECT icon{left,(r.top+r.bottom-iconSize)/2,left+iconSize,(r.top+r.bottom+iconSize)/2};DrawGlyph(item.hDC,static_cast<Glyph>(glyph-1),icon,ink);r.left=left+iconSize+gap;r.right=r.left+textWidth;}
    // Keep command captions readable and complete. Prefer two centered lines to
    // an ellipsis, then reduce only as far as a readable 14-DIP fallback.
    LOGFONTW originalFont{};GetObjectW(font,sizeof(originalFont),&originalFont);HFONT fitted=nullptr;bool single=true,fits=false;RECT measured{};
    int minimum=std::min(static_cast<int>(std::abs(originalFont.lfHeight)),MulDiv(14,dpi,96));
    for(int height=std::abs(originalFont.lfHeight);height>=minimum;--height){
        if(height!=std::abs(originalFont.lfHeight)){auto value=originalFont;value.lfHeight=-height;HFONT next=CreateFontIndirectW(&value);SelectObject(item.hDC,next);if(fitted)DeleteObject(fitted);fitted=next;}
        SIZE extent{};GetTextExtentPoint32W(item.hDC,text,static_cast<int>(wcslen(text)),&extent);single=extent.cx<=r.right-r.left;
        measured={0,0,r.right-r.left,0};DrawTextW(item.hDC,text,-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        fits=single?extent.cy<=r.bottom-r.top:measured.bottom<=r.bottom-r.top-2&&measured.right<=r.right-r.left;
        if(fits)break;
    }
    SetPropW(item.hwndItem,L"GoodView.CaptionFits",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(fits?1:2)));
    if(single)DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    else{r.top+=(r.bottom-r.top-measured.bottom)/2;DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);}
    SelectObject(item.hDC,old);if(fitted)DeleteObject(fitted);
}
namespace {
LRESULT CALLBACK UiButtonProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR background){
    if(message==WM_MOUSEMOVE&&!GetPropW(window,L"GoodView.Hover")){for(HWND sibling=GetWindow(window,GW_HWNDFIRST);sibling;sibling=GetWindow(sibling,GW_HWNDNEXT))if(sibling!=window&&RemovePropW(sibling,L"GoodView.Hover"))InvalidateRect(sibling,nullptr,FALSE);SetPropW(window,L"GoodView.Hover",reinterpret_cast<HANDLE>(TRUE));TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);InvalidateRect(window,nullptr,FALSE);}
    if(message==WM_MOUSELEAVE){RemovePropW(window,L"GoodView.Hover");InvalidateRect(window,nullptr,FALSE);}
    if(message==WM_ERASEBKGND){RECT rect;GetClientRect(window,&rect);HBRUSH brush=CreateSolidBrush(static_cast<COLORREF>(background));FillRect(reinterpret_cast<HDC>(w),&rect,brush);DeleteObject(brush);return 1;}
    if(message==WM_NCDESTROY){RemovePropW(window,L"GoodView.Hover");RemovePropW(window,L"GoodView.Glyph");RemovePropW(window,L"GoodView.CaptionFits");RemoveWindowSubclass(window,UiButtonProc,1);}return DefSubclassProc(window,message,w,l);
}
}
void StyleUiButton(HWND window,COLORREF background){if(background==CLR_INVALID)background=UiTheme().panel;SetWindowTheme(window,L"",L"");SetWindowSubclass(window,UiButtonProc,1,background);}
void ButtonGlyph(HWND window,Glyph glyph){SetPropW(window,L"GoodView.Glyph",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(glyph)+1));}
bool SaveBitmapPng(HBITMAP bitmap,const fs::path& path){
    std::error_code error;fs::create_directories(path.parent_path(),error);if(error)return false;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> source;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&source))||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr)))return false;
    UINT width=0,height=0;source->GetSize(&width,&height);auto format=GUID_WICPixelFormat32bppBGRA;return SUCCEEDED(frame->SetSize(width,height))&&SUCCEEDED(frame->SetPixelFormat(&format))&&SUCCEEDED(frame->WriteSource(source.Get(),nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
}
std::shared_ptr<MonoIcon> IconCache::Get(const std::wstring& target){
    if(auto it=icons_.find(target);it!=icons_.end())return it->second;
    auto icon=std::make_shared<MonoIcon>();ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICFormatConverter> converter;HICON handle=nullptr;SHFILEINFOW info{};
    // Ask the shell for an actual larger image before falling back to the 32px icon.
    ComPtr<IShellItem> item;if(SUCCEEDED(SHCreateItemFromParsingName(target.c_str(),nullptr,IID_PPV_ARGS(&item)))){ComPtr<IShellItemImageFactory> image;if(SUCCEEDED(item.As(&image))){HBITMAP h{};if(SUCCEEDED(image->GetImage({64,64},static_cast<SIIGBF>(SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK),&h))){if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))factory->CreateBitmapFromHBITMAP(h,nullptr,WICBitmapUsePremultipliedAlpha,&bitmap);DeleteObject(h);}}}
    if(!bitmap&&SHGetFileInfoW(target.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))handle=info.hIcon;
    if(!factory)CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(handle&&factory)factory->CreateBitmapFromHICON(handle,&bitmap);if(handle)DestroyIcon(handle);
    if(bitmap&&factory&&SUCCEEDED(factory->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){UINT width=0,height=0;converter->GetSize(&width,&height);if(width&&height&&width==height&&width<=256){icon->size=static_cast<int>(width);icon->pixels.resize(width*height);converter->CopyPixels(nullptr,width*4,static_cast<UINT>(icon->pixels.size()*4),reinterpret_cast<BYTE*>(icon->pixels.data()));for(auto& pixel:icon->pixels){UINT alpha=pixel>>24,b=pixel&255,g=(pixel>>8)&255,r=(pixel>>16)&255;UINT gray=(r*54+g*183+b*19)/256;gray=gray*alpha/255;pixel=(alpha<<24)|(gray<<16)|(gray<<8)|gray;}}}
    if(!icon->pixels.empty()){BITMAPINFO bmi{};bmi.bmiHeader={sizeof(BITMAPINFOHEADER),icon->size,-icon->size,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;icon->bitmap=CreateDIBSection(nullptr,&bmi,DIB_RGB_COLORS,&pixels,nullptr,0);if(pixels)memcpy(pixels,icon->pixels.data(),icon->pixels.size()*4);}
    icons_[target]=icon;return icon;
}
void DrawMonoIcon(HDC dc,const MonoIcon& icon,RECT r){
    if(!icon.bitmap){DrawGlyph(dc,Glyph::Apps,r,UiTheme().text);return;}
    // AlphaBlend uses a low quality stretch. Resample once, then blit at native size.
    int size=std::max(1,std::min({icon.size,static_cast<int>(r.right-r.left),static_cast<int>(r.bottom-r.top)}));HBITMAP image=icon.bitmap;
    int cacheKey=size;if(size!=icon.size){auto found=icon.scaled.find(cacheKey);if(found!=icon.scaled.end())image=found->second;else{
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> source;ComPtr<IWICBitmapScaler> scaler;BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        bool ready=bitmap&&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateBitmapFromMemory(icon.size,icon.size,GUID_WICPixelFormat32bppPBGRA,icon.size*4,static_cast<UINT>(icon.pixels.size()*4),reinterpret_cast<BYTE*>(const_cast<uint32_t*>(icon.pixels.data())),&source))&&SUCCEEDED(factory->CreateBitmapScaler(&scaler))&&SUCCEEDED(scaler->Initialize(source.Get(),size,size,WICBitmapInterpolationModeFant))&&SUCCEEDED(scaler->CopyPixels(nullptr,size*4,size*size*4,static_cast<BYTE*>(pixels)));
        if(ready){if(icon.scaled.size()>=16){DeleteObject(icon.scaled.begin()->second);icon.scaled.erase(icon.scaled.begin());}icon.scaled[cacheKey]=bitmap;image=bitmap;}else{if(bitmap)DeleteObject(bitmap);}
    }}
    int x=r.left+(r.right-r.left-size)/2,y=r.top+(r.bottom-r.top-size)/2;HDC src=CreateCompatibleDC(dc);auto old=SelectObject(src,image);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};int sourceSize=image==icon.bitmap?icon.size:size;AlphaBlend(dc,x,y,size,size,src,0,0,sourceSize,sourceSize,blend);SelectObject(src,old);DeleteDC(src);
}
std::shared_ptr<MonoIcon> IconCache::GetWindow(HWND window){LiveProbeScope profile(ProbeArea::IconLookup);
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);auto key=L"window:"+std::to_wstring(pid)+L":"+std::to_wstring(reinterpret_cast<ULONG_PTR>(window));auto previous=icons_.find(key);auto now=GetTickCount64();if(previous!=icons_.end()&&now<windowExpiry_[key])return previous->second;
    LiveProbeCount(ProbeCount::IconMisses);HICON shared=nullptr;for(WPARAM kind:{ICON_BIG,ICON_SMALL2,ICON_SMALL}){DWORD_PTR result=0;SendMessageTimeoutW(window,WM_GETICON,kind,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,35,&result);if(result){shared=reinterpret_cast<HICON>(result);break;}}
    if(!shared)shared=reinterpret_cast<HICON>(GetClassLongPtrW(window,GCLP_HICON));if(!shared)shared=reinterpret_cast<HICON>(GetClassLongPtrW(window,GCLP_HICONSM));auto icon=MonoFromIcon(shared);
    if(icon->pixels.empty()){
        ComPtr<IPropertyStore> properties;PROPVARIANT value{};
        if(SUCCEEDED(SHGetPropertyStoreForWindow(window,IID_PPV_ARGS(&properties)))&&SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)icon=Get(std::wstring(L"shell:AppsFolder\\")+value.pwszVal);PropVariantClear(&value);
    }
    if(icon->pixels.empty()){
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);wchar_t path[32768]{};DWORD length=32768;
        if(process){if(QueryFullProcessImageNameW(process,0,path,&length))icon=Get(path);CloseHandle(process);}
    }
    if(icon->pixels.empty())icon=MonoFromIcon(LoadIconW(nullptr,IDI_APPLICATION));
    if(previous!=icons_.end()&&previous->second->pixels==icon->pixels)icon=previous->second;
    icons_[key]=icon;windowExpiry_[key]=now+5000;return icon;
}
std::shared_ptr<MonoIcon> MonoFromIcon(HICON source){
    HICON copy=source?CopyIcon(source):nullptr;
    auto icon=std::make_shared<MonoIcon>();ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmap> bitmap;ComPtr<IWICFormatConverter> converter;
    if(copy&&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateBitmapFromHICON(copy,&bitmap))&&SUCCEEDED(factory->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(bitmap.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))){UINT width=0,height=0;converter->GetSize(&width,&height);if(width&&width==height&&width<=256){icon->size=static_cast<int>(width);icon->pixels.resize(width*height);converter->CopyPixels(nullptr,width*4,static_cast<UINT>(icon->pixels.size()*4),reinterpret_cast<BYTE*>(icon->pixels.data()));for(auto& pixel:icon->pixels){UINT a=pixel>>24,b=pixel&255,g=(pixel>>8)&255,r=(pixel>>16)&255,gray=((r*54+g*183+b*19)/256)*a/255;pixel=(a<<24)|(gray<<16)|(gray<<8)|gray;}}}
    if(copy&&(icon->pixels.empty()||std::none_of(icon->pixels.begin(),icon->pixels.end(),[](uint32_t pixel){return (pixel>>24)!=0;}))){
        // Legacy masked HICONs can have an entirely empty alpha channel. Recover
        // coverage from rendering against two backgrounds instead of filling it.
        int size=icon->size;HDC black=CreateCompatibleDC(nullptr),white=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB,0,0,0,0,0};void *bp=nullptr,*wp=nullptr;
        HBITMAP bb=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bp,nullptr,0),wb=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&wp,nullptr,0);
        if(bb&&wb&&bp&&wp){auto bo=SelectObject(black,bb),wo=SelectObject(white,wb);memset(bp,0,size*size*4);memset(wp,255,size*size*4);DrawIconEx(black,0,0,copy,size,size,0,nullptr,DI_NORMAL);DrawIconEx(white,0,0,copy,size,size,0,nullptr,DI_NORMAL);GdiFlush();icon->pixels.resize(size*size);
            for(int i=0;i<size*size;++i){auto b=static_cast<uint32_t*>(bp)[i],w=static_cast<uint32_t*>(wp)[i];int difference=std::max({std::abs(static_cast<int>(b&255)-static_cast<int>(w&255)),std::abs(static_cast<int>((b>>8)&255)-static_cast<int>((w>>8)&255)),std::abs(static_cast<int>((b>>16)&255)-static_cast<int>((w>>16)&255))});UINT alpha=255-difference,gray=((b&255)*19+((b>>8)&255)*183+((b>>16)&255)*54)/256;gray=std::min(gray,alpha);icon->pixels[i]=(alpha<<24)|(gray<<16)|(gray<<8)|gray;}
            SelectObject(black,bo);SelectObject(white,wo);
        }
        if(bb)DeleteObject(bb);if(wb)DeleteObject(wb);DeleteDC(black);DeleteDC(white);
    }
    if(copy)DestroyIcon(copy);
    if(std::none_of(icon->pixels.begin(),icon->pixels.end(),[](uint32_t pixel){return (pixel>>24)!=0;}))icon->pixels.clear();
    if(!icon->pixels.empty()){BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),icon->size,-icon->size,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;icon->bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(pixels)memcpy(pixels,icon->pixels.data(),icon->pixels.size()*4);}
    return icon;
}
void MenuIcon(HMENU menu,UINT id,const MonoIcon* icon){MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_DATA;item.dwItemData=reinterpret_cast<ULONG_PTR>(icon);SetMenuItemInfoW(menu,id,FALSE,&item);}

namespace {
struct MenuRow {std::wstring text;UINT id{},state{},type{};HMENU child{};const MonoIcon* icon{};RECT rect{};int height=0,shortcutWidth=0;};
struct MenuState {
    HWND window{},owner{};HFONT font{};UINT dpi=96,result=0;bool done=false;int hover=-1,selected=-1,scroll=0;std::vector<HMENU> stack;std::vector<MenuRow> rows;POINT anchor{};int width=320,height=0;
    int U(int v)const{return MulDiv(v,dpi,96);}
    void Load(){rows.clear();hover=selected=-1;scroll=0;HMENU menu=stack.back();for(int i=0;i<GetMenuItemCount(menu);++i){wchar_t text[1024]{};MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_STRING|MIIM_ID|MIIM_STATE|MIIM_FTYPE|MIIM_SUBMENU|MIIM_DATA;info.dwTypeData=text;info.cch=1024;GetMenuItemInfoW(menu,i,TRUE,&info);rows.push_back({text,info.wID,info.fState,info.fType,info.hSubMenu,reinterpret_cast<const MonoIcon*>(info.dwItemData)});}Layout();}
    void Layout(){
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&info);
        HDC measure=GetDC(window);auto previous=SelectObject(measure,font);int desired=U(216);
        for(auto& row:rows){auto tab=row.text.find(L'\t');auto caption=row.text.substr(0,tab);SIZE extent{};GetTextExtentPoint32W(measure,caption.c_str(),static_cast<int>(caption.size()),&extent);row.shortcutWidth=0;
            if(tab!=std::wstring::npos){auto shortcut=row.text.substr(tab+1);SIZE size{};GetTextExtentPoint32W(measure,shortcut.c_str(),static_cast<int>(shortcut.size()),&size);row.shortcutWidth=size.cx+U(20);}
            desired=std::max(desired,static_cast<int>(extent.cx)+row.shortcutWidth+U(row.child?92:72));
        }
        width=std::min(std::clamp(desired,U(216),U(520)),static_cast<int>(info.rcWork.right-info.rcWork.left-U(24)));
        height=U(stack.size()>1?58:10);
        for(auto& row:rows){auto caption=row.text.substr(0,row.text.find(L'\t'));RECT text{0,0,std::max(U(40),width-U(row.child?92:72)-row.shortcutWidth),0};DrawTextW(measure,caption.c_str(),-1,&text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);row.height=(row.type&MFT_SEPARATOR)?U(11):std::max(U(36),static_cast<int>(text.bottom)+U(14));row.rect={U(8),height,width-U(8),height+row.height};height+=row.height;}
        height=std::min(height+U(12),static_cast<int>(info.rcWork.bottom-info.rcWork.top-U(24)));SelectObject(measure,previous);ReleaseDC(window,measure);
        int x=std::clamp(static_cast<int>(anchor.x),static_cast<int>(info.rcWork.left+U(12)),static_cast<int>(info.rcWork.right-width-U(12))),y=std::clamp(static_cast<int>(anchor.y),static_cast<int>(info.rcWork.top+U(12)),static_cast<int>(info.rcWork.bottom-height-U(12)));
        SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER);RoundWindow(window,16);InvalidateRect(window,nullptr,FALSE);
    }
    void Choose(int index){if(index<0||index>=static_cast<int>(rows.size()))return;auto row=rows[index];if((row.type&MFT_SEPARATOR)||(row.state&(MFS_DISABLED|MFS_GRAYED)))return;if(row.child){stack.push_back(row.child);Load();}else{result=row.id;done=true;}}
    int Hit(POINT p)const {for(size_t i=0;i<rows.size();++i)if(PtInRect(&rows[i].rect,p)&&!(rows[i].type&MFT_SEPARATOR))return static_cast<int>(i);return -1;}
};
LRESULT CALLBACK MenuProc(HWND h,UINT m,WPARAM w,LPARAM l){auto* s=reinterpret_cast<MenuState*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<MenuState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(!s)return DefWindowProcW(h,m,w,l);
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){auto theme=UiTheme();PAINTSTRUCT ps;HDC dc=(m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(w));RECT client;GetClientRect(h,&client);HDC mem=CreateCompatibleDC(dc);HBITMAP bm=CreateCompatibleBitmap(dc,client.right,client.bottom);auto old=SelectObject(mem,bm);RoundFill(mem,client,theme.panel,s->U(16),theme.border);SetBkMode(mem,TRANSPARENT);auto oldFont=SelectObject(mem,s->font);int y=s->U(10)-s->scroll;if(s->stack.size()>1){RECT icon{s->U(22),s->U(18),s->U(42),s->U(38)};DrawGlyph(mem,Glyph::Back,icon,theme.accent);RECT text{s->U(58),s->U(8),client.right-s->U(20),s->U(48)};SetTextColor(mem,theme.accent);DrawTextW(mem,L"Назад",-1,&text,DT_SINGLELINE|DT_VCENTER);y+=s->U(48);}
      for(size_t i=0;i<s->rows.size();++i){auto& row=s->rows[i];int height=row.height;row.rect={s->U(8),y,client.right-s->U(8),y+height};if(row.type&MFT_SEPARATOR){RECT line{s->U(22),y+s->U(6),client.right-s->U(22),y+s->U(7)};RoundFill(mem,line,theme.border,0);}else{bool active=static_cast<int>(i)==s->hover||static_cast<int>(i)==s->selected;if(active)RoundFill(mem,row.rect,theme.field,s->U(9));bool disabled=(row.state&(MFS_DISABLED|MFS_GRAYED))!=0;SetTextColor(mem,disabled?theme.muted:theme.text);RECT ir{s->U(20),y+s->U(8),s->U(40),y+s->U(28)};if(row.icon)DrawMonoIcon(mem,*row.icon,ir);else if(row.state&MFS_CHECKED)DrawGlyph(mem,Glyph::Check,ir,theme.accent);else if(row.text==L"Показать окно")DrawGlyph(mem,Glyph::Expand,ir,theme.muted);else if(row.text==L"Свернуть")DrawGlyph(mem,Glyph::Minus,ir,theme.muted);else if(row.text==L"Закрыть окно")DrawGlyph(mem,Glyph::Close,ir,theme.muted);RECT tr{s->U(50),y,client.right-s->U(row.child?42:22),y+height};auto tab=row.text.find(L'\t');auto caption=row.text.substr(0,tab);if(tab!=std::wstring::npos){RECT shortcut=tr;shortcut.left=shortcut.right-row.shortcutWidth+s->U(20);SetTextColor(mem,disabled?theme.muted:theme.muted);DrawTextW(mem,row.text.substr(tab+1).c_str(),-1,&shortcut,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);tr.right=shortcut.left-s->U(12);SetTextColor(mem,disabled?theme.muted:theme.text);}RECT measure=tr;DrawTextW(mem,caption.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);tr.top+=(height-(measure.bottom-measure.top))/2;DrawTextW(mem,caption.c_str(),-1,&tr,DT_WORDBREAK|DT_NOPREFIX);if(row.child){RECT cr{client.right-s->U(34),y+s->U(10),client.right-s->U(18),y+s->U(26)};DrawGlyph(mem,Glyph::Chevron,cr,theme.muted);}}y+=height;}
      BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);SelectObject(mem,oldFont);SelectObject(mem,old);DeleteObject(bm);DeleteDC(mem);if(m==WM_PAINT)EndPaint(h,&ps);return 0;}
    if(m==WM_MOUSEMOVE){int hit=s->Hit({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(s->hover!=hit){s->hover=hit;InvalidateRect(h,nullptr,FALSE);}return 0;}
    if(m==WM_LBUTTONUP||m==WM_RBUTTONUP){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};RECT r;GetClientRect(h,&r);if(!PtInRect(&r,p)){s->done=true;return 0;}if(s->stack.size()>1&&p.y<s->U(52)){s->stack.pop_back();s->Load();}else s->Choose(s->Hit(p));return 0;}
    if(m==WM_MOUSEWHEEL){int total=s->U(22)+(s->stack.size()>1?s->U(48):0);for(auto& r:s->rows)total+=r.height;s->scroll=std::clamp(s->scroll-(GET_WHEEL_DELTA_WPARAM(w)>0?s->U(92):-s->U(92)),0,std::max(0,total-s->height));InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_KEYDOWN){if(w==VK_ESCAPE)s->done=true;else if(w==VK_LEFT&&s->stack.size()>1){s->stack.pop_back();s->Load();}else if(w==VK_RETURN||w==VK_RIGHT)s->Choose(s->selected);else if(w==VK_DOWN||w==VK_UP){int direction=w==VK_DOWN?1:-1;int size=static_cast<int>(s->rows.size());if(size)for(int n=0;n<size;++n){s->selected=(s->selected+direction+size)%size;auto& r=s->rows[s->selected];if(!(r.type&MFT_SEPARATOR)&&!(r.state&(MFS_DISABLED|MFS_GRAYED)))break;}int y=s->U(s->stack.size()>1?58:10);for(int i=0;i<s->selected;++i)y+=s->rows[i].height;if(y-s->scroll<s->U(8))s->scroll=std::max(0,y-s->U(8));if(y-s->scroll+(s->selected>=0?s->rows[s->selected].height:s->U(36))+s->U(10)>s->height-s->U(8))s->scroll=y+(s->selected>=0?s->rows[s->selected].height:s->U(36))+s->U(18)-s->height;InvalidateRect(h,nullptr,FALSE);}return 0;}
    if(m==WM_ACTIVATE&&LOWORD(w)==WA_INACTIVE)s->done=true;if(m==WM_CLOSE||m==WM_CANCELMODE)s->done=true;return DefWindowProcW(h,m,w,l);
}
}
UINT StyledMenu(HMENU menu,HWND owner,POINT point){UINT branchResult=0;if(TryBranchMenu(menu,owner,point,branchResult))return branchResult;MenuState s;s.owner=owner;s.anchor=point;s.stack.push_back(menu);WNDCLASSW cls{};cls.lpfnWndProc=MenuProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.Menu";RegisterClassW(&cls);s.window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"GoodView — меню",WS_POPUP,point.x,point.y,500,100,owner,nullptr,cls.hInstance,&s);if(!s.window)return 0;s.dpi=GetDpiForWindow(s.window);s.font=UiFont(15,s.dpi);s.Load();if(MenuMotionEnabled())ShowBlueprintWindow(s.window,SW_SHOWNORMAL);else ShowWindow(s.window,SW_SHOWNORMAL);SetForegroundWindow(s.window);SetFocus(s.window);SetCapture(s.window);MSG msg{};while(!s.done&&IsWindow(s.window)){BOOL result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(HandleExitShortcut(msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}if(GetCapture()==s.window)ReleaseCapture();DestroyWindow(s.window);DeleteObject(s.font);return s.result;}
}
