#include "ui.h"
#include <algorithm>
#include <array>

namespace gv {
int RunTypographyProbe(const fs::path& directory){
    Application app(directory,true);json::JsonArray checks;bool passed=true;
    auto check=[&](bool ok,std::wstring name){json::JsonObject item;item.SetNamedValue(L"name",json::JsonValue::CreateStringValue(name));item.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(ok));checks.Append(item);passed&=ok;};
    auto collection=app.typography.Collection();
    const std::wstring sample=L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzЁёАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя0123456789…—·%";
    std::vector<UINT32> codepoints(sample.begin(),sample.end());std::vector<UINT16> glyphs(sample.size());
    HDC dc=CreateCompatibleDC(nullptr);
    for(auto role:{FontRole::Interface,FontRole::Body})for(int weight:{300,400,500,600}){
        if(role==FontRole::Body&&weight!=400)continue;
        std::wstring label=std::wstring(FontFamily(role))+L" "+std::to_wstring(weight);UINT32 index=0;BOOL exists=FALSE;
        bool found=SUCCEEDED(collection->FindFamilyName(FontFamily(role),&index,&exists))&&exists;check(found,label+L" embedded family");
        ComPtr<IDWriteFontFamily> family;ComPtr<IDWriteFont> font;ComPtr<IDWriteFontFace> face;
        bool matched=found&&SUCCEEDED(collection->GetFontFamily(index,&family))&&SUCCEEDED(family->GetFirstMatchingFont(static_cast<DWRITE_FONT_WEIGHT>(weight),DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,&font));
        check(matched&&font->GetWeight()==weight&&font->GetSimulations()==DWRITE_FONT_SIMULATIONS_NONE,label+L" real weight without synthesis");
        bool covered=matched&&SUCCEEDED(font->CreateFontFace(&face))&&SUCCEEDED(face->GetGlyphIndices(codepoints.data(),static_cast<UINT32>(codepoints.size()),glyphs.data()));
        check(covered&&std::all_of(glyphs.begin(),glyphs.end(),[](UINT16 value){return value!=0;}),label+L" DirectWrite Cyrillic and Latin coverage");
        auto format=app.Font(18,weight,role);wchar_t name[64]{};format->GetFontFamilyName(name,64);
        check(std::wstring(name)==FontFamily(role)&&format->GetFontWeight()==weight&&app.Font(18,weight,role)==format,label+L" application format and cache");
        HFONT gdi=UiFont(18,96,weight,role);auto previous=SelectObject(dc,gdi);wchar_t selected[64]{};GetTextFaceW(dc,64,selected);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);
        check(std::wstring(selected)==GdiFontFamily(role,weight)&&metrics.tmWeight==weight,label+L" private GDI family and weight");
        std::vector<WORD> indices(sample.size());auto result=GetGlyphIndicesW(dc,sample.data(),static_cast<int>(sample.size()),indices.data(),GGI_MARK_NONEXISTING_GLYPHS);
        check(result!=GDI_ERROR&&std::all_of(indices.begin(),indices.end(),[](WORD value){return value!=0xffff;}),label+L" GDI Cyrillic and Latin coverage");
        SelectObject(dc,previous);DeleteObject(gdi);
    }
    check(app.Font(18,400,FontRole::Interface)!=app.Font(18,400,FontRole::Body),L"family-specific text cache");
    for(float scale:{1.f,1.3f})for(auto text:{L"Работа",L"Поиск",L"Добавить",L"100%"}){
        ComPtr<IDWriteTextLayout> layout;app.dwrite->CreateTextLayout(text,static_cast<UINT32>(wcslen(text)),app.Font(15*scale,500),1000,100,&layout);DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);
        check(metrics.height<=40*scale&&metrics.width>0,std::wstring(text)+L" text fits button height at "+std::to_wstring(static_cast<int>(scale*100))+L"%");
    }
    DeleteDC(dc);json::JsonObject report;report.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(passed));report.SetNamedValue(L"checks",checks);WriteAtomic(directory/L"typography-smoke.json",std::wstring(report.Stringify()));return passed?0:1;
}
}
