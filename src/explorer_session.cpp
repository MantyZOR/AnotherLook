#include "explorer_session.h"
#include "platform.h"
#include <restartmanager.h>
#include <sddl.h>
#include <algorithm>
#include <stdexcept>

namespace gv {
namespace {
constexpr auto Record=L"explorer-session.json",Cancel=L"explorer-cancel.flag";
struct Handle {HANDLE value{};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
uint64_t Number(FILETIME time){return (uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;}
ShellProcess Stamp(DWORD pid){
    ShellProcess result;Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid)};
    FILETIME created{},exit{},kernel{},user{};DWORD session=0;
    if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&GetProcessTimes(process.value,&created,&exit,&kernel,&user)&&ProcessIdToSessionId(pid,&session))result={pid,session,Number(created)};
    return result;
}
bool NormalToken(){Handle token;TOKEN_ELEVATION elevation{};DWORD bytes=0;return OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)&&GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&bytes)&&!elevation.TokenIsElevated;}
std::wstring ObjectName(HANDLE object){wchar_t name[256]{};DWORD bytes=0;return GetUserObjectInformationW(object,UOI_NAME,name,sizeof(name),&bytes)?name:L"";}
std::wstring DesktopName(){return ObjectName(GetThreadDesktop(GetCurrentThreadId()));}
std::wstring UserSid(HANDLE process=GetCurrentProcess()){
    Handle token;if(!OpenProcessToken(process,TOKEN_QUERY,&token.value))return {};DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);std::vector<BYTE> data(bytes);
    if(!bytes||!GetTokenInformation(token.value,TokenUser,data.data(),bytes,&bytes))return {};LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return {};std::wstring result=sid;LocalFree(sid);return result;
}
fs::path ExplorerPath(){wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return {};return fs::path(windows)/L"explorer.exe";}
bool ExplorerProcess(DWORD pid){
    if(!pid)return false;Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};wchar_t path[32768]{};DWORD length=32768;
    if(!process.value||!QueryFullProcessImageNameW(process.value,0,path,&length)||_wcsicmp(path,ExplorerPath().c_str()))return false;
    auto identity=Stamp(pid),own=Stamp(GetCurrentProcessId());auto user=UserSid();return identity.pid&&identity.session==own.session&&!user.empty()&&UserSid(process.value)==user;
}
std::optional<ShellProcess> CurrentExplorer(){
    DWORD pid=0;GetWindowThreadProcessId(GetShellWindow(),&pid);if(ExplorerProcess(pid))return Stamp(pid);
    HWND window=nullptr;while((window=FindWindowExW(nullptr,window,L"Shell_TrayWnd",nullptr))){GetWindowThreadProcessId(window,&pid);if(ExplorerProcess(pid))return Stamp(pid);}return {};
}
bool Ready(){
    DWORD desktopPid=0;GetWindowThreadProcessId(GetShellWindow(),&desktopPid);if(!ExplorerProcess(desktopPid))return false;
    HWND window=nullptr;while((window=FindWindowExW(nullptr,window,L"Shell_TrayWnd",nullptr))){DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==desktopPid&&IsWindowVisible(window))return true;}return false;
}
bool RestoreNative(std::wstring& error){
    if(GetSystemMetrics(SM_SHUTTINGDOWN))return true;
    if(Ready())return true;
    if(!NormalToken()){error=L"Для возврата Explorer запусти Another Look без прав администратора.";return false;}
    // One recovery launcher per interactive session, including the guardian.
    auto name=L"Local\\AnotherLook.ExplorerRecovery."+std::to_wstring(Stamp(GetCurrentProcessId()).session);
    Handle lock{CreateMutexW(nullptr,FALSE,name.c_str())};if(!lock.value){error=L"Не удалось подготовить возврат Explorer.";return false;}
    DWORD acquired=WaitForSingleObject(lock.value,12000);if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){error=L"Другой помощник ещё возвращает Explorer. Попробуй ещё раз.";return false;}
    struct Unlock{HANDLE value;~Unlock(){ReleaseMutex(value);}} unlock{lock.value};
    if(Ready())return true;
    if(!CurrentExplorer()){
        auto path=ExplorerPath();std::wstring command=L"\""+path.wstring()+L"\"",desktop=ObjectName(GetProcessWindowStation())+L"\\"+DesktopName();STARTUPINFOW start{sizeof(start)};start.lpDesktop=desktop.data();start.dwFlags=STARTF_USESHOWWINDOW;start.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION process{};
        if(path.empty()||!CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,path.parent_path().c_str(),&start,&process)){error=L"Windows не смогла запустить Explorer. Можно повторить возврат или запустить explorer.exe через Диспетчер задач.";return false;}
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
    for(int i=0;i<120;++i){if(Ready())return true;Sleep(100);}
    error=L"Explorer ещё не восстановил рабочий стол и панель задач. Another Look остаётся доступен; повтори возврат.";return false;
}
class NativeBackend final:public ExplorerSessionBackend {
    HANDLE lease_{};fs::path directory_;std::wstring desktop_;
public:
    explicit NativeBackend(fs::path directory):directory_(std::move(directory)),desktop_(DesktopName()){}
    ~NativeBackend(){Release();}
    bool Acquire(std::wstring& error)override{
        if(desktop_.empty()||DesktopName()!=desktop_){error=L"Рабочий стол изменился во время подготовки.";return false;}
        if(!NormalToken()){error=L"Замена Explorer доступна при обычном запуске, без прав администратора.";return false;}
        if(lease_)return true;auto name=L"Local\\AnotherLook.ExplorerOwner."+std::to_wstring(Owner().session);lease_=CreateMutexW(nullptr,FALSE,name.c_str());DWORD result=GetLastError();
        if(!lease_||result==ERROR_ALREADY_EXISTS){Release();error=L"Другой экземпляр Another Look уже управляет заменой Explorer.";return false;}return true;
    }
    void Release()override{if(lease_){CloseHandle(lease_);lease_=nullptr;}}
    ShellProcess Owner()override{return Stamp(GetCurrentProcessId());}
    std::wstring User()override{return UserSid();}
    std::optional<ShellProcess> Explorer()override{return DesktopName()==desktop_?CurrentExplorer():std::nullopt;}
    bool Stop(const ShellProcess& process,std::wstring& error)override{
        auto current=Explorer();if(!current||*current!=process||!ExplorerProcess(process.pid)){error=L"Explorer изменился во время подготовки. Переключение отменено.";return false;}
        return ShutdownShellProcess(process,error);
    }
    bool Restore(std::wstring& error)override{if(DesktopName()!=desktop_){error=L"Возврат запрошен с другого рабочего стола.";return false;}ShellChrome::Recover(directory_);return RestoreNative(error);}
};
std::wstring Field(const json::JsonObject& object,const wchar_t* key){return std::wstring(object.GetNamedString(key));}
void RemoveRecord(const fs::path& dir){std::error_code error;fs::remove(dir/Record,error);}
}
bool ShutdownShellProcess(const ShellProcess& process,std::wstring& error){
    if(!process.pid||Stamp(process.pid)!=process){error=L"Процесс изменился до запроса остановки.";return false;}
    Handle target{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process.pid)};if(!target.value){error=L"Нет доступа к Explorer.";return false;}
    DWORD session=0;wchar_t key[CCH_RM_SESSION_KEY+1]{};DWORD result=RmStartSession(&session,0,key);if(result!=ERROR_SUCCESS){error=L"Не удалось открыть Restart Manager: "+std::to_wstring(result);return false;}
    struct End{DWORD session;~End(){RmEndSession(session);}} end{session};
    RM_UNIQUE_PROCESS app{};app.dwProcessId=process.pid;app.ProcessStartTime={static_cast<DWORD>(process.created),static_cast<DWORD>(process.created>>32)};
    result=RmRegisterResources(session,0,nullptr,1,&app,0,nullptr);
    UINT needed=0,count=16;DWORD reboot=0;RM_PROCESS_INFO affected[16]{};
    if(result==ERROR_SUCCESS)result=RmGetList(session,&needed,&count,affected,&reboot);
    bool exact=result==ERROR_SUCCESS&&count==1&&!reboot&&affected[0].Process.dwProcessId==process.pid&&Number(affected[0].Process.ProcessStartTime)==process.created;
    if(!exact){error=L"Restart Manager не подтвердил остановку только выбранного процесса. Переключение отменено.";return false;}
    result=RmShutdown(session,0,nullptr); // No force flag, no resources/services by filename.
    if(result!=ERROR_SUCCESS){error=L"Windows отклонила штатную остановку Explorer: "+std::to_wstring(result);return false;}
    if(WaitForSingleObject(target.value,10000)!=WAIT_OBJECT_0){error=L"Explorer не завершился. Принудительная остановка не используется.";return false;}return true;
}
bool IsExplorerShellWindow(HWND window){wchar_t name[80]{};GetClassNameW(window,name,80);if(window!=GetShellWindow()&&wcscmp(name,L"Shell_TrayWnd")&&wcscmp(name,L"Progman"))return false;DWORD pid=0;GetWindowThreadProcessId(window,&pid);return ExplorerProcess(pid);}
void CancelExplorerReplacement(const fs::path& directory){WriteAtomic(directory/Cancel,L"Return to Windows requested");}
bool RecoverExplorerSession(const fs::path& directory,bool force){
    auto raw=ReadUtf8(directory/Record);if(!raw)return true;if(raw->size()>4096)return false;
    try{
        auto record=json::JsonObject::Parse(*raw);auto own=Stamp(GetCurrentProcessId());ShellProcess owner{static_cast<DWORD>(record.GetNamedNumber(L"ownerPid")),static_cast<DWORD>(record.GetNamedNumber(L"session")),std::stoull(Field(record,L"ownerCreated"))};
        if(record.GetNamedNumber(L"version")!=1||!owner.pid||!owner.created||owner.session!=own.session||Field(record,L"user")!=UserSid()||Field(record,L"desktop")!=DesktopName())return false;
        if(!force&&Stamp(owner.pid)==owner)return true;
        CancelExplorerReplacement(directory);ShellChrome::Recover(directory);std::wstring error;if(!RestoreNative(error))return false;RemoveRecord(directory);return true;
    }catch(...){return false;}
}
ExplorerSession::ExplorerSession(fs::path directory,std::shared_ptr<ExplorerSessionBackend> backend):dir_(std::move(directory)),backend_(backend?std::move(backend):std::make_shared<NativeBackend>(dir_)){}
ExplorerSession::~ExplorerSession(){
    cancel_=true;Wait();if(Engaged()){try{ReturnWork();}catch(...){/* Journal remains for guardian/startup recovery. */}}
}
ExplorerStatus ExplorerSession::Status()const{std::lock_guard lock(mutex_);return status_;}
void ExplorerSession::Publish(ExplorerPhase phase,bool busy,std::wstring message,std::wstring error){
    {std::lock_guard lock(mutex_);status_={phase,busy,std::move(message),std::move(error)};phase_=phase;}
    if(notify_)PostMessageW(notify_,ExplorerSessionChanged,0,0);
}
void ExplorerSession::Wait(){if(worker_.joinable())worker_.join();}
bool ExplorerSession::Cancelled()const{std::error_code error;return cancel_||fs::exists(dir_/Cancel,error);}
bool ExplorerSession::Enter(HWND notify){
    if(Engaged()||Status().busy)return false;Wait();std::error_code ec;fs::remove(dir_/Cancel,ec);cancel_=false;notify_=notify;
    Publish(ExplorerPhase::Preparing,true,L"Подготавливаем замену Explorer…");
    try{worker_=std::thread([this]{try{winrt::init_apartment();EnterWork();winrt::uninit_apartment();}catch(...){Publish(ExplorerPhase::Overlay,false,{},L"Не удалось запустить подготовку.");}});}catch(...){Publish(ExplorerPhase::Overlay,false,{},L"Не удалось запустить подготовку.");return false;}return true;
}
bool ExplorerSession::Return(HWND notify){
    cancel_=true;CancelExplorerReplacement(dir_);if(Status().busy)return true;Wait();notify_=notify;
    if(!Engaged())return true;
    Publish(ExplorerPhase::Restoring,true,L"Возвращаем рабочий стол Windows…");
    try{worker_=std::thread([this]{try{winrt::init_apartment();ReturnWork();winrt::uninit_apartment();}catch(...){Publish(ExplorerPhase::RecoveryRequired,false,{},L"Не удалось запустить возврат. Повтори действие.");}});}catch(...){Publish(ExplorerPhase::RecoveryRequired,false,{},L"Не удалось запустить возврат. Повтори действие.");return false;}return true;
}
void ExplorerSession::EnterWork(){
    bool recorded=false;try{
        if(fs::exists(dir_/Record)){Publish(ExplorerPhase::RecoveryRequired,false,{},L"Сначала заверши предыдущий возврат Explorer через меню рабочего стола.");return;}
        std::wstring error;if(!backend_->Acquire(error)){Publish(ExplorerPhase::Overlay,false,{},error);return;}
        auto explorer=backend_->Explorer();auto owner=backend_->Owner();auto user=backend_->User();
        if(!explorer||!owner.pid||!owner.created||user.empty()){backend_->Release();Publish(ExplorerPhase::Overlay,false,{},L"Не найден рабочий стол Explorer. Сначала верни обычный рабочий стол Windows.");return;}
        if(Cancelled()){backend_->Release();Publish(ExplorerPhase::Overlay,false);return;}
        json::JsonObject record;record.SetNamedValue(L"version",json::JsonValue::CreateNumberValue(1));record.SetNamedValue(L"ownerPid",json::JsonValue::CreateNumberValue(owner.pid));record.SetNamedValue(L"session",json::JsonValue::CreateNumberValue(owner.session));record.SetNamedValue(L"ownerCreated",json::JsonValue::CreateStringValue(std::to_wstring(owner.created)));record.SetNamedValue(L"user",json::JsonValue::CreateStringValue(user));record.SetNamedValue(L"desktop",json::JsonValue::CreateStringValue(DesktopName()));
        if(!WriteAtomic(dir_/Record,std::wstring(record.Stringify()))){backend_->Release();Publish(ExplorerPhase::Overlay,false,{},L"Не удалось записать состояние восстановления. Explorer продолжает работать.");return;}
        recorded=true;if(Cancelled()){ReturnWork();return;}
        if(!backend_->Stop(*explorer,error)){ReturnWork(error);return;}
        for(int i=0;i<10&&!Cancelled();++i)Sleep(50);
        if(Cancelled()){ReturnWork();return;}
        if(backend_->Explorer()){ReturnWork(L"Explorer запустился снова. Замена отменена; обычный рабочий стол восстановлен.");return;}
        Publish(ExplorerPhase::Standalone,false,L"Another Look работает вместо Explorer. При выходе Explorer вернётся.");
    }catch(...){if(recorded)ReturnWork(L"Не удалось завершить переключение.");else{backend_->Release();Publish(ExplorerPhase::Overlay,false,{},L"Не удалось подготовить замену Explorer.");}}
}
void ExplorerSession::ReturnWork(std::wstring reason){
    Publish(ExplorerPhase::Restoring,true,L"Возвращаем рабочий стол Windows…");std::wstring error;
    try{if(backend_->Restore(error)){RemoveRecord(dir_);backend_->Release();Publish(ExplorerPhase::Overlay,false,L"Explorer восстановлен.",std::move(reason));}
        else Publish(ExplorerPhase::RecoveryRequired,false,L"Возврат ещё не завершён.",std::move(error));}
    catch(...){Publish(ExplorerPhase::RecoveryRequired,false,{},L"Не удалось вернуть Explorer. Повтори возврат; запись восстановления сохранена.");}
}
}
