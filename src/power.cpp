#include "ui.h"
#include <powrprof.h>
#include <algorithm>

namespace gv {
void Application::HidePowerPanel(){
    powerOpen_=false;for(auto& tool:tools)if(tool->element==UiElement::Power)tool->Show(false);
}
void Application::TogglePowerPanel(){
    if(boards.empty())return;
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    if(powerOpen_&&foregroundPid==GetCurrentProcessId()){HidePowerPanel();ShowDesktop();return;}
    HidePowerPanel();
    CancelProgramActivation();launcher.Hide();
    // Returning home must not expose the next application underneath the first.
    // Minimise, never hide or close: every window remains available in the rail.
    for(const auto& window:ListAppWindows())if(window.pid!=GetCurrentProcessId()&&IsWindowVisible(window.hwnd)&&!IsIconic(window.hwnd))ShowWindowAsync(window.hwnd,SW_MINIMIZE);
    ShowDesktop();POINT point{};GetCursorPos(&point);auto selected=MonitorFromPoint(point,MONITOR_DEFAULTTOPRIMARY);powerMonitor_=0;
    for(size_t i=0;i<boards.size();++i)if(MonitorFromWindow(boards[i]->hwnd,MONITOR_DEFAULTTOPRIMARY)==selected)powerMonitor_=static_cast<int>(i);
    powerOpen_=true;
    for(auto& tool:tools)if(tool->element==UiElement::Power){bool show=tool->monitor==powerMonitor_&&config.ui.powerPosition!=L"hidden";tool->Place(false);tool->Show(show);if(show){FocusAppWindow(tool->hwnd);if(GetForegroundWindow()!=tool->hwnd)SwitchToThisWindow(tool->hwnd,TRUE);}}
    SyncPanels(true);
}
void Application::PowerAction(int action,HWND owner){
    if(action<0||action>2)return;
    const wchar_t* labels[]={L"Выключить компьютер",L"Перезагрузить компьютер",L"Перейти в спящий режим"};
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,labels[action]);AppendMenuW(menu,MF_STRING,2,L"Отмена");RECT rect{};GetWindowRect(owner,&rect);
    UINT choice=StyledMenu(menu,owner,{rect.left,rect.top});DestroyMenu(menu);if(choice!=1)return;
    windows.Capture();Save();HidePowerPanel();
    HANDLE token=nullptr;TOKEN_PRIVILEGES wanted{},previous{};DWORD previousSize=sizeof(previous);bool adjusted=false;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token)&&LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&wanted.Privileges[0].Luid)){
        wanted.PrivilegeCount=1;wanted.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
        adjusted=AdjustTokenPrivileges(token,FALSE,&wanted,sizeof(previous),&previous,&previousSize)!=FALSE&&GetLastError()==ERROR_SUCCESS;
    }
    // Never force termination or force a suspended state through busy drivers.
    BOOL result=action==2?SetSuspendState(FALSE,FALSE,FALSE):ExitWindowsEx(action==0?EWX_POWEROFF:EWX_REBOOT,SHTDN_REASON_MAJOR_APPLICATION|SHTDN_REASON_FLAG_PLANNED);
    if(token){if(adjusted)AdjustTokenPrivileges(token,FALSE,&previous,0,nullptr,nullptr);CloseHandle(token);}
    if(!result)ShowNotice(owner,L"Питание",L"Windows не выполнила команду. Проверь доступность режима и права текущего пользователя.");
}
}
