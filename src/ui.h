#pragma once
#include <tuple>
#include "platform.h"
#include "explorer_session.h"
#include "web.h"
#include "launcher.h"
#include "design.h"
#include "tray.h"
#include "customize.h"
#include "system_load.h"
#include "file_index.h"
#include "file_settings.h"
#include "network.h"
#include "orbit_layout.h"
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <set>
#include <tuple>
struct IWICBitmap;
namespace gv {
class Application;
Application* CurrentApplication();
struct Hit {Box box;std::function<void()> action;bool canvas=false;std::function<void()> context;std::wstring draggable,tooltip;int orbitSlot=-1;};
class Surface {
public:
    Surface(Application& app,bool bar,int monitor,bool rail=false,UiElement element=UiElement::Navigation);
    ~Surface();
    HWND hwnd{};bool bar=false,rail=false,covered=false;int monitor=0;
    UiElement element=UiElement::Navigation;
    void Create(bool preview);
    void Paint();
    void Invalidate();
    void Discard();
    void SyncWeb();
    void Place(bool preview);
    void Show(bool visible,bool raise=false);
    void SetPanelLayer(bool topmost,HWND behind=nullptr);
    void ShowSettings();
    void Customize();
    void ShowActivity();
    void Reveal(const Card& card);
    void ResetView();
    bool ExportPreview(const fs::path& path);
    bool RenderCardPreview(HWND host,HDC target,RECT bounds,const Card& card);
    bool HasVisibleSystemWidget()const;
    unsigned VisibleSystemMetrics()const;
    bool TickSeconds(bool paint=false);
    void SyncNetworkAnimation(bool active);
    void ResetInteraction(){hover_=-1;pressedHit_.reset();orbitHover_.Reset();}
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
private:
    Application& app_;
    ComPtr<ID2D1RenderTarget> rt_;ComPtr<ID2D1SolidColorBrush> brush_;
    PanelBitmap panelBitmap_;
    std::vector<Hit> hits_;std::vector<std::pair<std::wstring,Box>> cards_;
    std::optional<Hit> pressedHit_;
    friend struct InteractionProbe;
    friend struct DesktopRegressionProbe;
    int hover_=-1;float panX_=0,panY_=0;POINT down_{};std::wstring drag_;bool resize_=false,panning_=false;
    bool appbarRegistered_=false;
    bool popupOpen_=false,drawingCards_=false,scaledUi_=false,panelTopmost_=true,cardPreview_=false;
    std::wstring hoverCard_;
    ComPtr<IWICBitmap> previewBitmap_;
    std::vector<uint32_t> previewPixels_;
    ComPtr<ID2D1Bitmap> networkHaloBlue_,networkHaloViolet_;
    ComPtr<ID2D1Bitmap> networkBackdrop_;
    COLORREF networkBackdropBase_=0;
    using TextKey=std::tuple<std::wstring,float,float,float,int,int>;
    std::map<TextKey,ComPtr<IDWriteTextLayout>> textLayouts_;
    void CachedText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight,bool wrap,FontRole role,bool center,bool inkCenter=false);
    RECT reservation_{};
    HWND tooltip_{};std::wstring tooltipText_;
    std::array<HCURSOR,3> pointerCursors_{};HCURSOR pointerCursor_{};
    void SetPointerCursor(bool hand,bool resize=false);
    std::map<std::wstring,Box> before_;std::set<std::wstring> selected_;
    void Button(std::wstring_view text,Box box,std::function<void()> action,bool active=false,bool quiet=false);
    void Text(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight=400,bool wrap=false,FontRole role=FontRole::Interface);
    void CenterText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight=400);
    void FitCenterText(std::wstring_view text,Box box,float size,D2D1_COLOR_F color,int weight=400);
    void GlyphIcon(Glyph glyph,Box box,D2D1_COLOR_F color);
    void IconButton(Glyph glyph,std::wstring_view label,Box box,std::function<void()> action,bool active=false,bool quiet=true);
    void ProgramIcon(const std::wstring& target,Box box);
    void WindowIcon(const WindowInfo& window,Box box,float opacity=1);
    void Image(const fs::path& path,Box box,float opacity=1,bool topAligned=false);
    struct Bitmap {ComPtr<ID2D1Bitmap> value;fs::file_time_type modified{};};
    std::map<std::wstring,Bitmap> bitmaps_;
    std::map<std::wstring,ComPtr<ID2D1Bitmap>> iconBitmaps_;
    Box Viewport()const;
    void DrawBar(float width,float height);
    void DrawTool(float width,float height);
    void DrawRail(float width,float height);
    void DrawOrbit(float width,float height);
    std::vector<const WindowInfo*> OrbitWindows()const;
    void ScrollOrbit(int delta);
    void TickOrbit();
    void HoverOrbit(int hit);
    void SyncOrbitTimer();
    bool ContainsHit(const Hit& hit,float x,float y)const;
    OrbitMotion orbit_;
    OrbitHoverMotion orbitHover_;
    Box orbitFrame_{};
    int orbitVisible_=0;bool orbitTimerActive_=false;
    ComPtr<ID2D1PathGeometry> orbitArc_;
    int orbitArcCount_=0;
    struct OrbitCoverage {int x=0,y=0,w=0,h=0;std::vector<uint8_t> alpha;};
    std::array<OrbitCoverage,OrbitCapacity> orbitCoverage_;
    std::vector<uint32_t> orbitBase_;
    using OrbitRasterKey=std::tuple<int,int,UINT,float,int,int,int,COLORREF,COLORREF,COLORREF,COLORREF,COLORREF>;
    std::optional<OrbitRasterKey> orbitRasterKey_;
    int orbitRasterPart_=0; // 0: full vector fallback, 1: coverage, 2: marks/centre.
    bool PaintOrbitRaster(int width,int height,UINT dpi);
    void DrawNetwork(float width,float height,Box dirty={});
    void TrackNetwork(POINT point,bool leave=false);
    void TickNetwork();
    CanvasNetwork network_;
    Box networkKey_{};float networkZoom_=0,networkDensity_=0,networkMotionTime_=0,networkLastTime_=-1;
    bool networkAnimating_=false;UINT networkInterval_=50;ULONGLONG networkFrameTick_=0;
    bool profilePaint_=false;double paintMs_[5]{};
    POINT networkPointer_{-10000,-10000},networkPainted_{-10000,-10000};
    ULONGLONG networkTick_=0;bool networkPending_=false;
    void Fill(Box box,D2D1_COLOR_F color,float radius=0);
    void Stroke(Box box,D2D1_COLOR_F color,float radius=0,float width=1);
    void DrawCard(Card& card,Box box);
    void CardMenu(const std::wstring& id);
    void AddMenu();
    void WindowsMenu();
    void OpenWindowMenu(const std::vector<WindowInfo>& windows);
    void ThemeMenu();
    void MouseDown(POINT point);
    void MouseMove(POINT point,WPARAM keys);
    void MouseUp(POINT point);
    Box CanvasBox(const Card& card)const;
    float Zoom()const;
    int CardMonitor(const Card& card)const;
    void Popup(HMENU menu,std::function<void(UINT)> onCommand);
    void SceneMenu(const std::wstring& id);
};
class Application {
public:
    // Declared first so private fonts outlive all windows, GDI handles and formats.
    Typography typography;
    Config config;fs::path dataDir;bool preview=false,quitting=false,showAppsOnStart=false,showSettingsOnStart=false,initializeFileSearch=false,enableFastOnStart=false;
    std::wstring fastSetupOnStart;
    std::vector<Monitor> monitors;
    ExplorerSession explorerSession;bool replaceExplorerOnStart=false;
    WindowManager windows;Wallpaper wallpaper;WebManager web;ShellChrome chrome;Launcher launcher;
    WinKeyGuard winKey;
    IconCache icons;
    NotificationTray notificationTray;
    Customization customization;
    SystemLoad systemLoad;
    FileIndex fileIndex;
    FileSearchSettings fileSettings;
    ComPtr<ID2D1Factory> d2d;ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1StrokeStyle> iconStroke;
    ID2D1PathGeometry* IconPath(Glyph glyph);
    std::vector<std::unique_ptr<Surface>> boards,bars,rails,tools;
    std::vector<WindowInfo> openWindows;
    HWND events{};HICON icon{};HWINEVENTHOOK foregroundHook{},windowHook{},moveHook{},geometryHook{},menuHook{};
    HANDLE guardianStop{},guardianProcess{};
    uint64_t paints=0,panelPaints=0,windowEvents=0,fullscreenNotices=0,clockTicks=0;
    explicit Application(fs::path directory,bool isPreview);
    ~Application();
    int Run(bool background);
    bool RenderPreview(const fs::path& path,const std::wstring& scene);
    Scene& Active();
    void SwitchScene(const std::wstring& id,bool show=true);
    bool RemoveScene(const std::wstring& id);
    bool RequestIndexAccess(HWND owner,std::wstring_view journals={});
    void ToggleOverview();
    void ShowOverview();
    void Enable();
    void Pause();
    void SaveSoon();
    void Save();
    void Refresh();
    void EditCard(const std::wstring& id);
    void AddCard(const std::wstring& type,int monitor);
    void AddFolder(int monitor);
    void AddScene();
    void OpenCard(const std::wstring& id,Surface& surface,bool separate=false);
    void UseProgram(const Program& program,bool pin,int monitor=-1);
    void LaunchProgram(const std::wstring& target,const std::wstring& arguments=L"",const std::wstring& name=L"",HWND existingWindow=nullptr);
    bool ActivateWindow(HWND window);
    void TryActivateProgram();
    void CancelProgramActivation();
    void ShowDesktop();
    void TogglePowerPanel();
    void HidePowerPanel();
    void PowerAction(int action,HWND owner);
    bool PowerPanelVisible()const{return powerOpen_;}
    void EnterWorkspace();
    void RefreshWindows();
    void WindowSwitcher(HWND owner);
    void SetTheme(int theme);
    void ChangeWallpaper();
    void ToggleStartup();
    void Quit();
    void UpdateMonitors();
    void ApplyState(bool show,bool sceneOnly=false);
    void ApplyInterface();
    void BeginTrayMenu(DWORD pid);
    struct WindowGroup {std::wstring key,name;std::vector<WindowInfo> windows;};
    std::vector<WindowGroup> WindowGroups(int monitor)const;
    Box PanelBox(UiElement element,int monitor,Box area={})const;
    void HandleWindowEvent(DWORD event,HWND window);
    void SyncPanels(bool reveal=false);
    void SchedulePanelSync(bool geometryChange=false);
    void ShowTrayMenu();
    void ExplorerModeMenu(HWND owner);void ReplaceExplorer();void ExplorerChanged();
    void RefreshExplorerChrome();bool ExplorerReturning()const{return explorerReturnPending_;}
    void UpdateTray();
    std::wstring ResourceText()const;
    void SyncSystemWidget();void SampleSystemWidget();
    void ConfigureFiles(bool rebuild=false);void SyncSeconds();
    Card* FindCard(const std::wstring& id);
    IDWriteTextFormat* Font(float size,int weight,FontRole role=FontRole::Interface);
    static LRESULT CALLBACK EventProc(HWND,UINT,WPARAM,LPARAM);
    static void CALLBACK WinEvent(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
private:
    std::map<std::tuple<int,int,FontRole>,ComPtr<IDWriteTextFormat>> fonts_;
    std::map<Glyph,ComPtr<ID2D1PathGeometry>> glyphPaths_;
    std::set<std::wstring> launched_;
    bool saving_=false,applying_=false;ULONGLONG lastTile_=0;
    HWND addingSceneOwner_{};
    bool explorerReturnPending_=false,explorerQuitPending_=false;
    ExplorerPhase explorerNotified_=ExplorerPhase::Overlay;std::wstring explorerError_;
    bool powerOpen_=false;int powerMonitor_=0;unsigned winReleaseRetries_=0;
    bool panelSyncPending_=false,syncingPanels_=false;
    bool systemPrompt_=false;
    friend struct DesktopRegressionProbe;
    std::set<HWND> externalMenus_;
    bool trayMenuPending_=false;DWORD trayMenuPid_=0;
    struct PendingProgram {ProgramTarget target;std::wstring name;DWORD pid=0,inputTime=0;ULONGLONG started=0,until=0;HWND preferred{};};
    std::optional<PendingProgram> pendingProgram_;
    bool activatingProgram_=false;
    bool systemTimer_=false;
    bool secondsTimer_=false;
    DWORD railForegroundPid_=0;
    std::map<HWND,HMONITOR> windowMonitors_;
    void SetHooks(bool on);
    void StartGuardian();
    void ShutdownStage(std::wstring_view stage);
    bool WithSystemPrompt(HWND owner,const std::function<bool()>& action);
};
D2D1_COLOR_F Color(std::wstring_view hex,float alpha=1);
D2D1_COLOR_F Ink(COLORREF color,float alpha=1);
bool EditDialog(HWND owner,Card& card,const std::wstring& caption);
bool ExportCardEditor(const Card& card,const fs::path& output);
}
