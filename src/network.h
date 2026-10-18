#pragma once
#include "core.h"
namespace gv {
struct NetworkNode {float x,y,radius,baseX=0,baseY=0,phase=0;};
struct NetworkEdge {int a,b;};
// A bounded decoration. Links are built once; optional motion only offsets nodes.
struct CanvasNetwork {
    std::vector<NetworkNode> nodes;
    std::vector<NetworkEdge> edges;
    void Build(float width,float height,float panX=0,float panY=0,float zoom=1,float density=1);
    void Animate(float seconds);
    int Nearest(float x,float y,float maxDistance=220)const;
};
}
