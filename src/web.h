#pragma once
#include "platform.h"
#include <functional>
#ifdef GOODVIEW_WEBVIEW2
#include <WebView2.h>
#endif
namespace gv {
class WebManager {
public:
    WebManager(fs::path directory,std::function<void()> changed);
    ~WebManager();
    bool Available()const;
    void Open(const Card& card,const std::wstring& scene,HWND parent,RECT bounds,bool standalone=false);
    void Position(const std::wstring& id,HWND parent,RECT bounds,bool visible);
    void SceneChanged(const std::wstring& scene);
    void HideEmbedded(HWND parent=nullptr);
    void Close(const std::wstring& id);
    void CloseAll();
    void SetPolicy(const std::wstring& id,const std::wstring& policy);
    fs::path PreviewPath(const Card& card)const;
    void Freeze(const std::wstring& id);
    void Back(const std::wstring& id);
    void Forward(const std::wstring& id);
    void Navigate(const std::wstring& id,const std::wstring& url);
    bool CanBack(const std::wstring& id)const;
    bool CanForward(const std::wstring& id)const;
    std::wstring Address(const std::wstring& id)const;
    void Reload(const std::wstring& id);
    bool Exists(const std::wstring& id)const;
    bool Detached(const std::wstring& id)const;
    std::wstring Status(const std::wstring& id)const;
    std::wstring Activity()const;
    bool Busy()const;
    unsigned NavigationCount(const std::wstring& id)const;
    size_t Count()const;
private:
    struct Site;
    fs::path dir_;
    std::function<void()> changed_;
    std::map<std::wstring,std::shared_ptr<Site>> sites_;
    std::wstring scene_;
    bool creating_=false;
    HWND events_{};std::vector<std::function<void()>> deferred_;
    void Defer(std::function<void()> action);
    static LRESULT CALLBACK DeferredProc(HWND,UINT,WPARAM,LPARAM);
    std::shared_ptr<bool> alive_=std::make_shared<bool>(true);
#ifdef GOODVIEW_WEBVIEW2
    ComPtr<ICoreWebView2Environment> env_;
#endif
    void Create(const std::shared_ptr<Site>& site);
    void Notify();
    void Capture(const std::shared_ptr<Site>& site,bool closeAfter=false);
    static LRESULT CALLBACK SiteProc(HWND,UINT,WPARAM,LPARAM);
};
}
