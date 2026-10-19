#include "window_motion.h"
#include "design.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <algorithm>
namespace gv {
std::span<const SpaceIcon> SpaceIcons(){
    static constexpr SpaceIcon icons[]={
        {L"work",L"Работа",Glyph::Briefcase},{L"play",L"Игры",Glyph::Game},{L"web",L"Интернет",Glyph::Globe},{L"canvas",L"Идеи",Glyph::Layers},
        {L"book",L"Учёба",Glyph::Book},{L"code",L"Разработка",Glyph::Code},{L"note",L"Заметки",Glyph::Note},{L"folder",L"Проект",Glyph::Folder},
        {L"music",L"Музыка",Glyph::Music},{L"headphones",L"Слушать",Glyph::Headphones},{L"photo",L"Фото",Glyph::Photo},{L"film",L"Видео",Glyph::Film},
        {L"mountain",L"Путешествия",Glyph::Mountain},{L"coffee",L"Перерыв",Glyph::Coffee},{L"star",L"Избранное",Glyph::Star},{L"heart",L"Личное",Glyph::Heart},
        {L"leaf",L"Спокойствие",Glyph::Leaf},{L"compass",L"Исследовать",Glyph::Compass},{L"rocket",L"Новый старт",Glyph::Rocket},{L"sun",L"День",Glyph::Sun},
        {L"moon",L"Ночь",Glyph::Moon},{L"clock",L"Фокус",Glyph::Clock},{L"calendar",L"Планы",Glyph::Calendar},{L"cpu",L"Система",Glyph::Cpu}
    };return icons;
}
Glyph SceneGlyph(const Scene& scene){auto key=scene.icon.empty()?scene.id:scene.icon;if(key==L"board")key=L"canvas";for(const auto& icon:SpaceIcons())if(key==icon.key)return icon.glyph;return scene.gaming?Glyph::Game:Glyph::Layers;}
std::optional<std::wstring> ChooseFolder(HWND owner,const wchar_t* title,const std::wstring& initial){
    ComPtr<IFileOpenDialog> picker;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker))))return {};
    DWORD options=0;picker->GetOptions(&options);picker->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR|FOS_PATHMUSTEXIST);picker->SetTitle(title);
    if(!initial.empty()){ComPtr<IShellItem> folder;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&folder))))picker->SetFolder(folder.Get());}
    if(FAILED(picker->Show(owner)))return {};ComPtr<IShellItem> selected;if(FAILED(picker->GetResult(&selected)))return {};PWSTR path=nullptr;
    if(FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH,&path)))return {};std::wstring result=path;CoTaskMemFree(path);return result;
}
namespace {
struct IconPicker {
    HWND owner{},window{};std::wstring key;bool saved=false;HFONT font{},heading{};UINT dpi=96;
    int U(int value)const{return MulDiv(value,dpi,96);}
    static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* s=reinterpret_cast<IconPicker*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<IconPicker*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(!s)return DefWindowProcW(h,m,w,l);
        if(m==WM_CREATE){s->dpi=GetDpiForWindow(h);s->font=UiFont(12,s->dpi);s->heading=UiFont(20,s->dpi,FW_SEMIBOLD);auto rect=CenteredWindow(s->owner,s->U(538),s->U(444));SetWindowPos(h,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);int i=0;for(const auto& icon:SpaceIcons()){
            auto button=CreateWindowW(L"BUTTON",icon.name,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,s->U(20+(i%6)*84),s->U(72+(i/6)*84),s->U(78),s->U(76),h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);StyleUiButton(button);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(s->font),FALSE);++i;}
            auto close=CreateWindowW(L"BUTTON",L"Закрыть выбор значка",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,s->U(478),s->U(14),s->U(40),s->U(40),h,reinterpret_cast<HMENU>(IDCANCEL),nullptr,nullptr);StyleUiButton(close);ButtonGlyph(close,Glyph::Close);SendMessageW(close,WM_SETFONT,reinterpret_cast<WPARAM>(s->font),FALSE);
            RoundWindow(h,18);return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps;auto dc=(m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(w));RECT rect;GetClientRect(h,&rect);RoundFill(dc,rect,UiTheme().panel,s->U(18),UiTheme().border);auto old=SelectObject(dc,s->heading);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UiTheme().text);RECT title{s->U(24),s->U(20),rect.right-s->U(24),s->U(52)};DrawTextW(dc,L"Значок пространства",-1,&title,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);if(m==WM_PAINT)EndPaint(h,&ps);return 0;}
        if(m==WM_DRAWITEM){auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item.CtlID==IDCANCEL){DrawUiButton(item,s->font);return TRUE;}int index=static_cast<int>(item.CtlID)-100;if(index<0||index>=static_cast<int>(SpaceIcons().size()))return FALSE;const auto& icon=SpaceIcons()[index];bool active=s->key==icon.key,hot=(item.itemState&(ODS_SELECTED|ODS_FOCUS))||GetPropW(item.hwndItem,L"GoodView.Hover");auto r=item.rcItem;RoundFill(item.hDC,r,active||hot?UiTheme().field:UiTheme().panel,s->U(12),active?UiTheme().accent:UiTheme().border);int cx=(r.left+r.right)/2;DrawGlyph(item.hDC,icon.glyph,{cx-s->U(12),r.top+s->U(12),cx+s->U(12),r.top+s->U(36)},active?UiTheme().accent:UiTheme().text);auto old=SelectObject(item.hDC,s->font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,UiTheme().muted);RECT label{r.left+s->U(2),r.top+s->U(43),r.right-s->U(2),r.bottom-s->U(6)};DrawTextW(item.hDC,icon.name,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(item.hDC,old);return TRUE;}
        if(m==WM_COMMAND){int command=LOWORD(w);if(command==IDOK&&GetParent(GetFocus())==h)command=GetDlgCtrlID(GetFocus());int index=command-100;if(index>=0&&index<static_cast<int>(SpaceIcons().size())){s->key=SpaceIcons()[index].key;s->saved=true;}DestroyWindow(h);return 0;}
        if(m==WM_KEYDOWN&&w==VK_ESCAPE){DestroyWindow(h);return 0;}
        if(m==WM_NCHITTEST){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<s->U(64))return HTCAPTION;}
        if(m==WM_CLOSE){DestroyWindow(h);return 0;}return DefWindowProcW(h,m,w,l);
    }
};
}
bool ChooseSpaceIcon(HWND owner,std::wstring& key){
    IconPicker s;s.owner=owner;s.key=key;WNDCLASSW cls{};cls.lpfnWndProc=IconPicker::Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"GoodView.SpaceIcons";RegisterClassW(&cls);
    HWND h=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Значок пространства",WS_POPUP|WS_CLIPCHILDREN,0,0,538,444,owner,nullptr,cls.hInstance,&s);if(!h)return false;EnableWindow(owner,FALSE);ShowBlueprintWindow(h,SW_SHOW);SetForegroundWindow(h);int selected=0;for(size_t i=0;i<SpaceIcons().size();++i)if(key==SpaceIcons()[i].key)selected=static_cast<int>(i);SetFocus(GetDlgItem(h,100+selected));
    MSG msg;while(IsWindow(h)){auto result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage(static_cast<int>(msg.wParam));DestroyWindow(h);break;}if(HandleExitShortcut(msg))continue;if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){DestroyWindow(h);break;}if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    EnableWindow(owner,TRUE);if(IsWindowVisible(owner))SetForegroundWindow(owner);DeleteObject(s.font);DeleteObject(s.heading);if(s.saved)key=s.key;return s.saved;
}
}
