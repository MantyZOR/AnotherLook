#pragma once
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <memory>

namespace gv {
enum class FontRole { Interface, Body };
const wchar_t* FontFamily(FontRole role);
const wchar_t* GdiFontFamily(FontRole role,int weight);

// Fonts belong to this process. No system installation, extraction or network I/O.
class Typography {
public:
    Typography();
    ~Typography();
    Typography(const Typography&)=delete;
    Typography& operator=(const Typography&)=delete;
    void Initialize(IDWriteFactory* factory);
    IDWriteFontCollection* Collection()const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
