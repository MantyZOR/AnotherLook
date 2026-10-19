#pragma once
#include "design.h"
#include <shellapi.h>

namespace gv {
class Application;
struct AccessibleTrayAction;
struct TrayIcon {
    HWND window{};DWORD pid{};UINT id{},callback{},version{},state{};GUID guid{};
    std::wstring tip;std::shared_ptr<MonoIcon> image;
    bool recovered=false;std::shared_ptr<AccessibleTrayAction> accessibility;
};
std::vector<TrayIcon> ReadExplorerTray(HWND explorer);
bool InvokeAccessibleTray(const TrayIcon& icon,bool rightClick);
bool WriteExplorerTrayDiagnostics(const fs::path& path);
struct TrayCommand {std::wstring label,target,arguments;};
std::vector<TrayCommand> SystemTrayCommands(const GUID& guid);
enum class TrayUtility {Network,Devices};
void ShowTrayUtility(Application& app,HWND owner,POINT point,TrayUtility utility);
bool IsNetworkTrayIcon(const GUID& guid);
bool IsDeviceTrayIcon(const GUID& guid);
// Shell32's interprocess notification packet uses 32-bit USER handles even on x64.
// Wire layout reference: ManagedShell.Interop/NativeMethods.Shell32.cs (Apache-2.0).
namespace traywire {
#pragma pack(push,4)
struct IconData {DWORD size,window,id,flags,callback,icon;wchar_t tip[128];DWORD state,stateMask;wchar_t info[256];UINT version;wchar_t infoTitle[64];DWORD infoFlags;GUID guid;DWORD balloon;};
struct Packet {DWORD signature,operation;IconData icon;};
struct Identifier {DWORD signature,operation,size,padding,window,id;GUID guid;};
#pragma pack(pop)
static_assert(sizeof(IconData)==956);
struct Update {UINT operation{};IconData data{};};
std::optional<Update> Decode(const COPYDATASTRUCT& packet);
std::pair<WPARAM,LPARAM> Callback(const TrayIcon& icon,UINT event,POINT point);
}
class NotificationTray {
public:
    explicit NotificationTray(Application& app):app_(app){}
    ~NotificationTray();
    bool Start(bool isolatedTest=false);
    void Stop();void Sync(bool visible,bool topmost=true,HWND behind=nullptr);void Expand();
    const std::vector<TrayIcon>& Items()const{return icons_;}
    bool Active()const{return proxy_!=nullptr;}
    bool Visible()const{return panel_&&IsWindowVisible(panel_);}
    HWND Window()const{return panel_;}
    bool ExportPreview(const fs::path& path);
    void UpdateStyle();
    bool WriteDiagnostics(const fs::path& path)const;
    bool Invoke(const TrayIcon& icon,bool rightClick,POINT point);
private:
    friend struct TrayProbe;
    Application& app_;HWND proxy_{},panel_{},explorer_{},tooltip_{};HFONT font_{};
    std::vector<TrayIcon> icons_;std::vector<std::pair<RECT,size_t>> hits_;
    bool isolated_=false,expanded_=false,dirty_=false,forwarding_=false,doubleRelease_=false,topmost_=true;
    float textScale_=0;
    PanelBitmap panelBitmap_;
    int columns_=1,rows_=1;int hover_=-1;RECT more_{},network_{},devices_{};uint64_t updates_=0,paints_=0;
    void Update(const traywire::Update& update);void Changed();void Layout(bool refresh=true);void Paint();void Draw(HDC target,RECT client);void Tooltips();
    void SendTaskbarCreated();LRESULT Forward(HWND,UINT,WPARAM,LPARAM);
    void RecoverExplorerIcons();
    bool InvokeSystem(const std::vector<TrayCommand>& commands,bool rightClick,POINT point);
    static LRESULT CALLBACK ProxyProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK PanelProc(HWND,UINT,WPARAM,LPARAM);
};
}
