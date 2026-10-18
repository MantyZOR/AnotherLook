#include "app_index.h"
#include <algorithm>
#include <set>

namespace gv {
namespace {
constexpr size_t MaxEntries=4000,MaxLearned=256,MaxBytes=4*1024*1024;
uint64_t Now(){FILETIME t;GetSystemTimeAsFileTime(&t);return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
bool Valid(const Program& p){return !p.name.empty()&&p.name.size()<=256&&!p.target.empty()&&p.target.size()<=32760&&p.name.find(L'\0')==std::wstring::npos&&p.target.find(L'\0')==std::wstring::npos;}
bool LaunchFile(const std::wstring& target){
    fs::path path(target);if(!path.is_absolute())return false;auto extension=AppIndex::Key(path.extension().wstring());
    if(extension!=L".exe"&&extension!=L".lnk"&&extension!=L".appref-ms"&&extension!=L".url")return false;
    DWORD attributes=GetFileAttributesW(target.c_str());return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
bool Missing(const std::wstring& target){
    if(!fs::path(target).is_absolute())return false;
    if(GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
    DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;
}
Program Prepare(Program p){p.window=nullptr;p.searchKey=AppIndex::Key(p.name);return p;}
}
std::wstring AppIndex::Key(std::wstring value){
    if(value.empty())return value;
    int count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr,0);if(count<=0)return value;
    std::wstring lower(count,L'\0');if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),lower.data(),count,nullptr,nullptr,0))return value;return lower;
}
bool AppIndex::Load(){
    catalogue_.clear();learned_.clear();updated_=0;
    std::error_code error;auto bytes=fs::file_size(file_,error);if(error||bytes>MaxBytes)return false;
    auto raw=ReadUtf8(file_);if(!raw)return false;
    try{
        auto root=json::JsonObject::Parse(*raw);if(root.GetNamedNumber(L"version",0)!=1)return false;
        uint64_t updated=std::stoull(std::wstring(root.GetNamedString(L"updated",L"0")));
        std::vector<Program> catalogue,learned;
        auto read=[&](const wchar_t* key,std::vector<Program>& to,size_t maximum){
            auto list=root.GetNamedArray(key);if(list.Size()>maximum)throw std::runtime_error("oversized catalogue");std::set<std::wstring> targets;
            for(auto item:list){auto o=item.GetObject();Program p{std::wstring(o.GetNamedString(L"name")),std::wstring(o.GetNamedString(L"target"))};
                if(!Valid(p))throw std::runtime_error("invalid catalogue entry");if(targets.insert(Key(p.target)).second)to.push_back(Prepare(std::move(p)));}
        };
        read(L"catalogue",catalogue,MaxEntries);read(L"learned",learned,MaxLearned);
        catalogue_=std::move(catalogue);learned_=std::move(learned);updated_=updated;return true;
    }catch(...){return false;}
}
bool AppIndex::Save()const{
    json::JsonObject root;root.SetNamedValue(L"version",json::JsonValue::CreateNumberValue(1));root.SetNamedValue(L"updated",json::JsonValue::CreateStringValue(std::to_wstring(updated_)));
    auto write=[&](const wchar_t* key,const std::vector<Program>& from){json::JsonArray list;for(const auto& p:from){json::JsonObject o;o.SetNamedValue(L"name",json::JsonValue::CreateStringValue(p.name));o.SetNamedValue(L"target",json::JsonValue::CreateStringValue(p.target));list.Append(o);}root.SetNamedValue(key,list);};
    write(L"catalogue",catalogue_);write(L"learned",learned_);auto text=std::wstring(root.Stringify());if(Utf8(text).size()>MaxBytes)return false;return WriteAtomic(file_,text);
}
void AppIndex::ReplaceCatalogue(std::vector<Program> programs){
    catalogue_.clear();std::set<std::wstring> targets;
    for(auto& p:programs)if(Valid(p)&&targets.insert(Key(p.target)).second&&catalogue_.size()<MaxEntries)catalogue_.push_back(Prepare(std::move(p)));
    std::erase_if(learned_,[](const Program& p){return Missing(p.target);});updated_=Now();
}
bool AppIndex::Learn(const Program& p){
    if(!Valid(p)||!LaunchFile(p.target))return false;auto target=Key(p.target);
    for(const auto* group:{&catalogue_,&learned_})for(const auto& existing:*group)if(Key(existing.target)==target)return false;
    if(learned_.size()>=MaxLearned)learned_.erase(learned_.begin());learned_.push_back(Prepare(p));return true;
}
std::vector<Program> AppIndex::Entries()const{
    auto all=catalogue_;std::set<std::wstring> targets;for(const auto& p:all)targets.insert(Key(p.target));for(const auto& p:learned_)if(targets.insert(Key(p.target)).second)all.push_back(p);
    std::sort(all.begin(),all.end(),[](const Program& a,const Program& b){return a.searchKey<b.searchKey;});return all;
}
bool AppIndex::RefreshDue()const{auto now=Now();return !updated_||updated_>now||now-updated_>24ull*60*60*10000000;}
std::optional<Program> AppIndex::Discover(std::wstring query){
    while(!query.empty()&&iswspace(query.front()))query.erase(query.begin());while(!query.empty()&&iswspace(query.back()))query.pop_back();
    if(query.size()>1&&query.front()==L'"'&&query.back()==L'"')query=query.substr(1,query.size()-2);
    if(query.empty()||query.size()>32760)return {};
    if(LaunchFile(query))return Program{fs::path(query).stem().wstring(),query};
    if(query.size()>128||query.find_first_of(L"\\/:*?\"")!=std::wstring::npos)return {};
    auto name=query;if(!Key(name).ends_with(L".exe"))name+=L".exe";
    for(HKEY root:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
        HKEY key{};auto subkey=L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"+name;
        if(RegOpenKeyExW(root,subkey.c_str(),0,KEY_QUERY_VALUE|view,&key)!=ERROR_SUCCESS)continue;
        wchar_t path[32768]{};DWORD bytes=sizeof(path);auto result=RegGetValueW(key,nullptr,nullptr,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,path,&bytes);RegCloseKey(key);
        if(result==ERROR_SUCCESS&&LaunchFile(path))return Program{fs::path(name).stem().wstring(),path};
    }
    wchar_t path[32768]{};DWORD count=SearchPathW(nullptr,name.c_str(),nullptr,32768,path,nullptr);if(count>0&&count<32768&&LaunchFile(path))return Program{fs::path(name).stem().wstring(),path};return {};
}
}
