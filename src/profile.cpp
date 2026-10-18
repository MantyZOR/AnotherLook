#include "profile.h"
#include "platform.h"
#include <shlobj.h>

namespace gv {
bool IsInstalledApplication(){std::error_code ec;return fs::is_regular_file(ExePath().parent_path()/L"installed.mode",ec);}
fs::path SelectDataDirectory(const fs::path& executable,const fs::path& localAppData,bool installed){
    if(installed){if(localAppData.empty())throw std::runtime_error("LocalAppData is unavailable");return localAppData/L"WindowsGoodView";}return executable.parent_path()/L"data";
}
fs::path DefaultDataDirectory(){
    auto executable=ExePath();bool installed=IsInstalledApplication();fs::path user;
    if(installed){PWSTR folder=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))user=folder;CoTaskMemFree(folder);}
    return SelectDataDirectory(executable,user,installed);
}
void RememberPortableProfile(const fs::path& directory){
    HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\WindowsGoodView",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;
    auto write=[&](const wchar_t* name,const std::wstring& value){RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));};
    write(L"PortableData",directory.wstring());write(L"PortableExe",ExePath().wstring());RegCloseKey(key);
}
bool MigratePortableProfile(const fs::path& destination,std::wstring& error){
    if(fs::exists(destination/L"settings.json"))return true;
    wchar_t path[32768]{},executable[32768]{};DWORD bytes=sizeof(path);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\WindowsGoodView",L"PortableData",RRF_RT_REG_SZ,nullptr,path,&bytes)!=ERROR_SUCCESS)return true;
    bytes=sizeof(executable);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\WindowsGoodView",L"PortableExe",RRF_RT_REG_SZ,nullptr,executable,&bytes)!=ERROR_SUCCESS||!fs::exists(fs::path(path)/L"settings.json"))return true;
    auto source=fs::weakly_canonical(path);if(source==fs::weakly_canonical(destination))return true;
    if(HWND running=FindWindowW(L"GoodView.Events",source.c_str())){
        DWORD pid=0;GetWindowThreadProcessId(running,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);bool stopped=false;
        if(process){wchar_t image[32768]{};DWORD count=32768;
            if(QueryFullProcessImageNameW(process,0,image,&count)&&_wcsicmp(image,executable)==0){PostMessageW(running,WM_APP+31,0,0);stopped=WaitForSingleObject(process,5000)==WAIT_OBJECT_0;}
            CloseHandle(process);
        }
        if(!stopped){error=L"Закрой запущенную переносимую версию WindowsGoodView и снова открой установленную: так настройки перенесутся после сохранения.";return false;}
    }
    if(!ImportProfile(source,destination,error))return false;
    std::wstring detail;auto config=LoadConfig(destination/L"settings.json",detail);if(config.autoStart&&IsAutoStartEnabled())SetAutoStart(true,destination);return true;
}
bool ImportProfile(const fs::path& from,const fs::path& to,std::wstring& error){
    try{
        auto source=fs::weakly_canonical(from),destination=fs::weakly_canonical(to);
        if(source==destination){error=L"Источник и папка данных совпадают.";return false;}
        if(fs::exists(destination/L"settings.json")){error=L"В папке назначения уже есть настройки. Они оставлены без изменений.";return false;}
        if(FindWindowW(L"GoodView.Events",source.c_str())||FindWindowW(L"GoodView.Events",destination.c_str())){error=L"Закрой оболочку перед переносом её данных.";return false;}
        auto raw=ReadUtf8(source/L"settings.json");Config config;std::wstring details;
        if(!raw||!Deserialize(*raw,config,details)){error=L"В источнике нет корректных настроек WindowsGoodView.";return false;}
        fs::create_directories(destination);
        // Never copy recovery journals or locks. Only the closed app's own data.
        for(auto name:{L"previews",L"browser"}){
            auto folder=source/name;if(!fs::is_directory(folder))continue;
            for(fs::recursive_directory_iterator it(folder,fs::directory_options::skip_permission_denied),end;it!=end;++it){
                if(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();continue;}
                auto itemName=it->path().filename().wstring();if(it->is_directory()&&(itemName==L"Cache"||itemName==L"Code Cache"||itemName==L"GPUCache"||itemName==L"DawnCache"||itemName==L"ShaderCache"||itemName==L"Crashpad")){it.disable_recursion_pending();continue;}
                auto target=destination/name/it->path().lexically_relative(folder);
                if(it->is_directory())fs::create_directories(target);
                else if(it->is_regular_file()){fs::create_directories(target.parent_path());fs::copy_file(it->path(),target,fs::copy_options::skip_existing);}
            }
        }
        if(!WriteAtomic(destination/L"settings.json",*raw)){error=L"Не удалось сохранить перенесённые настройки.";return false;}return true;
    }catch(...){error=L"Не удалось перенести профиль. Исходные данные оставлены без изменений.";return false;}
}
}
