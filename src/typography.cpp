#include "typography.h"
#include <wrl/implements.h>
#include <array>
#include <cstring>
#include <stdexcept>

namespace gv {
namespace {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
constexpr std::array<UINT,5> FontResources{201,202,203,204,205};
struct Bytes {const void* data{};DWORD size{};};
Bytes Resource(UINT id){
    HMODULE module=GetModuleHandleW(nullptr);
    auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)return {};
    return {LockResource(LoadResource(module,resource)),SizeofResource(module,resource)};
}
void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("Embedded typography initialization failed");}
class Stream final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IDWriteFontFileStream> {
public:
    explicit Stream(Bytes bytes):bytes_(bytes){}
    HRESULT STDMETHODCALLTYPE ReadFileFragment(const void** fragment,UINT64 offset,UINT64 length,void** context)override{
        if(!fragment||!context)return E_POINTER;
        *fragment=nullptr;*context=nullptr;
        if(offset>bytes_.size||length>bytes_.size-offset)return E_INVALIDARG;
        *fragment=static_cast<const BYTE*>(bytes_.data)+offset;return S_OK;
    }
    void STDMETHODCALLTYPE ReleaseFileFragment(void*)override{}
    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* size)override{if(!size)return E_POINTER;*size=bytes_.size;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* time)override{if(!time)return E_POINTER;*time=0;return S_OK;}
private:Bytes bytes_;
};
class Enumerator final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IDWriteFontFileEnumerator> {
public:
    Enumerator(IDWriteFactory* factory,IDWriteFontFileLoader* loader):factory_(factory),loader_(loader){}
    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* available)override{
        if(!available)return E_POINTER;*available=FALSE;current_.Reset();
        if(next_==FontResources.size())return S_OK;
        UINT key=FontResources[next_++];auto result=factory_->CreateCustomFontFileReference(&key,sizeof(key),loader_.Get(),&current_);
        if(SUCCEEDED(result))*available=TRUE;return result;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file)override{
        if(!file)return E_POINTER;*file=nullptr;return current_?current_.CopyTo(file):E_FAIL;
    }
private:ComPtr<IDWriteFactory> factory_;ComPtr<IDWriteFontFileLoader> loader_;ComPtr<IDWriteFontFile> current_;size_t next_=0;
};
class Loader final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IDWriteFontFileLoader,IDWriteFontCollectionLoader> {
public:
    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(const void* key,UINT32 size,IDWriteFontFileStream** stream)override{
        if(!stream)return E_POINTER;*stream=nullptr;if(!key||size!=sizeof(UINT))return E_INVALIDARG;
        UINT id;std::memcpy(&id,key,sizeof(id));if(id<FontResources.front()||id>FontResources.back())return E_INVALIDARG;
        auto bytes=Resource(id);if(!bytes.data||!bytes.size)return E_FAIL;
        auto instance=Make<Stream>(bytes);return instance?instance.CopyTo(stream):E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory,const void* key,UINT32 size,IDWriteFontFileEnumerator** enumerator)override{
        if(!enumerator)return E_POINTER;*enumerator=nullptr;
        if(!factory||!key||size!=sizeof(UINT))return E_INVALIDARG;
        UINT id;std::memcpy(&id,key,sizeof(id));if(id!=1)return E_INVALIDARG;
        auto instance=Make<Enumerator>(factory,static_cast<IDWriteFontFileLoader*>(this));return instance?instance.CopyTo(enumerator):E_OUTOFMEMORY;
    }
};
}
struct Typography::State {
    ComPtr<IDWriteFactory> factory;ComPtr<Loader> loader;ComPtr<IDWriteFontCollection> collection;
    std::array<HANDLE,FontResources.size()> gdi{};bool fileRegistered=false,collectionRegistered=false;
    ~State(){
        collection.Reset();
        // Unregister outside loader destruction: the factory owns references too.
        if(collectionRegistered)factory->UnregisterFontCollectionLoader(loader.Get());
        if(fileRegistered)factory->UnregisterFontFileLoader(loader.Get());
        for(auto font:gdi)if(font)RemoveFontMemResourceEx(font);
    }
};
Typography::Typography()=default;
Typography::~Typography()=default;
void Typography::Initialize(IDWriteFactory* factory){
    if(state_)return;
    auto state=std::make_unique<State>();state->factory=factory;state->loader=Make<Loader>();
    if(!factory||!state->loader)throw std::runtime_error("Embedded typography unavailable");
    Check(factory->RegisterFontFileLoader(state->loader.Get()));state->fileRegistered=true;
    Check(factory->RegisterFontCollectionLoader(state->loader.Get()));state->collectionRegistered=true;
    const UINT key=1;Check(factory->CreateCustomFontCollection(state->loader.Get(),&key,sizeof(key),&state->collection));
    for(size_t i=0;i<FontResources.size();++i){auto bytes=Resource(FontResources[i]);DWORD count=0;
        if(!bytes.data||!bytes.size)throw std::runtime_error("Embedded font missing");
        state->gdi[i]=AddFontMemResourceEx(const_cast<void*>(bytes.data),bytes.size,nullptr,&count);
        if(!state->gdi[i]||!count)throw std::runtime_error("Embedded GDI font unavailable");
    }
    state_=std::move(state);
}
IDWriteFontCollection* Typography::Collection()const{return state_?state_->collection.Get():nullptr;}
const wchar_t* FontFamily(FontRole role){return role==FontRole::Body?L"Onest":L"Rubik";}
const wchar_t* GdiFontFamily(FontRole role,int weight){
    if(role==FontRole::Body)return L"Onest";
    return weight<=300?L"Rubik Light":weight>=600?L"Rubik SemiBold":weight>=500?L"Rubik Medium":L"Rubik";
}
}
