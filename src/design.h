#pragma once
#include "platform.h"
#include "typography.h"
#include "panel_bitmap.h"
#include <span>

namespace gv {
enum class Glyph { Apps, Desktop, Windows, Add, More, Settings, Exit, Back, Forward, Reload, Snapshot, Close, Minus, Note, Globe, Clock, Calendar, Pin, Folder, Check, Chevron, Resize, Layout, Tray, Activity, Search, Link, Expand, Collapse, Horizontal, Vertical, Sun, Moon, Cpu, Memory, Disk, Network, Briefcase, Game, Book, Code, Music, Headphones, Photo, Film, Mountain, Coffee, Star, Heart, Leaf, Compass, Rocket, Layers };
struct SpaceIcon {const wchar_t* key;const wchar_t* name;Glyph glyph;};
std::span<const SpaceIcon> SpaceIcons();
Glyph SceneGlyph(const Scene& scene);
bool ChooseSpaceIcon(HWND owner,std::wstring& key);
std::optional<std::wstring> ChooseFolder(HWND owner,const wchar_t* title,const std::wstring& initial=L"");
Palette UiTheme();
struct Segment {float x1,y1,x2,y2;};
std::vector<Segment> GlyphLines(Glyph glyph);
struct Circle {float x,y,r;};
std::vector<Circle> GlyphCircles(Glyph glyph);
struct GlyphPathCommand {char verb;float x1=0,y1=0,x2=0,y2=0,x3=0,y3=0;};
std::span<const GlyphPathCommand> GlyphPath(Glyph glyph);
void DrawGlyph(HDC dc,Glyph glyph,RECT rect,COLORREF color);
void RoundFill(HDC dc,RECT rect,COLORREF fill,int radius=12,COLORREF border=CLR_INVALID);
void RoundWindow(HWND window,int radius=20);
void DrawRingFrame(HDC dc,RECT rect,COLORREF fill,COLORREF border,int count);
RECT CenteredWindow(HWND owner,int width,int height);
HFONT UiFont(int size,UINT dpi,int weight=FW_MEDIUM,FontRole role=FontRole::Interface);
void DrawUiButton(const DRAWITEMSTRUCT& item,HFONT font,bool active=false,COLORREF background=CLR_INVALID);
void DrawAccentChoice(const DRAWITEMSTRUCT& item,HFONT font,COLORREF color,bool active);
void StyleUiButton(HWND window,COLORREF background=CLR_INVALID);
void ButtonGlyph(HWND window,Glyph glyph);
bool SaveBitmapPng(HBITMAP bitmap,const fs::path& path);
void ShowNotice(HWND owner,std::wstring_view title,std::wstring_view body);
UINT StyledMenu(HMENU menu,HWND owner,POINT point);
bool HandleExitShortcut(const MSG& message);

struct MonoIcon {
    int size=48;std::vector<uint32_t> pixels;HBITMAP bitmap{};
    mutable std::map<int,HBITMAP> scaled;
    ~MonoIcon(){if(bitmap)DeleteObject(bitmap);for(auto [dimension,value]:scaled)DeleteObject(value);}
};
std::shared_ptr<MonoIcon> MonoFromIcon(HICON source);
class IconCache {
public:
    std::shared_ptr<MonoIcon> Get(const std::wstring& target);
    std::shared_ptr<MonoIcon> GetWindow(HWND window);
private:
    std::map<std::wstring,std::shared_ptr<MonoIcon>> icons_;
    std::map<std::wstring,ULONGLONG> windowExpiry_;
};
void DrawMonoIcon(HDC dc,const MonoIcon& icon,RECT rect);
void MenuIcon(HMENU menu,UINT id,const MonoIcon* icon);
}
