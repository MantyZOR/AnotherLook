#include "live_probe.h"
#include "ui.h"
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <uxtheme.h>
namespace gv {
namespace {
bool Contains(Box b,float x,float y){return x>=b.x&&y>=b.y&&x<b.x+b.w&&y<b.y+b.h;}
D2D1_RECT_F R(Box b){return D2D1::RectF(b.x,b.y,b.x+b.w,b.y+b.h);}
std::wstring Truncate(std::wstring s,size_t max){if(s.size()>max)s=s.substr(0,max-1)+L"…";return s;}
std::wstring DateText(){SYSTEMTIME t;GetLocalTime(&t);const wchar_t* months[]={L"января",L"февраля",L"марта",L"апреля",L"мая",L"июня",L"июля",L"августа",L"сентября",L"октября",L"ноября",L"декабря"};return std::to_wstring(t.wDay)+L" "+months[t.wMonth-1];}
std::wstring TimeText(){SYSTEMTIME t;GetLocalTime(&t);wchar_t b[8];swprintf_s(b,L"%02u:%02u",t.wHour,t.wMinute);return b;}
}
Surface::Surface(Application&a,bool isBar,int screen,bool isRail,UiElement kind):bar(isBar),rail(isRail),monitor(screen),element(isRail?UiElement::Windows:kind),app_(a){}
Surface::~Surface(){if(appbarRegistered_){APPBARDATA a{sizeof(a)};a.hWnd=hwnd;SHAppBarMessage(ABM_REMOVE,&a);}if(tooltip_&&IsWindow(tooltip_)){auto font=reinterpret_cast<HFONT>(RemovePropW(tooltip_,L"GoodView.Font"));DestroyWindow(tooltip_);if(font)DeleteObject(font);}if(hwnd)DestroyWindow(hwnd);Discard();}
void Surface::Create(bool previewMode){WNDCLASSEXW c{sizeof(c)};c.style=CS_DBLCLKS;c.lpfnWndProc=Proc;c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=app_.icon;c.hIconSm=app_.icon;c.lpszClassName=L"GoodView.Surface";RegisterClassExW(&c);
    auto title=(bar?L"GoodView — панель · ":L"GoodView — рабочий стол · ")+std::to_wstring(monitor+1);
    auto origin=app_.monitors[monitor].bounds;HWND owner=(bar||rail)&&monitor<static_cast<int>(app_.boards.size())?app_.boards[monitor]->hwnd:nullptr;hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|((bar||rail)?WS_EX_LAYERED:0),c.lpszClassName,title.c_str(),WS_POPUP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,origin.left,origin.top,1360,900,owner,nullptr,c.hInstance,this);
    // Our full-monitor canvas is a desktop, not a fullscreen app. Set before showing.
    if(!bar&&!rail)SetPropW(hwnd,L"NonRudeHWND",reinterpret_cast<HANDLE>(TRUE));
    BOOL dark=!UiTheme().light;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));BOOL transitions=TRUE;DwmSetWindowAttribute(hwnd,DWMWA_TRANSITIONS_FORCEDISABLED,&transitions,sizeof(transitions));if((bar&&(element==UiElement::Settings||element==UiElement::Power))||rail){tooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,c.hInstance,nullptr);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_SUBCLASS;info.hwnd=hwnd;info.uId=1;info.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,540);SetWindowTheme(tooltip_,L"",L"");SendMessageW(tooltip_,TTM_SETTIPBKCOLOR,RGB(19,28,30),0);SendMessageW(tooltip_,TTM_SETTIPTEXTCOLOR,RGB(216,233,221),0);auto font=UiFont(16,GetDpiForWindow(hwnd));SetPropW(tooltip_,L"GoodView.Font",font);SendMessageW(tooltip_,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);RECT padding{12,10,12,10};SendMessageW(tooltip_,TTM_SETMARGIN,0,reinterpret_cast<LPARAM>(&padding));}Place(previewMode);}
void Surface::Place(bool previewMode){if(monitor>=static_cast<int>(app_.monitors.size()))return;auto m=app_.monitors[monitor];RECT r=m.work;UINT dpi=GetDpiForWindow(hwnd);float scale=dpi/96.f;
    if(bar||rail){
        auto position=UiPosition(app_.config.ui,element);
        auto box=app_.PanelBox(element,monitor,{0,0,(m.bounds.right-m.bounds.left)/scale,(m.bounds.bottom-m.bounds.top)/scale});
        RECT next{m.bounds.left+static_cast<LONG>(std::lround(box.x*scale)),m.bounds.top+static_cast<LONG>(std::lround(box.y*scale)),m.bounds.left+static_cast<LONG>(std::lround((box.x+box.w)*scale)),m.bounds.top+static_cast<LONG>(std::lround((box.y+box.h)*scale))};
        bool edgeVertical=position==L"left"||position==L"right";bool reserve=(element==UiElement::Navigation||element==UiElement::Windows)&&!(rail&&app_.config.ui.railOrbit)&&!previewMode&&(position==L"left"||position==L"right"||position==L"top"||position==L"bottom")&&UiVertical(app_.config.ui,element)==edgeVertical;
        UINT edge=position==L"left"?ABE_LEFT:position==L"right"?ABE_RIGHT:position==L"bottom"?ABE_BOTTOM:ABE_TOP;
        RECT requested=m.bounds;int thickness=static_cast<int>((edge==ABE_LEFT||edge==ABE_RIGHT?box.w+24:box.h+24)*scale);
        if(edge==ABE_LEFT)requested.right=requested.left+thickness;else if(edge==ABE_RIGHT)requested.left=requested.right-thickness;else if(edge==ABE_BOTTOM)requested.top=requested.bottom-thickness;else requested.bottom=requested.top+thickness;
        if(appbarRegistered_&&(!reserve||!EqualRect(&requested,&reservation_))){APPBARDATA data{sizeof(data)};data.hWnd=hwnd;SHAppBarMessage(ABM_REMOVE,&data);appbarRegistered_=false;reservation_={};}
        if(reserve){APPBARDATA data{sizeof(data)};data.hWnd=hwnd;data.uCallbackMessage=WM_APP+10;if(!appbarRegistered_)appbarRegistered_=SHAppBarMessage(ABM_NEW,&data)!=0;data.uEdge=edge;data.rc=requested;SHAppBarMessage(ABM_QUERYPOS,&data);if(edge==ABE_LEFT)data.rc.right=data.rc.left+thickness;else if(edge==ABE_RIGHT)data.rc.left=data.rc.right-thickness;else if(edge==ABE_BOTTOM)data.rc.top=data.rc.bottom-thickness;else data.rc.bottom=data.rc.top+thickness;SHAppBarMessage(ABM_SETPOS,&data);reservation_=requested;LONG width=next.right-next.left,height=next.bottom-next.top,margin=static_cast<LONG>(12*scale);if(edge==ABE_LEFT)next.left=data.rc.left+margin;else if(edge==ABE_RIGHT)next.left=data.rc.right-margin-width;else if(edge==ABE_TOP)next.top=data.rc.top+static_cast<LONG>((bar?8:12)*scale);else next.top=data.rc.bottom-margin-height;next.right=next.left+width;next.bottom=next.top+height;}
        RECT current{};GetWindowRect(hwnd,&current);if(!EqualRect(&current,&next))SetWindowPos(hwnd,nullptr,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER);// Pixel alpha, rather than a binary HRGN, owns the outer edge and the ring hole.
        HRGN oldRegion=CreateRectRgn(0,0,0,0);if(GetWindowRgn(hwnd,oldRegion)!=ERROR)SetWindowRgn(hwnd,nullptr,FALSE);DeleteObject(oldRegion);return;
    }
    LONG_PTR style=WS_POPUP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_VISIBLE);if(previewMode)style|=WS_THICKFRAME;
    LONG_PTR extended=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);LONG_PTR nextExtended=(extended&~(WS_EX_APPWINDOW|WS_EX_TOOLWINDOW))|(previewMode?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW);if(nextExtended!=extended)SetWindowLongPtrW(hwnd,GWL_EXSTYLE,nextExtended);
    bool styleChanged=GetWindowLongPtrW(hwnd,GWL_STYLE)!=style;if(styleChanged)SetWindowLongPtrW(hwnd,GWL_STYLE,style);
    if(previewMode){int width=std::min(static_cast<int>(1360*scale),static_cast<int>(r.right-r.left-48)),height=std::min(static_cast<int>(900*scale),static_cast<int>(r.bottom-r.top-48));r={r.left+(r.right-r.left-width)/2,r.top+(r.bottom-r.top-height)/2,0,0};r.right=r.left+width;r.bottom=r.top+height;}
    else r=m.bounds;
    RECT old{};GetWindowRect(hwnd,&old);if(styleChanged||!EqualRect(&old,&r)){
        // Geometry must not depend on another process accepting a Z-order change.
        SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE|(styleChanged?SWP_FRAMECHANGED:0));Discard();
    }
}
void Surface::Show(bool visible,bool raise){
    (void)raise;
    if(!visible){KillTimer(hwnd,82);networkAnimating_=false;KillTimer(hwnd,83);orbitTimerActive_=false;orbit_.Finish();orbitHover_.Reset();hover_=-1;}
    bool wasVisible=IsWindowVisible(hwnd)!=FALSE;
    if(visible&&(bar||rail)){
        if(!wasVisible){Paint();ShowWindow(hwnd,SW_SHOWNOACTIVATE);}
        SetShellPanelLayer(hwnd,panelTopmost_);
        if(!wasVisible)Invalidate();return;
    }
    if(wasVisible==visible)return;
    ShowWindow(hwnd,visible?SW_SHOWNOACTIVATE:SW_HIDE);if(visible)SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);if(!visible)Discard();else Invalidate();
}
void Surface::SetPanelLayer(bool topmost,HWND behind){
    panelTopmost_=topmost;if(!hwnd||(!bar&&!rail))return;
    SetShellPanelLayer(hwnd,topmost,behind);
}
void Surface::Invalidate(){if(hwnd&&!covered&&IsWindowVisible(hwnd))InvalidateRect(hwnd,nullptr,FALSE);app_.SyncSystemWidget();}
void Surface::Discard(){orbitCoverage_={};std::vector<uint32_t>{}.swap(orbitBase_);orbitRasterKey_.reset();networkBackdrop_.Reset();textLayouts_.clear();networkHaloBlue_.Reset();networkHaloViolet_.Reset();bitmaps_.clear();iconBitmaps_.clear();brush_.Reset();rt_.Reset();panelBitmap_.Reset();}
bool Surface::ExportPreview(const fs::path& path){std::error_code ec;fs::create_directories(path.parent_path(),ec);ComPtr<IWICImagingFactory>wic;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))))return false;RECT r;GetClientRect(hwnd,&r);ComPtr<IWICBitmap>bitmap;if(FAILED(wic->CreateBitmap(r.right,r.bottom,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap)))return false;auto p=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(GetDpiForWindow(hwnd)),static_cast<float>(GetDpiForWindow(hwnd)));Discard();if(FAILED(app_.d2d->CreateWicBitmapRenderTarget(bitmap.Get(),p,&rt_)))return false;rt_->CreateSolidColorBrush(Ink(UiTheme().text),&brush_);Paint();ComPtr<IWICStream>stream;ComPtr<IWICBitmapEncoder>encoder;ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IWICFormatConverter>converter;bool ok=SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))&&SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->SetSize(r.right,r.bottom));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;if(ok)ok=SUCCEEDED(frame->SetPixelFormat(&format))&&SUCCEEDED(wic->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))&&SUCCEEDED(frame->WriteSource(converter.Get(),nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());Discard();return ok;}
void Surface::Fill(Box b,D2D1_COLOR_F c,float radius){brush_->SetColor(c);if(radius)rt_->FillRoundedRectangle(D2D1::RoundedRect(R(b),radius,radius),brush_.Get());else rt_->FillRectangle(R(b),brush_.Get());}
void Surface::Stroke(Box b,D2D1_COLOR_F c,float radius,float width){brush_->SetColor(c);if(radius)rt_->DrawRoundedRectangle(D2D1::RoundedRect(R(b),radius,radius),brush_.Get(),width);else rt_->DrawRectangle(R(b),brush_.Get(),width);}
void Surface::CachedText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight,bool wrap,FontRole role,bool center,bool inkCenter){
    if(text.empty()||box.w<=0||box.h<=0)return;float pixels=size*(scaledUi_?1:app_.config.ui.textScale);
    TextKey key{std::wstring(text),box.w,box.h,pixels,weight,static_cast<int>(role)*8+(wrap?2:0)+(center?1:0)+(inkCenter?4:0)};
    auto found=textLayouts_.find(key);
    if(found==textLayouts_.end()){
        if(textLayouts_.size()>=128)textLayouts_.clear();ComPtr<IDWriteTextLayout> layout;
        if(FAILED(app_.dwrite->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),app_.Font(pixels,weight,role),box.w,box.h,&layout)))return;
        layout->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);layout->SetTextAlignment(center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);layout->SetParagraphAlignment(center&&!inkCenter?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        found=textLayouts_.emplace(std::move(key),std::move(layout)).first;
    }
    float y=box.y;if(inkCenter){DWRITE_OVERHANG_METRICS ink{};found->second->GetOverhangMetrics(&ink);y=box.y+box.h/2-(-ink.top+box.h+ink.bottom)/2;}
    brush_->SetColor(color);rt_->DrawTextLayout(D2D1::Point2F(box.x,y),found->second.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Surface::Text(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight,bool wrap,FontRole role){CachedText(text,box,size,color,weight,wrap,role,false);}
void Surface::CenterText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight){CachedText(text,box,size,color,weight,false,FontRole::Interface,true);}
void Surface::Button(std::wstring_view t,Box b,std::function<void()> action,bool active,bool quiet){float radius=bar||rail?std::min(10.f,UiRadius(app_.config.ui,element)):10;int index=static_cast<int>(hits_.size());auto accent=Ink(UiTheme().accent);bool hover=index==hover_;if(active)Fill(b,Ink(UiTheme().accent,hover?0.25f:0.16f),radius);else if(hover)Fill(b,Ink(UiTheme().text,0.09f),radius);else if(!quiet)Fill(b,Ink(UiTheme().text,0.045f),radius);if(!quiet&&!active)Stroke(b,Ink(UiTheme().border),radius);CenterText(t,{b.x+5,b.y,b.w-10,b.h},15,active?accent:Ink(UiTheme().text),active?600:500);hits_.push_back({b,std::move(action),drawingCards_});}
void Surface::GlyphIcon(Glyph glyph,Box box,D2D1_COLOR_F color){brush_->SetColor(color);if(auto path=app_.IconPath(glyph)){D2D1_MATRIX_3X2_F previous;rt_->GetTransform(&previous);rt_->SetTransform(D2D1::Matrix3x2F::Scale(box.w/24,box.h/24)*D2D1::Matrix3x2F::Translation(box.x,box.y)*previous);rt_->DrawGeometry(path,brush_.Get(),1.65f,app_.iconStroke.Get());rt_->SetTransform(previous);return;}for(auto line:GlyphLines(glyph))rt_->DrawLine(D2D1::Point2F(box.x+line.x1*box.w/24,box.y+line.y1*box.h/24),D2D1::Point2F(box.x+line.x2*box.w/24,box.y+line.y2*box.h/24),brush_.Get(),1.65f,app_.iconStroke.Get());for(auto circle:GlyphCircles(glyph)){auto ellipse=D2D1::Ellipse(D2D1::Point2F(box.x+circle.x*box.w/24,box.y+circle.y*box.h/24),circle.r*box.w/24,circle.r*box.h/24);if(circle.r<=1.1f)rt_->FillEllipse(ellipse,brush_.Get());else rt_->DrawEllipse(ellipse,brush_.Get(),1.65f,app_.iconStroke.Get());}}
void Surface::IconButton(Glyph glyph,std::wstring_view label,Box box,std::function<void()> action,bool active,bool quiet){
    Button(L"",box,std::move(action),active,quiet);auto color=active?Ink(UiTheme().accent):Ink(UiTheme().text);
    if(label.empty()){GlyphIcon(glyph,{box.x+(box.w-20)/2,box.y+(box.h-20)/2,20,20},color);return;}
    float scale=scaledUi_?1:app_.config.ui.textScale,size=15;
    auto measure=[&]{ComPtr<IDWriteTextLayout> layout;auto* format=app_.Font(size*scale,active?600:500);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);app_.dwrite->CreateTextLayout(label.data(),static_cast<UINT32>(label.size()),format,4096,box.h,&layout);DWRITE_TEXT_METRICS metrics{};if(layout)layout->GetMetrics(&metrics);return metrics.widthIncludingTrailingWhitespace;};
    float textWidth=measure(),available=std::max(1.f,box.w-44);
    if(textWidth>available){size*=available/textWidth;textWidth=measure();}
    float start=box.x+(box.w-textWidth-28)/2;
    GlyphIcon(glyph,{start,box.y+(box.h-20)/2,20,20},color);
    CenterText(label,{start+28,box.y,textWidth+1,box.h},size,color,active?600:500);
}
void Surface::FitCenterText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight){
    float scale=scaledUi_?1:app_.config.ui.textScale;ComPtr<IDWriteTextLayout> layout;
    auto* format=app_.Font(size*scale,weight);format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    app_.dwrite->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format,4096,4096,&layout);
    DWRITE_TEXT_METRICS metrics{};if(layout)layout->GetMetrics(&metrics);
    float fit=std::min({1.f,box.w/std::max(1.f,metrics.widthIncludingTrailingWhitespace),box.h/std::max(1.f,metrics.height)});
    CenterText(text,box,size*fit,color,weight);
}
void Surface::ProgramIcon(const std::wstring& target,Box box){auto source=app_.icons.Get(target);auto& bitmap=iconBitmaps_[target+L":mono"];if(!bitmap){if(source->pixels.empty()){GlyphIcon(Glyph::Apps,box,Ink(UiTheme().text));return;}auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));const auto& pixels=source->pixels;rt_->CreateBitmap(D2D1::SizeU(source->size,source->size),pixels.data(),source->size*4,properties,&bitmap);}float cap=source->size*96.f/GetDpiForWindow(hwnd);float size=std::min({box.w,box.h,cap});box={box.x+(box.w-size)/2,box.y+(box.h-size)/2,size,size};if(bitmap)rt_->DrawBitmap(bitmap.Get(),R(box),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}
void Surface::WindowIcon(const WindowInfo& window,Box box,float opacity){if(opacity<=0)return;auto source=app_.icons.GetWindow(window.hwnd);auto key=L"window:"+std::to_wstring(window.pid)+L":"+std::to_wstring(reinterpret_cast<ULONG_PTR>(window.hwnd));auto bitmapKey=key+L":"+std::to_wstring(reinterpret_cast<ULONG_PTR>(source.get()));for(auto it=iconBitmaps_.begin();it!=iconBitmaps_.end();)if(it->first.starts_with(key+L":")&&it->first!=bitmapKey)it=iconBitmaps_.erase(it);else ++it;auto& bitmap=iconBitmaps_[bitmapKey];if(!bitmap){if(source->pixels.empty()){GlyphIcon(Glyph::Apps,box,Ink(UiTheme().text,opacity));return;}auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));const auto& pixels=source->pixels;rt_->CreateBitmap(D2D1::SizeU(source->size,source->size),pixels.data(),source->size*4,properties,&bitmap);}float cap=source->size*96.f/GetDpiForWindow(hwnd)/(scaledUi_?app_.config.ui.textScale:1);float size=std::min({box.w,box.h,cap});box={box.x+(box.w-size)/2,box.y+(box.h-size)/2,size,size};if(bitmap)rt_->DrawBitmap(bitmap.Get(),R(box),opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);}
float Surface::Zoom()const{return app_.Active().zoom;}
Box Surface::Viewport()const{RECT rect;GetClientRect(hwnd,&rect);float scale=96.f/GetDpiForWindow(hwnd),left=28,right=28,top=98,bottom=28;Box area{0,0,rect.right*scale,rect.bottom*scale};auto& ui=app_.config.ui;
    for(auto e:{UiElement::Navigation,UiElement::Windows,UiElement::Dock}){if(e==UiElement::Windows&&!app_.config.enabled)continue;auto position=UiPosition(ui,e);if(position==L"hidden"||position==L"custom")continue;auto box=app_.PanelBox(e,monitor,area);if(position==L"left"&&UiVertical(ui,e))left=std::max(left,box.x+box.w+20);else if(position==L"right"&&UiVertical(ui,e))right=std::max(right,area.w-box.x+20);else if(position==L"top")top=std::max(top,box.y+box.h+24);else if(position==L"bottom"||(e==UiElement::Dock&&position==L"center"))bottom=std::max(bottom,area.h-box.y+16);}
    return {left,top,std::max(100.f,area.w-left-right),std::max(100.f,area.h-top-bottom)};
}
Box Surface::CanvasBox(const Card&c)const{float z=Zoom();auto view=Viewport();return {view.x+(c.box.x-panX_)*z,view.y+(c.box.y-panY_)*z,c.box.w*z,c.box.h*z};}
void Surface::Reveal(const Card& card){auto viewport=Viewport();panX_=std::max(0.f,card.box.x-(viewport.w/Zoom()-card.box.w)/2);panY_=std::max(0.f,card.box.y-24);Invalidate();}
void Surface::ResetView(){panX_=panY_=0;selected_.clear();drag_.clear();before_.clear();pressedHit_.reset();panning_=false;if(GetCapture()==hwnd)ReleaseCapture();Invalidate();}
bool Surface::TickSeconds(bool paint){
    if(bar||rail||covered||!hwnd||!IsWindowVisible(hwnd)||IsIconic(hwnd))return false;auto viewport=Viewport();HWND foreground=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);RECT cover{};bool external=pid&&pid!=GetCurrentProcessId()&&!IsIconic(foreground)&&GetWindowRect(foreground,&cover);bool any=false;
    for(const auto& card:app_.Active().cards)if(card.type==L"clock"&&card.appearance.showSeconds&&CardMonitor(card)==monitor){auto box=CanvasBox(card);if(box.x+box.w<=viewport.x||box.y+box.h<=viewport.y||box.x>=viewport.x+viewport.w||box.y>=viewport.y+viewport.h)continue;float scale=GetDpiForWindow(hwnd)/96.f;RECT rect{static_cast<LONG>(box.x*scale),static_cast<LONG>(box.y*scale),static_cast<LONG>((box.x+box.w)*scale),static_cast<LONG>((box.y+box.h)*scale)};if(external){POINT points[]={{rect.left,rect.top},{rect.right,rect.bottom}};MapWindowPoints(hwnd,nullptr,points,2);if(cover.left<=points[0].x&&cover.top<=points[0].y&&cover.right>=points[1].x&&cover.bottom>=points[1].y)continue;}any=true;if(paint)InvalidateRect(hwnd,&rect,FALSE);}
    return any;
}
bool Surface::HasVisibleSystemWidget()const{return VisibleSystemMetrics()!=0;}
unsigned Surface::VisibleSystemMetrics()const{
    if(bar||rail||covered||!hwnd||!IsWindowVisible(hwnd)||IsIconic(hwnd))return false;
    auto* scene=FindScene(app_.config,app_.config.active);if(!scene)return false;auto viewport=Viewport();
    HWND foreground=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(foreground,&pid);RECT cover{};bool external=pid&&pid!=GetCurrentProcessId()&&!IsIconic(foreground)&&GetWindowRect(foreground,&cover);
    unsigned metrics=0;for(const auto& card:scene->cards)if(card.type==L"system"&&CardMonitor(card)==monitor){
        Box box=CanvasBox(card);float left=std::max(viewport.x,box.x),top=std::max(viewport.y,box.y),right=std::min(viewport.x+viewport.w,box.x+box.w),bottom=std::min(viewport.y+viewport.h,box.y+box.h);if(right<=left||bottom<=top)continue;
        if(external){float scale=GetDpiForWindow(hwnd)/96.f;POINT points[]={{static_cast<LONG>(left*scale),static_cast<LONG>(top*scale)},{static_cast<LONG>(right*scale),static_cast<LONG>(bottom*scale)}};MapWindowPoints(hwnd,nullptr,points,2);if(cover.left<=points[0].x&&cover.top<=points[0].y&&cover.right>=points[1].x&&cover.bottom>=points[1].y)continue;}
        metrics|=card.appearance.metrics;
    }return metrics;
}
int Surface::CardMonitor(const Card&c)const{if(!c.monitorId.empty()){for(size_t i=0;i<app_.monitors.size();++i)if(app_.monitors[i].id==c.monitorId)return static_cast<int>(i);return 0;}return std::min(c.monitor,std::max(0,static_cast<int>(app_.monitors.size())-1));}
void Surface::Popup(HMENU menu,std::function<void(UINT)> cb){app_.CancelProgramActivation();popupOpen_=true;app_.web.HideEmbedded(hwnd);POINT p;GetCursorPos(&p);UINT id=StyledMenu(menu,hwnd,p);DestroyMenu(menu);popupOpen_=false;if(id)cb(id);SyncWeb();Invalidate();}
void Surface::WindowsMenu(){
    HMENU menu=CreatePopupMenu(), available=CreatePopupMenu(), managed=CreatePopupMenu(), floating=CreatePopupMenu(), other=CreatePopupMenu(), layouts=CreatePopupMenu();
    auto list=ListAppWindows();if(list.size()>128)list.resize(128);
    std::vector<HWND> attached, elsewhere;
    for(auto& entry:app_.windows.Entries()){
        if(entry.scene==app_.config.active){
            auto index=attached.size();attached.push_back(entry.info.hwnd);
            AppendMenuW(managed,MF_STRING,3000+index,Truncate(entry.info.title,60).c_str());
            AppendMenuW(floating,MF_STRING|(entry.floating?MF_CHECKED:0),5000+index,Truncate(entry.info.title,60).c_str());
        }else{
            auto index=elsewhere.size();elsewhere.push_back(entry.info.hwnd);
            auto* scene=FindScene(app_.config,entry.scene);
            auto title=(scene?scene->name:entry.scene)+L" / "+Truncate(entry.info.title,48);
            AppendMenuW(other,MF_STRING,4000+index,title.c_str());
        }
    }
    for(size_t i=0;i<list.size();++i)AppendMenuW(available,MF_STRING|(app_.windows.IsManaged(list[i].hwnd)?MF_CHECKED:0),1000+i,Truncate(list[i].title,64).c_str());
    if(list.empty())AppendMenuW(available,MF_GRAYED,0,L"Нет подходящих окон");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(available),L"Добавить существующее окно");
    if(!elsewhere.empty())AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(other),L"Перенести сюда из другого пространства");else DestroyMenu(other);
    if(!attached.empty()){
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(managed),L"Убрать окно из управления");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(floating),L"Плавающий режим");
    }else{DestroyMenu(managed);DestroyMenu(floating);}
    AppendMenuW(layouts,MF_STRING,10,L"Колонки");AppendMenuW(layouts,MF_STRING,11,L"Главное окно и стек");AppendMenuW(layouts,MF_STRING,12,L"Сетка");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(layouts),L"Раскладка");
    AppendMenuW(menu,MF_STRING|(app_.Active().autoTile?MF_CHECKED:0),13,L"Автоматически раскладывать выбранные окна");
    AppendMenuW(menu,MF_STRING,14,L"Сохранить расположение");
    Popup(menu,[this,list,attached,elsewhere](UINT id){
        if(id>=1000&&(!app_.config.enabled||app_.preview)){
            MessageBoxW(hwnd,L"Сначала нажми «Применить окружение». До этого обычные окна не изменяются.",L"Управление окнами",MB_OK);return;
        }
        if(id>=1000&&id<1000+list.size()){
            if(!app_.windows.Attach(list[id-1000].hwnd,app_.config.active))MessageBoxW(hwnd,L"Это окно недоступно для управления. Оно остаётся обычным окном Windows.",L"Окно",MB_OK);
        }else if(id>=3000&&id<3000+attached.size())app_.windows.Detach(attached[id-3000]);
        else if(id>=4000&&id<4000+elsewhere.size())app_.windows.MoveToScene(elsewhere[id-4000],app_.config.active);
        else if(id>=5000&&id<5000+attached.size())app_.windows.ToggleFloating(attached[id-5000]);
        else if(id>=10&&id<=12){app_.Active().layout=id==10?L"columns":id==11?L"master":L"grid";app_.Active().autoTile=true;}
        else if(id==13)app_.Active().autoTile=!app_.Active().autoTile;
        if(app_.config.enabled){app_.windows.Tile(app_.monitors);app_.windows.Capture();}
        app_.SaveSoon();app_.Refresh();
    });
}
void Surface::CardMenu(const std::wstring&id){auto*c=app_.FindCard(id);if(!c)return;HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"Редактировать…");if(c->type==L"site"||c->type==L"link"){AppendMenuW(m,MF_STRING,2,L"Открыть в отдельном окне");AppendMenuW(m,MF_STRING,3,L"Открыть в обычном браузере");if(app_.web.Exists(id))AppendMenuW(m,MF_STRING,4,L"Закрыть сайт");HMENU bg=CreatePopupMenu();AppendMenuW(bg,MF_STRING|(c->background==L"keep"?MF_CHECKED:0),5,L"Продолжать работу");AppendMenuW(bg,MF_STRING|(c->background==L"sleep"?MF_CHECKED:0),6,L"Попытаться усыпить");AppendMenuW(bg,MF_STRING|(c->background==L"unload"?MF_CHECKED:0),7,L"Выгрузить, затем загрузить заново");AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(bg),L"В другом пространстве");}HMENU monitors=CreatePopupMenu();for(size_t i=0;i<app_.monitors.size();++i)AppendMenuW(monitors,MF_STRING|(c->monitor==static_cast<int>(i)?MF_CHECKED:0),static_cast<UINT_PTR>(100+i),(L"Экран "+std::to_wstring(i+1)).c_str());AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(monitors),L"Переместить карточку на экран");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,8,L"Удалить карточку");Popup(m,[this,id](UINT cmd){auto*card=app_.FindCard(id);if(!card)return;if(cmd==1)app_.EditCard(id);else if(cmd==2)app_.OpenCard(id,*this,true);else if(cmd==3)LaunchTarget(card->target,L"");else if(cmd==4)app_.web.Close(id);else if(cmd>=5&&cmd<=7){card->background=cmd==5?L"keep":cmd==6?L"sleep":L"unload";app_.web.SetPolicy(id,card->background);}else if(cmd>=100&&cmd<100+app_.monitors.size()){card->monitor=static_cast<int>(cmd-100);card->monitorId=app_.monitors[card->monitor].id;app_.web.Position(id,app_.boards[card->monitor]->hwnd,{0,0,100,100},false);}else if(cmd==8){app_.web.Close(id);std::erase_if(app_.Active().cards,[&](const Card&c){return c.id==id;});}app_.SaveSoon();app_.Refresh();});}
void Surface::MouseDown(POINT p){if(element!=UiElement::Power&&app_.PowerPanelVisible())app_.HidePowerPanel();float scale=96.f/GetDpiForWindow(hwnd),x=p.x*scale,y=p.y*scale;down_=p;
    // Resize takes priority over card content and embedded browser controls.
    for(auto it=cards_.rbegin();it!=cards_.rend();++it){auto box=it->second;if(Contains(Viewport(),x,y)&&x>=box.x+box.w-30&&x<box.x+box.w&&y>=box.y+box.h-30&&y<box.y+box.h){auto* card=app_.FindCard(it->first);if(!card)break;selected_.clear();selected_.insert(card->id);drag_=card->id;resize_=true;before_.clear();before_[card->id]=card->box;app_.web.HideEmbedded(hwnd);SetCapture(hwnd);Invalidate();return;}}
    for(auto it=hits_.rbegin();it!=hits_.rend();++it)if(ContainsHit(*it,x,y)&&(!it->canvas||Contains(Viewport(),x,y))){if(!it->draggable.empty()&&(GetKeyState(VK_SHIFT)&0x8000)){if(selected_.contains(it->draggable))selected_.erase(it->draggable);else selected_.insert(it->draggable);Invalidate();return;}pressedHit_=*it;SetCapture(hwnd);Invalidate();return;}
    for(auto it=cards_.rbegin();it!=cards_.rend();++it)if(Contains(it->second,x,y)&&Contains(Viewport(),x,y)){auto*c=app_.FindCard(it->first);if(!c)break;Box b=it->second;if((c->type==L"launcher"&&app_.config.ui.compactLaunchers)||(y<b.y+56&&x<b.x+b.w-48)){
        if(GetKeyState(VK_SHIFT)&0x8000){if(selected_.contains(c->id))selected_.erase(c->id);else selected_.insert(c->id);Invalidate();return;}if(!selected_.contains(c->id)){selected_.clear();selected_.insert(c->id);}drag_=c->id;resize_=x>b.x+b.w-20&&y>b.y+b.h-20;before_.clear();for(auto&card:app_.Active().cards)if(selected_.contains(card.id)||(!c->group.empty()&&card.group==c->group))before_[card.id]=card.box;app_.web.HideEmbedded(hwnd);SetCapture(hwnd);Invalidate();return;}break;}
    selected_.clear();Invalidate();}
void Surface::SetPointerCursor(bool hand,bool resize){
    const size_t index=resize?2:hand?1:0;
    auto& cursor=pointerCursors_[index];
    if(!cursor)cursor=LoadCursorW(nullptr,resize?IDC_SIZENWSE:hand?IDC_HAND:IDC_ARROW);
    pointerCursor_=cursor;
    // Keep WM_SETCURSOR and WM_MOUSEMOVE on the same shared cursor. Changing
    // arrow -> hand -> arrow on every hardware mouse event is expensive live.
    if(cursor&&GetCursor()!=cursor)SetCursor(cursor);
}
void Surface::MouseMove(POINT p,WPARAM){LiveProbeScope profile(ProbeArea::MouseMove);{LiveProbeScope scope(ProbeArea::PointerNetwork);TrackNetwork(p);}
    if(pressedHit_&&!pressedHit_->draggable.empty()&&(std::abs(p.x-down_.x)>=GetSystemMetricsForDpi(SM_CXDRAG,GetDpiForWindow(hwnd))||std::abs(p.y-down_.y)>=GetSystemMetricsForDpi(SM_CYDRAG,GetDpiForWindow(hwnd)))){
        if(auto* card=app_.FindCard(pressedHit_->draggable)){if(!selected_.contains(card->id)){selected_.clear();selected_.insert(card->id);}drag_=card->id;resize_=false;before_.clear();for(const auto& item:app_.Active().cards)if(selected_.contains(item.id)||(!card->group.empty()&&item.group==card->group))before_[item.id]=item.box;app_.web.HideEmbedded(hwnd);}pressedHit_.reset();
    }
    float scale=96.f/GetDpiForWindow(hwnd);if(panning_){panX_-=((p.x-down_.x)*scale)/Zoom();panY_-=((p.y-down_.y)*scale)/Zoom();down_=p;Invalidate();return;}if(!drag_.empty()){float dx=(p.x-down_.x)*scale/Zoom(),dy=(p.y-down_.y)*scale/Zoom();for(auto&c:app_.Active().cards){auto it=before_.find(c.id);if(it==before_.end())continue;c.box=it->second;if(resize_&&c.id==drag_){auto minimum=MinimumCardSize(c);c.box.w=std::max(minimum.w,c.box.w+dx);c.box.h=std::max(minimum.h,c.box.h+dy);}else if(!resize_){c.box.x=std::max(0.f,c.box.x+dx);c.box.y=std::max(0.f,c.box.y+dy);}}Invalidate();return;}
    if(!bar&&!rail){std::wstring next;for(auto it=cards_.rbegin();it!=cards_.rend();++it)if(Contains(it->second,p.x*scale,p.y*scale)){auto* card=app_.FindCard(it->first);if(card&&card->type==L"clock"&&card->appearance.variant==L"floating")next=card->id;break;}if(next!=hoverCard_){for(const auto& [id,box]:cards_)if(id==next||id==hoverCard_){float dpi=GetDpiForWindow(hwnd)/96.f;RECT dirty{static_cast<LONG>((box.x-4)*dpi),static_cast<LONG>((box.y-4)*dpi),static_cast<LONG>((box.x+box.w+4)*dpi),static_cast<LONG>((box.y+box.h+4)*dpi)};InvalidateRect(hwnd,&dirty,FALSE);}hoverCard_=next;}}
    int hover=-1;{LiveProbeScope scope(ProbeArea::PointerHit);for(size_t i=0;i<hits_.size();++i)if(ContainsHit(hits_[i],p.x*scale,p.y*scale)&&(!hits_[i].canvas||Contains(Viewport(),p.x*scale,p.y*scale)))hover=static_cast<int>(i);}if(hover!=hover_){LiveProbeScope scope(ProbeArea::PointerHover);auto dirty=[&](int index){if(index<0||index>=static_cast<int>(hits_.size()))return;auto box=hits_[index].box;float dpi=GetDpiForWindow(hwnd)/96.f;RECT rect{static_cast<LONG>((box.x-2)*dpi),static_cast<LONG>((box.y-2)*dpi),static_cast<LONG>((box.x+box.w+2)*dpi),static_cast<LONG>((box.y+box.h+2)*dpi)};InvalidateRect(hwnd,&rect,FALSE);};if(!(rail&&app_.config.ui.railOrbit)){dirty(hover_);dirty(hover);}hover_=hover;HoverOrbit(hover);if((rail||element==UiElement::Power)&&tooltip_){LiveProbeScope tooltipScope(ProbeArea::TooltipPop);SendMessageW(tooltip_,TTM_POP,0,0);}}LiveProbeScope cursorScope(ProbeArea::PointerCursor);bool resizeHover=false;for(const auto& [id,box]:cards_)if(p.x*scale>=box.x+box.w-30&&p.x*scale<box.x+box.w&&p.y*scale>=box.y+box.h-30&&p.y*scale<box.y+box.h)resizeHover=true;SetPointerCursor(hover>=0,resizeHover);}
void Surface::MouseUp(POINT point){
    if(pressedHit_){auto hit=std::move(*pressedHit_);pressedHit_.reset();if(GetCapture()==hwnd)ReleaseCapture();Invalidate();float scale=96.f/GetDpiForWindow(hwnd);if(ContainsHit(hit,point.x*scale,point.y*scale)&&(!hit.canvas||Contains(Viewport(),point.x*scale,point.y*scale)))hit.action();return;}
    if(!drag_.empty()){for(auto&c:app_.Active().cards)if(before_.contains(c.id)){c.box.x=std::round(c.box.x/8)*8;c.box.y=std::round(c.box.y/8)*8;}drag_.clear();before_.clear();ReleaseCapture();app_.SaveSoon();Invalidate();SyncWeb();}if(panning_){panning_=false;ReleaseCapture();Invalidate();SyncWeb();}
}
LRESULT CALLBACK Surface::Proc(HWND h,UINT m,WPARAM w,LPARAM l){auto*s=reinterpret_cast<Surface*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(!s)return DefWindowProcW(h,m,w,l);LiveProbeScope profile(ProbeArea::SurfaceMessages);
    if(m==WM_MOUSEMOVE)LiveProbeCount(ProbeCount::SurfaceMoves);if(m==WM_NCHITTEST)LiveProbeCount(ProbeCount::SurfaceHitTests);if(m==WM_PAINT)LiveProbeCount(ProbeCount::SurfacePaints);if(m==WM_TIMER)LiveProbeCount(ProbeCount::SurfaceTimers);if(m==WM_WINDOWPOSCHANGED)LiveProbeCount(ProbeCount::SurfacePositions);if(m==WM_NOTIFY)LiveProbeCount(ProbeCount::SurfaceNotifications);
    if(m==WM_WINDOWPOSCHANGED&&(s->bar||s->rail)&&s->app_.events&&IsWindowVisible(h)&&(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TOPMOST))s->app_.SchedulePanelSync();
    if(m==WM_CANCELMODE||(m==WM_CAPTURECHANGED&&reinterpret_cast<HWND>(l)!=h)){s->pressedHit_.reset();bool moved=!s->drag_.empty()||s->panning_;s->drag_.clear();s->before_.clear();s->panning_=false;s->Invalidate();if(m==WM_CANCELMODE&&GetCapture()==h)ReleaseCapture();if(moved){s->app_.SaveSoon();s->SyncWeb();}}
    if(m==WM_SETTINGCHANGE){s->pointerCursors_={};s->pointerCursor_=nullptr;}
    if(m==WM_SETCURSOR&&reinterpret_cast<HWND>(w)==h&&LOWORD(l)==HTCLIENT){
        // DefWindowProc would reset the class cursor to an arrow before every
        // mouse move, even while staying over one clickable ring segment.
        if(!s->pointerCursor_)s->SetPointerCursor(false);
        else if(GetCursor()!=s->pointerCursor_)SetCursor(s->pointerCursor_);
        return TRUE;
    }
    if(m==WM_NCHITTEST&&s->rail&&s->app_.config.ui.railOrbit){LiveProbeScope hitTestProfile(ProbeArea::HitTest);POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);float dpi=96.f/GetDpiForWindow(h);for(const auto& hit:s->hits_)if(s->ContainsHit(hit,p.x*dpi,p.y*dpi))return HTCLIENT;return HTTRANSPARENT;}
    if(m==WM_PAINT){s->Paint();return 0;}if(m==WM_ERASEBKGND)return 1;if(m==WM_SIZE){bool changed=!s->rt_;if(s->panelBitmap_.Dc()&&(s->panelBitmap_.Size().cx!=LOWORD(l)||s->panelBitmap_.Size().cy!=HIWORD(l))){s->Discard();changed=true;}if(s->rt_){ComPtr<ID2D1HwndRenderTarget>target;if(SUCCEEDED(s->rt_.As(&target))){auto size=target->GetPixelSize();changed=size.width!=LOWORD(l)||size.height!=HIWORD(l);if(changed)target->Resize(D2D1::SizeU(LOWORD(l),HIWORD(l)));}}if(changed){s->Invalidate();s->SyncWeb();}return 0;}if(m==WM_LBUTTONDOWN){SetFocus(h);s->MouseDown({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;}if(m==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);s->MouseMove({GET_X_LPARAM(l),GET_Y_LPARAM(l)},w);return 0;}if(m==WM_LBUTTONUP||(m==WM_MBUTTONUP&&s->panning_)){s->MouseUp({GET_X_LPARAM(l),GET_Y_LPARAM(l)});return 0;}if(m==WM_MBUTTONDOWN&&!s->bar&&!s->rail){s->panning_=true;s->down_={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(h);s->app_.web.HideEmbedded(h);return 0;}
    if(m==WM_CONTEXTMENU){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(p.x==-1&&p.y==-1){if(!s->bar&&!s->rail)s->ShowSettings();return 0;}ScreenToClient(h,&p);float f=96.f/GetDpiForWindow(h);for(auto it=s->hits_.rbegin();it!=s->hits_.rend();++it)if(s->ContainsHit(*it,p.x*f,p.y*f)&&(!it->canvas||Contains(s->Viewport(),p.x*f,p.y*f))){auto context=it->context;if(context){context();return 0;}if(!it->canvas)return 0;break;}if(s->bar||s->rail)return 0;for(auto&[id,b]:s->cards_)if(Contains(b,p.x*f,p.y*f)&&Contains(s->Viewport(),p.x*f,p.y*f)){s->CardMenu(id);return 0;}s->ShowSettings();return 0;}
    if(m==WM_NOTIFY&&reinterpret_cast<NMHDR*>(l)->code==TTN_SHOW){RoundWindow(reinterpret_cast<NMHDR*>(l)->hwndFrom,10);return 0;}if(m==WM_NOTIFY&&reinterpret_cast<NMHDR*>(l)->code==TTN_GETDISPINFOW){if(s->rail||s->element==UiElement::Power){s->tooltipText_.clear();POINT p{};GetCursorPos(&p);ScreenToClient(h,&p);float f=96.f/GetDpiForWindow(h);for(const auto& hit:s->hits_)if(s->ContainsHit(hit,p.x*f,p.y*f)){s->tooltipText_=hit.tooltip;break;}reinterpret_cast<NMTTDISPINFOW*>(l)->lpszText=s->tooltipText_.data();return 0;}auto status=s->app_.fileIndex.Status();s->tooltipText_=L"Индекс файлов: "+std::to_wstring(status.entries)+L" записей"+(status.paused?L" · на паузе":status.busy?L" · обновляется":L"")+L"\n"+status.current+L"\n\n"+s->app_.web.Activity();reinterpret_cast<NMTTDISPINFOW*>(l)->lpszText=s->tooltipText_.data();return 0;}
    if(m==WM_TIMER&&w==82){s->TickNetwork();return 0;}
    if(m==WM_TIMER&&w==83){s->TickOrbit();return 0;}
    if(m==WM_TIMER&&w==81){KillTimer(h,81);s->networkPending_=false;s->TrackNetwork(s->networkPointer_);return 0;}if(m==WM_MOUSELEAVE){LiveProbeCount(ProbeCount::MouseLeaves);s->TrackNetwork({},true);if(s->rail&&s->app_.config.ui.railOrbit){s->hover_=-1;s->HoverOrbit(-1);if(s->tooltip_)SendMessageW(s->tooltip_,TTM_POP,0,0);}if(!s->hoverCard_.empty()){s->hoverCard_.clear();s->Invalidate();}return 0;}
    if(m==WM_MOUSEWHEEL&&s->rail){if(s->app_.config.ui.railOrbit)s->ScrollOrbit(GET_WHEEL_DELTA_WPARAM(w));else{s->panY_=std::max(0.f,s->panY_+(GET_WHEEL_DELTA_WPARAM(w)>0?-120.f:120.f));s->Invalidate();}return 0;}if(m==WM_MOUSEWHEEL&&!s->bar){int delta=GET_WHEEL_DELTA_WPARAM(w);if(GetKeyState(VK_CONTROL)&0x8000){s->app_.Active().zoom=std::clamp(s->app_.Active().zoom+(delta>0?0.1f:-0.1f),0.5f,1.5f);s->app_.SaveSoon();}else if(GetKeyState(VK_SHIFT)&0x8000)s->panX_=std::max(0.f,s->panX_-delta/2.f/s->Zoom());else s->panY_=std::max(0.f,s->panY_-delta/2.f/s->Zoom());s->app_.web.HideEmbedded(h);s->Invalidate();return 0;}
    if(m==WM_KEYDOWN){if(w==VK_ESCAPE){s->app_.HidePowerPanel();s->app_.launcher.Hide();return 0;}if(w==VK_F5){s->Invalidate();return 0;}if(w==VK_F10&&(GetKeyState(VK_SHIFT)&0x8000)){s->ShowSettings();return 0;}if(w=='Q'&&(GetKeyState(VK_CONTROL)&0x8000)){s->app_.Quit();return 0;}if(w=='N'&&(GetKeyState(VK_CONTROL)&0x8000)){s->app_.AddCard(L"note",s->monitor);return 0;}if(w=='G'&&(GetKeyState(VK_CONTROL)&0x8000)){auto group=(GetKeyState(VK_SHIFT)&0x8000)?L"":NewId();for(auto&c:s->app_.Active().cards)if(s->selected_.contains(c.id))c.group=group;s->app_.SaveSoon();s->Invalidate();return 0;}}
    if(m==WM_ACTIVATE||m==WM_SHOWWINDOW)s->app_.SyncSystemWidget();
    if(m==WM_APP+10&&w==ABN_FULLSCREENAPP){++s->app_.fullscreenNotices;s->app_.SchedulePanelSync();return 0;}
    if(m==WM_SYSCOMMAND&&(w&0xfff0)==SC_MINIMIZE&&s->app_.config.enabled)return 0;
    if(m==WM_DPICHANGED){s->Discard();auto*r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);s->Place(!s->app_.config.enabled||s->app_.preview);return 0;}if(m==WM_CLOSE){s->app_.Quit();return 0;}if(m==WM_GETMINMAXINFO&&!s->bar&&!s->rail){auto*p=reinterpret_cast<MINMAXINFO*>(l);p->ptMinTrackSize={960,680};return 0;}return DefWindowProcW(h,m,w,l);
}
}
