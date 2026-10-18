#pragma once
#include "design.h"

namespace gv {
class Application;
class Customization {
public:
    explicit Customization(Application& app):app_(app){}
    ~Customization();
    void Show(HWND owner,bool appearance=false);
    void Close();void Refresh();
    bool Message(const MSG& message);
    bool ExportPreview(const fs::path& path,bool appearance=false);
    HWND Window()const{return window_;}
private:
    friend struct InteractionProbe;
    friend struct DesktopRegressionProbe;
    Application& app_;HWND window_{};HFONT font_{},title_{},small_{},mini_{};HBRUSH background_{};
    UiElement selected_=UiElement::Navigation;bool appearance_=false,dragging_=false;int monitor_=0;
    RECT preview_{};POINT dragOffset_{};std::optional<Config::Interface> beforeDrag_;
    std::vector<std::pair<RECT,UiElement>> elements_;
    std::vector<std::shared_ptr<MonoIcon>> windowIcons_,trayIcons_;
    std::shared_ptr<MonoIcon> appIcon_;
    void Layout();void Draw(HDC dc,RECT client);void Apply();void Command(int id);
    void CapturePreviewIcons();void DrawMiniPanel(HDC dc,UiElement element,RECT rect,float factor);
    void MoveElement(POINT point);void EndDrag(bool cancel);
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
};
}
