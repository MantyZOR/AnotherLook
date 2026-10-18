#pragma once
#include "core.h"
#include <memory>

namespace gv {
struct FileHit {std::wstring name,path;bool directory=false;};
struct FileIndexStatus {
    bool enabled=false,paused=false,busy=false,ready=false,trigram=false;
    uint64_t entries=0,folders=0,skipped=0,bytes=0,elapsedMs=0,queryMs=0,scannedEntries=0,currentEntries=0;
    std::wstring current,error;
    bool fastRequested=false,fastConnected=false,fastCached=false;
    uint64_t fastEntries=0,mftReadMs=0,mftBuildMs=0,mftRecords=0,mftIndexed=0,mftCapacity=0,remainingMs=0,usnChanges=0;
    std::wstring provider,note,journalMissing;
};
// One low priority worker owns SQLite and directory notifications. Only bounded
// result pages and small progress snapshots cross into the UI thread.
class FileIndex {
public:
    explicit FileIndex(fs::path database,bool snapshotOnly=false);
    ~FileIndex();
    void Start(const Config::FileSearch& config,HWND notify=nullptr,UINT message=WM_APP+84);
    void Stop();
    void RequestStop();
    void Configure(const Config::FileSearch& config,bool rebuild=false);
    void Query(std::wstring query);
    std::vector<FileHit> Results(std::wstring_view query)const;
    FileIndexStatus Status()const;
    // Called on the STA UI thread. Only the metadata helper is elevated.
    bool EnableFast(HWND owner=nullptr,bool elevate=true,std::wstring_view createJournals={});
    static std::vector<Config::FileRoot> Roots(const Config::FileSearch& config);
    static std::wstring Key(std::wstring_view value);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
