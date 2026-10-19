#include "ui.h"
#include <algorithm>

namespace gv {
bool Application::WithSystemPrompt(HWND owner,const std::function<bool()>& action){
    if(systemPrompt_)return false;
    if(!config.enabled||preview)return action();
    systemPrompt_=true;HidePowerPanel();CancelProgramActivation();launcher.Hide();SetHooks(false);
    // Release the Shell_TrayWnd proxy too: system consent must communicate with
    // Explorer's real shell window while our desktop is out of the way.
    web.HideEmbedded();notificationTray.Stop();
    for(auto* list:{&tools,&bars,&rails,&boards})for(auto& surface:*list)surface->Show(false);
    chrome.Restore();
    if(IsWindow(owner)&&IsWindowVisible(owner)){
        SetWindowPos(owner,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        FocusAppWindow(owner);
    }
    auto resume=[&]{
        systemPrompt_=false;
        if(quitting||!config.enabled||preview)return;
        if(!chrome.Begin()){Pause();return;}
        SetHooks(true);
        for(auto* list:{&bars,&rails,&tools})for(auto& surface:*list)surface->Show(surface->element!=UiElement::Power&&UiPosition(config.ui,surface->element)!=L"hidden");
        if(config.ui.showTray)notificationTray.Start();
        notificationTray.Sync(config.ui.showTray);ShowDesktop();
        if(IsWindow(owner)&&IsWindowVisible(owner)){
            SetWindowPos(owner,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            FocusAppWindow(owner);
        }
    };
    try{bool result=action();resume();return result;}catch(...){resume();throw;}
}
bool Application::RequestIndexAccess(HWND owner,std::wstring_view journals){
    if(config.enabled&&!preview&&(!IsWindow(owner)||!IsWindowVisible(owner)||owner==events)){
        fileSettings.Show(boards.empty()?nullptr:boards.front()->hwnd);owner=fileSettings.Window();
    }
    return WithSystemPrompt(owner,[&]{return fileIndex.EnableFast(owner,true,journals);});
}
bool Application::RemoveScene(const std::wstring& sourceId){
    std::wstring id=sourceId;
    auto found=std::find_if(config.scenes.begin(),config.scenes.end(),[&](const Scene& scene){return scene.id==id;});
    if(found==config.scenes.end()||config.scenes.size()<2)return false;
    windows.Capture();
    auto backup=dataDir/(L"settings.before-delete-"+Timestamp()+L"-"+NewId()+L".json");
    if(!WriteAtomic(backup,Serialize(config))){
        ShowNotice(boards.empty()?nullptr:boards.front()->hwnd,L"Удаление пространства",L"Не удалось сохранить резервную копию. Пространство осталось на месте.");return false;
    }
    size_t index=static_cast<size_t>(found-config.scenes.begin());
    std::wstring destination=config.active==id?config.scenes[index?index-1:1].id:config.active;
    std::vector<HWND> move;for(const auto& entry:windows.Entries())if(entry.scene==id)move.push_back(entry.info.hwnd);
    for(HWND window:move)windows.MoveToScene(window,destination);
    std::vector<std::wstring> pages;for(const auto& card:found->cards)if(card.type==L"site"||card.type==L"link")pages.push_back(card.id);
    for(const auto& page:pages)web.Close(page);
    if(config.active==id)SwitchScene(destination);
    std::erase_if(config.scenes,[&](const Scene& scene){return scene.id==id;});launched_.erase(id);
    ApplyInterface();Save();customization.Refresh();return true;
}
}
