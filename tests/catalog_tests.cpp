#include "app_index.h"
#include "profile.h"
#include "system_load.h"
#include <iostream>
#include <stdexcept>

using namespace gv;
namespace {int checks=0;void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}}
int main(int argc,char** argv){try{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    if(argc==2&&std::string_view(argv[1])=="--inspect-load"){
        gv::SystemLoad load;load.Sample(15);Sleep(250);load.Sample(15);json::JsonObject report;
        auto number=[&](const wchar_t* key,std::optional<double> value){report.SetNamedValue(key,value?json::JsonValue::CreateNumberValue(*value):json::JsonValue::CreateNullValue());};
        number(L"cpuPercent",load.cpu);number(L"memoryUsedBytes",static_cast<double>(load.memoryUsed));number(L"memoryTotalBytes",static_cast<double>(load.memoryTotal));number(L"diskBusyPercent",load.disk);number(L"networkPercent",load.network);number(L"downloadBytesPerSecond",load.download);number(L"uploadBytesPerSecond",load.upload);std::cout<<Utf8(std::wstring(report.Stringify()))<<"\n";return 0;
    }
    auto root=fs::temp_directory_path()/(L"GoodView-catalog-"+NewId());fs::create_directories(root);auto file=root/L"program-index.json";
    auto exe=ExePath().wstring();AppIndex index(file);Check(!index.Load()&&index.RefreshDue(),"missing index requests initial scan");
    index.ReplaceCatalogue({{L"Программа",L"shell:AppsFolder\\Example!App",reinterpret_cast<HWND>(55)},{L"Программа",L"shell:AppsFolder\\Example!App"},{L"",L"bad"}});
    Check(index.Entries().size()==1&&index.Entries()[0].window==nullptr&&index.Entries()[0].searchKey==L"программа","catalogue deduplicates and stores precomputed Unicode keys without HWNDs");
    Check(!index.RefreshDue()&&index.Save(),"atomic catalogue persistence and refresh timestamp");AppIndex warm(file);Check(warm.Load()&&warm.Entries().size()==1&&!warm.RefreshDue(),"warm start loads without enumeration");
    Check(warm.Learn({L"Portable test",exe,reinterpret_cast<HWND>(77)}),"dynamic portable executable is learned");Check(!warm.Learn({L"Other name",exe}),"same path is not learned twice");
    Check(!warm.Learn({L"Web",L"https://example.test"})&&!warm.Learn({L"Missing",(root/L"missing.exe").wstring()}),"website and missing executable do not pollute app index");Check(warm.Save(),"save dynamic discovery");
    AppIndex restart(file);Check(restart.Load()&&restart.Entries().size()==2,"dynamic app survives process restart");for(const auto& p:restart.Entries())Check(!p.window&&!p.searchKey.empty(),"all restored entries are ready for search and have no stale window handles");
    auto stale=root/L"deleted.exe";WriteAtomic(stale,L"fixture");Check(restart.Learn({L"Deleted app",stale.wstring()}),"existing launch target can enter learned catalogue");fs::remove(stale);restart.ReplaceCatalogue({{L"New name",exe}});Check(restart.Entries().size()==1,"refresh replaces removed catalogue entries and prunes deleted portable programs");
    auto quick=AppIndex::Discover(exe);Check(quick&&quick->target==exe,"explicit path fallback discovers unindexed executable");Check(!AppIndex::Discover(L"https://example.test")&&!AppIndex::Discover(L"not-a-real-goodview-program-123456"),"dynamic fallback does not invent apps for URLs or unknown names");
    WriteAtomic(file,L"{bad");AppIndex corrupt(file);Check(!corrupt.Load()&&corrupt.Entries().empty(),"malformed cache falls back to rebuilding");
    WriteAtomic(file,L"{\"version\":99,\"updated\":\"0\",\"catalogue\":[],\"learned\":[]}");Check(!corrupt.Load(),"unsupported cache version is ignored");
    Check(SelectDataDirectory(L"C:\\Program Files\\WindowsGoodView\\WindowsGoodView.exe",L"C:\\Users\\example\\AppData\\Local",true)==fs::path(L"C:\\Users\\example\\AppData\\Local\\WindowsGoodView"),"installed app uses per-user writable data");
    Check(SelectDataDirectory(root/L"WindowsGoodView.exe",{},false)==root/L"data","portable exe keeps adjacent data directory");
    auto source=root/L"portable-data",destination=root/L"installed-data";auto config=DefaultConfig();Card card;card.id=L"system-widget";card.type=L"system";config.scenes[0].cards.push_back(card);WriteAtomic(source/L"settings.json",Serialize(config));WriteAtomic(source/L"recovery.json",L"must not migrate");WriteAtomic(source/L"previews"/L"view.png",L"fixture");std::wstring error;
    Check(ImportProfile(source,destination,error),"closed portable profile can migrate to user data");auto copied=LoadConfig(destination/L"settings.json",error);Check(copied.scenes[0].cards.back().type==L"system"&&fs::exists(destination/L"previews"/L"view.png")&&!fs::exists(destination/L"recovery.json"),"migration preserves widgets and previews without copying recovery journals");
    Check(!ImportProfile(source,destination,error),"migration never overwrites an existing user profile");
    Check(!CpuUsage({10,50,50},{10,50,50}),"zero CPU interval stays unavailable instead of dividing by zero");auto usage=CpuUsage({50,100,100},{70,130,110});Check(usage&&*usage==50,"CPU load subtracts idle included in kernel time");Check(!CpuUsage({50,100,100},{10,90,80}),"counter reset cannot underflow CPU load");Check(!CpuUsage({0,0,0},{100,10,10}),"inconsistent CPU times are rejected");
    gv::SystemLoad load;load.Sample();Check(!load.cpu&&load.memoryTotal>0&&load.memoryUsed<=load.memoryTotal,"first sample exposes real physical memory without inventing CPU usage");Sleep(80);load.Sample();Check(load.cpu&&*load.cpu>=0&&*load.cpu<=100,"subsequent native CPU sample is bounded");load.Reset();Check(!load.cpu,"resuming a paused widget requires a fresh CPU interval");
    load.Sample(15);Check(load.Metrics()==15&&!load.cpu&&!load.download,"enabling disk and network starts fresh intervals without invented rates");Sleep(80);load.Sample(15);Check((!load.disk||(*load.disk>=0&&*load.disk<=100))&&(!load.network||(*load.network>=0&&*load.network<=100))&&(!load.download||*load.download>=0)&&(!load.upload||*load.upload>=0),"disk and physical network counters are bounded or explicitly unavailable");load.Sample(2);Check(load.Metrics()==2&&!load.cpu&&!load.disk&&!load.network&&!load.download&&load.memoryTotal>0,"choosing memory alone releases disk and network monitoring");load.Reset();Check(load.Metrics()==0&&!load.disk&&!load.network&&!load.download,"hiding the widget releases optional monitoring state");
    // Every created path is inside this test's fresh private temp directory.
    Check(fs::weakly_canonical(root).parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&root.filename().wstring().starts_with(L"GoodView-catalog-"),"cleanup target is the private test directory");fs::remove_all(root);std::cout<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}catch(const winrt::hresult_error& e){std::wcerr<<L"FAIL: "<<e.message().c_str()<<L"\n";return 1;}}
