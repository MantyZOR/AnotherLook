#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace gv {
inline constexpr UINT_PTR WindowMotionTimer=0x47564d;
inline constexpr unsigned WindowMotionDuration=840,MenuMotionDuration=400;
inline float MotionEase(float value){value=std::clamp(value,0.f,1.f);return 1-std::pow(1-value,3.f);}
inline float MenuItemProgress(float progress,int index,int count,bool links){
    (void)count;const float delay=(links?.18f:0)+std::min(index,11)*.016f;
    return MotionEase((progress-delay)/(1-delay));
}
bool ClientMotionAllowed();
bool MenuMotionEnabled();
bool WindowMotionActive(HWND window);
void FinishWindowMotion(HWND window);
void ShowBlueprintWindow(HWND window,int command=SW_SHOWNORMAL);
void EditMotionPreferences(HWND owner);
}
