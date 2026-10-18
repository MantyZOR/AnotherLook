#include "file_index.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <psapi.h>

using namespace gv;
namespace {
int checks=0;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
bool Wait(const std::function<bool()>& condition,DWORD timeout=10000){auto until=GetTickCount64()+timeout;while(GetTickCount64()<until){if(condition())return true;Sleep(20);}return condition();}
void Put(const fs::path& path){fs::create_directories(path.parent_path());std::ofstream stream(path);stream<<"This content is not indexed: content_only_marker";}
bool Has(const std::vector<FileHit>& hits,const fs::path& path){return std::any_of(hits.begin(),hits.end(),[&](const auto& hit){return _wcsicmp(hit.path.c_str(),path.c_str())==0;});}
}
int main(int argc,char** argv){try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    fs::path root=argc>1?fs::absolute(Wide(argv[1])):fs::current_path();root/=L"file-index-"+NewId();fs::create_directories(root);
    auto files=root/L"files",cache=root/L"cache";fs::create_directories(files);fs::create_directories(cache);
    if(argc>2&&std::string_view(argv[2])=="--benchmark"){
        for(int i=0;i<20000;++i)Put(files/(L"Документ-"+std::to_wstring(i)+L"-benchmark.txt"));
        PROCESS_MEMORY_COUNTERS_EX before{sizeof(before)},after{sizeof(after)};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));
        Config::FileSearch config;config.initialized=config.enabled=true;config.allFixedDrives=false;config.roots={{files.wstring(),-1}};FileIndex index(cache/L"files.sqlite");auto began=GetTickCount64();index.Start(config);Check(Wait([&]{auto s=index.Status();return s.ready&&!s.busy||!s.error.empty();},90000),"benchmark finishes");Check(index.Status().error.empty(),"benchmark opens database");auto buildMs=GetTickCount64()-began;
        began=GetTickCount64();index.Query(L"19999-benchmark");Check(Wait([&]{return !index.Results(L"19999-benchmark").empty();}),"benchmark cold substring query finds one document");auto queryMs=GetTickCount64()-began;auto status=index.Status();GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
        json::JsonObject result;auto number=[&](const wchar_t* key,uint64_t value){result.SetNamedValue(key,json::JsonValue::CreateNumberValue(static_cast<double>(value)));};number(L"entries",status.entries);number(L"buildMs",buildMs);number(L"queryRoundTripMs",queryMs);number(L"databaseBytes",status.bytes);number(L"privateBytesBefore",before.PrivateUsage);number(L"privateBytesAfter",after.PrivateUsage);result.SetNamedValue(L"trigram",json::JsonValue::CreateBooleanValue(status.trigram));index.Stop();number(L"databaseBytesClosed",fs::file_size(cache/L"files.sqlite"));WriteAtomic(root/L"benchmark.json",std::wstring(result.Stringify()));std::cout<<Utf8(std::wstring(result.Stringify()))<<"\n";return 0;
    }
    auto report=files/L"Календарь report 2026.txt",deep=files/L"a"/L"b"/L"c"/L"d"/L"deep-unique.txt",included=deep.parent_path().parent_path()/L"depth-three.txt";
    Put(report);Put(deep);Put(included);Put(files/L"report [final].txt");Put(files/L"a"/L"two-letters.txt");
    Config::FileSearch config;config.initialized=config.enabled=true;config.allFixedDrives=false;config.roots={{files.wstring(),3}};
    auto overlap=config;overlap.roots.push_back({(files/L"a").wstring(),-1});Check(FileIndex::Roots(overlap).size()==2,"a bounded parent does not suppress an unlimited child root");overlap.roots[0].depth=-1;Check(FileIndex::Roots(overlap).size()==1,"unlimited parents deduplicate covered roots");
    FileIndex index(cache/L"files.sqlite");index.Start(config);
    Check(Wait([&]{auto s=index.Status();return s.enabled&&s.ready&&!s.busy||!s.error.empty();}),"initial background scan finishes");Check(index.Status().error.empty(),"index opens the Windows SQLite provider");
    index.Query(L"КАЛЕНДАРЬ");Check(Wait([&]{return Has(index.Results(L"календарь"),report);}),"Unicode invariant case insensitive filename search");
    if(index.Status().trigram){index.Query(L"лендарь");Check(Wait([&]{return Has(index.Results(L"лендарь"),report);}),"trigram index finds a substring inside a filename");}
    index.Query(L"depth-three");Check(Wait([&]{return Has(index.Results(L"depth-three"),included);}),"files in the third nested directory are indexed");
    index.Query(L"deep-unique");Sleep(200);Check(index.Results(L"deep-unique").empty(),"depth limit excludes deeper files");
    index.Query(L"report [final]");Check(Wait([&]{return Has(index.Results(L"report [final]"),files/L"report [final].txt");}),"GLOB metacharacters in names are escaped");
    index.Query(L"content_only_marker");Sleep(200);Check(index.Results(L"content_only_marker").empty(),"file contents are never indexed");
    auto created=files/L"created-by-watch.txt";Put(created);index.Query(L"created-by-watch");Check(Wait([&]{return Has(index.Results(L"created-by-watch"),created);}),"directory notifications learn a new file without a global rebuild");
    auto renamed=files/L"renamed-by-watch.txt";fs::rename(created,renamed);index.Query(L"renamed-by-watch");Check(Wait([&]{return Has(index.Results(L"renamed-by-watch"),renamed);}),"rename updates the searchable path");
    fs::remove(renamed);Check(Wait([&]{return index.Results(L"renamed-by-watch").empty();}),"delete removes the cached result");
    auto child=files/L"new-folder"/L"nested-created.txt";Put(child);index.Query(L"nested-created");Check(Wait([&]{return Has(index.Results(L"nested-created"),child);}),"a newly created directory is discovered with its contents");
    auto moved=files/L"moved-folder";fs::rename(child.parent_path(),moved);index.Query(L"nested-created");Check(Wait([&]{auto hits=index.Results(L"nested-created");return Has(hits,moved/child.filename())&&!Has(hits,child);}),"directory rename removes stale descendants and indexes new paths");
    config.paused=true;index.Configure(config);Check(Wait([&]{return index.Status().paused&&!index.Status().busy;}),"pause releases background enumeration while search remains available");
    auto later=files/L"after-pause.txt";Put(later);index.Query(L"Календарь");Check(Wait([&]{return Has(index.Results(L"Календарь"),report);}),"cached queries continue during pause");
    config.paused=false;index.Configure(config);index.Query(L"after-pause");Check(Wait([&]{return Has(index.Results(L"after-pause"),later);}),"resume applies changes accumulated during pause");
    config.roots[0].depth=0;index.Configure(config);Check(Wait([&]{return index.Status().ready&&!index.Status().busy;}),"changing depth reconciles existing cache");index.Query(L"depth-three");Sleep(300);Check(index.Results(L"depth-three").empty(),"narrowing depth prunes previously indexed descendants");
    config.roots[0].depth=-1;index.Configure(config);index.Query(L"deep-unique");Check(Wait([&]{return Has(index.Results(L"deep-unique"),deep);}),"unlimited depth finds deep nested files");
    index.Stop();Check(fs::exists(cache/L"files.sqlite"),"the index is durable on disk");Put(files/L"while-closed.txt");index.Start(config);index.Query(L"while-closed");Check(Wait([&]{return Has(index.Results(L"while-closed"),files/L"while-closed.txt");}),"restart reconciles names changed while the worker was stopped");
    index.Query(L"Календарь");index.Query(L"deep-unique");Check(Wait([&]{return Has(index.Results(L"deep-unique"),deep);}),"newer requests supersede stale queries");
    auto hot=files/L"hot-directory";for(int i=0;i<900;++i)Put(hot/(L"item-"+std::to_wstring(i)+L".txt"));
    Wait([&]{auto status=index.Status();return status.current==hot.wstring()&&status.currentEntries>=256;},2000);auto changedWhileScanning=hot/L"0000-created-during-scan.txt";Put(changedWhileScanning);index.Query(L"0000-created-during-scan");Check(Wait([&]{return Has(index.Results(L"0000-created-during-scan"),changedWhileScanning);}),"notifications during a large directory pass preserve newly created files");
    config.enabled=false;index.Configure(config);Check(Wait([&]{return !index.Status().enabled;}),"disabling search stops watchers and removes out of scope entries");index.Stop();
    json::JsonObject output;output.SetNamedValue(L"passed",json::JsonValue::CreateBooleanValue(true));output.SetNamedValue(L"checks",json::JsonValue::CreateNumberValue(checks));output.SetNamedValue(L"trigram",json::JsonValue::CreateBooleanValue(index.Status().trigram));output.SetNamedValue(L"fixture",json::JsonValue::CreateStringValue(root.wstring()));WriteAtomic(root/L"result.json",std::wstring(output.Stringify()));std::cout<<checks<<" file index checks passed; trigram="<<index.Status().trigram<<"\n";return 0;
}catch(const std::exception& error){std::cerr<<"File index failure after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}}
