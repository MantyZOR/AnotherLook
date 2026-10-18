#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace gv {
// Small, retained, top-down BGRA buffer for floating chrome. The full desktop
// keeps its existing render target; no full-monitor transparency buffer is used.
class PanelBitmap {
public:
    ~PanelBitmap(){Reset();}
    PanelBitmap()=default;
    PanelBitmap(const PanelBitmap&)=delete;
    PanelBitmap& operator=(const PanelBitmap&)=delete;
    bool Resize(int width,int height);
    void Reset();
    void RoundAlpha(float radius);
    bool Present(HWND window)const;
    HDC Dc()const{return dc_;}
    uint32_t* Pixels()const{return pixels_;}
    SIZE Size()const{return size_;}
    size_t Bytes()const{return static_cast<size_t>(size_.cx)*size_.cy*4;}
private:
    HDC dc_{};HBITMAP bitmap_{};HGDIOBJ original_{};
    uint32_t* pixels_{};SIZE size_{};
};
}
