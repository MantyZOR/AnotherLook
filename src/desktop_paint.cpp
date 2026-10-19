#include "ui.h"
#include <commctrl.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <chrono>
namespace gv {
namespace {
D2D1_RECT_F Rect(Box b){return D2D1::RectF(b.x,b.y,b.x+b.w,b.y+b.h);}
std::wstring Clock(){SYSTEMTIME t;GetLocalTime(&t);wchar_t value[8];swprintf_s(value,L"%02u:%02u",t.wHour,t.wMinute);return value;}
std::wstring Date(){SYSTEMTIME t;GetLocalTime(&t);const wchar_t* months[]={L"января",L"февраля",L"марта",L"апреля",L"мая",L"июня",L"июля",L"августа",L"сентября",L"октября",L"ноября",L"декабря"};return std::to_wstring(t.wDay)+L" "+months[t.wMonth-1];}
}
void Surface::Image(const fs::path& path,Box box,float opacity,bool topAligned){
    std::error_code error;if(!fs::exists(path,error))return;auto modified=fs::last_write_time(path,error);auto& bitmap=bitmaps_[path.wstring()];
    if(!bitmap.value||bitmap.modified!=modified){
        ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(decoder->GetFrame(0,&frame))||FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return;
        bitmap.value.Reset();if(FAILED(rt_->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&bitmap.value)))return;bitmap.modified=modified;
    }
    auto size=bitmap.value->GetSize();if(topAligned){float scale=box.w/size.width,sourceHeight=std::min(size.height,box.h/scale);Box target{box.x,box.y,box.w,sourceHeight*scale};rt_->DrawBitmap(bitmap.value.Get(),Rect(target),opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,D2D1::RectF(0,0,size.width,sourceHeight));return;}
    float scale=std::max(box.w/size.width,box.h/size.height),sw=box.w/scale,sh=box.h/scale;
    auto source=D2D1::RectF((size.width-sw)/2,(size.height-sh)/2,(size.width+sw)/2,(size.height+sh)/2);
    rt_->DrawBitmap(bitmap.value.Get(),Rect(box),opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
}
void Surface::DrawBar(float width,float height){
    float radius=UiRadius(app_.config.ui,UiElement::Navigation);Fill({0,0,width,height},Ink(UiTheme().panel),radius);Stroke({.5f,.5f,width-1,height-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));
    bool vertical=app_.config.ui.barVertical;float cursor=8,available=(vertical?height:width)-64;size_t count=0;
    float stride=vertical?46.f:112.f;for(const auto& scene:app_.config.scenes){if(cursor+stride>available)break;
        Box cell=vertical?Box{8,cursor,width-16,40}:Box{cursor,8,stride-4,height-16};
        IconButton(SceneGlyph(scene),scene.name,cell,[this,id=scene.id]{app_.SwitchScene(id);},scene.id==app_.config.active);
        hits_.back().context=[this,id=scene.id]{SceneMenu(id);};
        cursor+=stride;++count;
    }
    if(count<app_.config.scenes.size()){
        Box more=vertical?Box{8,std::max(8.f,height-104),width-16,40}:Box{std::max(8.f,width-104),8,40,height-16};
        IconButton(Glyph::More,L"",more,[this]{HMENU menu=CreatePopupMenu();for(size_t i=0;i<app_.config.scenes.size();++i)AppendMenuW(menu,MF_STRING|(app_.config.scenes[i].id==app_.config.active?MF_CHECKED:0),i+1,app_.config.scenes[i].name.c_str());Popup(menu,[this](UINT id){if(id&&id<=app_.config.scenes.size())app_.SwitchScene(app_.config.scenes[id-1].id);});});
    }
    Box add=vertical?Box{8,height-48,width-16,40}:Box{width-48,8,40,height-16};IconButton(Glyph::Add,L"",add,[this]{app_.AddScene();});
}
void Surface::DrawTool(float width,float height){
    if(element==UiElement::Power){float radius=UiRadius(app_.config.ui,element);Fill({0,0,width,height},Ink(UiTheme().panel),radius);Stroke({.5f,.5f,width-1,height-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));bool vertical=app_.config.ui.powerVertical;const Glyph icons[]={Glyph::Power,Glyph::Restart,Glyph::Moon};const wchar_t* labels[]={L"Выключить компьютер",L"Перезагрузить",L"Спящий режим"};for(int i=0;i<3;++i){Box box=vertical?Box{8.f,8.f+i*56,width-16,48}:Box{8.f+i*56,8,48,height-16};IconButton(icons[i],L"",box,[this,i]{app_.PowerAction(i,hwnd);});hits_.back().tooltip=labels[i];}return;}
    float radius=UiRadius(app_.config.ui,element);
    if(element==UiElement::Search){
        bool hover=hover_==static_cast<int>(hits_.size());Box button{0,0,width,height};
        Fill(button,Ink(hover?UiTheme().field:UiTheme().panel),radius);
        Stroke({.5f,.5f,width-1,height-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));
        auto color=Ink(UiTheme().text);
        if(width<150)GlyphIcon(Glyph::Search,{(width-20)/2,(height-20)/2,20,20},color);
        else{
            constexpr std::wstring_view label=L"Программы, папки, файлы";ComPtr<IDWriteTextLayout> layout;
            app_.dwrite->CreateTextLayout(label.data(),static_cast<UINT32>(label.size()),app_.Font(15*(scaledUi_?1:app_.config.ui.textScale),500),1000,height,&layout);
            DWRITE_TEXT_METRICS metrics{};if(layout)layout->GetMetrics(&metrics);
            float textWidth=std::min(metrics.width,width-42),start=(width-textWidth-28)/2;
            GlyphIcon(Glyph::Search,{start,(height-20)/2,20,20},color);
            CenterText(label,{start+28,0,textWidth+1,height},15,color,500);
        }
        hits_.push_back({button,[this]{app_.ToggleOverview();}});
    }else{
        Fill({0,0,width,height},Ink(UiTheme().panel),radius);Stroke({.5f,.5f,width-1,height-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));
        if(app_.config.ui.showActivity)IconButton(Glyph::Activity,L"",{6,6,40,height-12},[this]{ShowActivity();},app_.web.Busy()||app_.fileIndex.Status().busy);
        IconButton(Glyph::Settings,L"",{width-46,6,40,height-12},[this]{ShowSettings();});
    }
}
void Surface::DrawRail(float width,float height){
    if(app_.config.ui.railOrbit){DrawOrbit(width,height);return;}
    if(orbit_.Running()){orbit_.Finish();KillTimer(hwnd,83);}
    auto& ui=app_.config.ui;float radius=UiRadius(ui,UiElement::Windows);Fill({0,0,width,height},Ink(UiTheme().panel),radius);Stroke({.5f,.5f,width-1,height-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));
    auto groups=app_.WindowGroups(monitor);bool labels=ui.railLabels;auto grid=LayoutRail(static_cast<int>(groups.size()),ui.railVertical,labels,{0,0,width,height});
    int first=std::clamp(static_cast<int>(panY_/48),0,std::max(0,static_cast<int>(groups.size())-grid.capacity));panY_=static_cast<float>(first*48);
    if(groups.empty()){GlyphIcon(Glyph::Windows,{(width-20)/2,(height-20)/2,20,20},Ink(UiTheme().muted,.6f));return;}
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);rt_->PushAxisAlignedClip(Rect({4,4,width-8,height-8}),D2D1_ANTIALIAS_MODE_ALIASED);
    for(int i=first;i<std::min(static_cast<int>(groups.size()),first+grid.capacity);++i){const auto& group=groups[i];auto row=grid.Cell(i-first);row={row.x+2,row.y+2,row.w-4,row.h-4};bool active=std::any_of(group.windows.begin(),group.windows.end(),[&](const auto& window){return window.pid==foreground;});
        Button(L"",row,[this,group]{if(group.windows.size()==1){auto window=group.windows.front().hwnd;if(!IsWindow(window))return;if(window==GetForegroundWindow())ShowWindowAsync(window,SW_MINIMIZE);else app_.ActivateWindow(window);}else{HMENU menu=CreatePopupMenu();for(size_t j=0;j<group.windows.size();++j){AppendMenuW(menu,MF_STRING,100+j,group.windows[j].title.c_str());MenuIcon(menu,100+static_cast<UINT>(j),app_.icons.GetWindow(group.windows[j].hwnd).get());}Popup(menu,[this,group](UINT id){if(id>=100&&id<100+group.windows.size())app_.ActivateWindow(group.windows[id-100].hwnd);});}},false,true);
        hits_.back().context=[this,group]{OpenWindowMenu(group.windows);};hits_.back().tooltip=group.name;for(const auto& item:group.windows)hits_.back().tooltip+=L"\n"+item.title;float x=labels?row.x+10:row.x+(row.w-24)/2;WindowIcon(group.windows.front(),{x,row.y+(row.h-24)/2,24,24});
        if(active){if(ui.railVertical)Fill({row.x-2,row.y+(row.h-16)/2,2,16},Ink(UiTheme().accent),1);else Fill({row.x+(row.w-16)/2,row.y+row.h,16,2},Ink(UiTheme().accent),1);}
        if(labels){Text(group.name,{row.x+46,row.y+6,row.w-52,22},15,Ink(UiTheme().text),500);Text(group.windows.size()>1?std::to_wstring(group.windows.size())+L" окна":group.windows.front().title,{row.x+46,row.y+29,row.w-52,20},12,Ink(UiTheme().muted),400,false,FontRole::Body);}
    }rt_->PopAxisAlignedClip();
}
void Surface::SyncWeb(){
    if(bar||rail||!IsWindowVisible(hwnd))return;if(!drag_.empty()||panning_||popupOpen_){app_.web.HideEmbedded(hwnd);return;}
    float scale=GetDpiForWindow(hwnd)/96.f;auto viewport=Viewport();
    for(auto& card:app_.Active().cards)if(card.type==L"site"&&CardMonitor(card)==monitor&&app_.web.Exists(card.id)&&!app_.web.Detached(card.id)){
        auto placement=PlaceEmbeddedWeb(CanvasBox(card),viewport,scale);app_.web.Position(card.id,hwnd,placement.bounds,placement.visible);
    }
}
void Surface::Paint(){
    if(bar||rail)++app_.panelPaints;else ++app_.paints;
    PAINTSTRUCT paint;BeginPaint(hwnd,&paint);RECT client;GetClientRect(hwnd,&client);UINT dpi=GetDpiForWindow(hwnd);float width=client.right*96.f/dpi,height=client.bottom*96.f/dpi;if(!client.right||!client.bottom){EndPaint(hwnd,&paint);return;}
    if((bar||rail)&&panelBitmap_.Dc()&&(panelBitmap_.Size().cx!=client.right||panelBitmap_.Size().cy!=client.bottom))Discard();
    if(!rt_&&(bar||rail)){
        ComPtr<ID2D1DCRenderTarget> target;
        auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),static_cast<float>(dpi),static_cast<float>(dpi));
        if(!panelBitmap_.Resize(client.right,client.bottom)||FAILED(app_.d2d->CreateDCRenderTarget(&properties,&target))||FAILED(target->BindDC(panelBitmap_.Dc(),&client))){Discard();EndPaint(hwnd,&paint);return;}
        target.As(&rt_);rt_->CreateSolidColorBrush(Ink(UiTheme().text),&brush_);
    }
    if(!rt_){bool animated=!bar&&!rail&&networkAnimating_;auto properties=D2D1::RenderTargetProperties(app_.config.softwareRendering&&!animated?D2D1_RENDER_TARGET_TYPE_SOFTWARE:D2D1_RENDER_TARGET_TYPE_DEFAULT);properties.dpiX=properties.dpiY=static_cast<float>(dpi);ComPtr<ID2D1HwndRenderTarget> target;
        if(FAILED(app_.d2d->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(client.right,client.bottom),D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS),&target))){EndPaint(hwnd,&paint);return;}target.As(&rt_);rt_->CreateSolidColorBrush(Ink(UiTheme().text),&brush_);
    }
    rt_->BeginDraw();bool clipped=!panelBitmap_.Dc()&&!IsRectEmpty(&paint.rcPaint);if(clipped){float scale=96.f/dpi;rt_->PushAxisAlignedClip(D2D1::RectF(paint.rcPaint.left*scale,paint.rcPaint.top*scale,paint.rcPaint.right*scale,paint.rcPaint.bottom*scale),D2D1_ANTIALIAS_MODE_ALIASED);}rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);hits_.clear();cards_.clear();auto& scene=app_.Active();auto accent=Ink(UiTheme().accent),muted=Ink(UiTheme().muted),foreground=Ink(UiTheme().text);if(bar||rail||(app_.config.wallpaperEnabled&&!scene.wallpaper.empty()))rt_->Clear(rail&&app_.config.ui.railOrbit?D2D1::ColorF(0,0.f):Ink((bar||rail)?UiTheme().panel:UiTheme().canvas));
    auto drawUi=[&](Box box,auto draw){float scale=app_.config.ui.textScale;size_t first=hits_.size();scaledUi_=true;rt_->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(box.x,box.y));draw(box.w/scale,box.h/scale);rt_->SetTransform(D2D1::Matrix3x2F::Identity());scaledUi_=false;for(size_t i=first;i<hits_.size();++i){auto& hit=hits_[i];hit.box={box.x+hit.box.x*scale,box.y+hit.box.y*scale,hit.box.w*scale,hit.box.h*scale};}};
    if(bar)drawUi({0,0,width,height},[&](float w,float h){if(element==UiElement::Navigation)DrawBar(w,h);else DrawTool(w,h);});else if(rail)drawUi({0,0,width,height},[&](float w,float h){DrawRail(w,h);});else{
        if(app_.config.wallpaperEnabled&&!scene.wallpaper.empty()){Image(scene.wallpaper,{0,0,width,height});Fill({0,0,width,height},Ink(UiTheme().canvas,UiTheme().light?.45f:.6f));}
        else DrawNetwork(width,height,clipped?Box{paint.rcPaint.left*96.f/dpi,paint.rcPaint.top*96.f/dpi,(paint.rcPaint.right-paint.rcPaint.left)*96.f/dpi,(paint.rcPaint.bottom-paint.rcPaint.top)*96.f/dpi}:Box{});
        if(!app_.config.enabled||app_.preview){
            drawUi(app_.PanelBox(UiElement::Navigation,monitor,{0,0,width,height}),[&](float w,float h){DrawBar(w,h);});
            if(app_.config.ui.railPosition!=L"hidden")drawUi(app_.PanelBox(UiElement::Windows,monitor,{0,0,width,height}),[&](float w,float h){DrawRail(w,h);});
            auto previous=element;for(auto kind:{UiElement::Search,UiElement::Settings}){element=kind;drawUi(app_.PanelBox(kind,monitor,{0,0,width,height}),[&](float w,float h){DrawTool(w,h);});}element=previous;
        }
        auto viewport=Viewport();rt_->PushAxisAlignedClip(Rect(viewport),D2D1_ANTIALIAS_MODE_ALIASED);size_t count=0;drawingCards_=true;
        for(auto& card:scene.cards)if(CardMonitor(card)==monitor){auto box=CanvasBox(card);if(box.x<viewport.x+viewport.w&&box.y<viewport.y+viewport.h&&box.x+box.w>viewport.x&&box.y+box.h>viewport.y)DrawCard(card,box);++count;}
        drawingCards_=false;
        if(!count){float x=std::max(70.f,(width-760)/2),y=std::max(230.f,(height-420)/2);
            Text(Clock(),{x,y,760,130},104,foreground,300);Text(scene.gaming?L"Время для себя.":L"Твоё свободное пространство.",{x+5,y+146,754,54},32,foreground,400);
            Text(scene.gaming?L"Игры, музыка и любимые приложения.":L"Добавь сайты, заметки или приложения.",{x+7,y+210,740,36},18,muted,400,false,FontRole::Body);
            IconButton(Glyph::Apps,L"Приложения",{x+6,y+276,222,50},[this](){app_.launcher.Show();},true);IconButton(Glyph::Add,L"Добавить",{x+244,y+276,174,50},[this](){AddMenu();},false,false);
        }
        rt_->PopAxisAlignedClip();
        if(app_.config.ui.dockPosition!=L"hidden"){
            drawUi(InterfaceBox(app_.config.ui,UiElement::Dock,{0,0,width,height}),[&](float w,float h){
                bool vertical=app_.config.ui.dockVertical,compact=!vertical&&w<420;float radius=UiRadius(app_.config.ui,UiElement::Dock);
                float addWidth=compact?44.f:136.f,windowsWidth=compact?44.f:106.f;Box add=vertical?Box{0,0,w,46}:Box{0,0,addWidth,h},windows=vertical?Box{0,54,w,46}:Box{addWidth+8,0,windowsWidth,h},zoom=vertical?Box{0,108,w,46}:Box{addWidth+windowsWidth+16,0,w-addWidth-windowsWidth-16,h};
                for(auto box:{add,windows,zoom}){Fill(box,Ink(UiTheme().panel),radius);Stroke({box.x+.5f,box.y+.5f,box.w-1,box.h-1},Ink(UiTheme().border),std::max(0.f,radius-.5f));}
                IconButton(Glyph::Add,compact?L"":L"Добавить",{add.x+4,add.y+4,add.w-8,add.h-8},[this]{AddMenu();});
                IconButton(Glyph::Layout,compact?L"":L"Окна",{windows.x+4,windows.y+4,windows.w-8,windows.h-8},[this]{WindowsMenu();});
                IconButton(Glyph::Minus,L"",{zoom.x+4,zoom.y+4,32,zoom.h-8},[this]{app_.Active().zoom=std::max(.5f,app_.Active().zoom-.1f);app_.SaveSoon();Invalidate();});
                Button(std::to_wstring(static_cast<int>(std::lround(scene.zoom*100)))+L"%",{zoom.x+40,zoom.y+4,zoom.w-80,zoom.h-8},[this]{app_.Active().zoom=1;app_.SaveSoon();Invalidate();},false,true);
                IconButton(Glyph::Add,L"",{zoom.x+zoom.w-36,zoom.y+4,32,zoom.h-8},[this]{app_.Active().zoom=std::min(1.5f,app_.Active().zoom+.1f);app_.SaveSoon();Invalidate();});

            });
        }
    }
    if(clipped)rt_->PopAxisAlignedClip();auto presentStarted=profilePaint_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};auto result=rt_->EndDraw();if(profilePaint_)paintMs_[4]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-presentStarted).count();if(SUCCEEDED(result)&&panelBitmap_.Dc()){if(!(rail&&app_.config.ui.railOrbit))panelBitmap_.RoundAlpha(UiRadius(app_.config.ui,element)*app_.config.ui.textScale*dpi/96.f);panelBitmap_.Present(hwnd);}if(result==D2DERR_RECREATE_TARGET)Discard();EndPaint(hwnd,&paint);
    if(!bar)SyncWeb();if((bar||rail)&&tooltip_){float scale=dpi/96.f;TOOLINFOW info{sizeof(info)};info.hwnd=hwnd;info.uId=1;info.rect=(rail||element==UiElement::Power)?RECT{0,0,client.right,client.bottom}:RECT{static_cast<LONG>(6*scale),static_cast<LONG>(6*scale),static_cast<LONG>(46*scale),static_cast<LONG>((height-6)*scale)};SendMessageW(tooltip_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&info));}
}
}
