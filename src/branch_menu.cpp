#include "branch_menu.h"
#include "window_motion.h"
#include "ui.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>

namespace gv {
namespace {
constexpr size_t CacheLimit=6*1024*1024;
constexpr UINT_PTR AnimationTimer=1;
Glyph ItemGlyph(std::wstring_view text){
    if(text.find(L"Замет")!=text.npos)return Glyph::Note;
    if(text.find(L"Сайт")!=text.npos||text.find(L"сайт")!=text.npos||text.find(L"браузер")!=text.npos)return Glyph::Globe;
    if(text.find(L"Папка")!=text.npos)return Glyph::Folder;
    if(text.find(L"Часы")!=text.npos)return Glyph::Clock;
    if(text.find(L"Календарь")!=text.npos)return Glyph::Calendar;
    if(text.find(L"Нагрузка")!=text.npos)return Glyph::Activity;
    if(text.find(L"Удалить")!=text.npos||text.find(L"Закрыть")!=text.npos||text.find(L"Завершить")!=text.npos)return Glyph::Close;
    if(text.find(L"Редактировать")!=text.npos||text.find(L"Оформление")!=text.npos||text.find(L"интерфейса")!=text.npos)return Glyph::Settings;
    if(text.find(L"Экран")!=text.npos||text.find(L"экран")!=text.npos)return Glyph::Desktop;
    if(text.find(L"приложение")!=text.npos||text.find(L"Приложение")!=text.npos)return Glyph::Apps;
    if(text.find(L"Показать окно")!=text.npos)return Glyph::Expand;
    if(text.find(L"Свернуть")!=text.npos)return Glyph::Minus;
    return Glyph::More;
}
}
struct BranchMenuRenderer::Impl {
    std::vector<BranchMenuItem> items;std::vector<RECT> final,targets;
    std::vector<std::unique_ptr<PanelBitmap>> tiles;
    PanelBitmap output,backTile;HFONT font{};UINT dpi=96;bool back=false;
    SIZE size{};POINT hub{};std::wstring mode;Palette theme;
    std::vector<MotionScene::Node> nodes,highlights;int highlighted=-1;
    MotionScene::Node links=MotionScene::None;
    ~Impl(){if(font)DeleteObject(font);}
    int U(int value)const{return MulDiv(value,dpi,96);}
    bool Tile(int index,PanelBitmap& bitmap,bool active){
        auto rect=final[index];int w=rect.right-rect.left,h=rect.bottom-rect.top;
        if(!bitmap.Resize(w,h))return false;HDC dc=bitmap.Dc();RECT client{0,0,w,h};
        HBRUSH background=CreateSolidBrush(active?theme.field:theme.panel);FillRect(dc,&client,background);DeleteObject(background);
        RoundFill(dc,client,active?theme.field:theme.panel,U(14),active?theme.accent:theme.border);
        auto& item=items[index];int iconSize=U(20);RECT icon{U(16),(h-iconSize)/2,U(36),(h+iconSize)/2};
        bool disabled=(item.state&(MFS_DISABLED|MFS_GRAYED))!=0;
        if(item.icon)DrawMonoIcon(dc,*item.icon,icon);
        else DrawGlyph(dc,(item.state&MFS_CHECKED)?Glyph::Check:ItemGlyph(item.text),icon,disabled?theme.muted:theme.accent);
        auto previous=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,disabled?theme.muted:theme.text);
        RECT text{U(48),0,w-U(item.child?36:14),h};
        // Shortcut text is kept readable on a second line instead of widening
        // every action card; the complete string is measured before layout.
        auto label=item.text;std::replace(label.begin(),label.end(),L'\t',L'\n');RECT measure=text;
        DrawTextW(dc,label.c_str(),-1,&measure,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        text.top=std::max<LONG>(0,(h-(measure.bottom-measure.top))/2);
        DrawTextW(dc,label.c_str(),-1,&text,DT_WORDBREAK|DT_NOPREFIX);
        if(item.child)DrawGlyph(dc,Glyph::Chevron,{w-U(30),(h-U(16))/2,w-U(14),(h+U(16))/2},theme.muted);
        SelectObject(dc,previous);bitmap.RoundAlpha(static_cast<float>(U(14)));return true;
    }
};
BranchMenuRenderer::BranchMenuRenderer():impl_(std::make_unique<Impl>()){}
BranchMenuRenderer::~BranchMenuRenderer()=default;
bool BranchMenuRenderer::Build(HMENU menu,UINT dpi,SIZE maximum,bool back,std::wstring mode){
    impl_=std::make_unique<Impl>();auto& s=*impl_;s.dpi=std::clamp(dpi,96u,384u);s.back=back;s.mode=std::move(mode);s.theme=UiTheme();
    for(int i=0;i<GetMenuItemCount(menu);++i){wchar_t label[1024]{};MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_STRING|MIIM_ID|MIIM_STATE|MIIM_FTYPE|MIIM_SUBMENU|MIIM_DATA;item.dwTypeData=label;item.cch=1024;
        if(!GetMenuItemInfoW(menu,i,TRUE,&item))return false;if(item.fType&MFT_SEPARATOR)continue;
        s.items.push_back({label,item.wID,item.fState,item.hSubMenu,reinterpret_cast<const MonoIcon*>(item.dwItemData)});
    }
    if(s.items.empty()||s.items.size()>16)return false;
    int columns=s.items.size()>2?2:1;int padding=s.U(12),gap=columns==2?s.U(38):0;
    int tileWidth=std::min(s.U(248),static_cast<int>((maximum.cx-padding*2-gap)/columns));
    if(tileWidth<s.U(160))return false;
    s.font=UiFont(15,s.dpi,FW_MEDIUM);HDC dc=GetDC(nullptr);auto old=SelectObject(dc,s.font);int tileHeight=s.U(58);
    for(const auto& item:s.items){auto label=item.text;std::replace(label.begin(),label.end(),L'\t',L'\n');RECT r{0,0,tileWidth-s.U(item.child?84:62),0};DrawTextW(dc,label.c_str(),-1,&r,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);tileHeight=std::max(tileHeight,static_cast<int>(r.bottom)+s.U(20));}
    SelectObject(dc,old);ReleaseDC(nullptr,dc);
    int rows=(static_cast<int>(s.items.size())+columns-1)/columns,header=back?s.U(44):0;
    int rowGap=s.U(s.items.size()==3?56:24);
    s.size={tileWidth*columns+gap+padding*2,rows*tileHeight+(rows-1)*rowGap+padding*2+header};
    size_t estimated=static_cast<size_t>(s.size.cx)*s.size.cy*4+static_cast<size_t>(tileWidth)*tileHeight*4*s.items.size();
    if(back)estimated+=static_cast<size_t>(s.size.cx-s.U(24))*s.U(34)*4;
    if(s.size.cx>maximum.cx||s.size.cy>maximum.cy||estimated>CacheLimit)return false;
    s.hub={s.size.cx/2,header+(s.size.cy-header)/2};
    if(s.items.size()==3){
        // Equal visible distance from the hub to all three action rectangles:
        // sqrt((gap/2)^2+d^2) == rowGap-d. The lower branch used to be 5 px.
        double d=(rowGap*rowGap-gap*gap/4.)/(2*rowGap);
        s.hub.y=padding+header+tileHeight+static_cast<LONG>(std::lround(d));
    }
    for(size_t i=0;i<s.items.size();++i){int col=static_cast<int>(i)%columns,row=static_cast<int>(i)/columns;
        int x=padding+col*(tileWidth+gap),y=padding+header+row*(tileHeight+rowGap);
        if(columns==2&&i+1==s.items.size()&&s.items.size()%2)x=(s.size.cx-tileWidth)/2;
        s.final.push_back({x,y,x+tileWidth,y+tileHeight});s.tiles.push_back(std::make_unique<PanelBitmap>());
        if(!s.Tile(static_cast<int>(i),*s.tiles.back(),false))return false;
    }
    s.targets=s.final;
    if(back){
        int w=s.size.cx-s.U(24),h=s.U(34);if(!s.backTile.Resize(w,h))return false;
        HDC target=s.backTile.Dc();RECT r{0,0,w,h};HBRUSH brush=CreateSolidBrush(s.theme.panel);FillRect(target,&r,brush);DeleteObject(brush);
        RoundFill(target,r,s.theme.panel,s.U(10),s.theme.border);auto font=SelectObject(target,s.font);SetBkMode(target,TRANSPARENT);SetTextColor(target,s.theme.text);
        DrawTextW(target,L"Назад",-1,&r,DT_SINGLELINE|DT_CENTER|DT_VCENTER);SelectObject(target,font);s.backTile.RoundAlpha(static_cast<float>(s.U(10)));
    }
    return CacheBytes()<=CacheLimit;
}
void BranchMenuRenderer::UpdateTargets(float progress){
    auto& s=*impl_;const bool links=s.mode==L"constellation";
    for(size_t i=0;i<s.items.size();++i){auto r=s.final[i];float value=MenuItemProgress(progress,static_cast<int>(i),static_cast<int>(s.items.size()),links);
        float scale=.86f+.14f*value,cx=(r.left+r.right)*.5f,cy=(r.top+r.bottom)*.5f;
        cx+=(s.hub.x-cx)*.22f*(1-value);cy+=(s.hub.y-cy)*.22f*(1-value);
        int w=std::max(1,static_cast<int>(std::lround((r.right-r.left)*scale))),h=std::max(1,static_cast<int>(std::lround((r.bottom-r.top)*scale)));
        RECT target{static_cast<LONG>(std::lround(cx-w*.5f)),static_cast<LONG>(std::lround(cy-h*.5f)),0,0};target.right=target.left+w;target.bottom=target.top+h;
        s.targets[i]=value>.03f?target:RECT{};
    }
}
bool BranchMenuRenderer::Compose(MotionScene& scene,bool animated){
    auto& s=*impl_;using P=MotionScene::Property;const bool links=s.mode==L"constellation";
    auto color=[](COLORREF c,float alpha){return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,alpha);};
    if(links&&s.items.size()>2){
        auto node=scene.Draw(s.size,[&](ID2D1RenderTarget* dc){
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;dc->CreateSolidColorBrush(color(s.theme.accent,.35f),&brush);if(!brush)return;
            for(auto r:s.final)dc->DrawLine(D2D1::Point2F(static_cast<float>(s.hub.x),static_cast<float>(s.hub.y)),D2D1::Point2F((r.left+r.right)*.5f,(r.top+r.bottom)*.5f),brush.Get(),static_cast<float>(s.U(1)));
            brush->SetColor(color(s.theme.accent,.78f));dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(s.hub.x),static_cast<float>(s.hub.y)),s.U(3)*1.f,s.U(3)*1.f),brush.Get());
        });
        if(node==MotionScene::None)return false;s.links=node;if(animated)scene.Animate(node,P::Opacity,0,1,0,.13);
    }
    MotionScene::Node sharedHighlight=MotionScene::None;
    for(size_t i=0;i<s.items.size();++i){auto r=s.final[i];auto node=scene.Add(*s.tiles[i],{r.left,r.top});if(node==MotionScene::None)return false;s.nodes.push_back(node);
        float w=static_cast<float>(r.right-r.left),h=static_cast<float>(r.bottom-r.top);
        if(animated){
            float cx=(r.left+r.right)*.5f,cy=(r.top+r.bottom)*.5f;
            float x=cx+(s.hub.x-cx)*.22f-w*.86f/2,y=cy+(s.hub.y-cy)*.22f-h*.86f/2;
            double delay=((links?.18:0)+std::min(i,size_t{11})*.016)*MenuMotionDuration/1000.;double duration=MenuMotionDuration/1000.-delay;
            scene.Animate(node,P::X,x,static_cast<float>(r.left),delay,duration);scene.Animate(node,P::Y,y,static_cast<float>(r.top),delay,duration);
            scene.Animate(node,P::ScaleX,.86f,1,delay,duration);scene.Animate(node,P::ScaleY,.86f,1,delay,duration);scene.Animate(node,P::Opacity,0,1,delay,duration);
        }
        auto highlight=sharedHighlight==MotionScene::None?scene.Draw({static_cast<LONG>(w),static_cast<LONG>(h)},[&](ID2D1RenderTarget* dc){
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;dc->CreateSolidColorBrush(color(s.theme.accent,16/255.f),&brush);if(!brush)return;
            float radius=static_cast<float>(s.U(13));auto box=D2D1::RoundedRect(D2D1::RectF(1,1,w-1,h-1),radius,radius);dc->FillRoundedRectangle(box,brush.Get());
            brush->SetColor(color(s.theme.accent,210/255.f));dc->DrawRoundedRectangle(box,brush.Get(),static_cast<float>(s.U(1)));
        },{},node):scene.Clone(sharedHighlight,node);
        if(highlight==MotionScene::None)return false;if(sharedHighlight==MotionScene::None)sharedHighlight=highlight;s.highlights.push_back(highlight);scene.Set(highlight,P::Opacity,0);
    }
    if(s.back&&scene.Add(s.backTile,{s.U(12),s.U(4)})==MotionScene::None)return false;
    s.highlighted=-1;return scene.Commit();
}
bool BranchMenuRenderer::Highlight(MotionScene& scene,int index){
    auto& s=*impl_;if(index==s.highlighted)return true;
    if(s.highlighted>=0)scene.Set(s.highlights[s.highlighted],MotionScene::Property::Opacity,0);
    s.highlighted=index;if(index>=0)scene.Set(s.highlights[index],MotionScene::Property::Opacity,1);
    return scene.Commit();
}
void BranchMenuRenderer::Complete(MotionScene& scene){
    auto& s=*impl_;using P=MotionScene::Property;
    if(s.links!=MotionScene::None)scene.Set(s.links,P::Opacity,1);
    for(size_t i=0;i<s.nodes.size();++i){auto node=s.nodes[i];scene.Set(node,P::X,static_cast<float>(s.final[i].left));scene.Set(node,P::Y,static_cast<float>(s.final[i].top));scene.Set(node,P::ScaleX,1);scene.Set(node,P::ScaleY,1);scene.Set(node,P::Opacity,1);}
    UpdateTargets(1);scene.Commit();
}
bool BranchMenuRenderer::Render(float progress,int hover,int selected){
    auto& s=*impl_;if(!s.output.Resize(s.size.cx,s.size.cy))return false;progress=std::clamp(progress,0.f,1.f);
    int active=hover>=0?hover:selected;
    auto pixels=s.output.Pixels();std::fill_n(pixels,static_cast<size_t>(s.size.cx)*s.size.cy,0u);
    const bool links=s.mode==L"constellation";
    if(links&&s.items.size()>2){
        Gdiplus::Bitmap image(s.size.cx,s.size.cy,s.size.cx*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels));Gdiplus::Graphics g(&image);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto c=s.theme.accent;float phase=MotionEase(progress/.32f);Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(90*phase),GetRValue(c),GetGValue(c),GetBValue(c)),std::max(1.f,s.U(1)*1.f));
        for(auto r:s.final){float x=(r.left+r.right)*.5f,y=(r.top+r.bottom)*.5f;g.DrawLine(&pen,static_cast<float>(s.hub.x),static_cast<float>(s.hub.y),s.hub.x+(x-s.hub.x)*phase,s.hub.y+(y-s.hub.y)*phase);}
        Gdiplus::SolidBrush dot(Gdiplus::Color(static_cast<BYTE>(200*phase),GetRValue(c),GetGValue(c),GetBValue(c)));int d=s.U(6);g.FillEllipse(&dot,s.hub.x-d/2,s.hub.y-d/2,d,d);
    }
    UpdateTargets(progress);
    for(size_t i=0;i<s.items.size();++i){float value=MenuItemProgress(progress,static_cast<int>(i),static_cast<int>(s.items.size()),links);auto target=s.targets[i];if(IsRectEmpty(&target))continue;
        int w=target.right-target.left,h=target.bottom-target.top;
        const auto& tile=*s.tiles[i];auto size=tile.Size();
        BLENDFUNCTION blend{AC_SRC_OVER,0,static_cast<BYTE>(std::lround(value*255)),AC_SRC_ALPHA};
        AlphaBlend(s.output.Dc(),target.left,target.top,w,h,tile.Dc(),0,0,size.cx,size.cy,blend);
    }
    if(s.back){auto size=s.backTile.Size();BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(s.output.Dc(),s.U(12),s.U(4),size.cx,size.cy,s.backTile.Dc(),0,0,size.cx,size.cy,blend);}
    GdiFlush();
    if(active>=0&&active<static_cast<int>(s.targets.size())){
        auto r=s.targets[active];if(!IsRectEmpty(&r)){
            Gdiplus::Bitmap image(s.size.cx,s.size.cy,s.size.cx*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels));Gdiplus::Graphics g(&image);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            float x=r.left+1.f,y=r.top+1.f,w=r.right-r.left-2.f,h=r.bottom-r.top-2.f,d=std::min({static_cast<float>(s.U(26)),w,h});
            Gdiplus::GraphicsPath path;path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
            auto color=s.theme.accent;float amount=MenuItemProgress(progress,active,static_cast<int>(s.items.size()),links);
            Gdiplus::SolidBrush wash(Gdiplus::Color(static_cast<BYTE>(16*amount),GetRValue(color),GetGValue(color),GetBValue(color)));g.FillPath(&wash,&path);
            Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(210*amount),GetRValue(color),GetGValue(color),GetBValue(color)),std::max(1.f,s.U(1)*1.f));g.DrawPath(&pen,&path);
        }
    }
    return true;
}
int BranchMenuRenderer::Hit(POINT point)const{for(size_t i=0;i<impl_->targets.size();++i)if(PtInRect(&impl_->targets[i],point))return static_cast<int>(i);return -1;}
SIZE BranchMenuRenderer::Size()const{return impl_->size;}
POINT BranchMenuRenderer::Hub()const{return impl_->hub;}
const std::vector<BranchMenuItem>& BranchMenuRenderer::Items()const{return impl_->items;}
const std::vector<RECT>& BranchMenuRenderer::Targets()const{return impl_->targets;}
const PanelBitmap& BranchMenuRenderer::Bitmap()const{return impl_->output;}
size_t BranchMenuRenderer::CacheBytes()const{auto& s=*impl_;size_t bytes=s.output.Bytes()+s.backTile.Bytes();for(const auto& tile:s.tiles)bytes+=tile->Bytes();return bytes;}
namespace {
struct BranchState {
    HWND window{},owner{};POINT anchor{};UINT result=0,dpi=96;bool done=false,inChild=false,animated=false;int hover=-1,selected=-1;double began=0;
    std::wstring mode;std::vector<HMENU> stack;BranchMenuRenderer renderer;std::unique_ptr<MotionScene> scene;
    float Progress()const{return animated?std::min(1.f,static_cast<float>((MotionNow()-began)*1000/MenuMotionDuration)):1;}
    bool Load(){
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST),&info);SIZE maximum{info.rcWork.right-info.rcWork.left-24,info.rcWork.bottom-info.rcWork.top-24};
        scene.reset();KillTimer(window,AnimationTimer);
        if(!renderer.Build(stack.back(),dpi,maximum,stack.size()>1,mode))return false;
        hover=selected=-1;auto size=renderer.Size();auto hub=renderer.Hub();
        int x=std::clamp(static_cast<int>(anchor.x-hub.x),static_cast<int>(info.rcWork.left+12),static_cast<int>(info.rcWork.right-size.cx-12));
        int y=std::clamp(static_cast<int>(anchor.y-hub.y),static_cast<int>(info.rcWork.top+12),static_cast<int>(info.rcWork.bottom-size.cy-12));
        SetWindowPos(window,HWND_TOPMOST,x,y,size.cx,size.cy,SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        animated=ClientMotionAllowed();scene=std::make_unique<MotionScene>();
        auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);SetWindowLongPtrW(window,GWL_EXSTYLE,(style|WS_EX_NOREDIRECTIONBITMAP)&~WS_EX_LAYERED);
        if(!animated||!scene->Open(window,6*1024*1024)||!renderer.Compose(*scene,true)){
            // Fully static fallback, not a second low-frame-rate animation.
            scene.reset();animated=false;SetWindowLongPtrW(window,GWL_EXSTYLE,(style|WS_EX_LAYERED)&~WS_EX_NOREDIRECTIONBITMAP);
        }
        began=MotionNow();if(animated&&!SetTimer(window,AnimationTimer,MenuMotionDuration+20,nullptr)){animated=false;renderer.Complete(*scene);}
        return Paint();
    }
    bool Paint(){renderer.UpdateTargets(Progress());if(scene)return renderer.Highlight(*scene,hover>=0?hover:selected);return renderer.Render(1,hover,selected)&&renderer.Bitmap().Present(window);}
    void Choose(int index){
        if(index<0||index>=static_cast<int>(renderer.Items().size()))return;auto item=renderer.Items()[index];
        if(item.state&(MFS_DISABLED|MFS_GRAYED))return;
        if(!item.child){result=item.id;done=true;return;}
        stack.push_back(item.child);if(Load())return;stack.pop_back();
        // Long lists retain the scrolling list implementation, including search
        // and tray submenus with arbitrary numbers of entries.
        inChild=true;ReleaseCapture();result=StyledMenu(item.child,window,anchor);inChild=false;
        if(result)done=true;else{Load();SetForegroundWindow(window);SetCapture(window);}
    }
};
LRESULT CALLBACK BranchProc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* s=reinterpret_cast<BranchState*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){s=static_cast<BranchState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(h,m,w,l);
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);s->Paint();EndPaint(h,&p);return 0;}
    if(m==WM_TIMER&&w==AnimationTimer){s->animated=false;KillTimer(h,AnimationTimer);s->renderer.UpdateTargets(1);POINT point;GetCursorPos(&point);ScreenToClient(h,&point);s->hover=s->renderer.Hit(point);s->Paint();return 0;}
    if(m==WM_MOUSEMOVE){s->renderer.UpdateTargets(s->Progress());int hover=s->renderer.Hit({GET_X_LPARAM(l),GET_Y_LPARAM(l)});if(hover!=s->hover){s->hover=hover;s->Paint();}return 0;}
    if(m==WM_LBUTTONUP||m==WM_RBUTTONUP){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};RECT r{};GetClientRect(h,&r);if(!PtInRect(&r,p)){s->done=true;return 0;}
        if(s->stack.size()>1&&p.y<MulDiv(44,s->dpi,96)){s->stack.pop_back();s->Load();}else{s->renderer.UpdateTargets(s->Progress());int hit=s->renderer.Hit(p);if(hit<0)s->done=true;else s->Choose(hit);}return 0;
    }
    if(m==WM_KEYDOWN){if(w==VK_ESCAPE)s->done=true;else if(w==VK_LEFT&&s->stack.size()>1){s->stack.pop_back();s->Load();}
        else if(w==VK_RETURN||w==VK_RIGHT)s->Choose(s->selected);
        else if(w==VK_UP||w==VK_DOWN||w==VK_TAB){int count=static_cast<int>(s->renderer.Items().size());int direction=w==VK_UP?-1:1;
            for(int i=0;i<count;++i){s->selected=s->selected<0?(direction>0?0:count-1):(s->selected+direction+count)%count;if(!(s->renderer.Items()[s->selected].state&(MFS_DISABLED|MFS_GRAYED)))break;}
            s->hover=-1;s->animated=false;KillTimer(h,AnimationTimer);if(s->scene)s->renderer.Complete(*s->scene);s->Paint();
        }return 0;
    }
    if(m==WM_ACTIVATE&&LOWORD(w)==WA_INACTIVE&&!s->inChild)s->done=true;
    if(m==WM_SETTINGCHANGE&&s->animated&&!ClientMotionAllowed()){s->animated=false;KillTimer(h,AnimationTimer);if(s->scene)s->renderer.Complete(*s->scene);s->Paint();}
    if(m==WM_CLOSE||(m==WM_CANCELMODE&&!s->inChild))s->done=true;
    if(m==WM_DESTROY)KillTimer(h,AnimationTimer);
    return DefWindowProcW(h,m,w,l);
}
}
bool TryBranchMenu(HMENU menu,HWND owner,POINT anchor,UINT& result){
    auto* app=CurrentApplication();if(!app||app->config.ui.menuMotion==L"none"||GetMenuItemCount(menu)<3||GetMenuItemCount(menu)>20)return false;
    BranchState state;state.owner=owner;state.anchor=anchor;state.mode=app->config.ui.menuMotion;state.stack.push_back(menu);
    WNDCLASSW cls{};cls.lpfnWndProc=BranchProc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.BranchMenu";RegisterClassW(&cls);
    HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"GoodView — действия",WS_POPUP,anchor.x,anchor.y,1,1,owner,nullptr,cls.hInstance,&state);
    if(!h)return false;state.dpi=GetDpiForWindow(h);
    if(!state.Load()){DestroyWindow(h);return false;}
    ShowWindow(h,SW_SHOWNORMAL);SetForegroundWindow(h);SetFocus(h);SetCapture(h);MSG msg{};
    while(!state.done&&IsWindow(h)){BOOL got=GetMessageW(&msg,nullptr,0,0);if(got<=0){if(!got)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(HandleExitShortcut(msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(GetCapture()==h)ReleaseCapture();DestroyWindow(h);result=state.result;return true;
}
}
