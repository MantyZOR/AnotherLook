#pragma once
#include "design.h"
#include <memory>

namespace gv {
struct BranchMenuItem {std::wstring text;UINT id=0,state=0;HMENU child{};const MonoIcon* icon{};};
// Renderer and targets share the same coordinates, including during opening.
// No window, focus changes or timers are needed to render a preview or measure it.
class BranchMenuRenderer {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    BranchMenuRenderer();~BranchMenuRenderer();
    bool Build(HMENU menu,UINT dpi,SIZE maximum,bool back,std::wstring mode);
    bool Render(float progress,int hover=-1,int selected=-1);
    int Hit(POINT point)const;
    SIZE Size()const;POINT Hub()const;
    const std::vector<BranchMenuItem>& Items()const;
    const std::vector<RECT>& Targets()const;
    const PanelBitmap& Bitmap()const;
    size_t CacheBytes()const;
};
bool TryBranchMenu(HMENU menu,HWND owner,POINT anchor,UINT& result);
}
