#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

namespace gv {
namespace fs = std::filesystem;
namespace json = winrt::Windows::Data::Json;
struct Box { float x=0, y=0, w=0, h=0; };
struct WebPlacement {RECT bounds{};bool visible=false;};
WebPlacement PlaceEmbeddedWeb(Box card,Box viewport,float dpiScale);
struct LauncherLayout {Box title,subtitle,close,search,results,info,actions,open;};
LauncherLayout LayoutLauncher(float width,float height);
struct Card {
    std::wstring id, type=L"note", title=L"Заметка", content, target, arguments;
    Box box{0,0,360,220};
    std::wstring group;
    int monitor=0;
    // keep, sleep (best effort), unload (explicitly selected by the user).
    std::wstring background=L"sleep";
    bool autoLaunch=false;
    std::wstring monitorId;
    struct Appearance {
        std::wstring variant=L"auto";
        float textScale=1;
        bool showTitle=true,showDate=true,clock24=true,showSeconds=false;
        unsigned metrics=3; // CPU, memory, disk activity, physical network: 1/2/4/8.
        float radius=-1; // -1 inherits the canvas card setting; zero is square.
    } appearance;
};
struct SavedWindow {
    std::wstring process, title, className, monitor;
    Box normalized{0.05f,0.05f,0.6f,0.8f};
    bool floating=false;
};
struct Scene {
    std::wstring id, name, description, accent=L"A5C7A3", backdrop=L"18211F", wallpaper;
    bool gaming=false, autoTile=false;
    std::wstring layout=L"columns";
    float zoom=1;
    std::vector<Card> cards;
    std::vector<SavedWindow> windows;
    std::wstring appearance=L"dark";
    std::wstring icon=L"",network=L"links";
    float networkDensity=1;
    float networkSpeed=1;
    bool networkMotion=false;
};
struct Palette {
    bool light=false;
    COLORREF canvas{},panel{},card{},field{},text{},muted{},border{},accent{};
};
Palette PaletteFor(const Scene& scene);
struct Config {
    int version=1;
    std::wstring active=L"play";
    bool enabled=false, wallpaperEnabled=true, autoStart=false, softwareRendering=true;
    struct Interface {
        std::wstring searchPosition=L"bottom",dockPosition=L"custom",railPosition=L"left";
        std::wstring barPosition=L"top",trayPosition=L"right",settingsPosition=L"custom";
        bool railLabels=false,showActivity=true,compactLaunchers=true,showTray=true;
        bool barVertical=false,railVertical=true,dockVertical=false,trayVertical=false,searchVertical=false;
        bool settingsVertical=false;
        float searchX=0.5f,searchY=0.5f;
        float barX=0.5f,barY=0,railX=0,railY=0.5f,dockX=0,dockY=1,trayX=1,trayY=1,settingsX=1,settingsY=0;
        float textScale=1;
        int designVersion=2;
        float barRadius=18,railRadius=18,dockRadius=18,trayRadius=18,searchRadius=22,settingsRadius=18,cardRadius=18;
        float windowRadius=20;
        bool railOrbit=false;
    } ui;
    struct FileRoot {std::wstring path;int depth=-1;};
    struct FileSearch {bool initialized=false,enabled=false,paused=false,allFixedDrives=true,fastNtfs=false;std::vector<FileRoot> roots;} files;
    std::vector<Scene> scenes;
};
Box MinimumCardSize(const Card& card);
enum class UiElement { Navigation, Windows, Dock, Tray, Search, Settings };
std::wstring& UiPosition(Config::Interface& ui,UiElement element);
std::wstring_view UiPosition(const Config::Interface& ui,UiElement element);
bool& UiVertical(Config::Interface& ui,UiElement element);
bool UiVertical(const Config::Interface& ui,UiElement element);
std::pair<float&,float&> UiAnchor(Config::Interface& ui,UiElement element);
Box InterfaceBox(const Config::Interface& ui,UiElement element,Box area,Box size={});
float& UiRadius(Config::Interface& ui,UiElement element);
float UiRadius(const Config::Interface& ui,UiElement element);
Box LauncherBox(const Config::Interface& ui,Box area);
struct RailGrid {
    Box size;float cellWidth=48,cellHeight=48,padding=8;int rows=1,columns=1,capacity=1;bool vertical=true;
    Box Cell(int index)const;
};
RailGrid LayoutRail(int count,bool vertical,bool labels,Box available,float scale=1);
struct Monitor {
    std::wstring id, label;
    RECT bounds{}, work{};
    bool primary=false;
    UINT dpi=96;
};
Config DefaultConfig();
std::wstring NewId();
std::string Utf8(std::wstring_view value);
std::wstring Wide(std::string_view value);
std::optional<std::wstring> ReadUtf8(const fs::path& path);
bool WriteAtomic(const fs::path& path, std::wstring_view text);
std::wstring Serialize(const Config& config);
bool Deserialize(std::wstring_view text, Config& result, std::wstring& error);
Config LoadConfig(const fs::path& path, std::wstring& error);
Scene* FindScene(Config& config, std::wstring_view id);
Box ClampBox(Box box, Box area, float minWidth=180, float minHeight=100);
std::vector<Box> TileLayout(Box work, int count, std::wstring_view layout, float gap=12);
Box Normalize(Box box, Box work);
Box Denormalize(Box box, Box work);
bool IsWebUrl(std::wstring_view url);
bool IsLikelyWebAddress(std::wstring_view input);
std::wstring NormalizeWebUrl(std::wstring value);
std::wstring Timestamp();
}
