#pragma once
#include "design.h"
namespace gv {
class Application;
class FileSearchSettings {
public:
    explicit FileSearchSettings(Application& app):app_(app){}
    ~FileSearchSettings();
    void Show(HWND owner=nullptr);void Refresh();void Close();bool Message(const MSG& message);
    HWND Window()const{return window_;}
    bool ExportPreview(const fs::path& output);
private:
    Application& app_;HWND window_{};HFONT font_{},small_{},title_{};std::vector<Config::FileRoot> roots_;int selected_=0,page_=0;
    void Layout();void Draw(HDC dc,RECT client);void Command(int id);void Apply(bool rebuild=false);void MakeExplicit();
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
};
}
