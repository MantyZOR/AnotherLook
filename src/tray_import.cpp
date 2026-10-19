#include "tray.h"
#include <UIAutomation.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <cstddef>

namespace gv {
struct AccessibleTrayAction {ComPtr<IUIAutomationElement> element;};
namespace {
// Compatibility layout used by Explorer's classic notification toolbar. It is
// version-dependent: every pointer, owner, callback and version is validated.
// Layout reference: ManagedShell ExplorerTrayService.cs (Apache-2.0).
struct ExplorerItem {
    HWND window;UINT id,callback,state,version;HICON icon;ULONG_PTR timer;DWORD preference,lastSound;
    wchar_t executable[MAX_PATH],tip[MAX_PATH];DWORD seconds;GUID guid;
};
static_assert(offsetof(ExplorerItem,icon)==24&&offsetof(ExplorerItem,version)==20);
struct RemoteBuffer {
    HANDLE process{};void* data{};
    ~RemoteBuffer(){if(data)VirtualFreeEx(process,data,0,MEM_RELEASE);if(process)CloseHandle(process);}
};
bool Message(HWND window,UINT message,WPARAM w,LPARAM l,DWORD_PTR& result){return SendMessageTimeoutW(window,message,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,80,&result)!=0;}
bool Read(HANDLE process,const void* source,void* target,size_t size){SIZE_T read=0;return source&&ReadProcessMemory(process,source,target,size,&read)&&read==size;}
std::vector<HWND> Toolbars(HWND root){
    std::vector<HWND> result;if(root)EnumChildWindows(root,[](HWND window,LPARAM value)->BOOL{wchar_t name[64]{};GetClassNameW(window,name,64);auto& found=*reinterpret_cast<std::vector<HWND>*>(value);if(std::wstring_view(name)==L"ToolbarWindow32"&&found.size()<8)found.push_back(window);return TRUE;},reinterpret_cast<LPARAM>(&result));return result;
}
std::wstring PropertyText(IUIAutomationElement* element,bool name){BSTR text=nullptr;if(name)element->get_CurrentName(&text);else element->get_CurrentClassName(&text);std::wstring value=text?std::wstring(text,SysStringLen(text)):L"";SysFreeString(text);return value;}
void AccessibleItems(HWND root,HWND explorer,DWORD pid,std::vector<TrayIcon>& result){
    if(!root)return;ComPtr<IUIAutomation> automation;
    if(FAILED(CoCreateInstance(__uuidof(CUIAutomation8),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))))return;
    ComPtr<IUIAutomation2> timeouts;if(SUCCEEDED(automation.As(&timeouts))){timeouts->put_ConnectionTimeout(250);timeouts->put_TransactionTimeout(250);}
    ComPtr<IUIAutomationElement> element;ComPtr<IUIAutomationCondition> condition;ComPtr<IUIAutomationElementArray> children;
    if(FAILED(automation->ElementFromHandle(root,&element))||FAILED(automation->CreateTrueCondition(&condition))||FAILED(element->FindAll(TreeScope_Descendants,condition.Get(),&children)))return;
    int count=0;children->get_Length(&count);auto deadline=GetTickCount64()+400;
    for(int i=0;i<std::min(count,256)&&result.size()<128&&GetTickCount64()<deadline;++i){
        ComPtr<IUIAutomationElement> child;if(FAILED(children->GetElement(i,&child)))continue;
        auto cls=PropertyText(child.Get(),false);if(cls.find(L"NotifyIcon")==std::wstring::npos&&cls!=L"SystemTray.NormalIconView"&&cls!=L"SystemTray.IconView")continue;
        CONTROLTYPEID type=0;child->get_CurrentControlType(&type);if(type!=UIA_ButtonControlTypeId)continue;
        auto label=PropertyText(child.Get(),true);if(label.empty()||label.size()>512)continue;
        if(std::any_of(result.begin(),result.end(),[&](const auto& icon){return icon.tip==label;}))continue;
        TrayIcon icon;icon.window=explorer;icon.pid=pid;icon.id=0x80000000u+static_cast<UINT>(result.size());icon.tip=std::move(label);icon.recovered=true;icon.accessibility=std::make_shared<AccessibleTrayAction>();icon.accessibility->element=std::move(child);result.push_back(std::move(icon));
    }
}
}
std::vector<TrayIcon> ReadExplorerTray(HWND explorer){
    std::vector<TrayIcon> result;DWORD pid=0;GetWindowThreadProcessId(explorer,&pid);if(!pid)return result;
    // Only the actual Windows Explorer process may supply these private records.
    RemoteBuffer remote;remote.process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ|PROCESS_VM_OPERATION,FALSE,pid);
    wchar_t image[32768]{},windows[MAX_PATH]{};DWORD length=32768;GetWindowsDirectoryW(windows,MAX_PATH);
    if(!remote.process)remote.process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!remote.process||!QueryFullProcessImageNameW(remote.process,0,image,&length)||_wcsicmp(image,(fs::path(windows)/L"explorer.exe").c_str())!=0)return result;
    BOOL wow64=FALSE;if(!IsWow64Process(remote.process,&wow64)||wow64)return result;
    HWND notify=FindWindowExW(explorer,nullptr,L"TrayNotifyWnd",nullptr);std::vector<HWND> roots;if(notify)roots.push_back(notify);
    HWND overflow=nullptr;while((overflow=FindWindowExW(nullptr,overflow,L"NotifyIconOverflowWindow",nullptr))){DWORD owner=0;GetWindowThreadProcessId(overflow,&owner);if(owner==pid)roots.push_back(overflow);}
    remote.data=VirtualAllocEx(remote.process,nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    bool classic=false;auto deadline=GetTickCount64()+250;
    if(remote.data)for(HWND root:roots)for(HWND toolbar:Toolbars(root)){
        DWORD_PTR count=0;if(!Message(toolbar,TB_BUTTONCOUNT,0,0,count)||count>512)continue;classic|=count>0;
        for(UINT i=0;i<count&&result.size()<128&&GetTickCount64()<deadline;++i){
            DWORD_PTR ok=0;TBBUTTON button{};ExplorerItem item{};
            if(!Message(toolbar,TB_GETBUTTON,i,reinterpret_cast<LPARAM>(remote.data),ok)||!ok||!Read(remote.process,remote.data,&button,sizeof(button))||!button.dwData)continue;
            bool full=Read(remote.process,reinterpret_cast<const void*>(button.dwData),&item,sizeof(item));
            if(!full&&!Read(remote.process,reinterpret_cast<const void*>(button.dwData),&item,32))continue;
            DWORD owner=0;GetWindowThreadProcessId(item.window,&owner);
            if(!owner||!IsWindow(item.window)||item.callback>0xffff||(item.callback&&item.callback<WM_USER)||item.version>NOTIFYICON_VERSION_4)continue;
            if(std::any_of(result.begin(),result.end(),[&](const auto& icon){return icon.window==item.window&&icon.id==item.id;}))continue;
            TrayIcon icon;icon.window=item.window;icon.pid=owner;icon.id=item.id;icon.callback=item.callback;icon.state=item.state&NIS_HIDDEN;icon.version=item.version;icon.recovered=true;
            if(full){item.tip[MAX_PATH-1]=0;icon.tip=item.tip;icon.guid=item.guid;}
            if(icon.tip.empty()&&button.iString>65535){wchar_t text[128]{};if(Read(remote.process,reinterpret_cast<const void*>(button.iString),text,sizeof(text))){text[127]=0;icon.tip=text;}}
            icon.image=MonoFromIcon(item.icon);result.push_back(std::move(icon));
        }
    }
    // Windows 11's XAML tray may have no classic toolbar. Its accessibility
    // provider exposes native Invoke and ShowContextMenu without guessing an
    // application's private callback protocol.
    if(!classic||result.empty()){AccessibleItems(notify?notify:explorer,explorer,pid,result);for(HWND root:roots)if(root!=notify)AccessibleItems(root,explorer,pid,result);}
    return result;
}
bool InvokeAccessibleTray(const TrayIcon& icon,bool rightClick){
    if(!icon.accessibility||!icon.accessibility->element)return false;
    static std::atomic<bool> busy=false;if(busy.exchange(true))return false;
    IStream* stream=nullptr;if(FAILED(CoMarshalInterThreadInterfaceInStream(__uuidof(IUIAutomationElement),icon.accessibility->element.Get(),&stream))){busy=false;return false;}
    try{std::thread([stream,rightClick]{
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);ComPtr<IUIAutomationElement> element;
        if(SUCCEEDED(initialized)&&SUCCEEDED(CoGetInterfaceAndReleaseStream(stream,IID_PPV_ARGS(&element)))){
            if(rightClick){ComPtr<IUIAutomationElement3> context;if(SUCCEEDED(element.As(&context)))context->ShowContextMenu();}
            else{ComPtr<IUIAutomationInvokePattern> invoke;if(SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke))))invoke->Invoke();else{ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;if(SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,IID_PPV_ARGS(&legacy))))legacy->DoDefaultAction();}}
            element.Reset();CoUninitialize();
        }else{if(FAILED(initialized))stream->Release();if(SUCCEEDED(initialized))CoUninitialize();}busy=false;
    }).detach();}catch(...){stream->Release();busy=false;return false;}return true;
}
bool WriteExplorerTrayDiagnostics(const fs::path& path){
    std::vector<TrayIcon> snapshot;HWND root=nullptr;while((root=FindWindowExW(nullptr,root,L"Shell_TrayWnd",nullptr))){snapshot=ReadExplorerTray(root);if(!snapshot.empty())break;}json::JsonObject report;json::JsonArray items;
    for(const auto& icon:snapshot){json::JsonObject item;wchar_t guid[40]{};StringFromGUID2(icon.guid,guid,40);item.SetNamedValue(L"guid",json::JsonValue::CreateStringValue(guid));item.SetNamedValue(L"tip",json::JsonValue::CreateStringValue(icon.tip));item.SetNamedValue(L"pid",json::JsonValue::CreateNumberValue(icon.pid));item.SetNamedValue(L"id",json::JsonValue::CreateNumberValue(icon.id));item.SetNamedValue(L"version",json::JsonValue::CreateNumberValue(icon.version));item.SetNamedValue(L"hidden",json::JsonValue::CreateBooleanValue((icon.state&NIS_HIDDEN)!=0));item.SetNamedValue(L"hasImage",json::JsonValue::CreateBooleanValue(icon.image&&!icon.image->pixels.empty()));item.SetNamedValue(L"source",json::JsonValue::CreateStringValue(icon.accessibility?L"accessibility":L"explorer"));items.Append(item);}
    report.SetNamedValue(L"count",json::JsonValue::CreateNumberValue(static_cast<double>(snapshot.size())));report.SetNamedValue(L"icons",items);return WriteAtomic(path,std::wstring(report.Stringify()));
}
}
