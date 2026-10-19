#include "core.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <atomic>
#include <sstream>
#include <iomanip>

namespace gv {
namespace {
json::JsonValue Str(std::wstring_view s) { return json::JsonValue::CreateStringValue(winrt::hstring(s)); }
json::JsonValue Num(double n) { return json::JsonValue::CreateNumberValue(n); }
json::JsonValue Bool(bool b) { return json::JsonValue::CreateBooleanValue(b); }
std::wstring String(const json::JsonObject& o, std::wstring_view k, std::wstring def=L"") {
    auto v=o.TryLookup(winrt::hstring(k));
    return v && v.ValueType()==json::JsonValueType::String ? std::wstring(v.GetString()) : def;
}
double Number(const json::JsonObject& o, std::wstring_view k, double def) {
    auto v=o.TryLookup(winrt::hstring(k));
    if(!v || v.ValueType()!=json::JsonValueType::Number) return def;
    auto n=v.GetNumber(); return std::isfinite(n)?n:def;
}
bool Boolean(const json::JsonObject& o, std::wstring_view k, bool def=false) {
    auto v=o.TryLookup(winrt::hstring(k)); return v && v.ValueType()==json::JsonValueType::Boolean ? v.GetBoolean():def;
}
Box ReadBox(const json::JsonObject& o, Box d) {
    return {static_cast<float>(Number(o,L"x",d.x)),static_cast<float>(Number(o,L"y",d.y)),
            static_cast<float>(Number(o,L"width",d.w)),static_cast<float>(Number(o,L"height",d.h))};
}
void PutBox(json::JsonObject& o, Box b) { o.SetNamedValue(L"x",Num(b.x));o.SetNamedValue(L"y",Num(b.y));o.SetNamedValue(L"width",Num(b.w));o.SetNamedValue(L"height",Num(b.h)); }
bool HexColor(std::wstring_view s) { return s.size()==6 && std::all_of(s.begin(),s.end(),[](wchar_t c){return iswxdigit(c)!=0;}); }
}
std::string Utf8(std::wstring_view v) {
    if(v.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0'); if(n)WideCharToMultiByte(CP_UTF8,0,v.data(),static_cast<int>(v.size()),r.data(),n,nullptr,nullptr);return r;
}
std::wstring Wide(std::string_view v) {
    if(v.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),nullptr,0);
    std::wstring r(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,v.data(),static_cast<int>(v.size()),r.data(),n);return r;
}
std::optional<std::wstring> ReadUtf8(const fs::path& path) {
    std::ifstream f(path,std::ios::binary); if(!f)return std::nullopt;
    f.seekg(0,std::ios::end);auto size=f.tellg();if(size<0 || size>8*1024*1024)return std::nullopt;
    f.seekg(0);std::string s(static_cast<size_t>(size),'\0');f.read(s.data(),size);if(!f && size)return std::nullopt;
    if(s.starts_with("\xEF\xBB\xBF"))s.erase(0,3);return Wide(s);
}
bool WriteAtomic(const fs::path& path, std::wstring_view text) {
    std::error_code ec;fs::create_directories(path.parent_path(),ec);if(ec)return false;
    auto temp=path;temp+=L".tmp";auto s=Utf8(text);if(s.size()>8*1024*1024)return false;
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(f,s.data(),static_cast<DWORD>(s.size()),&written,nullptr) && written==s.size();
    if(ok)ok=FlushFileBuffers(f)!=FALSE;CloseHandle(f);
    if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    return ok;
}
std::wstring NewId() { static std::atomic<unsigned> sequence=0;return std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence); }
std::wstring Timestamp() { SYSTEMTIME s;GetLocalTime(&s);wchar_t b[40];swprintf_s(b,L"%04u%02u%02u-%02u%02u%02u",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond);return b; }
Config DefaultConfig() {
    Config c;
    Scene play{L"play",L"PLAY",L"Твоё привычное пространство. Только игра и нужные приложения.",L"B8ACD5",L"19171F"};play.gaming=true;
    Scene work{L"work",L"FOCUS",L"Другой вид. Другой ритм. Место для того, что важно.",L"B4CEAC",L"18211F"};
    work.cards={
      {L"work-clock",L"clock",L"СЕЙЧАС",L"",L"",L"",{0,0,350,200}},
      {L"work-note",L"note",L"Один фокус на сегодня",L"Запиши здесь самое важное.\n\nМеньше шума. Больше внимания.",L"",L"",{374,0,410,260}},
      {L"work-calendar",L"calendar",L"КАЛЕНДАРЬ",L"",L"",L"",{0,224,350,300}},
      {L"work-link",L"link",L"Google",L"Поиск и всё, что нужно для начала.",L"https://www.google.com",L"",{374,284,410,140}}
    };
    Scene web{L"web",L"WEB",L"Твои сайты. Своё место для каждого.",L"A5BED5",L"182129"};
    web.cards={
      {L"web-google",L"site",L"Google",L"",L"https://www.google.com",L"",{0,0,560,420},L"",0,L"sleep"},
      {L"web-youtube",L"site",L"YouTube",L"",L"https://www.youtube.com",L"",{584,0,480,420},L"",0,L"keep"}
    };
    Scene board{L"board",L"CANVAS",L"Свободное полотно для мыслей, ссылок и маленьких привычек.",L"D6B998",L"252019"};
    board.cards={
      {L"board-clock",L"clock",L"СЕЙЧАС",L"",L"",L"",{0,0,350,200}},
      {L"board-note",L"note",L"Место для мыслей",L"Перетаскивай карточки за заголовок.\nМеняй размер за нижний правый угол.\n\nДобавь заметку, сайт или приложение через +.",L"",L"",{374,0,420,270}},
      {L"board-calendar",L"calendar",L"КАЛЕНДАРЬ",L"",L"",L"",{0,224,350,300}}
    };
    c.scenes={play,work,web,board};return c;
}
std::wstring Serialize(const Config& c) {
    json::JsonObject root;root.SetNamedValue(L"version",Num(c.version));root.SetNamedValue(L"activeScene",Str(c.active));
    root.SetNamedValue(L"enabled",Bool(c.enabled));root.SetNamedValue(L"wallpaperEnabled",Bool(c.wallpaperEnabled));root.SetNamedValue(L"autoStart",Bool(c.autoStart));root.SetNamedValue(L"softwareRendering",Bool(c.softwareRendering));
    json::JsonObject ui;ui.SetNamedValue(L"searchPosition",Str(c.ui.searchPosition));ui.SetNamedValue(L"searchX",Num(c.ui.searchX));ui.SetNamedValue(L"searchY",Num(c.ui.searchY));ui.SetNamedValue(L"dockPosition",Str(c.ui.dockPosition));ui.SetNamedValue(L"railPosition",Str(c.ui.railPosition));ui.SetNamedValue(L"railLabels",Bool(c.ui.railLabels));ui.SetNamedValue(L"showActivity",Bool(c.ui.showActivity));ui.SetNamedValue(L"showTray",Bool(c.ui.showTray));ui.SetNamedValue(L"compactLaunchers",Bool(c.ui.compactLaunchers));root.SetNamedValue(L"interface",ui);
    for(auto element:{UiElement::Navigation,UiElement::Windows,UiElement::Dock,UiElement::Tray,UiElement::Search,UiElement::Settings,UiElement::Power}){
        const wchar_t* key=element==UiElement::Navigation?L"bar":element==UiElement::Windows?L"rail":element==UiElement::Dock?L"dock":element==UiElement::Tray?L"tray":element==UiElement::Settings?L"settings":element==UiElement::Power?L"power":L"search";
        auto prefix=std::wstring(key);auto [x,y]=UiAnchor(const_cast<Config::Interface&>(c.ui),element);
        ui.SetNamedValue(prefix+L"Radius",Num(UiRadius(c.ui,element)));ui.SetNamedValue(prefix+L"Position",Str(UiPosition(c.ui,element)));ui.SetNamedValue(prefix+L"Vertical",Bool(UiVertical(c.ui,element)));ui.SetNamedValue(prefix+L"X",Num(x));ui.SetNamedValue(prefix+L"Y",Num(y));
    }ui.SetNamedValue(L"designVersion",Num(c.ui.designVersion));ui.SetNamedValue(L"cardRadius",Num(c.ui.cardRadius));ui.SetNamedValue(L"windowRadius",Num(c.ui.windowRadius));ui.SetNamedValue(L"railOrbit",Bool(c.ui.railOrbit));ui.SetNamedValue(L"textScale",Num(c.ui.textScale));root.SetNamedValue(L"interface",ui);
    json::JsonObject files;files.SetNamedValue(L"initialized",Bool(c.files.initialized));files.SetNamedValue(L"enabled",Bool(c.files.enabled));files.SetNamedValue(L"paused",Bool(c.files.paused));files.SetNamedValue(L"allFixedDrives",Bool(c.files.allFixedDrives));files.SetNamedValue(L"fastNtfs",Bool(c.files.fastNtfs));json::JsonArray roots;for(const auto& folder:c.files.roots){json::JsonObject item;item.SetNamedValue(L"path",Str(folder.path));item.SetNamedValue(L"depth",Num(folder.depth));roots.Append(item);}files.SetNamedValue(L"roots",roots);root.SetNamedValue(L"fileSearch",files);json::JsonArray scenes;
    for(auto& s:c.scenes){json::JsonObject o;o.SetNamedValue(L"id",Str(s.id));o.SetNamedValue(L"name",Str(s.name));o.SetNamedValue(L"description",Str(s.description));
      o.SetNamedValue(L"icon",Str(s.icon));o.SetNamedValue(L"network",Str(s.network));o.SetNamedValue(L"networkDensity",Num(s.networkDensity));o.SetNamedValue(L"networkMotion",Bool(s.networkMotion));o.SetNamedValue(L"networkSpeed",Num(s.networkSpeed));o.SetNamedValue(L"appearance",Str(s.appearance));o.SetNamedValue(L"accent",Str(s.accent));o.SetNamedValue(L"backdrop",Str(s.backdrop));o.SetNamedValue(L"wallpaper",Str(s.wallpaper));
      o.SetNamedValue(L"gaming",Bool(s.gaming));o.SetNamedValue(L"autoTile",Bool(s.autoTile));o.SetNamedValue(L"layout",Str(s.layout));o.SetNamedValue(L"zoom",Num(s.zoom));
      json::JsonArray cards;
      for(auto& a:s.cards){json::JsonObject j;j.SetNamedValue(L"id",Str(a.id));j.SetNamedValue(L"type",Str(a.type));j.SetNamedValue(L"title",Str(a.title));j.SetNamedValue(L"content",Str(a.content));j.SetNamedValue(L"target",Str(a.target));j.SetNamedValue(L"arguments",Str(a.arguments));j.SetNamedValue(L"group",Str(a.group));j.SetNamedValue(L"monitor",Num(a.monitor));j.SetNamedValue(L"monitorId",Str(a.monitorId));j.SetNamedValue(L"background",Str(a.background));j.SetNamedValue(L"autoLaunch",Bool(a.autoLaunch));json::JsonObject appearance;appearance.SetNamedValue(L"radius",Num(a.appearance.radius));appearance.SetNamedValue(L"variant",Str(a.appearance.variant));appearance.SetNamedValue(L"textScale",Num(a.appearance.textScale));appearance.SetNamedValue(L"showTitle",Bool(a.appearance.showTitle));appearance.SetNamedValue(L"showDate",Bool(a.appearance.showDate));appearance.SetNamedValue(L"clock24",Bool(a.appearance.clock24));appearance.SetNamedValue(L"showSeconds",Bool(a.appearance.showSeconds));appearance.SetNamedValue(L"metrics",Num(a.appearance.metrics));j.SetNamedValue(L"appearance",appearance);PutBox(j,a.box);cards.Append(j);}
      o.SetNamedValue(L"cards",cards);json::JsonArray windows;
      for(auto& a:s.windows){json::JsonObject j;j.SetNamedValue(L"process",Str(a.process));j.SetNamedValue(L"title",Str(a.title));j.SetNamedValue(L"class",Str(a.className));j.SetNamedValue(L"monitor",Str(a.monitor));j.SetNamedValue(L"floating",Bool(a.floating));PutBox(j,a.normalized);windows.Append(j);}
      o.SetNamedValue(L"windows",windows);scenes.Append(o);
    }root.SetNamedValue(L"scenes",scenes);return std::wstring(root.Stringify());
}
bool Deserialize(std::wstring_view text,Config& out,std::wstring& error) {
    try {
      json::JsonObject root;if(!json::JsonObject::TryParse(winrt::hstring(text),root)){error=L"Некорректный JSON";return false;}
      int version=static_cast<int>(Number(root,L"version",0));if(version!=1){error=L"Неизвестная версия настроек";return false;}
      Config c;c.active=String(root,L"activeScene",L"play");c.enabled=Boolean(root,L"enabled");c.wallpaperEnabled=Boolean(root,L"wallpaperEnabled",true);c.autoStart=Boolean(root,L"autoStart");c.softwareRendering=Boolean(root,L"softwareRendering",true);
      if(auto value=root.TryLookup(L"interface");value&&value.ValueType()==json::JsonValueType::Object){auto ui=value.GetObject();
        auto choice=[&](const wchar_t* key,std::wstring fallback,std::initializer_list<std::wstring_view> allowed){auto value=String(ui,key,fallback);return std::find(allowed.begin(),allowed.end(),value)!=allowed.end()?value:fallback;};
        c.ui.railOrbit=Boolean(ui,L"railOrbit",false);c.ui.railLabels=Boolean(ui,L"railLabels",false);c.ui.showActivity=Boolean(ui,L"showActivity",true);c.ui.showTray=Boolean(ui,L"showTray",true);c.ui.compactLaunchers=Boolean(ui,L"compactLaunchers",true);
        for(auto element:{UiElement::Navigation,UiElement::Windows,UiElement::Dock,UiElement::Tray,UiElement::Search,UiElement::Settings,UiElement::Power}){
          const wchar_t* key=element==UiElement::Navigation?L"bar":element==UiElement::Windows?L"rail":element==UiElement::Dock?L"dock":element==UiElement::Tray?L"tray":element==UiElement::Settings?L"settings":element==UiElement::Power?L"power":L"search";auto prefix=std::wstring(key);
          UiRadius(c.ui,element)=std::clamp(static_cast<float>(Number(ui,(prefix+L"Radius").c_str(),UiRadius(c.ui,element))),0.f,36.f);UiPosition(c.ui,element)=choice((prefix+L"Position").c_str(),UiPosition(c.ui,element),{L"top",L"bottom",L"left",L"right",L"center",L"custom",L"hidden"});
          UiVertical(c.ui,element)=Boolean(ui,(prefix+L"Vertical").c_str(),UiVertical(c.ui,element));auto [x,y]=UiAnchor(c.ui,element);x=std::clamp(static_cast<float>(Number(ui,(prefix+L"X").c_str(),x)),0.f,1.f);y=std::clamp(static_cast<float>(Number(ui,(prefix+L"Y").c_str(),y)),0.f,1.f);
        }
        // Navigation and search must remain reachable through their normal entry points.
        if(c.ui.barPosition==L"hidden")c.ui.barPosition=L"top";if(c.ui.searchPosition==L"hidden")c.ui.searchPosition=L"center";
        c.ui.cardRadius=std::clamp(static_cast<float>(Number(ui,L"cardRadius",18)),0.f,36.f);c.ui.windowRadius=std::clamp(static_cast<float>(Number(ui,L"windowRadius",20)),0.f,36.f);
        if(Number(ui,L"designVersion",0)<2){if(c.ui.searchPosition==L"center")c.ui.searchPosition=L"bottom";if(c.ui.dockPosition==L"center"){c.ui.dockPosition=L"custom";c.ui.dockX=0;c.ui.dockY=1;}c.ui.railLabels=false;}
        c.ui.textScale=std::clamp(static_cast<float>(Number(ui,L"textScale",1)),0.9f,1.3f);
      }
      if(auto value=root.TryLookup(L"fileSearch");value&&value.ValueType()==json::JsonValueType::Object){auto files=value.GetObject();c.files.initialized=Boolean(files,L"initialized",true);c.files.enabled=Boolean(files,L"enabled");c.files.paused=Boolean(files,L"paused");c.files.allFixedDrives=Boolean(files,L"allFixedDrives",false);c.files.fastNtfs=Boolean(files,L"fastNtfs",false);for(auto entry:files.GetNamedArray(L"roots",json::JsonArray())){if(c.files.roots.size()>=32)break;auto item=entry.GetObject();auto path=String(item,L"path");if(!path.empty()&&path.size()<32760)c.files.roots.push_back({path,std::clamp(static_cast<int>(Number(item,L"depth",-1)),-1,32)});}}auto arr=root.GetNamedArray(L"scenes");if(arr.Size()==0 || arr.Size()>32){error=L"Нужно от 1 до 32 пространств";return false;}
      for(auto v:arr){auto o=v.GetObject();Scene s;s.id=String(o,L"id");s.name=String(o,L"name");s.description=String(o,L"description");s.accent=String(o,L"accent",L"B4CEAC");s.backdrop=String(o,L"backdrop",L"18211F");s.wallpaper=String(o,L"wallpaper");s.icon=String(o,L"icon");s.network=String(o,L"network",L"links");s.networkDensity=std::clamp(static_cast<float>(Number(o,L"networkDensity",1)),.5f,2.f);s.networkMotion=Boolean(o,L"networkMotion");s.networkSpeed=std::clamp(static_cast<float>(Number(o,L"networkSpeed",1)),.35f,2.f);if(s.network!=L"links"&&s.network!=L"near"&&s.network!=L"static"&&s.network!=L"none")s.network=L"links";s.appearance=String(o,L"appearance",L"dark");if(s.appearance!=L"dark"&&s.appearance!=L"warm"&&s.appearance!=L"cool")s.appearance=L"dark";
        if(s.id.empty() || s.id.size()>100 || s.name.empty() || s.name.size()>80 || std::any_of(c.scenes.begin(),c.scenes.end(),[&](const Scene& x){return x.id==s.id;})){error=L"Некорректное или повторяющееся имя пространства";return false;}
        if(!HexColor(s.accent))s.accent=L"B4CEAC";if(!HexColor(s.backdrop))s.backdrop=L"18211F";
        s.gaming=Boolean(o,L"gaming");s.autoTile=Boolean(o,L"autoTile");s.layout=String(o,L"layout",L"columns");s.zoom=std::clamp(static_cast<float>(Number(o,L"zoom",1)),0.5f,1.5f);
        auto ca=o.GetNamedArray(L"cards",json::JsonArray());if(ca.Size()>128){error=L"Слишком много карточек";return false;}
        for(auto cv:ca){auto a=cv.GetObject();Card card;card.id=String(a,L"id",NewId());card.type=String(a,L"type",L"note");if(card.type==L"link")card.type=L"site";card.title=String(a,L"title",L"Карточка");card.content=String(a,L"content");card.target=String(a,L"target");card.arguments=String(a,L"arguments");card.group=String(a,L"group");card.monitor=std::clamp(static_cast<int>(Number(a,L"monitor",0)),0,15);card.monitorId=String(a,L"monitorId");card.background=String(a,L"background",L"sleep");if(card.background!=L"keep"&&card.background!=L"sleep"&&card.background!=L"unload")card.background=L"sleep";card.autoLaunch=Boolean(a,L"autoLaunch");card.box=ReadBox(a,{0,0,360,220});if(auto value=a.TryLookup(L"appearance");value&&value.ValueType()==json::JsonValueType::Object){auto appearance=value.GetObject();card.appearance.radius=std::clamp(static_cast<float>(Number(appearance,L"radius",-1)),-1.f,36.f);card.appearance.variant=String(appearance,L"variant",L"auto");const std::wstring_view variants[]={L"auto",L"paper",L"plain",L"accent",L"icon",L"tile",L"row",L"digital",L"analog",L"minimal",L"floating",L"month",L"week",L"date",L"rings",L"bars",L"tiles"};if(std::find(std::begin(variants),std::end(variants),card.appearance.variant)==std::end(variants))card.appearance.variant=L"auto";card.appearance.textScale=std::clamp(static_cast<float>(Number(appearance,L"textScale",1)),0.85f,1.5f);card.appearance.showTitle=Boolean(appearance,L"showTitle",true);card.appearance.showDate=Boolean(appearance,L"showDate",true);card.appearance.clock24=Boolean(appearance,L"clock24",true);card.appearance.showSeconds=Boolean(appearance,L"showSeconds");card.appearance.metrics=static_cast<unsigned>(std::clamp(Number(appearance,L"metrics",3),1.,15.));}auto minimum=MinimumCardSize(card);card.box=ClampBox(card.box,{0,0,6000,6000},minimum.w,minimum.h);s.cards.push_back(card);}
        auto wa=o.GetNamedArray(L"windows",json::JsonArray());if(wa.Size()>256){error=L"Слишком много окон";return false;}
        for(auto wv:wa){auto a=wv.GetObject();SavedWindow w;w.process=String(a,L"process");w.title=String(a,L"title");w.className=String(a,L"class");w.monitor=String(a,L"monitor");w.floating=Boolean(a,L"floating");w.normalized=ReadBox(a,{0.05f,0.05f,0.6f,0.8f});w.normalized=ClampBox(w.normalized,{0,0,1,1},0.08f,0.08f);s.windows.push_back(w);}
        c.scenes.push_back(std::move(s));
      }
      if(!FindScene(c,c.active))c.active=c.scenes.front().id;out=std::move(c);return true;
    } catch(const winrt::hresult_error&){error=L"Повреждённая структура настроек";return false;}catch(...){error=L"Не удалось прочитать настройки";return false;}
}
Config LoadConfig(const fs::path& path,std::wstring& error){auto raw=ReadUtf8(path);if(!raw){if(fs::exists(path))throw std::runtime_error(Utf8(L"Не удалось прочитать настройки. Исходный файл оставлен без изменений: "+path.wstring()));return DefaultConfig();}Config c;if(Deserialize(*raw,c,error))return c;
    // Preserve the invalid file before the caller can save a fresh configuration.
    auto backup=path;backup+=L".invalid-"+Timestamp()+L"-"+NewId();std::error_code ec;fs::copy_file(path,backup,fs::copy_options::none,ec);
    if(ec)throw std::runtime_error(Utf8(L"Не удалось создать резервную копию повреждённых настроек. Исходный файл оставлен без изменений: "+path.wstring()));
    error+=L". Копия сохранена рядом с настройками.";return DefaultConfig();
}
Box MinimumCardSize(const Card& card){
    if(card.type==L"calendar"){if(card.appearance.variant==L"date")return {0,0,180,180};if(card.appearance.variant==L"week")return {0,0,340,210};return {0,0,300,300};}
    if(card.type==L"launcher"){if(card.appearance.variant==L"row")return {0,0,340,104};if(card.appearance.variant==L"tile")return {0,0,220,180};if(card.appearance.variant==L"icon")return {0,0,88,88};return {0,0,180,104};}
    if(card.type!=L"system")return {0,0,card.type==L"site"?320.f:card.type==L"launcher"?(card.appearance.variant==L"icon"?88.f:180.f):200.f,card.type==L"site"?280.f:card.type==L"launcher"?(card.appearance.variant==L"icon"?88.f:104.f):140.f};
    int count=0;for(unsigned bits=card.appearance.metrics&15;bits;bits>>=1)count+=bits&1;
    bool bars=card.appearance.variant==L"bars",tiles=card.appearance.variant==L"tiles";
    int columns=bars||count==1?1:2,rows=(count+columns-1)/columns;float header=card.appearance.showTitle?58.f:28.f;
    return {0,0,bars?192.f:columns*(tiles?148.f:136.f),header+rows*(bars?54.f:tiles?98.f:110.f)+20.f};
}
Palette PaletteFor(const Scene& scene){
    auto rgb=[](std::wstring_view hex){unsigned value=0;try{value=std::stoul(std::wstring(hex),nullptr,16);}catch(...){}return RGB((value>>16)&255,(value>>8)&255,value&255);};
    Palette p;p.light=scene.appearance==L"warm"||scene.appearance==L"cool";p.accent=rgb(scene.accent);
    if(!p.light){auto background=rgb(scene.backdrop);auto blend=[](int a,int b){return (a*22+b*78)/100;};p.canvas=RGB(blend(GetRValue(background),21),blend(GetGValue(background),36),blend(GetBValue(background),59));p.panel=RGB(24,39,61);p.card=RGB(28,44,67);p.field=RGB(39,57,84);p.text=RGB(236,242,253);p.muted=RGB(169,189,218);p.border=RGB(65,87,119);}
    else{
        bool warm=scene.appearance==L"warm";p.canvas=warm?RGB(246,242,234):RGB(235,240,247);p.panel=warm?RGB(255,252,247):RGB(250,252,255);p.card=RGB(255,255,253);p.field=warm?RGB(230,234,224):RGB(226,233,241);p.text=warm?RGB(48,43,37):RGB(31,40,52);p.muted=warm?RGB(113,105,94):RGB(95,110,128);p.border=warm?RGB(206,216,202):RGB(201,212,224);
                int red=GetRValue(p.accent),green=GetGValue(p.accent),blue=GetBValue(p.accent),low=std::min({red,green,blue}),high=std::max({red,green,blue});
        auto channel=[&](int value){return high==low?80:40+(value-low)*80/std::max(1,high-low);};
        p.accent=RGB(channel(red),channel(green),channel(blue));
        auto tint=[&](COLORREF base,int weight){return RGB((GetRValue(base)*(100-weight)+GetRValue(p.accent)*weight)/100,(GetGValue(base)*(100-weight)+GetGValue(p.accent)*weight)/100,(GetBValue(base)*(100-weight)+GetBValue(p.accent)*weight)/100);};
        p.field=tint(p.panel,14);p.border=tint(p.panel,26);
    }
    return p;
}
Scene* FindScene(Config& c,std::wstring_view id){for(auto& s:c.scenes)if(s.id==id)return &s;return nullptr;}
Box ClampBox(Box b,Box a,float mw,float mh){
    if(!std::isfinite(b.x))b.x=a.x;if(!std::isfinite(b.y))b.y=a.y;
    if(!std::isfinite(b.w))b.w=mw;if(!std::isfinite(b.h))b.h=mh;
    b.w=std::clamp(b.w,std::min(mw,a.w),a.w);b.h=std::clamp(b.h,std::min(mh,a.h),a.h);
    b.x=std::clamp(b.x,a.x,a.x+a.w-b.w);b.y=std::clamp(b.y,a.y,a.y+a.h-b.h);return b;
}
std::vector<Box> TileLayout(Box a,int count,std::wstring_view layout,float gap){
    std::vector<Box> r;if(count<=0 || count>256 || a.w<=0 || a.h<=0)return r;
    a={a.x+gap,a.y+gap,std::max(1.f,a.w-2*gap),std::max(1.f,a.h-2*gap)};
    if(count==1){r.push_back(a);return r;}
    if(layout==L"master"){
      float master=(a.w-gap)*0.58f;r.push_back({a.x,a.y,master,a.h});float h=std::max(1.f,(a.h-gap*(count-2))/(count-1));
      for(int i=1;i<count;++i)r.push_back({a.x+master+gap,a.y+(h+gap)*(i-1),std::max(1.f,a.w-master-gap),h});return r;
    }
    int cols=layout==L"grid"?static_cast<int>(std::ceil(std::sqrt(count))):count;int rows=(count+cols-1)/cols;
    float w=std::max(1.f,(a.w-gap*(cols-1))/cols),h=std::max(1.f,(a.h-gap*(rows-1))/rows);
    for(int i=0;i<count;++i)r.push_back({a.x+(i%cols)*(w+gap),a.y+(i/cols)*(h+gap),w,h});return r;
}
Box Normalize(Box b,Box a){if(a.w<=0||a.h<=0)return {0,0,1,1};return {(b.x-a.x)/a.w,(b.y-a.y)/a.h,b.w/a.w,b.h/a.h};}
WebPlacement PlaceEmbeddedWeb(Box card,Box viewport,float dpiScale){
    if(!std::isfinite(dpiScale)||dpiScale<=0)return {};
    RECT page{static_cast<LONG>(std::ceil((card.x+12)*dpiScale)),static_cast<LONG>(std::ceil((card.y+100)*dpiScale)),static_cast<LONG>(std::floor((card.x+card.w-12)*dpiScale)),static_cast<LONG>(std::floor((card.y+card.h-28)*dpiScale))};
    RECT clip{static_cast<LONG>(std::ceil(viewport.x*dpiScale)),static_cast<LONG>(std::ceil(viewport.y*dpiScale)),static_cast<LONG>(std::floor((viewport.x+viewport.w)*dpiScale)),static_cast<LONG>(std::floor((viewport.y+viewport.h)*dpiScale))},bounds{};
    bool visible=IntersectRect(&bounds,&page,&clip)&&bounds.right-bounds.left>32&&bounds.bottom-bounds.top>32;
    return {visible?bounds:page,visible};
}
Box Denormalize(Box b,Box a){return ClampBox({a.x+b.x*a.w,a.y+b.y*a.h,b.w*a.w,b.h*a.h},a);}
bool IsWebUrl(std::wstring_view s){auto scheme=std::wstring(s.substr(0,std::min<size_t>(8,s.size())));std::transform(scheme.begin(),scheme.end(),scheme.begin(),towlower);return scheme.starts_with(L"https://")||scheme.starts_with(L"http://");}
bool IsLikelyWebAddress(std::wstring_view input){
    if(IsWebUrl(input))return true;
    if(input.empty()||input.find_first_of(L" \\:\t\r\n")!=std::wstring_view::npos)return false;
    auto host=std::wstring(input.substr(0,input.find_first_of(L"/?#")));std::transform(host.begin(),host.end(),host.begin(),towlower);
    for(auto extension:{L".exe",L".lnk",L".cmd",L".bat",L".msi",L".appref-ms",L".url"})if(host.ends_with(extension))return false;
    auto dot=host.find(L'.');return dot!=std::wstring::npos&&dot>0&&dot+1<host.size();
}
LauncherLayout LayoutLauncher(float width,float height){
    float margin=width<620?16.f:24.f,footer=height-72,searchTop=height<360?76.f:96.f;
    LauncherLayout layout;
    layout.title={margin,18,width-margin*2-56,40};layout.subtitle={margin,59,width-margin*2-56,24};
    if(height<360)layout.subtitle.w=0;
    layout.close={width-margin-36,24,36,36};layout.search={margin,searchTop,width-margin*2,52};
    layout.results={margin,searchTop+64,width-margin*2,std::max(0.f,footer-16-(searchTop+64))};
    float openWidth=width<540?128.f:152.f,actionsWidth=width<540?112.f:132.f;
    layout.open={width-margin-openWidth,footer,openWidth,44};layout.actions={layout.open.x-12-actionsWidth,footer,actionsWidth,44};
    layout.info={margin,footer,std::max(0.f,layout.actions.x-margin-16),44};if(layout.info.w<120)layout.info.w=0;return layout;
}
std::wstring& UiPosition(Config::Interface& ui,UiElement e){switch(e){case UiElement::Navigation:return ui.barPosition;case UiElement::Windows:return ui.railPosition;case UiElement::Dock:return ui.dockPosition;case UiElement::Tray:return ui.trayPosition;case UiElement::Settings:return ui.settingsPosition;case UiElement::Power:return ui.powerPosition;default:return ui.searchPosition;}}
std::wstring_view UiPosition(const Config::Interface& ui,UiElement e){return UiPosition(const_cast<Config::Interface&>(ui),e);}
bool& UiVertical(Config::Interface& ui,UiElement e){switch(e){case UiElement::Navigation:return ui.barVertical;case UiElement::Windows:return ui.railVertical;case UiElement::Dock:return ui.dockVertical;case UiElement::Tray:return ui.trayVertical;case UiElement::Settings:return ui.settingsVertical;case UiElement::Power:return ui.powerVertical;default:return ui.searchVertical;}}
bool UiVertical(const Config::Interface& ui,UiElement e){return UiVertical(const_cast<Config::Interface&>(ui),e);}
std::pair<float&,float&> UiAnchor(Config::Interface& ui,UiElement e){switch(e){case UiElement::Navigation:return {ui.barX,ui.barY};case UiElement::Windows:return {ui.railX,ui.railY};case UiElement::Dock:return {ui.dockX,ui.dockY};case UiElement::Tray:return {ui.trayX,ui.trayY};case UiElement::Settings:return {ui.settingsX,ui.settingsY};case UiElement::Power:return {ui.powerX,ui.powerY};default:return {ui.searchX,ui.searchY};}}
Box InterfaceBox(const Config::Interface& ui,UiElement e,Box area,Box size){
    float s=std::clamp(ui.textScale,0.9f,1.3f),margin=12;bool vertical=UiVertical(ui,e);auto position=UiPosition(ui,e);
    float w=0,h=0;
    switch(e){
    case UiElement::Navigation:w=(vertical?164:564)*s;h=(vertical?300:60)*s;break;
    case UiElement::Windows:w=(vertical?(ui.railLabels?192:64):256)*s;h=(vertical?256:64)*s;break;
    case UiElement::Dock:w=(vertical?184:430)*s;h=(vertical?170:58)*s;break;
    case UiElement::Tray:w=280*s;h=(vertical?244:88)*s;break;
    case UiElement::Search:w=(vertical?56:350)*s;h=54*s;break;
    case UiElement::Settings:w=100*s;h=54*s;break;
    case UiElement::Power:w=(vertical?64:176)*s;h=(vertical?176:64)*s;break;
    }
    if(size.w>0)w=size.w;if(size.h>0)h=size.h;
    w=std::clamp(w,1.f,std::max(1.f,area.w-margin*2));h=std::clamp(h,1.f,std::max(1.f,area.h-margin*2));
    float x=(area.w-w)/2,y=(area.h-h)/2;
    if(e==UiElement::Dock||e==UiElement::Tray)y=area.h-margin-h;
    if(position==L"top")y=margin;else if(position==L"bottom")y=area.h-margin-h;
    else if(position==L"left")x=margin;else if(position==L"right")x=area.w-margin-w;
    if(e==UiElement::Navigation&&position==L"top")y=8;
    if(e==UiElement::Windows&&(position==L"left"||position==L"right")&&vertical)y=(area.h-h)/2;
    if(e==UiElement::Windows&&position==L"top"&&ui.barPosition==L"top"&&!ui.barVertical)y=92*s;
    if(e==UiElement::Windows&&position==L"bottom"&&ui.barPosition==L"bottom"&&!ui.barVertical)y=area.h-h-92*s;
    if(e==UiElement::Windows&&vertical&&ui.barVertical&&ui.barPosition==position){if(position==L"left")x+=188*s+24;else if(position==L"right")x-=188*s+24;}
    if(e==UiElement::Navigation&&(position==L"left"||position==L"right")&&!vertical)y=margin;
    if(e==UiElement::Search&&position==L"bottom"&&area.w<1180*s)y=area.h-h-88*s;
    if(position==L"custom"){auto [nx,ny]=UiAnchor(const_cast<Config::Interface&>(ui),e);x=margin+std::max(0.f,area.w-w-margin*2)*std::clamp(nx,0.f,1.f);y=margin+std::max(0.f,area.h-h-margin*2)*std::clamp(ny,0.f,1.f);}
    return {area.x+std::clamp(x,margin,std::max(margin,area.w-w-margin)),area.y+std::clamp(y,8.f,std::max(8.f,area.h-h-margin)),w,h};
}
float& UiRadius(Config::Interface& ui,UiElement e){switch(e){case UiElement::Navigation:return ui.barRadius;case UiElement::Windows:return ui.railRadius;case UiElement::Dock:return ui.dockRadius;case UiElement::Tray:return ui.trayRadius;case UiElement::Settings:return ui.settingsRadius;case UiElement::Power:return ui.powerRadius;default:return ui.searchRadius;}}
float UiRadius(const Config::Interface& ui,UiElement e){return UiRadius(const_cast<Config::Interface&>(ui),e);}
Box LauncherBox(const Config::Interface& ui,Box area){float s=ui.textScale;return InterfaceBox(ui,UiElement::Search,area,{0,0,(ui.searchVertical?620:820)*s,(ui.searchVertical?720:620)*s});}
Box RailGrid::Cell(int index)const{return {padding+(vertical?index/rows:index%columns)*cellWidth,padding+(vertical?index%rows:index/columns)*cellHeight,cellWidth,cellHeight};}
RailGrid LayoutRail(int count,bool vertical,bool labels,Box available,float scale){
    RailGrid grid;grid.vertical=vertical;scale=std::clamp(scale,.9f,1.3f);grid.padding=8*scale;grid.cellWidth=(labels?184:48)*scale;grid.cellHeight=(labels?56:48)*scale;count=std::max(1,count);
    int maxRows=std::max(1,static_cast<int>((available.h-grid.padding*2)/grid.cellHeight)),maxCols=std::max(1,static_cast<int>((available.w-grid.padding*2)/grid.cellWidth));
    if(vertical){grid.rows=std::min(count,maxRows);grid.columns=std::min(maxCols,(count+grid.rows-1)/grid.rows);}else{grid.columns=std::min(count,maxCols);grid.rows=std::min(maxRows,(count+grid.columns-1)/grid.columns);}
    grid.capacity=grid.rows*grid.columns;grid.size={0,0,std::min(available.w,grid.columns*grid.cellWidth+grid.padding*2),std::min(available.h,grid.rows*grid.cellHeight+grid.padding*2)};return grid;
}
std::wstring NormalizeWebUrl(std::wstring v){while(!v.empty()&&iswspace(v.back()))v.pop_back();while(!v.empty()&&iswspace(v.front()))v.erase(v.begin());if(v.find(L"://")==std::wstring::npos && v.find(L':')==std::wstring::npos)v=L"https://"+v;return v;}
}
