#include "web.h"
#include <wrl/event.h>
#include <algorithm>
#include <shlwapi.h>
#ifdef GOODVIEW_WEBVIEW2
#include <WebView2EnvironmentOptions.h>
#endif
namespace gv {
using Microsoft::WRL::Callback;
struct WebManager::Site {
    WebManager* owner{};std::wstring id,title,url,initialUrl,scene,policy,status=L"Открываем…";HWND parent{};RECT bounds{};
    bool detached=false,visible=true,closed=false,loading=true,creating=false,capturing=false,closeAfterCapture=false;int downloads=0;unsigned navigations=0;
#ifdef GOODVIEW_WEBVIEW2
    ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> web;
#endif
};
WebManager::WebManager(fs::path d,std::function<void()> c):dir_(std::move(d)),changed_(std::move(c)){WNDCLASSW cls{};cls.lpfnWndProc=DeferredProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GoodView.WebEvents";RegisterClassW(&cls);events_=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,this);}
WebManager::~WebManager(){*alive_=false;deferred_.clear();CloseAll();if(events_)DestroyWindow(events_);}
void WebManager::Defer(std::function<void()> action){deferred_.push_back(std::move(action));PostMessageW(events_,WM_APP,0,0);}
LRESULT CALLBACK WebManager::DeferredProc(HWND window,UINT message,WPARAM w,LPARAM l){auto* self=reinterpret_cast<WebManager*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<WebManager*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(message==WM_APP&&self){auto actions=std::move(self->deferred_);self->deferred_.clear();for(auto& action:actions)action();return 0;}return DefWindowProcW(window,message,w,l);}
bool WebManager::Available()const{
#ifdef GOODVIEW_WEBVIEW2
    LPWSTR version=nullptr;auto hr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);CoTaskMemFree(version);return SUCCEEDED(hr);
#else
    return false;
#endif
}
void WebManager::Notify(){if(*alive_&&changed_)changed_();}
size_t WebManager::Count()const{return sites_.size();}
fs::path WebManager::PreviewPath(const Card& card)const{return dir_/L"previews"/(std::to_wstring(std::hash<std::wstring>{}(card.id+L"\n"+card.target))+L".png");}
void WebManager::Capture(const std::shared_ptr<Site>& site,bool closeAfter){
#ifdef GOODVIEW_WEBVIEW2
    if(site->closed||!site->web){if(closeAfter)Close(site->id);return;}site->closeAfterCapture|=closeAfter;if(site->capturing)return;
    Card card;card.id=site->id;card.target=site->initialUrl;auto path=PreviewPath(card),temp=path;temp+=L".tmp";std::error_code error;fs::create_directories(path.parent_path(),error);
    ComPtr<IStream> stream;if(FAILED(SHCreateStreamOnFileEx(temp.c_str(),STGM_CREATE|STGM_READWRITE|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,TRUE,nullptr,&stream))){if(closeAfter)Close(site->id);return;}
    site->capturing=true;auto navigation=site->navigations;auto alive=alive_;auto result=site->web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),Callback<ICoreWebView2CapturePreviewCompletedHandler>([this,alive,site,stream,path,temp,navigation](HRESULT result)mutable->HRESULT{
        if(!*alive)return S_OK;
        // Closing a controller inside its COM callback can destroy the callback owner.
        Defer([this,site,stream,path,temp,result,navigation]()mutable{if(SUCCEEDED(result)){stream->Commit(STGC_DEFAULT);stream.Reset();MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);}else{stream.Reset();DeleteFileW(temp.c_str());}site->capturing=false;if(site->closed)return;if(navigation!=site->navigations)Capture(site,site->closeAfterCapture);else if(site->closeAfterCapture)Close(site->id);else Notify();});return S_OK;
    }).Get());if(FAILED(result)){site->capturing=false;stream.Reset();DeleteFileW(temp.c_str());if(closeAfter)Close(site->id);}
#else
    if(closeAfter)Close(site->id);
#endif
}
void WebManager::Freeze(const std::wstring& id){auto it=sites_.find(id);if(it!=sites_.end())Capture(it->second,true);}
void WebManager::Back(const std::wstring& id){
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);if(it!=sites_.end()&&it->second->web)it->second->web->GoBack();
#endif
}
void WebManager::Reload(const std::wstring& id){
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);if(it!=sites_.end()&&it->second->web)it->second->web->Reload();
#endif
}
void WebManager::Forward(const std::wstring& id){
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);if(it!=sites_.end()&&it->second->web)it->second->web->GoForward();
#endif
}
void WebManager::Navigate(const std::wstring& id,const std::wstring& url){
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);if(IsWebUrl(url)&&it!=sites_.end()&&it->second->web){it->second->loading=true;it->second->status=L"Загружается…";it->second->web->Navigate(url.c_str());Notify();}
#endif
}
bool WebManager::CanBack(const std::wstring& id)const{
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);BOOL can=FALSE;if(it!=sites_.end()&&it->second->web)it->second->web->get_CanGoBack(&can);return can!=FALSE;
#else
    return false;
#endif
}
bool WebManager::CanForward(const std::wstring& id)const{
#ifdef GOODVIEW_WEBVIEW2
    auto it=sites_.find(id);BOOL can=FALSE;if(it!=sites_.end()&&it->second->web)it->second->web->get_CanGoForward(&can);return can!=FALSE;
#else
    return false;
#endif
}
std::wstring WebManager::Address(const std::wstring& id)const{auto it=sites_.find(id);if(it==sites_.end())return {};
#ifdef GOODVIEW_WEBVIEW2
    if(it->second->web){LPWSTR value=nullptr;if(SUCCEEDED(it->second->web->get_Source(&value))&&value){std::wstring result=value;CoTaskMemFree(value);return result;}CoTaskMemFree(value);}
#endif
    return it->second->url;
}
bool WebManager::Busy()const{for(const auto& [id,site]:sites_)if(site->loading||site->downloads>0)return true;return false;}
unsigned WebManager::NavigationCount(const std::wstring& id)const{auto it=sites_.find(id);return it==sites_.end()?0:it->second->navigations;}
bool WebManager::Exists(const std::wstring& id)const{return sites_.contains(id);}
bool WebManager::Detached(const std::wstring& id)const{auto it=sites_.find(id);return it!=sites_.end()&&it->second->detached;}
std::wstring WebManager::Status(const std::wstring& id)const{auto i=sites_.find(id);return i==sites_.end()?L"Нажми, чтобы открыть":i->second->status;}
std::wstring WebManager::Activity()const{std::wstring s=L"Только сайты, открытые внутри GoodView.\n\n";if(sites_.empty())s+=L"Открытых встроенных сайтов нет.";for(auto&[id,p]:sites_){s+=p->title+L": "+p->status+(p->visible?L"":L" · в фоне")+L"\n";}s+=L"\nОтслеживаются загрузка страниц и скачивание файлов.\nSteam, Chrome, Edge и другие отдельные программы сюда не входят.";return s;}
void WebManager::Open(const Card& card,const std::wstring& scene,HWND parent,RECT bounds,bool standalone){
    if(!IsWebUrl(card.target))return;
    auto it=sites_.find(card.id);
    if(it!=sites_.end()&&!standalone){if(it->second->detached){FocusAppWindow(it->second->parent);}else Position(card.id,parent,bounds,true);return;}
    std::shared_ptr<Site>s;if(it!=sites_.end())s=it->second;else{s=std::make_shared<Site>();s->owner=this;s->id=card.id;s->title=card.title;s->url=card.target;s->initialUrl=card.target;s->scene=scene;s->policy=card.background;sites_[card.id]=s;}
    s->bounds=bounds;s->parent=parent;s->visible=true;
    if(standalone&&!s->detached){WNDCLASSW wc{};wc.lpfnWndProc=SiteProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GoodView.Web";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));RegisterClassW(&wc);
      HWND window=CreateWindowExW(0,wc.lpszClassName,card.title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,740,nullptr,nullptr,wc.hInstance,s.get());
      if(window){s->parent=window;s->detached=true;GetClientRect(window,&s->bounds);ShowWindow(window,SW_SHOW);}
    }
#ifdef GOODVIEW_WEBVIEW2
    if(s->controller){s->controller->put_ParentWindow(s->parent);s->controller->put_Bounds(s->bounds);s->controller->put_IsVisible(TRUE);Notify();return;}
    if(env_)Create(s);
    else if(!creating_){creating_=true;auto alive=alive_;auto path=(dir_/L"browser").wstring();
      auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();ComPtr<ICoreWebView2EnvironmentOptions8> appearance;if(SUCCEEDED(options.As(&appearance)))appearance->put_ScrollBarStyle(COREWEBVIEW2_SCROLLBAR_STYLE_FLUENT_OVERLAY);
      auto hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,path.c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this,alive](HRESULT result,ICoreWebView2Environment* environment)->HRESULT{
        if(!*alive)return S_OK;creating_=false;
        if(SUCCEEDED(result)&&environment){if(sites_.empty())return S_OK;env_=environment;auto pending=sites_;for(auto&[id,item]:pending)if(!item->closed&&!item->controller)Create(item);}
        else for(auto&[id,item]:sites_){item->loading=false;item->status=L"WebView2 недоступен ("+std::to_wstring(static_cast<unsigned long>(result))+L"). Можно открыть в браузере.";}Notify();return S_OK;}).Get());
      if(FAILED(hr)){creating_=false;s->loading=false;s->status=L"Не удалось запустить сайт: "+std::to_wstring(static_cast<unsigned long>(hr));}
    }
#else
    s->loading=false;s->status=L"Сборка без WebView2. Открой ссылку в браузере.";
#endif
    Notify();
}
void WebManager::Create(const std::shared_ptr<Site>& s){
#ifdef GOODVIEW_WEBVIEW2
    if(s->closed||s->creating||s->controller)return;s->creating=true;
    auto alive=alive_;auto createResult=env_->CreateCoreWebView2Controller(s->parent,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this,s,alive](HRESULT result,ICoreWebView2Controller* controller)->HRESULT{
      s->creating=false;
      if(!*alive||s->closed){if(controller)controller->Close();return S_OK;}
      if(FAILED(result)||!controller){s->status=L"Не удалось открыть сайт: "+std::to_wstring(static_cast<unsigned long>(result));s->loading=false;Notify();return S_OK;}
      s->controller=controller;controller->get_CoreWebView2(&s->web);controller->put_Bounds(s->bounds);controller->put_IsVisible(s->visible);
      ComPtr<ICoreWebView2Settings>settings;s->web->get_Settings(&settings);settings->put_AreDevToolsEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);
      std::weak_ptr<Site> weak=s;EventRegistrationToken token{};
      s->web->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>([this,alive](ICoreWebView2*,IUnknown*)->HRESULT{if(*alive)Notify();return S_OK;}).Get(),&token);
      s->web->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>([this,weak,alive](ICoreWebView2* sender,ICoreWebView2SourceChangedEventArgs*)->HRESULT{auto site=weak.lock();if(!*alive||!site)return S_OK;LPWSTR source=nullptr;if(SUCCEEDED(sender->get_Source(&source))&&source)site->url=source;CoTaskMemFree(source);Notify();return S_OK;}).Get(),&token);
      ComPtr<ICoreWebView2_2> web2;if(SUCCEEDED(s->web.As(&web2)))web2->add_DOMContentLoaded(Callback<ICoreWebView2DOMContentLoadedEventHandler>([this,weak,alive](ICoreWebView2*,ICoreWebView2DOMContentLoadedEventArgs*)->HRESULT{auto site=weak.lock();if(!*alive||!site)return S_OK;site->loading=false;site->status=L"Открыт";Notify();return S_OK;}).Get(),&token);
      s->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this,weak,alive](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{auto site=weak.lock();if(!*alive||!site)return S_OK;LPWSTR uri=nullptr;args->get_Uri(&uri);if(uri&&!IsWebUrl(uri)&&std::wstring_view(uri)!=L"about:blank")args->put_Cancel(TRUE);if(uri&&IsWebUrl(uri))site->url=uri;CoTaskMemFree(uri);site->loading=true;site->status=L"Загружается…";Notify();return S_OK;}).Get(),&token);
      s->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this,weak,alive](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{auto site=weak.lock();if(!*alive||!site||site->closed)return S_OK;BOOL ok=FALSE;args->get_IsSuccess(&ok);++site->navigations;site->loading=false;site->status=ok?L"Открыт":L"Не удалось загрузить страницу";if(ok)Capture(site);Notify();return S_OK;}).Get(),&token);
      ComPtr<ICoreWebView2_4>web4;if(SUCCEEDED(s->web.As(&web4)))web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([this,weak,alive](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT{
        auto site=weak.lock();if(!*alive||!site)return S_OK;ComPtr<ICoreWebView2DownloadOperation>op;args->get_DownloadOperation(&op);++site->downloads;site->status=L"Загружается файл";Notify();
        EventRegistrationToken t{};auto finished=std::make_shared<bool>(false);auto lastUpdate=std::make_shared<ULONGLONG>(0);
        op->add_BytesReceivedChanged(Callback<ICoreWebView2BytesReceivedChangedEventHandler>([this,weak,alive,lastUpdate](ICoreWebView2DownloadOperation* sender,IUnknown*)->HRESULT{auto item=weak.lock();if(!*alive||!item)return S_OK;auto now=GetTickCount64();if(now-*lastUpdate<500)return S_OK;*lastUpdate=now;INT64 received=0,total=0;sender->get_BytesReceived(&received);sender->get_TotalBytesToReceive(&total);item->status=total>0?L"Загрузка файла · "+std::to_wstring(static_cast<int>(100.0*received/total))+L"%":L"Загрузка файла";Notify();return S_OK;}).Get(),&t);
        op->add_StateChanged(Callback<ICoreWebView2StateChangedEventHandler>([this,weak,alive,finished](ICoreWebView2DownloadOperation* sender,IUnknown*)->HRESULT{auto item=weak.lock();if(!*alive||!item)return S_OK;COREWEBVIEW2_DOWNLOAD_STATE state{};sender->get_State(&state);if(state!=COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS&&!*finished){*finished=true;item->downloads=std::max(0,item->downloads-1);item->status=state==COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED?L"Файл загружен":L"Загрузка прервана";if(!item->visible&&item->policy==L"unload"&&item->downloads==0){auto id=item->id;Defer([this,id]{Close(id);});}else Notify();}return S_OK;}).Get(),&t);return S_OK;}).Get(),&token);
      s->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this,weak,alive](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{auto site=weak.lock();if(!*alive||!site)return S_OK;LPWSTR uri=nullptr;args->get_Uri(&uri);args->put_Handled(TRUE);if(uri&&IsWebUrl(uri))site->web->Navigate(uri);CoTaskMemFree(uri);return S_OK;}).Get(),&token);
      s->web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT{args->put_State(COREWEBVIEW2_PERMISSION_STATE_DEFAULT);return S_OK;}).Get(),&token);
      s->web->Navigate(s->url.c_str());Notify();return S_OK;
    }).Get());if(FAILED(createResult)){s->creating=false;s->loading=false;s->status=L"Не удалось создать окно сайта: "+std::to_wstring(static_cast<unsigned long>(createResult));Notify();}
#endif
}
void WebManager::Position(const std::wstring& id,HWND parent,RECT bounds,bool visible){auto it=sites_.find(id);if(it==sites_.end()||it->second->detached)return;auto&s=it->second;bool parentChanged=s->parent!=parent,boundsChanged=memcmp(&s->bounds,&bounds,sizeof(bounds))!=0,visibilityChanged=s->visible!=visible;s->parent=parent;s->bounds=bounds;s->visible=visible;
#ifdef GOODVIEW_WEBVIEW2
    if(s->controller){if(parentChanged)s->controller->put_ParentWindow(parent);if(boundsChanged)s->controller->put_Bounds(bounds);if(visibilityChanged)s->controller->put_IsVisible(visible);}
#endif
}
void WebManager::HideEmbedded(HWND parent){for(auto&[id,s]:sites_)if(!s->detached&&(!parent||s->parent==parent)){s->visible=false;
#ifdef GOODVIEW_WEBVIEW2
    if(s->controller)s->controller->put_IsVisible(FALSE);
#endif
}}
void WebManager::SceneChanged(const std::wstring& scene){scene_=scene;std::vector<std::wstring>close;
    for(auto&[id,s]:sites_){bool visible=s->scene==scene;s->visible=visible;if(s->detached&&IsWindow(s->parent))ShowWindow(s->parent,visible?SW_SHOWNOACTIVATE:SW_HIDE);
#ifdef GOODVIEW_WEBVIEW2
      if(s->controller)s->controller->put_IsVisible(visible);if(!visible&&s->policy==L"unload"&&s->downloads==0)close.push_back(id);
      else if(!visible&&s->policy==L"sleep"&&s->downloads==0&&s->web){ComPtr<ICoreWebView2_3>w3;if(SUCCEEDED(s->web.As(&w3)))w3->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>([weak=std::weak_ptr<Site>(s),alive=alive_,this](HRESULT hr,BOOL success)->HRESULT{auto item=weak.lock();if(*alive&&item){item->status=SUCCEEDED(hr)&&success?L"Спит":L"Работает в фоне";Notify();}return S_OK;}).Get());}
      else if(visible&&s->web){ComPtr<ICoreWebView2_3>w3;if(SUCCEEDED(s->web.As(&w3)))w3->Resume();}
#endif
    }for(auto&id:close)Close(id);Notify();
}
void WebManager::SetPolicy(const std::wstring& id,const std::wstring& p){auto it=sites_.find(id);if(it!=sites_.end())it->second->policy=p;}
void WebManager::Close(const std::wstring& id){auto it=sites_.find(id);if(it==sites_.end())return;auto s=it->second;s->closed=true;
#ifdef GOODVIEW_WEBVIEW2
    if(s->controller){s->controller->Close();s->controller.Reset();}s->web.Reset();
#endif
    if(s->detached&&IsWindow(s->parent)){SetWindowLongPtrW(s->parent,GWLP_USERDATA,0);DestroyWindow(s->parent);}sites_.erase(it);
#ifdef GOODVIEW_WEBVIEW2
    if(sites_.empty())env_.Reset();
#endif
    Notify();
}
void WebManager::CloseAll(){while(!sites_.empty())Close(sites_.begin()->first);}
LRESULT CALLBACK WebManager::SiteProc(HWND h,UINT m,WPARAM w,LPARAM l){auto*s=reinterpret_cast<Site*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<Site*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(s){if(m==WM_SIZE){GetClientRect(h,&s->bounds);
#ifdef GOODVIEW_WEBVIEW2
      if(s->controller)s->controller->put_Bounds(s->bounds);
#endif
      return 0;}if(m==WM_CLOSE){auto id=s->id;auto* owner=s->owner;owner->Close(id);return 0;}}
    return DefWindowProcW(h,m,w,l);
}
}
