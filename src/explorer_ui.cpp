#include "ui.h"

namespace gv {
void Application::RefreshExplorerChrome(){if(!explorerSession.Engaged())chrome.Refresh();}
void Application::ExplorerModeMenu(HWND owner){
    auto status=explorerSession.Status();HMENU menu=CreatePopupMenu();
    auto detail=status.error.empty()?status.message:status.error;
    if(!detail.empty()){AppendMenuW(menu,MF_GRAYED,0,detail.c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);}
    if(explorerSession.Engaged()){
        AppendMenuW(menu,MF_GRAYED,0,L"При возврате запускается обычный рабочий стол Windows.");
        AppendMenuW(menu,MF_STRING,2,L"Вернуть Explorer и рабочий стол Windows");
    }else{
        AppendMenuW(menu,MF_GRAYED,0,L"Временная замена Explorer до выхода из Another Look.");
        AppendMenuW(menu,MF_GRAYED,0,L"Окна Проводника могут закрыться. Заверши копирование файлов.");
        AppendMenuW(menu,MF_GRAYED,0,L"Настройка входа в Windows не меняется.");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,status.busy||preview?MF_GRAYED:MF_STRING,1,L"Работать вместо Explorer сейчас");
    }
    AppendMenuW(menu,MF_STRING,3,L"Закрыть меню");POINT point{};GetCursorPos(&point);UINT action=StyledMenu(menu,owner,point);DestroyMenu(menu);
    if(action==1)ReplaceExplorer();else if(action==2)Pause();
}
void Application::ReplaceExplorer(){
    if(preview||quitting||systemPrompt_||explorerSession.Engaged())return;
    if(!config.enabled)Enable();
    if(!config.enabled||!guardianProcess||WaitForSingleObject(guardianProcess,0)!=WAIT_TIMEOUT)return;
    if(boards.empty()||std::any_of(boards.begin(),boards.end(),[](const auto& board){return !IsWindow(board->hwnd)||!IsWindowVisible(board->hwnd);})){ShowNotice(events,L"Режим рабочего стола",L"Сначала открой пространство на всех мониторах.");return;}
    // Keep the existing receiver and its imported icons across Explorer exit.
    // A receiver is required even when its visible tray panel is disabled.
    if(!notificationTray.Start()){ShowNotice(boards.front()->hwnd,L"Режим рабочего стола",L"Не удалось подготовить приём значков фоновых приложений. Explorer продолжает работать.");return;}
    for(auto& board:boards)UpdateWindow(board->hwnd);
    explorerReturnPending_=explorerQuitPending_=false;explorerError_.clear();explorerSession.Enter(events);
}
void Application::ExplorerChanged(){
    if(systemPrompt_)return;
    auto status=explorerSession.Status();for(auto& tool:tools)if(tool->element==UiElement::Settings)tool->Invalidate();if(status.busy)return;
    explorerSession.Wait();
    if(status.phase==ExplorerPhase::Standalone&&explorerNotified_!=status.phase){notificationTray.ShellChanged();SyncPanels(true);}
    if(status.phase==ExplorerPhase::Overlay){
        if(explorerReturnPending_){bool close=explorerQuitPending_;explorerReturnPending_=explorerQuitPending_=false;explorerNotified_=status.phase;if(close)Quit();else Pause();return;}
        if(config.enabled){chrome.Begin();if(config.ui.showTray){notificationTray.Start();notificationTray.ShellChanged();}else notificationTray.Stop();SyncPanels();}
    }
    if(status.phase==ExplorerPhase::RecoveryRequired){explorerReturnPending_=explorerQuitPending_=false;notificationTray.Start();notificationTray.ShellChanged();SyncPanels();}
    explorerNotified_=status.phase;
    if(!status.error.empty()&&status.error!=explorerError_){explorerError_=status.error;ShowNotice(boards.empty()?events:boards.front()->hwnd,L"Режим рабочего стола",status.error);}
}
}
