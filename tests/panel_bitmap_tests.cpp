#include "panel_bitmap.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace gv;
namespace {void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
int main(){try{
    PanelBitmap bitmap;
    Check(!bitmap.Resize(0,20)&&!bitmap.Dc(),"zero-sized buffers allocate nothing");
    for(int dpi:{96,120,144,192})for(int radius:{0,1,9,20,80}){
        int w=MulDiv(211,dpi,96),h=MulDiv(57,dpi,96);Check(bitmap.Resize(w,h),"allocate physical pixel buffer");
        auto pointer=bitmap.Pixels();auto dc=bitmap.Dc();Check(bitmap.Resize(w,h)&&pointer==bitmap.Pixels()&&dc==bitmap.Dc(),"unchanged size reuses the buffer");
        std::fill_n(pointer,static_cast<size_t>(w)*h,0x00c08040);bitmap.RoundAlpha(radius*dpi/96.f);
        bool symmetric=true,premultiplied=true;int partial=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=pointer[static_cast<size_t>(y)*w+x];unsigned a=p>>24;
            symmetric&=p==pointer[static_cast<size_t>(h-1-y)*w+x]&&p==pointer[static_cast<size_t>(y)*w+w-1-x];
            premultiplied&=(p&255)<=a&&((p>>8)&255)<=a&&((p>>16)&255)<=a;
            partial+=a>0&&a<255;
        }
        Check(symmetric,"all four rounded corners have matching pixel coverage");Check(premultiplied,"edge colors use premultiplied alpha without white fringes");
        Check(radius==0||partial>0,"a rounded edge includes partially covered pixels");
        Check((pointer[static_cast<size_t>(h/2)*w+w/2]>>24)==255,"the centre stays opaque");
    }
    bitmap.Reset();Check(!bitmap.Dc()&&!bitmap.Pixels()&&bitmap.Bytes()==0,"reset frees bitmap and DC");
    std::cout<<"20 DPI/radius combinations passed: symmetry, coverage, alpha, reuse and release\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
