#pragma once
#include "platform.h"
#include "file_index.h"
#include <thread>
#include <mutex>
#include <atomic>
namespace gv {
class Application;
class Launcher {
public:
    explicit Launcher(Application& app):app_(app){}
    ~Launcher();
    void Show(bool addToDesktop=false,int monitor=-1);void Hide();void Toggle();
    bool Visible()const{return window_&&IsWindowVisible(window_);}
    HWND Window()const{return window_;}
    bool Message(const MSG& message);
    void SetStatus(std::wstring status,bool busy=false);
    bool ExportPreview(const fs::path& path);
    void UpdateStyle();
    void PrepareIndex();void StopIndex();void RequestStopIndex();
    void FilesChanged();
private:
    friend struct InteractionProbe;
    friend struct DesktopRegressionProbe;
    friend class Application;
    Application& app_;HWND window_{},search_{},list_{};HFONT font_{},large_{},small_{},badge_{};HBRUSH background_{},field_{};
    std::wstring status_;bool launching_=false,addToDesktop_=false;int addMonitor_=-1;
    std::vector<WindowInfo> running_;
    std::thread worker_;std::mutex mutex_;std::vector<Program> programs_,filtered_;bool loaded_=false,busy_=false;
    HANDLE indexStop_{},indexWake_{};
    std::vector<Program> pendingLearn_;std::wstring pendingQuery_,lastQuery_;
    bool fullRefresh_=false;std::atomic<bool> indexing_=false,indexWriteFailed_=false;
    std::vector<FileHit> fileResults_;
    void IndexLoop();void RequestRefresh();void Learn(const Program& program);void PublishIndex(const std::vector<Program>& programs);
    void Create(bool index=true);void Filter();void Execute(bool pin);void Browse();void Actions();void Layout();void Fonts(UINT dpi);
    std::optional<Program> Selected()const;
    void PaintFrame(HDC dc,RECT client);void DrawResult(HDC dc,RECT row,const Program& program,bool selected);
    void DrawCommand(const DRAWITEMSTRUCT& item);
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
};
}
