#include "window_motion.h"
#include "ui.h"
#include <shobjidl.h>
#include <algorithm>
#include <windowsx.h>

namespace gv {
namespace {
std::wstring DepthName(int depth){return depth<0?L"Все подпапки":depth==0?L"Без вложений":std::to_wstring(depth)+(depth==1?L" уровень":depth<5?L" уровня":L" уровней");}
std::wstring DriveNames(std::wstring_view letters){std::wstring text;for(auto drive:letters){if(!text.empty())text+=L", ";text+=drive;text+=L':';}return text;}
std::wstring DepthExample(int depth){
    if(depth<0)return L"Все уровни вложенности. Большие папки обрабатываются дольше.";
    if(!depth)return L"Только файлы выбранной папки. Содержимое подпапок не попадает в поиск.";
    if(depth>3)return L"Файлы до "+std::to_wstring(depth)+L" вложенных папок от выбранного корня. Более глубокие файлы не индексируются.";
    std::wstring example=L"Корень";for(int i=1;i<=depth;++i)example+=L"\\"+std::to_wstring(i);
    return example+L"\\файл.txt входит в поиск. Содержимое более глубоких папок пропускается.";
}
}
FileSearchSettings::~FileSearchSettings(){Close();}
void FileSearchSettings::Close(){if(window_)DestroyWindow(window_);}
bool FileSearchSettings::Message(const MSG& message){return window_&&IsWindowVisible(window_)&&(message.hwnd==window_||IsChild(window_,message.hwnd))&&IsDialogMessageW(window_,const_cast<MSG*>(&message));}
void FileSearchSettings::Show(HWND owner){
    roots_=FileIndex::Roots(app_.config.files);
    if(!window_){WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.FileSearch";cls.hIcon=app_.icon;RegisterClassW(&cls);UINT initialDpi=owner?GetDpiForWindow(owner):GetDpiForSystem();RECT r=CenteredWindow(owner,MulDiv(960,initialDpi,96),MulDiv(static_cast<int>((840-(6-std::clamp(static_cast<int>(roots_.size()),1,6))*44)*960.f/1060),initialDpi,96));window_=CreateWindowExW(app_.preview?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW,cls.lpszClassName,L"GoodView — поиск файлов",WS_POPUP|WS_CLIPCHILDREN,r.left,r.top,r.right-r.left,r.bottom-r.top,owner,nullptr,cls.hInstance,this);RECT size{};GetClientRect(window_,&size);float factor=size.right/1060.f;font_=UiFont(std::max(14,static_cast<int>(16*factor)),96);small_=UiFont(std::max(12,static_cast<int>(14*factor)),96,FW_NORMAL,FontRole::Body);title_=UiFont(static_cast<int>(24*factor),96,FW_SEMIBOLD);
        auto button=[&](int id,const wchar_t* text){HWND child=CreateWindowW(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),cls.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);StyleUiButton(child);};
        button(IDCANCEL,L"Закрыть");button(1,L"Поиск включён");button(9,L"Пауза");button(3,L"Пересоздать индекс");button(10,L"Подключить MFT");button(4,L"Добавить папку");button(5,L"Все локальные диски");button(6,L"Убрать папку");button(7,L"Назад");button(8,L"Дальше");
        for(int i=0;i<6;++i)button(100+i,L"");const wchar_t* depths[]={L"Без вложений",L"1 уровень",L"2 уровня",L"3 уровня",L"Без ограничений"};for(int i=0;i<5;++i)button(20+i,depths[i]);
    }Refresh();ShowBlueprintWindow(window_,SW_SHOWNORMAL);SetForegroundWindow(window_);SetFocus(GetDlgItem(window_,IDCANCEL));
}
void FileSearchSettings::Refresh(){if(!window_)return;roots_=FileIndex::Roots(app_.config.files);selected_=std::clamp(selected_,0,std::max(0,static_cast<int>(roots_.size())-1));page_=std::clamp(page_,0,std::max(0,(static_cast<int>(roots_.size())-1)/6));RECT client;GetClientRect(window_,&client);int desired=static_cast<int>((840-(6-std::clamp(static_cast<int>(roots_.size()),1,6))*44)*client.right/1060.f);if(client.bottom!=desired){auto rect=CenteredWindow(window_,client.right,desired);SetWindowPos(window_,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);}Layout();InvalidateRect(window_,nullptr,FALSE);}
void FileSearchSettings::Layout(){
    RECT client{};GetClientRect(window_,&client);int gap=(6-std::clamp(static_cast<int>(roots_.size()),1,6))*44;float sx=client.right/1060.f,sy=client.bottom/static_cast<float>(840-gap);auto place=[&](int id,int x,int y,int w,int h,bool shown=true){if(y>=554)y-=gap;HWND child=GetDlgItem(window_,id);ShowWindow(child,shown?SW_SHOWNOACTIVATE:SW_HIDE);SetWindowPos(child,nullptr,static_cast<int>(x*sx),static_cast<int>(y*sy),static_cast<int>(w*sx),static_cast<int>(h*sy),SWP_NOZORDER|SWP_NOACTIVATE);StyleUiButton(child);InvalidateRect(child,nullptr,FALSE);};
    place(IDCANCEL,888,26,140,44);place(1,32,118,212,44);place(9,256,118,150,44);place(3,418,118,240,44);place(10,670,118,358,44);place(4,32,208,196,44);place(5,240,208,242,44);place(6,494,208,172,44);place(7,764,208,126,44,roots_.size()>6);place(8,902,208,126,44,roots_.size()>6);
    for(int i=0;i<6;++i){int index=page_*6+i;place(100+i,32,270+i*44,996,40,index<static_cast<int>(roots_.size()));}
    for(int i=0;i<5;++i){place(20+i,32+i*201,602,189,44);EnableWindow(GetDlgItem(window_,20+i),!roots_.empty());}EnableWindow(GetDlgItem(window_,6),!roots_.empty());SetWindowTextW(GetDlgItem(window_,1),app_.config.files.enabled?L"Поиск включён":L"Поиск выключен");SetWindowTextW(GetDlgItem(window_,9),app_.config.files.paused?L"Продолжить":L"Пауза");SetWindowTextW(GetDlgItem(window_,10),!app_.fileIndex.Status().journalMissing.empty()?L"Включить USN · 64 МиБ":app_.fileIndex.Status().fastConnected?L"Отключить MFT":L"Подключить MFT");RoundWindow(window_,20);
}
void FileSearchSettings::Draw(HDC dc,RECT client){
    auto p=UiTheme();RoundFill(dc,client,p.panel,20,p.border);int gap=(6-std::clamp(static_cast<int>(roots_.size()),1,6))*44;float sx=client.right/1060.f,sy=client.bottom/static_cast<float>(840-gap);auto text=[&](std::wstring value,int x,int y,int w,int h,HFONT font,COLORREF color,UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS){if(y>=554)y-=gap;RECT r{static_cast<LONG>(x*sx),static_cast<LONG>(y*sy),static_cast<LONG>((x+w)*sx),static_cast<LONG>((y+h)*sy)};auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);};
    text(L"Поиск файлов",32,24,820,46,title_,p.text);text(L"Поиск по именам. Глубина задаётся отдельно для каждой папки.",34,76,970,28,font_,p.muted);text(L"Папки поиска",32,174,700,28,font_,p.text);
    if(roots_.empty())text(L"Добавь папку или выбери все локальные диски.",50,270,940,42,font_,p.muted);
    text(roots_.empty()?L"Глубина индексации":L"Глубина для "+roots_[selected_].path,32,554,996,34,font_,p.text);
    if(!roots_.empty())text(DepthExample(roots_[selected_].depth),32,650,996,28,small_,p.muted);
    auto status=app_.fileIndex.Status();std::wstring state=!app_.config.files.enabled?L"Выключен":status.error.empty()?app_.config.files.paused?L"На паузе":status.busy?L"Обновляется":L"Готов":L"Не удалось обновить: "+status.error;
    std::wstring progress=state+L" · "+std::to_wstring(status.entries)+L" записей · "+std::to_wstring((status.bytes+1048575)/1048576)+L" МБ на диске";
    if(status.busy&&!status.fastConnected&&!status.paused&&status.elapsedMs>=1000)progress+=L" · "+std::to_wstring(status.scannedEntries*1000/status.elapsedMs)+L" имён/с";if(status.skipped)progress+=L" · пропусков: "+std::to_wstring(status.skipped);
    text(progress,32,682,996,30,font_,p.text);
    text(status.fastRequested?status.provider+L" · "+std::to_wstring(status.mftRecords)+L" прочитано · "+std::to_wstring(status.mftIndexed)+L" подготовлено к поиску":status.current.empty()?L"Закрытые системные папки и переходы через ссылки не обходятся.":L"Проверено в папке: "+std::to_wstring(status.currentEntries)+L". "+status.current,32,721,996,26,small_,p.muted);
    text(!status.journalMissing.empty()?L"USN выключен на "+DriveNames(status.journalMissing)+L". MFT и уведомления работают. Журнал ускорит следующий запуск.":status.note.empty()?L"MFT ускоряет полные диски NTFS без ограничения глубины. Помощнику нужны права администратора.":status.note,32,754,996,28,small_,p.muted);
    text(!status.journalMissing.empty()?L"Целевой размер журнала 64 МиБ, шаг 8 МиБ. Windows может временно превысить его. Готовые журналы сохраняются.":status.remainingMs?L"До завершения поискового индекса около "+std::to_wstring((status.remainingMs+999)/1000)+L" с. Оценка уточняется по текущей скорости.":status.mftBuildMs?L"Последний снимок: "+std::to_wstring(status.mftBuildMs/1000)+L" с. Чтение MFT: "+std::to_wstring(status.mftReadMs)+L" мс. База хранится на диске.":L"Файлы не открываются для чтения содержимого. Память используется только для кэша и небольшой выдачи.",32,789,996,28,small_,p.muted);
}
void FileSearchSettings::MakeExplicit(){app_.config.files.roots=roots_;app_.config.files.allFixedDrives=false;}
void FileSearchSettings::Apply(bool rebuild){app_.config.files.initialized=true;app_.ConfigureFiles(rebuild);app_.SaveSoon();Refresh();}
void FileSearchSettings::Command(int id){
    if(id==IDCANCEL){Close();return;}if(id>=100&&id<106){selected_=page_*6+id-100;Refresh();return;}auto& config=app_.config.files;
    if(id==1)config.enabled=!config.enabled;
    else if(id==9)config.paused=!config.paused;
    else if(id==3){config.enabled=true;Apply(true);if(config.fastNtfs)app_.RequestIndexAccess(window_);return;}
    else if(id==10){auto status=app_.fileIndex.Status();if(!status.journalMissing.empty()){config.fastNtfs=true;config.enabled=true;Apply();app_.RequestIndexAccess(window_,status.journalMissing);}else if(status.fastConnected){config.fastNtfs=false;Apply();}else{config.fastNtfs=true;config.enabled=true;Apply();app_.RequestIndexAccess(window_);}return;}
    else if(id==5){config.allFixedDrives=true;config.enabled=true;}
    else if(id==6&&!roots_.empty()){MakeExplicit();config.roots.erase(config.roots.begin()+selected_);}
    else if(id==7||id==8){page_=std::clamp(page_+(id==8?1:-1),0,std::max(0,(static_cast<int>(roots_.size())-1)/6));selected_=page_*6;Refresh();return;}
    else if(id>=20&&id<=24&&!roots_.empty()){MakeExplicit();config.roots[selected_].depth=id==24?-1:id-20;}
    else if(id==4){ComPtr<IFileOpenDialog> picker;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker))))return;DWORD options=0;picker->GetOptions(&options);picker->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);picker->SetTitle(L"Папка для файлового поиска");if(FAILED(picker->Show(window_)))return;ComPtr<IShellItem> selected;if(FAILED(picker->GetResult(&selected)))return;PWSTR path=nullptr;if(SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH,&path))){config.roots.push_back({path,-1});CoTaskMemFree(path);config.enabled=true;}}
    else return;Apply();
}
LRESULT CALLBACK FileSearchSettings::Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<FileSearchSettings*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<FileSearchSettings*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,m,w,l);
    if(m==WM_ERASEBKGND||m==WM_NCPAINT)return 1;if(m==WM_NCCALCSIZE)return 0;if(m==WM_NCACTIVATE)return TRUE;
    if(m==WM_PAINT){PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);RECT client{};GetClientRect(h,&client);self->Draw(dc,client);EndPaint(h,&paint);return 0;}
    if(m==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);int id=item.CtlID;if(id>=100&&id<106){int index=self->page_*6+id-100;if(index<static_cast<int>(self->roots_.size())){auto p=UiTheme();RoundFill(item.hDC,item.rcItem,index==self->selected_?p.field:p.panel,8,index==self->selected_?p.accent:p.border);auto old=SelectObject(item.hDC,self->font_);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,p.text);RECT text=item.rcItem;InflateRect(&text,-18,0);RECT depth=text;depth.left=depth.right-(item.rcItem.right-item.rcItem.left)/4;text.right=depth.left-18;DrawTextW(item.hDC,self->roots_[index].path.c_str(),-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SetTextColor(item.hDC,p.muted);auto label=DepthName(self->roots_[index].depth);DrawTextW(item.hDC,label.c_str(),-1,&depth,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_NOPREFIX);SelectObject(item.hDC,old);}}
        else{bool active=id==1?self->app_.config.files.enabled:id==9?self->app_.config.files.paused:id==5?self->app_.config.files.allFixedDrives:false;if(id>=20&&id<=24&&!self->roots_.empty())active=self->roots_[self->selected_].depth==(id==24?-1:id-20);DrawUiButton(item,self->font_,active);}return TRUE;}
    if(m==WM_COMMAND&&HIWORD(w)==BN_CLICKED){self->Command(LOWORD(w));return 0;}if(m==WM_SIZE){if(self->font_)self->Layout();return 0;}if(m==WM_CLOSE||m==WM_KEYDOWN&&w==VK_ESCAPE){DestroyWindow(h);return 0;}
    if(m==WM_NCHITTEST){POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&point);if(point.y<90)return HTCAPTION;}
    if(m==WM_DESTROY){self->window_=nullptr;for(auto font:{self->font_,self->small_,self->title_})if(font)DeleteObject(font);self->font_=self->small_=self->title_=nullptr;return 0;}return DefWindowProcW(h,m,w,l);
}
bool FileSearchSettings::ExportPreview(const fs::path& output){Show();RECT client{};GetClientRect(window_,&client);HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),client.right,-client.bottom,1,32,BI_RGB,0,0,0,0,0};void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap){DeleteDC(dc);return false;}auto old=SelectObject(dc,bitmap);Draw(dc,client);for(HWND child=GetWindow(window_,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){DRAWITEMSTRUCT item{};item.CtlID=GetDlgCtrlID(child);item.hwndItem=child;item.hDC=dc;GetWindowRect(child,&item.rcItem);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT*>(&item.rcItem),2);SendMessageW(window_,WM_DRAWITEM,item.CtlID,reinterpret_cast<LPARAM>(&item));}bool ok=SaveBitmapPng(bitmap,output);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;}
}
