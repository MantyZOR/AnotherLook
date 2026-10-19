// Explicit opt-in only. Never calls SwitchDesktop. The visible desktop's
// Explorer PID is protected and never passed to shutdown or cleanup operations.
#include "explorer_session.h"
#include <iostream>
#include <stdexcept>

using namespace gv;
namespace {
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::wstring Name(HANDLE object){wchar_t name[256]{};DWORD size=0;return GetUserObjectInformationW(object,UOI_NAME,name,sizeof(name),&size)?name:L"";}
std::wstring Desktop(){return Name(GetThreadDesktop(GetCurrentThreadId()));}
std::wstring DesktopPath(){return Name(GetProcessWindowStation())+L"\\"+Desktop();}
fs::path Self(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return path;}
ShellProcess Stamp(DWORD pid){HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);FILETIME created{},exit{},kernel{},user{};DWORD session=0;ShellProcess result;
    if(process&&GetProcessTimes(process,&created,&exit,&kernel,&user)&&ProcessIdToSessionId(pid,&session))result={pid,session,(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime};if(process)CloseHandle(process);return result;}
DWORD Shell(DWORD protectedPid){HWND window=GetShellWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(!pid)return 0;Check(pid!=protectedPid,"visible desktop Explorer appeared in test context; refusing to touch it");if(!IsExplorerShellWindow(window)){wchar_t cls[128]{},path[32768]{};DWORD length=32768;GetClassNameW(window,cls,128);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(process){QueryFullProcessImageNameW(process,0,path,&length);CloseHandle(process);}throw std::runtime_error("unexpected shell: "+Utf8(cls)+" pid="+std::to_string(pid)+" image="+Utf8(path)+" desktop="+Utf8(Desktop()));}return pid;}
void Isolated(){if(!Desktop().starts_with(L"AnotherLookTest."))throw std::runtime_error("test requires its own hidden desktop; actual="+Utf8(Desktop()));}
PROCESS_INFORMATION Spawn(const fs::path& binary,std::wstring arguments,std::wstring desktop){std::wstring command=L"\""+binary.wstring()+L"\" "+arguments;STARTUPINFOW start{sizeof(start)};start.lpDesktop=desktop.data();PROCESS_INFORMATION child{};
    Check(CreateProcessW(binary.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE,"cannot create isolated test process");CloseHandle(child.hThread);child.hThread=nullptr;return child;}
DWORD ExitCode(PROCESS_INFORMATION& child,DWORD timeout=45000){DWORD result=WaitForSingleObject(child.hProcess,timeout),code=1;if(result==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);child.hProcess=nullptr;Check(result==WAIT_OBJECT_0,"isolated process timed out");return code;}
void WaitShell(DWORD protectedPid){for(int i=0;i<120;++i){if(Shell(protectedPid))return;Sleep(100);}throw std::runtime_error("isolated Explorer did not create a shell");}
void Cleanup(DWORD protectedPid){
    Isolated();DWORD pid=Shell(protectedPid);if(!pid)return;std::wstring error;auto stamp=Stamp(pid);if(ShutdownShellProcess(stamp,error))return;
    // Only our fresh hidden desktop's shell, never the protected visible shell.
    DWORD thread=GetWindowThreadProcessId(GetShellWindow(),&pid);Check(pid!=protectedPid&&Name(GetThreadDesktop(thread))==Desktop(),"cleanup desktop identity changed");
    HANDLE process=OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(process){if(Stamp(pid)==stamp)TerminateProcess(process,0);WaitForSingleObject(process,5000);CloseHandle(process);}
}
int Crash(const fs::path& dir,DWORD protectedPid){
    Isolated();Check(Shell(protectedPid)!=0,"no isolated shell to stop");fs::create_directories(dir);ExplorerSession session(dir);Check(session.Enter(),"crash entry refused");session.Wait();
    Check(session.Phase()==ExplorerPhase::Standalone,Utf8(session.Status().error).c_str());Check(!Shell(protectedPid),"isolated Explorer still exists");
    ExitProcess(0); // Deliberately retain the journal: exercises dead-owner recovery.
}
int Inside(const fs::path& dir,DWORD protectedPid){
    Isolated();fs::create_directories(dir);Check(!GetShellWindow(),"isolated desktop already has a shell");
    wchar_t windows[MAX_PATH]{};GetWindowsDirectoryW(windows,MAX_PATH);auto explorer=Spawn(fs::path(windows)/L"explorer.exe",L"",DesktopPath());CloseHandle(explorer.hProcess);
    struct Guard {DWORD protectedPid;~Guard(){try{Cleanup(protectedPid);}catch(...){}}} guard{protectedPid};
    WaitShell(protectedPid);DWORD first=Shell(protectedPid);bool threadSafe=false;std::exception_ptr threadFailure;std::thread probe([&]{try{threadSafe=Desktop().starts_with(L"AnotherLookTest.")&&Shell(protectedPid)==first;}catch(...){threadFailure=std::current_exception();}});probe.join();if(threadFailure)std::rethrow_exception(threadFailure);Check(threadSafe,"worker thread inherited a different desktop");
    auto normal=dir/L"normal";fs::create_directories(normal);
    {ExplorerSession session(normal);Check(session.Enter(),"native entry refused");session.Wait();Check(session.Phase()==ExplorerPhase::Standalone,Utf8(session.Status().error).c_str());Check(!Shell(protectedPid),"native entry did not stop isolated Explorer");
        session.Return();session.Wait();Check(session.Phase()==ExplorerPhase::Overlay,Utf8(session.Status().error).c_str());WaitShell(protectedPid);Check(!fs::exists(normal/L"explorer-session.json"),"successful native return left a journal");}
    auto crashDir=dir/L"crash";auto child=Spawn(Self(),L"--crash \""+crashDir.wstring()+L"\" "+std::to_wstring(protectedPid),DesktopPath());Check(ExitCode(child)==0,"crash fixture did not complete entry");Check(!Shell(protectedPid)&&fs::exists(crashDir/L"explorer-session.json"),"crash did not leave recoverable state");
    Check(RecoverExplorerSession(crashDir),"dead-owner native recovery failed");WaitShell(protectedPid);Check(!fs::exists(crashDir/L"explorer-session.json"),"recovery journal was not cleared");
    Check(WriteAtomic(dir/L"passed.txt",L"Real isolated Explorer entry, normal return, abrupt owner exit and dead-owner recovery passed. Visible Explorer was protected. No SwitchDesktop call."),"cannot save test result");return 0;
}
int Parent(const fs::path& dir){
    DWORD protectedPid=0;HWND original=GetShellWindow();GetWindowThreadProcessId(original,&protectedPid);Check(protectedPid&&IsExplorerShellWindow(original),"visible Explorer required for a protected control");auto stamp=Stamp(protectedPid);
    std::wstring name=L"AnotherLookTest."+std::to_wstring(GetCurrentProcessId());HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);Check(desktop!=nullptr,"cannot create isolated desktop");
    std::wstring full=Name(GetProcessWindowStation())+L"\\"+name;
    auto child=Spawn(Self(),L"--inside \""+dir.wstring()+L"\" "+std::to_wstring(protectedPid),full);DWORD code=ExitCode(child,100000);CloseDesktop(desktop);
    Check(GetShellWindow()==original&&Stamp(protectedPid)==stamp,"visible Explorer changed during isolated test");if(code){auto detail=ReadUtf8(dir/L"inside-error.txt");throw std::runtime_error(detail?Utf8(*detail):"isolated native Explorer test failed, exit="+std::to_string(code));}
    std::cout<<"Real Explorer entry, return and crash recovery passed on a hidden isolated desktop; visible Explorer unchanged.\n";return 0;
}
}
int wmain(int argc,wchar_t** argv){try{
    winrt::init_apartment();
    if(argc==4&&std::wstring_view(argv[1])==L"--inside")return Inside(fs::absolute(argv[2]),std::stoul(argv[3]));
    if(argc==4&&std::wstring_view(argv[1])==L"--crash")return Crash(fs::absolute(argv[2]),std::stoul(argv[3]));
    Check(argc==3&&std::wstring_view(argv[1])==L"--isolated-explorer","explicit --isolated-explorer <output> required");return Parent(fs::absolute(argv[2]));
}catch(const std::exception& error){if(argc>=3){try{auto name=std::wstring_view(argv[1])==L"--inside"?L"inside-error.txt":std::wstring_view(argv[1])==L"--crash"?L"crash-error.txt":L"error.txt";WriteAtomic(fs::absolute(argv[2])/name,Wide(error.what()));}catch(...){}}std::cerr<<error.what()<<'\n';return 1;}}
