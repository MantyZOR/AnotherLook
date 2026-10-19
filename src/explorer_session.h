#pragma once
#include "core.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace gv {
inline constexpr UINT ExplorerSessionChanged=WM_APP+0x92;
struct ShellProcess {DWORD pid=0,session=0;uint64_t created=0;bool operator==(const ShellProcess&)const=default;};
enum class ExplorerPhase {Overlay,Preparing,Standalone,Restoring,RecoveryRequired};
struct ExplorerStatus {ExplorerPhase phase=ExplorerPhase::Overlay;bool busy=false;std::wstring message,error;};
// The same transaction is used by the native adapter and fault-injection tests.
// No registry policy or persistent shell selection is changed by this mode.
class ExplorerSessionBackend {
public:
    virtual ~ExplorerSessionBackend()=default;
    virtual bool Acquire(std::wstring& error)=0;
    virtual void Release()=0;
    virtual ShellProcess Owner()=0;
    virtual std::wstring User()=0;
    virtual std::optional<ShellProcess> Explorer()=0;
    virtual bool Stop(const ShellProcess&,std::wstring& error)=0;
    virtual bool Restore(std::wstring& error)=0;
};
class ExplorerSession {
public:
    explicit ExplorerSession(fs::path directory,std::shared_ptr<ExplorerSessionBackend> backend={});
    ~ExplorerSession();
    bool Enter(HWND notify=nullptr);
    bool Return(HWND notify=nullptr);
    ExplorerStatus Status()const;
    ExplorerPhase Phase()const{return phase_.load();}
    bool Engaged()const{return Phase()!=ExplorerPhase::Overlay;}
    void Wait(); // shutdown/tests only; UI uses completion messages.
private:
    fs::path dir_;std::shared_ptr<ExplorerSessionBackend> backend_;
    mutable std::mutex mutex_;ExplorerStatus status_;std::thread worker_;
    std::atomic<bool> cancel_{false};std::atomic<ExplorerPhase> phase_{ExplorerPhase::Overlay};HWND notify_{};
    void Publish(ExplorerPhase,bool busy,std::wstring message={},std::wstring error={});
    void EnterWork();void ReturnWork(std::wstring reason={});
    bool Cancelled()const;
};
bool RecoverExplorerSession(const fs::path& directory,bool force=false);
void CancelExplorerReplacement(const fs::path& directory);
bool IsExplorerShellWindow(HWND window);
// Graceful, PID-and-creation-time-scoped Restart Manager operation. Never force.
// Exposed separately so the actual shutdown adapter can be tested on our fixture.
bool ShutdownShellProcess(const ShellProcess& process,std::wstring& error);
}
