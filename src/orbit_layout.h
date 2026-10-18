#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gv {
inline constexpr int OrbitCapacity=9;
inline constexpr float OrbitTau=6.2831853072f;
inline constexpr float OrbitRadius=110.f,OrbitBandWidth=44.f,OrbitSegmentGap=.075f;
struct OrbitArc {float start,sweep;};
inline OrbitArc OrbitSegment(int slot,int visible){
    float step=OrbitTau/std::clamp(visible,1,OrbitCapacity);
    return {-OrbitTau/4-(slot+1)*step+OrbitSegmentGap/2,step-OrbitSegmentGap};
}
struct OrbitLift {float x,y,scale;};
inline OrbitLift LiftOrbitSegment(int slot,int visible,float amount){
    float angle=-OrbitTau/4-(slot+.5f)*OrbitTau/std::clamp(visible,1,OrbitCapacity);
    return {3*amount*std::cos(angle),3*amount*std::sin(angle),1+.025f*amount};
}
inline bool ContainsOrbitSegment(int slot,int visible,float x,float y,float amount=0){
    if(slot<0||slot>=visible)return false;auto lift=LiftOrbitSegment(slot,visible,amount);
    x=(x-lift.x)/lift.scale;y=(y-lift.y)/lift.scale;float radius=std::hypot(x,y);
    if(radius<OrbitRadius-OrbitBandWidth/2||radius>OrbitRadius+OrbitBandWidth/2)return false;
    auto arc=OrbitSegment(slot,visible);float angle=std::fmod(std::atan2(y,x)-arc.start+2*OrbitTau,OrbitTau);
    return angle<=arc.sweep;
}
class OrbitHoverMotion {
public:
    bool Running()const{return running_;}
    float Value(int slot)const{return slot>=0&&slot<OrbitCapacity?values_[slot]:0;}
    void Reset(){values_.fill(0);from_.fill(0);target_=-1;running_=false;}
    bool Tick(uint64_t now){
        if(!running_)return false;float time=std::clamp(static_cast<float>(now-began_)/140.f,0.f,1.f),ease=time*time*(3-2*time);
        for(int i=0;i<OrbitCapacity;++i)values_[i]=from_[i]+((i==target_?1.f:0.f)-from_[i])*ease;
        if(time>=1)running_=false;return running_;
    }
    bool Target(int slot,uint64_t now,bool animate){
        Tick(now);if(slot==target_)return running_;from_=values_;target_=slot;began_=now;running_=false;
        for(int i=0;i<OrbitCapacity;++i){float target=i==slot?1.f:0.f;if(animate)running_|=values_[i]!=target;else values_[i]=target;}
        return running_;
    }
private:
    std::array<float,OrbitCapacity> values_{},from_{};int target_=-1;uint64_t began_=0;bool running_=false;
};
struct OrbitSlot {int before=-1,after=-1;float blend=0,x=0,y=0;};
struct OrbitLayout {std::array<OrbitSlot,OrbitCapacity> slots{};int visible=0;};
inline float OrbitLimit(int count){return static_cast<float>(std::max(0,count-OrbitCapacity));}
inline int OrbitItem(int advanced,int slot){return slot+(advanced>slot?((advanced-1-slot)/OrbitCapacity+1)*OrbitCapacity:0);}
inline OrbitLayout LayoutOrbit(int count,float position){
    OrbitLayout layout;layout.visible=std::clamp(count,0,OrbitCapacity);
    position=std::isfinite(position)?std::clamp(position,0.f,OrbitLimit(count)):0;
    int advanced=static_cast<int>(position);float fraction=position-advanced;
    for(int slot=0;slot<layout.visible;++slot){
        auto& cell=layout.slots[slot];cell.before=OrbitItem(advanced,slot);cell.after=cell.before;
        if(fraction>0&&slot==advanced%OrbitCapacity){cell.after=OrbitItem(advanced+1,slot);cell.blend=fraction;}
        // The seam stays at twelve o'clock. Numbering goes down the left side,
        // across the bottom, and up the right side, matching the user's sketch.
        float angle=-OrbitTau/4-(slot+.5f)*OrbitTau/layout.visible;
        cell.x=140.f+OrbitRadius*std::cos(angle);cell.y=140.f+OrbitRadius*std::sin(angle);
    }
    return layout;
}
class OrbitMotion {
public:
    static constexpr uint64_t DurationMs=180;
    float Position()const{return position_;}
    float Target()const{return target_;}
    bool Running()const{return running_;}
    void Set(float value,int count){position_=from_=target_=std::clamp(value,0.f,OrbitLimit(count));running_=false;}
    void Limit(int count){float maximum=OrbitLimit(count);position_=std::clamp(position_,0.f,maximum);from_=std::clamp(from_,0.f,maximum);target_=std::clamp(target_,0.f,maximum);if(position_==target_)running_=false;}
    bool Tick(uint64_t now){
        if(!running_)return false;float time=std::clamp(static_cast<float>(now-began_)/DurationMs,0.f,1.f);
        float ease=time*time*(3-2*time);position_=from_+(target_-from_)*ease;
        if(time>=1){position_=target_;running_=false;}return running_;
    }
    bool Scroll(float steps,int count,uint64_t now,bool animate){
        if(!std::isfinite(steps))return false;Tick(now);Limit(count);float next=std::clamp(target_+steps,0.f,OrbitLimit(count));
        if(next==target_)return running_;from_=position_;target_=next;began_=now;running_=animate&&from_!=target_;
        if(!running_)position_=target_;return running_;
    }
    void Finish(){position_=from_=target_;running_=false;}
private:
    float position_=0,from_=0,target_=0;uint64_t began_=0;bool running_=false;
};
}
