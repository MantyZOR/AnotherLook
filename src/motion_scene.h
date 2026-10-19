#pragma once
#include "panel_bitmap.h"
#include <d2d1.h>
#include <functional>
#include <memory>

namespace gv {
// Short-lived retained scene. Curves run in DWM; no UI frame timer or render
// loop is needed after Commit. Surfaces die with it; one device is shared.
class MotionScene {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    using Node=size_t;
    static constexpr Node None=static_cast<Node>(-1);
    enum class Property { X,Y,ScaleX,ScaleY,Opacity,Left,Top,Right,Bottom };
    MotionScene();~MotionScene();
    MotionScene(const MotionScene&)=delete;
    bool Open(HWND window,size_t budget=12*1024*1024);
    Node Add(const PanelBitmap& bitmap,POINT position={},Node parent=None);
    Node Group(SIZE size,Node parent=None);
    Node Draw(SIZE size,const std::function<void(ID2D1RenderTarget*)>& draw,POINT position={},Node parent=None);
    Node Clone(Node source,Node parent=None);
    bool Set(Node node,Property property,float value);
    bool Animate(Node node,Property property,float from,float to,double delay,double seconds);
    bool Commit();
    size_t Bytes()const;
    size_t Commits()const;
    static unsigned ActiveScenes();
};
double MotionNow();
bool WarmMotionDevice();
}
