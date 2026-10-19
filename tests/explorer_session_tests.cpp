#include "explorer_session.h"
#include <iostream>
#include <stdexcept>
#include <shellapi.h>

using namespace gv;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Mock:ExplorerSessionBackend {
    fs::path dir;bool lease=false,acquireOK=true,stopOK=true,restoreOK=true,respawn=false,throwStop=false,throwRestore=false;
    bool present=true,recordSeen=false;int stops=0,restores=0;
    HANDLE stopped{},proceed{};
    explicit Mock(fs::path path):dir(std::move(path)){}
    ~Mock(){if(stopped)CloseHandle(stopped);if(proceed)CloseHandle(proceed);}
    bool Acquire(std::wstring& error)override{if(!acquireOK){error=L"lease refused";return false;}lease=true;return true;}
    void Release()override{lease=false;}
    ShellProcess Owner()override{return {GetCurrentProcessId(),1,1234};}
    std::wstring User()override{return L"fixture-user-not-a-Windows-SID";}
    std::optional<ShellProcess> Explorer()override{return present?std::optional<ShellProcess>{{3456,1,789}}:std::nullopt;}
    bool Stop(const ShellProcess& process,std::wstring& error)override{
        ++stops;auto raw=ReadUtf8(dir/L"explorer-session.json");recordSeen=raw&&raw->find(L"ownerCreated")!=raw->npos;Check(recordSeen,"shutdown occurred without durable recovery information");Check(process.pid==3456,"wrong shell selected");
        if(stopped){SetEvent(stopped);Check(WaitForSingleObject(proceed,4000)==WAIT_OBJECT_0,"test gate timed out");}
        if(throwStop)throw std::runtime_error("shutdown exception");if(!stopOK){error=L"shutdown refused";return false;}present=respawn;return true;
    }
    bool Restore(std::wstring& error)override{++restores;if(throwRestore)throw std::runtime_error("recovery exception");if(!restoreOK){error=L"recovery refused";return false;}present=true;return true;}
};
bool Record(const fs::path& dir){return fs::exists(dir/L"explorer-session.json");}
void Cases(const fs::path& root){
    auto folder=[&](const wchar_t* name){auto path=root/name;fs::create_directories(path);return path;};
    {auto dir=folder(L"normal");auto mock=std::make_shared<Mock>(dir);ExplorerSession session(dir,mock);Check(session.Enter(),"entry rejected");session.Wait();Check(session.Status().phase==ExplorerPhase::Standalone&&Record(dir),"entry did not keep recovery state");
        for(int i=0;i<1000;++i)Check(!session.Enter(),"duplicate entry accepted");Check(mock->stops==1,"duplicate shutdown");session.Return();session.Wait();Check(!session.Engaged()&&!Record(dir)&&!mock->lease&&mock->restores==1,"normal return incomplete");
        Check(session.Enter(),"cannot start after return");session.Wait();session.Return();session.Wait();Check(!session.Engaged(),"second round failed");}
    {auto dir=folder(L"refused");auto mock=std::make_shared<Mock>(dir);mock->stopOK=false;ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(!session.Engaged()&&!Record(dir)&&mock->restores==1&&!session.Status().error.empty(),"refused shutdown did not roll back");}
    {auto dir=folder(L"respawn");auto mock=std::make_shared<Mock>(dir);mock->respawn=true;ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(!session.Engaged()&&!Record(dir)&&mock->restores==1,"Explorer restart loop was accepted");}
    {auto dir=folder(L"failed-recovery");auto mock=std::make_shared<Mock>(dir);ExplorerSession session(dir,mock);session.Enter();session.Wait();mock->restoreOK=false;session.Return();session.Wait();Check(session.Status().phase==ExplorerPhase::RecoveryRequired&&Record(dir)&&mock->lease,"failed recovery lost its journal or ownership");mock->restoreOK=true;session.Return();session.Wait();Check(!session.Engaged()&&!Record(dir),"recovery retry failed");}
    {auto file=folder(L"storage")/L"not-a-directory";Check(WriteAtomic(file,L"fixture"),"cannot create storage fixture");auto dir=file/L"profile";auto mock=std::make_shared<Mock>(dir);ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(!session.Engaged()&&mock->stops==0&&!mock->lease,"Explorer stopped after recovery record write failed");}
    {auto dir=folder(L"cancel-during-stop");auto mock=std::make_shared<Mock>(dir);mock->stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);mock->proceed=CreateEventW(nullptr,TRUE,FALSE,nullptr);ExplorerSession session(dir,mock);session.Enter();Check(WaitForSingleObject(mock->stopped,3000)==WAIT_OBJECT_0,"shutdown gate not reached");session.Return();Check(fs::exists(dir/L"explorer-cancel.flag"),"cancellation not persisted for the guardian");SetEvent(mock->proceed);session.Wait();Check(!session.Engaged()&&!Record(dir)&&mock->restores==1,"cancel during shutdown did not restore Explorer");}
    {auto dir=folder(L"lease-refused");auto mock=std::make_shared<Mock>(dir);mock->acquireOK=false;ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(!session.Engaged()&&mock->stops==0&&mock->restores==0&&!Record(dir),"ownership refusal touched the shell");}
    {auto dir=folder(L"pending-recovery");Check(WriteAtomic(dir/L"explorer-session.json",L"leftover"),"cannot write pending fixture");auto mock=std::make_shared<Mock>(dir);ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(session.Phase()==ExplorerPhase::RecoveryRequired&&mock->stops==0&&ReadUtf8(dir/L"explorer-session.json")==std::optional<std::wstring>(L"leftover"),"pending recovery was overwritten");session.Return();session.Wait();Check(!session.Engaged()&&!Record(dir),"pending recovery cannot be completed explicitly");}
    {auto dir=folder(L"shutdown-exception");auto mock=std::make_shared<Mock>(dir);mock->throwStop=true;ExplorerSession session(dir,mock);session.Enter();session.Wait();Check(!session.Engaged()&&!Record(dir)&&mock->restores==1,"shutdown exception lost recovery");}
    {auto dir=folder(L"recovery-exception");auto mock=std::make_shared<Mock>(dir);ExplorerSession session(dir,mock);session.Enter();session.Wait();mock->throwRestore=true;session.Return();session.Wait();Check(session.Engaged()&&Record(dir),"recovery exception removed the journal");mock->throwRestore=false;session.Return();session.Wait();Check(!session.Engaged(),"exception recovery not retryable");}
    {auto dir=folder(L"destructor");auto mock=std::make_shared<Mock>(dir);{ExplorerSession session(dir,mock);session.Enter();session.Wait();}Check(mock->present&&mock->restores==1&&!Record(dir),"destructor did not return Explorer");}
    {auto dir=folder(L"foreign-record");Check(WriteAtomic(dir/L"explorer-session.json",L"{\"version\":1,\"ownerPid\":123,\"session\":1,\"ownerCreated\":\"1234\",\"user\":\"foreign\"}"),"cannot write foreign fixture");Check(!RecoverExplorerSession(dir,true)&&Record(dir),"foreign recovery information was trusted");}
}
LRESULT CALLBACK FixtureProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_QUERYENDSESSION)return TRUE;
    if(message==WM_ENDSESSION){if(w)DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
int Fixture(const wchar_t* event){
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=FixtureProc;cls.lpszClassName=L"AnotherLook.RestartManagerFixture";RegisterClassW(&cls);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"Another Look shutdown fixture",WS_POPUP,-20000,-20000,100,100,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 3;RegisterApplicationRestart(L"--fixture",0);ShowWindow(window,SW_SHOWNOACTIVATE);HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,event);if(ready){SetEvent(ready);CloseHandle(ready);}MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
void NativeAdapter(){
    auto name=L"Local\\AnotherLook.ExplorerFixture."+std::to_wstring(GetCurrentProcessId());HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,name.c_str());Check(ready!=nullptr,"cannot create fixture event");wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --fixture \""+name+L"\"";STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION child{};
    bool launched=CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE;
    if(!launched){CloseHandle(ready);throw std::runtime_error("cannot launch fixture");}CloseHandle(child.hThread);
    struct Cleanup {HANDLE process,ready;DWORD pid;~Cleanup(){EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==static_cast<DWORD>(p))PostMessageW(h,WM_CLOSE,0,0);return TRUE;},pid);WaitForSingleObject(process,2000);CloseHandle(process);CloseHandle(ready);}} cleanup{child.hProcess,ready,child.dwProcessId};
    Check(WaitForSingleObject(ready,3000)==WAIT_OBJECT_0,"fixture not ready");FILETIME creation{},exit{},kernel{},user{};Check(GetProcessTimes(child.hProcess,&creation,&exit,&kernel,&user)!=FALSE,"cannot identify fixture");DWORD session=0;ProcessIdToSessionId(child.dwProcessId,&session);ShellProcess identity{child.dwProcessId,session,(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime};std::wstring error;
    auto wrong=identity;++wrong.created;Check(!ShutdownShellProcess(wrong,error)&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"stale process identity was accepted");
    error.clear();if(!ShutdownShellProcess(identity,error))throw std::runtime_error(Utf8(error));Check(WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0,"fixture remains running");
}
}
int wmain(int argc,wchar_t** argv){try{
    if(argc==3&&std::wstring_view(argv[1])==L"--fixture")return Fixture(argv[2]);
    winrt::init_apartment();Check(argc==2,"expected test directory");fs::path output=fs::absolute(argv[1])/std::to_wstring(GetCurrentProcessId());fs::create_directories(output);Cases(output);NativeAdapter();std::cout<<"Explorer transaction faults, cancellation, retry, 1000 duplicate actions and native own-process shutdown passed. Real Explorer was not stopped.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
