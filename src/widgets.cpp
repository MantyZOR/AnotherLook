#include "ui.h"
#include <algorithm>
#include <cmath>
#include <array>

namespace gv {
namespace {
constexpr float Pi=3.14159265359f;
const wchar_t* Months[]={L"Январь",L"Февраль",L"Март",L"Апрель",L"Май",L"Июнь",L"Июль",L"Август",L"Сентябрь",L"Октябрь",L"Ноябрь",L"Декабрь"};
std::wstring TimeText(SYSTEMTIME now,bool full,bool seconds=false){wchar_t text[32]{};swprintf_s(text,L"%02u:%02u",full?now.wHour:(now.wHour%12?now.wHour%12:12),now.wMinute);std::wstring value=text;if(seconds){swprintf_s(text,L":%02u",now.wSecond);value+=text;}return value+(full?L"":now.wHour<12?L" AM":L" PM");}
std::wstring DayText(SYSTEMTIME now){return std::to_wstring(now.wDay)+L" · "+Months[now.wMonth-1]+L" "+std::to_wstring(now.wYear);}
std::wstring Percent(std::optional<float> value){return value?std::to_wstring(static_cast<int>(std::lround(*value)))+L"%":L"—";}
std::wstring Rate(std::optional<double> value){if(!value)return L"—";wchar_t text[64]{};double bytes=*value;swprintf_s(text,L"%.1f %s",bytes>=1048576?bytes/1048576:bytes/1024,bytes>=1048576?L"МБ/с":L"КБ/с");return text;}
}
void Surface::DrawCard(Card& card,Box box){
    auto p=PaletteFor(app_.Active());auto accent=Ink(p.accent),muted=Ink(p.muted),foreground=Ink(p.text);float z=Zoom(),font=card.appearance.textScale,corner=(card.appearance.radius<0?app_.config.ui.cardRadius:card.appearance.radius)*z;
    auto variant=card.appearance.variant;bool selected=selected_.contains(card.id);cards_.push_back({card.id,box});
    bool floating=card.type==L"clock"&&variant==L"floating",decorations=!cardPreview_&&(!floating||selected||hoverCard_==card.id);
    auto resize=[&]{if(!decorations)return;GlyphIcon(Glyph::Resize,{box.x+box.w-23,box.y+box.h-23,16,16},Ink(p.muted,.6f));};
    auto more=[&]{if(!decorations)return;bool compactMenu=card.type==L"launcher"&&box.w<160;IconButton(Glyph::More,L"",compactMenu?Box{box.x+box.w-28,box.y+4,24,24}:Box{box.x+box.w-46,box.y+7,38,36},[this,id=card.id]{CardMenu(id);});};
    if(card.type==L"launcher"){
        bool compact=variant==L"icon"||(variant==L"auto"&&app_.config.ui.compactLaunchers),row=variant==L"row";
        if(!compact||selected){Fill(box,Ink(p.card),corner);Stroke({box.x+.5f,box.y+.5f,box.w-1,box.h-1},selected?accent:Ink(p.border),corner);}
        hits_.push_back({{box.x+4,box.y+4,box.w-8,box.h-8},[this,id=card.id]{app_.OpenCard(id,*this);},true,{},card.id});
        if(row){ProgramIcon(card.target,{box.x+22,box.y+(box.h-44)/2,44,44});std::wstring heading=card.title;if(card.type==L"system"&&box.w<220){unsigned bits=card.appearance.metrics;if(bits==1)heading=L"CPU";else if(bits==2)heading=L"RAM";else if(bits==4)heading=L"Диск";else if(bits==8)heading=L"Сеть";}if(card.appearance.showTitle&&!floating)Text(heading,{box.x+86,box.y+(box.h-30)/2,box.w-146,36},19*font,foreground,500);GlyphIcon(Glyph::Forward,{box.x+box.w-38,box.y+(box.h-20)/2,20,20},muted);}
        else{float image=std::clamp(box.h-(card.appearance.showTitle?70.f:28.f),24.f,compact?40.f:52.f),total=image+(card.appearance.showTitle?48.f:0),y=box.y+std::max(12.f,(box.h-total)/2);ProgramIcon(card.target,{box.x+(box.w-image)/2,y,image,image});if(card.appearance.showTitle)CenterText(card.title,{box.x+14,y+image+10,box.w-28,std::min(34.f,box.y+box.h-y-image-18)},17*font,foreground,500);}
        more();resize();return;
    }
    COLORREF cardColor=p.card,textColor=p.text;
    if(card.type==L"note"&&variant==L"paper"){cardColor=p.light?RGB(255,247,214):RGB(48,44,34);textColor=p.light?RGB(62,55,36):RGB(239,230,204);foreground=Ink(textColor);}
    if(card.type==L"note"&&variant==L"plain")cardColor=p.canvas;
    bool plain=card.type==L"note"&&variant==L"plain";
    if(!plain&&!floating)Fill({box.x,box.y+3,box.w,box.h},Color(L"000000",p.light?.035f:.12f),corner);
    if(!floating)Fill(box,Ink(cardColor),corner);if((!plain&&!floating)||selected)Stroke({box.x+.5f,box.y+.5f,box.w-1,box.h-1},selected?accent:Ink(p.border),corner);
    if(card.type==L"note"&&variant==L"accent")Fill({box.x+13,box.y+18,4,box.h-36},accent,2);
    bool site=card.type==L"site"||card.type==L"link",live=site&&app_.web.Exists(card.id)&&!app_.web.Detached(card.id);
    std::wstring heading=card.title;if(card.type==L"system"&&box.w<220){unsigned bits=card.appearance.metrics;if(bits==1)heading=L"CPU";else if(bits==2)heading=L"RAM";else if(bits==4)heading=L"Диск";else if(bits==8)heading=L"Сеть";}if(card.appearance.showTitle&&!floating)Text(heading,{box.x+22,box.y+16,box.w-(live?202:82),28},16*font,foreground,600);
    if(live)IconButton(Glyph::Snapshot,L"Снимок",{box.x+box.w-164,box.y+7,110,36},[this,id=card.id]{app_.web.Freeze(id);});more();
    float top=box.y+((card.appearance.showTitle&&!floating)||site?58.f:floating?12.f:28.f),height=box.y+box.h-top-20;
    if(card.type==L"note"){
        if(variant==L"paper"){brush_->SetColor(Ink(p.border,.45f));for(float y=top+31;y<box.y+box.h-24;y+=30*font)rt_->DrawLine(D2D1::Point2F(box.x+22,y),D2D1::Point2F(box.x+box.w-22,y),brush_.Get(),.7f);}
        Text(card.content,{box.x+24,top,box.w-48,height},std::max(16.f,18*z)*font,foreground,400,true,FontRole::Body);hits_.push_back({{box.x+18,top,box.w-36,height},[this,id=card.id]{app_.EditCard(id);},true});
    }else if(card.type==L"clock"){
        SYSTEMTIME now{};GetLocalTime(&now);bool analog=variant==L"analog",date=card.appearance.showDate&&height>86;
        if(analog){float radius=std::max(20.f,std::min((box.w-60)/2,(height-(date?34:0))/2-5)),cx=box.x+box.w/2,cy=top+radius+3;brush_->SetColor(Ink(p.border));rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),radius,radius),brush_.Get(),1.5f);
            for(int i=0;i<12;++i){float angle=i*Pi/6-Pi/2;brush_->SetColor(muted);rt_->DrawLine(D2D1::Point2F(cx+std::cos(angle)*(radius-5),cy+std::sin(angle)*(radius-5)),D2D1::Point2F(cx+std::cos(angle)*(radius-11),cy+std::sin(angle)*(radius-11)),brush_.Get(),1.5f);}
            auto hand=[&](float angle,float length,float width){brush_->SetColor(foreground);rt_->DrawLine(D2D1::Point2F(cx,cy),D2D1::Point2F(cx+std::cos(angle)*length,cy+std::sin(angle)*length),brush_.Get(),width);};hand((now.wHour%12+now.wMinute/60.f)*Pi/6-Pi/2,radius*.48f,3);hand(now.wMinute*Pi/30-Pi/2,radius*.73f,2);brush_->SetColor(accent);rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),3,3),brush_.Get());
            if(card.appearance.showSeconds){float angle=now.wSecond*Pi/30-Pi/2;brush_->SetColor(accent);rt_->DrawLine(D2D1::Point2F(cx-std::cos(angle)*radius*.18f,cy-std::sin(angle)*radius*.18f),D2D1::Point2F(cx+std::cos(angle)*radius*.83f,cy+std::sin(angle)*radius*.83f),brush_.Get(),1);}
            if(date)CenterText(DayText(now),{box.x+18,box.y+box.h-44,box.w-36,28},15*font,muted);
        }else{
            float timeHeight=std::max(30.f,height-(date?37:0)),size=std::min({(floating?104.f:76.f)*z*font,timeHeight*(floating?.74f:.64f)/app_.config.ui.textScale,(box.w-40)/(card.appearance.showSeconds?card.appearance.clock24?5.f:7.f:card.appearance.clock24?3.3f:5.5f)/app_.config.ui.textScale});
            if(variant==L"minimal"){
                wchar_t hour[8]{},minute[8]{},second[16]{};swprintf_s(hour,L"%02u",card.appearance.clock24?now.wHour:(now.wHour%12?now.wHour%12:12));swprintf_s(minute,L"%02u",now.wMinute);swprintf_s(second,L"%02u с",now.wSecond);float column=(box.w-54)*.53f,large=std::min(80.f*font,timeHeight*.82f/app_.config.ui.textScale);
                Text(hour,{box.x+24,top,column,timeHeight},large,foreground,300);Text(minute,{box.x+30+column,top+4,box.w-column-56,timeHeight*.62f},large*.57f,foreground,500);if(card.appearance.showSeconds)Text(second,{box.x+32+column,top+timeHeight*.62f,box.w-column-60,timeHeight*.38f},18*font,accent,500);else if(!card.appearance.clock24)Text(now.wHour<12?L"AM":L"PM",{box.x+32+column,top+timeHeight*.62f,box.w-column-60,timeHeight*.38f},16*font,muted,500);
            }else CenterText(TimeText(now,card.appearance.clock24,card.appearance.showSeconds),{box.x+16,top,box.w-32,timeHeight},size,foreground,300);
            if(date)CenterText(DayText(now),{box.x+18,box.y+box.h-47,box.w-36,28},15*font,muted);
        }
    }else if(card.type==L"calendar"){
        SYSTEMTIME today{};GetLocalTime(&today);const wchar_t* weekdays[]={L"Пн",L"Вт",L"Ср",L"Чт",L"Пт",L"Сб",L"Вс"};
        if(variant==L"date"){float size=std::min(90.f*font,(height-36)*.7f/app_.config.ui.textScale);CenterText(std::to_wstring(today.wDay),{box.x+20,top,box.w-40,std::max(35.f,height-36)},size,foreground,300);CenterText(weekdays[(today.wDayOfWeek+6)%7]+std::wstring(L" · ")+Months[today.wMonth-1],{box.x+20,box.y+box.h-48,box.w-40,30},16*font,muted);}
        else if(variant==L"week"){
            Text(Months[today.wMonth-1]+std::wstring(L" ")+std::to_wstring(today.wYear),{box.x+22,top,box.w-44,30},20*font,foreground,500);FILETIME file{};SystemTimeToFileTime(&today,&file);ULARGE_INTEGER time{};time.LowPart=file.dwLowDateTime;time.HighPart=file.dwHighDateTime;time.QuadPart-=uint64_t((today.wDayOfWeek+6)%7)*864000000000ull;
            float column=(box.w-36)/7;for(int i=0;i<7;++i){ULARGE_INTEGER value=time;value.QuadPart+=uint64_t(i)*864000000000ull;FILETIME ft{value.LowPart,value.HighPart};SYSTEMTIME day{};FileTimeToSystemTime(&ft,&day);float x=box.x+18+i*column,y=top+48;if(day.wDay==today.wDay&&day.wMonth==today.wMonth)Fill({x,y,column-3,std::max(60.f,height-56)},Ink(p.accent,.12f),12);CenterText(weekdays[i],{x,y+8,column-3,24},14*font,muted);CenterText(std::to_wstring(day.wDay),{x,y+36,column-3,32},20*font,foreground,500);}
        }else{
            Text(Months[today.wMonth-1]+std::wstring(L" ")+std::to_wstring(today.wYear),{box.x+22,top,box.w-44,32},20*font,foreground,500);float column=(box.w-32)/7;for(int i=0;i<7;++i)CenterText(weekdays[i],{box.x+16+i*column,top+38,column,26},14*font,muted);
            SYSTEMTIME first=today;first.wDay=1;FILETIME file{};SystemTimeToFileTime(&first,&file);FileTimeToSystemTime(&file,&first);int start=(first.wDayOfWeek+6)%7,days=31;if(today.wMonth==4||today.wMonth==6||today.wMonth==9||today.wMonth==11)days=30;if(today.wMonth==2)days=today.wYear%4==0&&(today.wYear%100!=0||today.wYear%400==0)?29:28;float row=std::max(20.f,std::min(34.f,(height-72)/6));
            for(int day=1;day<=days;++day){int position=start+day-1;Box cell{box.x+16+(position%7)*column,top+68+(position/7)*row,column-2,row-1};if(day==today.wDay)Fill(cell,Ink(p.accent,.15f),8);CenterText(std::to_wstring(day),cell,15*font,day==today.wDay?accent:foreground,day==today.wDay?600:400);}
        }
    }else if(card.type==L"system"){
        const auto& load=app_.systemLoad;float memory=load.memoryTotal?100.f*load.memoryUsed/load.memoryTotal:0;wchar_t ram[80]{};swprintf_s(ram,L"%.1f / %.1f ГБ",load.memoryUsed/1073741824.,load.memoryTotal/1073741824.);
        struct Metric {std::wstring label,value,detail;float percent;Glyph glyph;};std::vector<Metric> metrics;
        if(card.appearance.metrics&1)metrics.push_back({L"CPU",Percent(load.cpu),L"",load.cpu.value_or(0),Glyph::Cpu});
        if(card.appearance.metrics&2)metrics.push_back({L"Память",load.memoryTotal?Percent(memory):L"—",load.memoryTotal?ram:L"Недоступно",memory,Glyph::Memory});
        if(card.appearance.metrics&4)metrics.push_back({L"Диск",Percent(load.disk),L"Активность дисков",load.disk.value_or(0),Glyph::Disk});
        if(card.appearance.metrics&8)metrics.push_back({L"Сеть",Rate(load.download),L"Отдача "+Rate(load.upload),load.network.value_or(0),Glyph::Network});
        bool bars=variant==L"bars",tiles=variant==L"tiles";int count=static_cast<int>(metrics.size()),columns=bars?1:count>1?2:1,rows=std::max(1,(count+columns-1)/columns);float w=(box.w-36)/columns,h=height/rows;
        for(int i=0;i<count;++i){auto& metric=metrics[i];float x=box.x+18+(i%columns)*w,y=top+(i/columns)*h;
            if(bars){GlyphIcon(metric.glyph,{x+4,y+2,20,20},muted);Text(metric.label,{x+34,y,w*.45f,25},15*font,muted,500);Text(metric.value,{x+w*.56f,y,w*.4f,26},17*font,foreground,500);Fill({x+4,y+h-13,w-12,5},Ink(p.border),3);if(metric.percent>0)Fill({x+4,y+h-13,(w-12)*std::clamp(metric.percent/100,0.f,1.f),5},accent,3);}
            else if(tiles){Fill({x+3,y+3,w-8,h-8},Ink(p.field,.65f),12);GlyphIcon(metric.glyph,{x+14,y+14,21,21},muted);Text(metric.label,{x+46,y+12,w-58,26},15*font,muted);Text(metric.value,{x+14,y+45,w-28,34},std::min(26.f,22*font),foreground,500);if(h>110)Text(metric.detail,{x+14,y+h-35,w-28,24},14,muted,400,false,FontRole::Body);}
            else{float radius=std::max(18.f,std::min(36.f,(h-45)/2)),cx=x+w/2,cy=y+radius+4;brush_->SetColor(Ink(p.border));rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),radius,radius),brush_.Get(),3);brush_->SetColor(accent);int steps=static_cast<int>(std::ceil(metric.percent*.64f));for(int n=0;n<steps;++n){float a=-Pi/2+n*2*Pi/64,b=-Pi/2+std::min(n+1,64)*2*Pi/64;rt_->DrawLine(D2D1::Point2F(cx+std::cos(a)*radius,cy+std::sin(a)*radius),D2D1::Point2F(cx+std::cos(b)*radius,cy+std::sin(b)*radius),brush_.Get(),3);}if(metric.glyph==Glyph::Network){auto split=metric.value.find(L' ');auto number=metric.value.substr(0,split),unit=split==std::wstring::npos?L"":metric.value.substr(split+1);if(unit.empty())FitCenterText(number,{cx-radius+8,cy-14,2*radius-16,28},18*font,foreground,500);else{FitCenterText(number,{cx-radius+8,cy-19,2*radius-16,25},18*font,foreground,500);FitCenterText(unit,{cx-radius+8,cy+6,2*radius-16,17},12*font,muted,500);}}else FitCenterText(metric.value,{cx-radius+6,cy-14,2*radius-12,28},18*font,foreground,500);CenterText(metric.label,{x+4,cy+radius+9,w-8,24},std::min(15*font,(w-8)*1.7f/std::max<size_t>(1,metric.label.size())/app_.config.ui.textScale),muted);if(h>127)CenterText(metric.detail,{x+4,y+h-25,w-8,22},14,muted);}
        }
    }else if(site){
        Fill({box.x+12,box.y+54,box.w-24,38},Ink(p.field,.65f),10);
        if(live&&app_.web.CanBack(card.id))IconButton(Glyph::Back,L"",{box.x+14,box.y+54,34,38},[this,id=card.id]{app_.web.Back(id);});else GlyphIcon(Glyph::Back,{box.x+20,box.y+62,22,22},muted);
        if(live&&app_.web.CanForward(card.id))IconButton(Glyph::Forward,L"",{box.x+50,box.y+54,34,38},[this,id=card.id]{app_.web.Forward(id);});else GlyphIcon(Glyph::Forward,{box.x+56,box.y+62,22,22},muted);
        IconButton(Glyph::Reload,L"",{box.x+86,box.y+54,34,38},[this,id=card.id,live]{if(live)app_.web.Reload(id);else app_.OpenCard(id,*this);});Text(live?app_.web.Address(card.id):card.target,{box.x+130,box.y+63,box.w-154,24},14,muted,400,false,FontRole::Body);
        if(!live){auto path=app_.web.PreviewPath(card);std::error_code error;bool cached=fs::exists(path,error);Box image{box.x+12,box.y+100,box.w-24,std::max(1.f,box.h-166)};if(cached)Image(path,image,.95f,true);else{GlyphIcon(Glyph::Globe,{box.x+24,box.y+118,32,32},accent);Text(L"Страница и её сохранённый вид\nбудут здесь.",{box.x+24,box.y+168,box.w-48,std::max(30.f,box.h-238)},16,muted,400,true,FontRole::Body);}Text(cached?L"Сохранённая страница":app_.web.Status(card.id),{box.x+20,box.y+box.h-46,box.w-202,30},14,muted,400,false,FontRole::Body);Button(L"Открыть",{box.x+box.w-170,box.y+box.h-52,150,40},[this,id=card.id]{app_.OpenCard(id,*this);},true);}
    }
    if(!card.group.empty())GlyphIcon(Glyph::Link,{box.x+12,box.y+box.h-26,16,16},accent);resize();
}
}
