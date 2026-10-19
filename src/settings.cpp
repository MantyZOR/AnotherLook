#include "window_motion.h"
#include "ui.h"
#include <algorithm>
namespace gv {
void Surface::AddMenu(){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Заметка");AppendMenuW(menu,MF_STRING,2,L"Сайт");AppendMenuW(menu,MF_STRING,4,L"Приложение из каталога");AppendMenuW(menu,MF_STRING,8,L"Папка");AppendMenuW(menu,MF_STRING,5,L"Часы");AppendMenuW(menu,MF_STRING,6,L"Календарь");AppendMenuW(menu,MF_STRING,7,L"Нагрузка системы");Popup(menu,[this](UINT id){if(id==8){app_.AddFolder(monitor);return;}if(id==4){app_.launcher.Show(true,monitor);return;}const wchar_t* types[]={L"note",L"site",L"link",L"launcher",L"clock",L"calendar",L"system"};if(id>=1&&id<=7)app_.AddCard(types[id-1],monitor);});}
void Surface::ShowActivity(){HMENU menu=CreatePopupMenu();auto shell=app_.explorerSession.Status();if(!shell.message.empty()){AppendMenuW(menu,MF_GRAYED,0,shell.message.c_str());AppendMenuW(menu,MF_STRING,4,L"Режим рабочего стола");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);}auto status=app_.fileIndex.Status();AppendMenuW(menu,MF_GRAYED,0,(L"Файлы: "+std::to_wstring(status.entries)+L" записей · "+std::to_wstring((status.bytes+1048575)/1048576)+L" МБ").c_str());AppendMenuW(menu,MF_STRING,1,L"Настройки файлового поиска");AppendMenuW(menu,MF_STRING,2,app_.config.files.paused?L"Продолжить индексирование":L"Приостановить индексирование");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,L"Активность встроенных сайтов");Popup(menu,[this](UINT id){if(id==1)app_.fileSettings.Show(hwnd);else if(id==2){app_.config.files.paused=!app_.config.files.paused;app_.ConfigureFiles();app_.SaveSoon();}else if(id==3)ShowNotice(hwnd,L"Активность встроенных сайтов",app_.web.Activity());else if(id==4)app_.ExplorerModeMenu(hwnd);});}
void Surface::OpenWindowMenu(const std::vector<WindowInfo>& windows){
    if(windows.empty())return;HMENU menu=CreatePopupMenu();
    auto commands=[](HMENU target,UINT base){AppendMenuW(target,MF_STRING,base+1,L"Показать окно");AppendMenuW(target,MF_STRING,base+2,L"Свернуть");AppendMenuW(target,MF_SEPARATOR,0,nullptr);AppendMenuW(target,MF_STRING,base+3,L"Закрыть окно");};
    if(windows.size()==1)commands(menu,100);
    else{for(size_t i=0;i<windows.size();++i){HMENU child=CreatePopupMenu();commands(child,100+static_cast<UINT>(i)*10);AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(child),windows[i].title.c_str());MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_DATA;info.dwItemData=reinterpret_cast<ULONG_PTR>(app_.icons.GetWindow(windows[i].hwnd).get());SetMenuItemInfoW(menu,static_cast<UINT>(GetMenuItemCount(menu)-1),TRUE,&info);}AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,1,L"Закрыть все окна этой программы");}
    Popup(menu,[this,windows](UINT id){
        auto valid=[](const WindowInfo& window){DWORD pid=0;GetWindowThreadProcessId(window.hwnd,&pid);return IsWindow(window.hwnd)&&window.pid==pid;};
        if(id==1){for(const auto& window:windows)if(valid(window))PostMessageW(window.hwnd,WM_CLOSE,0,0);return;}
        if(id<100)return;size_t index=(id-100)/10;UINT action=(id-100)%10;if(index>=windows.size()||!valid(windows[index]))return;HWND target=windows[index].hwnd;
        if(action==1)app_.ActivateWindow(target);else if(action==2)ShowWindowAsync(target,SW_MINIMIZE);else if(action==3)PostMessageW(target,WM_CLOSE,0,0);
    });
}
void Surface::SceneMenu(const std::wstring& id){
    const auto* scene=FindScene(app_.config,id);if(!scene)return;
    HMENU menu=CreatePopupMenu(),remove=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"Значок пространства");
    AppendMenuW(remove,MF_GRAYED,0,(L"Карточек: "+std::to_wstring(scene->cards.size())+L". Сайты закроются.").c_str());
    AppendMenuW(remove,MF_GRAYED,0,L"Приложения перейдут в оставшееся пространство.");
    AppendMenuW(remove,MF_STRING,10,(L"Удалить «"+scene->name+L"»").c_str());AppendMenuW(remove,MF_STRING,11,L"Отмена");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_POPUP|(app_.config.scenes.size()<2?MF_GRAYED:0),reinterpret_cast<UINT_PTR>(remove),L"Удалить пространство");
    Popup(menu,[this,id](UINT action){
        if(action==10)app_.RemoveScene(id);
        else if(action==1){auto* scene=FindScene(app_.config,id);if(scene&&ChooseSpaceIcon(hwnd,scene->icon)){app_.SaveSoon();app_.Refresh();app_.customization.Refresh();}}
    });
}
void Surface::ShowSettings(){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,app_.config.enabled?L"Вернуться к рабочему столу Windows":L"Включить оболочку");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,20,L"Расположение и вид интерфейса");AppendMenuW(menu,MF_STRING,21,L"Оформление пространства");AppendMenuW(menu,MF_STRING,22,L"Индекс файлов");AppendMenuW(menu,MF_STRING,23,L"Движение интерфейса");AppendMenuW(menu,MF_STRING,24,L"Режим рабочего стола");
    AppendMenuW(menu,MF_STRING|(IsAutoStartEnabled()?MF_CHECKED:0),2,L"Запускать вместе с Windows");AppendMenuW(menu,MF_STRING|(app_.config.wallpaperEnabled?MF_CHECKED:0),3,L"Фон пространства");AppendMenuW(menu,MF_STRING,4,L"Сохранить расположение окон");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"Активность встроенных сайтов");AppendMenuW(menu,MF_STRING,6,L"Горячие клавиши");AppendMenuW(menu,MF_STRING,7,L"Закрыть встроенные сайты");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,8,L"Завершить WindowsGoodView");
    Popup(menu,[this](UINT id){switch(id){case 1:if(app_.config.enabled)app_.Pause();else app_.Enable();break;case 2:app_.ToggleStartup();break;case 3:app_.config.wallpaperEnabled=!app_.config.wallpaperEnabled;app_.Refresh();app_.SaveSoon();break;case 4:app_.windows.Capture();app_.Save();break;case 5:ShowActivity();break;case 6:ShowNotice(hwnd,L"Горячие клавиши",L"Ctrl + Q: завершить GoodView\nCtrl + Alt + 1…4 — пространства\nCtrl + Alt + Space — поиск приложений\nCtrl + Alt + D — показать полотно\nCtrl + Alt + A — подключить активное окно\nCtrl + Alt + Shift + Space — вернуться в Windows\n\nCtrl + колесо — масштаб полотна\nКолесо — прокрутка, Shift — по горизонтали\nСредняя кнопка — перемещение полотна\nShift + выбор, Ctrl + G — группа карточек");break;case 7:app_.web.CloseAll();break;case 8:app_.Quit();break;case 20:Customize();break;case 21:ThemeMenu();break;case 22:app_.fileSettings.Show(hwnd);break;case 23:EditMotionPreferences(hwnd);break;case 24:app_.ExplorerModeMenu(hwnd);break;}});
}
void Surface::Customize(){app_.customization.Show(hwnd);}
void Surface::ThemeMenu(){app_.customization.Show(hwnd,true);}
}
