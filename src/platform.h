#pragma once
#include <atomic>
#include "core.h"
#include <memory>
#include <map>
#include <set>
#include <shobjidl.h>
#include <wrl/client.h>

namespace gv {
using Microsoft::WRL::ComPtr;
fs::path ExePath();
std::vector<Monitor> GetMonitors();
Box RectBox(RECT r);
bool IsFullscreenOnMonitor(HWND window,const RECT& monitor,DWORD shellProcess);
void SetShellPanelLayer(HWND window,bool topmost,HWND behind=nullptr);
struct WindowInfo { HWND hwnd{};DWORD pid{};std::wstring title,process,className,imageName,appId; };
std::vector<WindowInfo> ListAppWindows(bool includeHidden=false);
struct ProgramTarget {std::wstring executable,imageName,arguments,workingDirectory,appId,url;};
ProgramTarget ResolveProgramTarget(std::wstring_view target,std::wstring_view arguments=L"");
bool MatchesProgram(const WindowInfo& window,const ProgramTarget& target);
std::vector<WindowInfo> FindProgramWindows(const ProgramTarget& target,DWORD launchedPid=0);
bool FocusAppWindow(HWND window);
bool LaunchTarget(std::wstring_view target,std::wstring_view args,DWORD* processId=nullptr);
bool SetAutoStart(bool enabled,const fs::path& dataDirectory={});
void RemoveOwnedAutoStart();
bool IsAutoStartEnabled();
HICON CreateAppIcon();
int RunGuardian(DWORD parent,const fs::path& dir);
void RecoverSession(const fs::path& dir);
void RequestRescue(DWORD parent);

// Pass real Win shortcuts through; mask only a solitary Win release.
struct WinTapSequence {
    bool left=false,right=false,combined=false;
    bool Key(UINT key,bool down,bool modifiers=false);
};
class WinKeyGuard {
public:
    ~WinKeyGuard(){Stop();}
    void Start(HWND receiver,UINT message);void Stop();
    bool TakeTap(WPARAM generation);
    bool ReleasePending();
private:
    HHOOK hook_{};WinTapSequence sequence_;HANDLE thread_{},ready_{};DWORD threadId_{};
    std::atomic<HWND> receiver_{};std::atomic<UINT> message_{},generation_{1};std::atomic<bool> tapQueued_{};
    std::atomic<UINT> pendingRelease_{};
    static thread_local WinKeyGuard* current_;
    static DWORD WINAPI Run(void*);
    static LRESULT CALLBACK Keyboard(int code,WPARAM message,LPARAM value);
};

// Explorer stays running. Only the recorded windows are hidden and restored.
class ShellChrome {
public:
    explicit ShellChrome(fs::path directory):dir_(std::move(directory)){}
    ~ShellChrome(){Restore();}
    bool Begin();
    bool Begin(const std::vector<HWND>& windows);
    void Refresh();
    void Restore();
    bool Active()const{return active_;}
    static std::vector<HWND> Discover();
    static void Recover(const fs::path& directory);
    static bool RepairTaskbars(const fs::path& reportPath={});
private:
    fs::path dir_;bool active_=false;
};

struct Program {std::wstring name,target;HWND window{};std::wstring searchKey;unsigned kind=0;}; // 1: file, 2: directory.
struct ProgramFolder {fs::path path;bool recursive=true;};
std::vector<ProgramFolder> ProgramFolders();
std::vector<Program> InstalledPrograms();
class WindowManager {
public:
    struct Entry {WindowInfo info;std::wstring scene;WINDOWPLACEMENT original{sizeof(WINDOWPLACEMENT)},placement{sizeof(WINDOWPLACEMENT)};ULONG_PTR token{};bool hidden=false,floating=false;};
    WindowManager(fs::path directory,Config& configuration);
    ~WindowManager();
    bool Attach(HWND hwnd,const std::wstring& scene);
    bool IsManaged(HWND hwnd)const;
    void Detach(HWND hwnd);
    void Switch(const std::wstring& scene);
    void Capture();
    void Tile(const std::vector<Monitor>& monitors, int topInset=76);
    void RestoreAll();
    void AdoptSaved();
    bool RepairSavedHelpers();
    void HandleNewWindow(HWND hwnd);
    void ToggleFloating(HWND hwnd);
    void MarkFloating(HWND hwnd);
    void MoveToScene(HWND hwnd,const std::wstring& scene);
    size_t Count(const std::wstring& scene)const;
    const std::vector<Entry>& Entries()const{return entries_;}
    void ExpectLaunch(DWORD pid,const std::wstring& scene);
private:
    fs::path dir_;Config& config_;std::vector<Entry> entries_;std::wstring prop_;ULONG_PTR nextToken_=1;
    std::map<DWORD,std::pair<std::wstring,ULONGLONG>> pending_;
    bool WriteRecovery();void Prune();void ApplyVisibility(Entry& e,bool visible);bool Valid(const Entry& e)const;
};
class Wallpaper {
public:
    explicit Wallpaper(fs::path directory):dir_(std::move(directory)){}
    bool Apply(const Scene& scene);
    void Restore(bool finish=false);
private:fs::path dir_;
};
}
