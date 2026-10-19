#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace gv {
inline constexpr UINT_PTR WindowMotionTimer=0x47564d;
inline constexpr unsigned WindowMotionDuration=260,MenuMotionDuration=280;
inline float MotionEase(float value){value=std::clamp(value,0.f,1.f);return 1-std::pow(1-value,3.f);}
inline RECT BlueprintClip(int width,int height,float progress,int band){
    progress=std::clamp(progress,0.f,1.f);
    if(progress<.3f)return {0,0,std::max(1,static_cast<int>(std::lround(width*MotionEase(progress/.3f)))),std::min(height,band)};
    return {0,0,width,std::max(band,static_cast<int>(std::lround(height*MotionEase((progress-.3f)/.7f))))};
}
inline float MenuItemProgress(float progress,int index,int count,bool links){
    const float delay=(links?.2f:0)+std::min(index,11)*.018f;
    return MotionEase((progress-delay)/std::max(.1f,1-delay-.018f*std::max(0,std::min(count,12)-1-index)));
}
bool ClientMotionAllowed();
bool MenuMotionEnabled();
bool WindowMotionActive(HWND window);
void FinishWindowMotion(HWND window);
void ShowBlueprintWindow(HWND window,int command=SW_SHOWNORMAL);
void EditMotionPreferences(HWND owner);
}
