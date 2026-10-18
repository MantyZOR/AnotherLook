#include "network.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace gv {
namespace {
uint32_t Hash(uint32_t x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;return x^(x>>16);}
float Unit(uint32_t seed){return (Hash(seed)&0xffffff)/16777215.f;}
}
void CanvasNetwork::Build(float width,float height,float panX,float panY,float zoom,float density){
    nodes.clear();edges.clear();if(width<=0||height<=0)return;
    // Large screens stay bounded, including very wide / scaled monitors.
    density=std::clamp(density,.5f,2.f);
    float spacing=std::max(145.f,std::sqrt(width*height/105.f))/std::sqrt(density);
    zoom=std::clamp(zoom,.5f,1.5f);float step=spacing*zoom;
    int left=static_cast<int>(std::floor(panX/spacing))-1,top=static_cast<int>(std::floor(panY/spacing))-1;
    int cols=static_cast<int>(std::ceil(width/step))+3,rows=static_cast<int>(std::ceil(height/step))+3;
    for(int row=0;row<rows&&nodes.size()<480;++row)for(int col=0;col<cols&&nodes.size()<480;++col){
        int gx=left+col,gy=top+row;uint32_t seed=Hash(static_cast<uint32_t>(gx)*73856093u^static_cast<uint32_t>(gy)*19349663u);
        float x=(gx*spacing+Unit(seed)*spacing*.76f-panX)*zoom,y=(gy*spacing+Unit(seed+17)*spacing*.76f-panY)*zoom;
        float weight=Unit(seed+59);float r=weight>.90f?6.f+(weight-.90f)*36.f:1.2f+std::pow(weight,2.2f)*3.2f;nodes.push_back({x,y,r,x,y,Unit(seed+83)*6.2831853f});
    }
    for(int i=0;i<static_cast<int>(nodes.size());++i){
        int nearest[2]={-1,-1};float distance[2]={step*step*2.3f,step*step*2.3f};
        for(int j=0;j<static_cast<int>(nodes.size());++j)if(i!=j){float dx=nodes[i].x-nodes[j].x,dy=nodes[i].y-nodes[j].y,d=dx*dx+dy*dy;
            if(d<distance[0]){nearest[1]=nearest[0];distance[1]=distance[0];nearest[0]=j;distance[0]=d;}else if(d<distance[1]){nearest[1]=j;distance[1]=d;}}
        for(int j:nearest)if(j>=0){int a=std::min(i,j),b=std::max(i,j);if(std::none_of(edges.begin(),edges.end(),[&](const auto& e){return e.a==a&&e.b==b;}))edges.push_back({a,b});}
    }
}
void CanvasNetwork::Animate(float seconds){
    // No allocation, random generator, neighbour search or growing history per frame.
    for(auto& n:nodes){
        float t=seconds*.09f,p=n.phase;
        n.x=n.baseX+12.f*(std::sin(t+p)-std::sin(p))+5.f*(std::sin(t*.61f+p*2)-std::sin(p*2));
        n.y=n.baseY+10.f*(std::sin(t*.79f+p*1.37f)-std::sin(p*1.37f))+4.f*(std::sin(t*.43f+p*3)-std::sin(p*3));
    }
}
int CanvasNetwork::Nearest(float x,float y,float maxDistance)const{int found=-1;float best=maxDistance*maxDistance;for(int i=0;i<static_cast<int>(nodes.size());++i){float dx=x-nodes[i].x,dy=y-nodes[i].y,d=dx*dx+dy*dy;if(d<best){best=d;found=i;}}return found;}
}
