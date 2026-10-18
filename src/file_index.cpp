#include "file_index.h"
#include "ntfs_index.h"
#include <winsqlite/winsqlite3.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <set>
#include <stdexcept>

namespace gv {
namespace {
struct Statement {
    sqlite3_stmt* value{};
    Statement(sqlite3* db,const char* sql){if(sqlite3_prepare_v2(db,sql,-1,&value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Statement(){sqlite3_finalize(value);}
    void Text(int index,std::wstring_view text){auto utf=Utf8(text);if(sqlite3_bind_text(value,index,utf.c_str(),static_cast<int>(utf.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("Cannot bind file index text");}
    void Number(int index,int64_t number){sqlite3_bind_int64(value,index,number);}
    bool Row(){int code=sqlite3_step(value);if(code!=SQLITE_ROW&&code!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(value)));return code==SQLITE_ROW;}
    int64_t Number(int column){return sqlite3_column_int64(value,column);}
    std::wstring Text(int column){auto text=sqlite3_column_text(value,column);return text?Wide(reinterpret_cast<const char*>(text)):L"";}
    void Reset(){sqlite3_reset(value);sqlite3_clear_bindings(value);}
};
void Execute(sqlite3* db,const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
std::wstring FullPath(std::wstring_view path){std::error_code error;auto result=fs::absolute(fs::path(path),error).lexically_normal().wstring();while(result.size()>3&&(result.back()==L'\\'||result.back()==L'/'))result.pop_back();return error?L"":result;}
bool Within(std::wstring_view path,std::wstring_view root){return path==root||(path.starts_with(root)&&(!root.empty()&&root.back()==L'\\'||path.size()>root.size()&&path[root.size()]==L'\\'));}
std::wstring Glob(std::wstring_view value){std::wstring result=L"*";for(wchar_t c:value){if(c==L'*')result+=L"[*]";else if(c==L'?')result+=L"[?]";else if(c==L'[')result+=L"[[]";else result+=c;}return result+L"*";}
}

std::wstring FileIndex::Key(std::wstring_view value){
    if(value.empty())return {};int length=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr,0);std::wstring result(length,L'\0');if(length)LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr,0);else result=value;return result;
}
std::vector<Config::FileRoot> FileIndex::Roots(const Config::FileSearch& config){
    auto roots=config.roots;if(config.allFixedDrives){wchar_t drives[512]{};GetLogicalDriveStringsW(512,drives);for(const wchar_t* p=drives;*p;p+=wcslen(p)+1)if(GetDriveTypeW(p)==DRIVE_FIXED)roots.push_back({p,-1});}
    std::vector<Config::FileRoot> result;
    for(auto root:roots){root.path=FullPath(root.path);if(root.path.empty())continue;root.depth=std::clamp(root.depth,-1,32);auto key=Key(root.path);bool covered=false;for(auto& existing:result)if(Key(existing.path)==key){existing.depth=existing.depth<0||root.depth<0?-1:std::max(existing.depth,root.depth);covered=true;break;}if(!covered)result.push_back(std::move(root));}
    auto all=result;std::erase_if(result,[&](const auto& root){auto key=Key(root.path);return std::any_of(all.begin(),all.end(),[&](const auto& parent){return parent.depth<0&&Key(parent.path)!=key&&Within(key,Key(parent.path));});});
    if(result.size()>58)result.resize(58);return result;
}

struct FileIndex::Impl {
    fs::path file;std::wstring profileKey;HWND notify{};UINT message{};HANDLE stop{},wake{};std::thread worker;
    mutable std::mutex mutex;Config::FileSearch requested;std::vector<Config::FileRoot> resolvedRoots;bool changed=false,rebuild=false;std::wstring pendingQuery,resultQuery;uint64_t requestVersion=0,doneVersion=0;std::vector<FileHit> results;FileIndexStatus status;
    sqlite3* db{};bool trigram=false;std::atomic<bool> stopping=false,finished=false;ULONGLONG started=0,published=0,queryUntil=0;uint64_t activeQueryVersion=0;bool queryRunning=false;std::wstring scanningKey;bool scanDirty=false,scanDirtyExpand=false;
    std::unique_ptr<NtfsIndex> ntfs;std::unique_ptr<NtfsConnection> incoming;std::wstring launchError,fastRoute,coveredDrives;uint64_t coveredEntries=0;bool snapshotOnly=false;
    struct Watch {
        std::wstring root;HANDLE directory=INVALID_HANDLE_VALUE,event{};OVERLAPPED overlap{};alignas(DWORD) BYTE buffer[65536]{};bool armed=false;
        ~Watch(){if(directory!=INVALID_HANDLE_VALUE){if(armed){CancelIoEx(directory,&overlap);DWORD transferred=0;GetOverlappedResult(directory,&overlap,&transferred,TRUE);}CloseHandle(directory);}if(event)CloseHandle(event);}
        bool Arm(){ResetEvent(event);overlap={};overlap.hEvent=event;armed=ReadDirectoryChangesW(directory,buffer,sizeof(buffer),TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME,nullptr,&overlap,nullptr)!=FALSE;return armed;}
    };
    std::vector<std::unique_ptr<Watch>> watches;
    explicit Impl(fs::path path,bool only):file(std::move(path)),profileKey(Key(FullPath(file.parent_path().wstring()))),snapshotOnly(only){}
    bool CoveredPath(std::wstring_view path)const{return path.size()>1&&path[1]==L':'&&coveredDrives.find(static_cast<wchar_t>(towupper(path[0])))!=std::wstring::npos;}
    void Publish(bool force=false){
        auto now=GetTickCount64();if(!force&&now-published<500)return;published=now;std::error_code error;uint64_t bytes=0;for(auto suffix:{L"",L"-wal",L"-shm"}){auto path=file.wstring()+suffix;auto size=fs::file_size(path,error);if(!error)bytes+=size;}
        auto fast=ntfs?ntfs->Progress():NtfsProgress{};
        if(db&&!stopping&&fast.drives!=coveredDrives){coveredDrives=fast.drives;uint64_t covered=0;for(auto drive:coveredDrives){auto begin=Key(std::wstring{drive,L':',L'\\'}),end=begin;++end.back();Statement count(db,"SELECT count(*) FROM entries e JOIN dirs d ON d.id=e.dir WHERE d.path>=? AND d.path<?");count.Text(1,begin);count.Text(2,end);if(count.Row())covered+=count.Number(0);}std::lock_guard lock(mutex);coveredEntries=covered;}
        {std::lock_guard lock(mutex);status.bytes=bytes;status.elapsedMs=now-started;status.fastRequested=requested.fastNtfs;status.fastConnected=fast.connected;status.fastCached=fast.cached;status.fastEntries=fast.entries;status.mftReadMs=fast.readMs;status.mftBuildMs=fast.buildMs;status.mftRecords=fast.records;status.mftIndexed=fast.indexed;status.mftCapacity=fast.capacity;status.remainingMs=fast.remainingMs;status.usnChanges=fast.changes;status.provider=fast.stage;status.note=fast.note;status.journalMissing=fast.journalMissing;if(fast.busy)status.busy=true;}if(notify)PostMessageW(notify,message,0,0);
    }
    void Open(){
        fs::create_directories(file.parent_path());auto path=Utf8(file.wstring());if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("Cannot open file index");
        sqlite3_busy_timeout(db,1000);Execute(db,"PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-4096; PRAGMA temp_store=FILE; PRAGMA mmap_size=0; PRAGMA wal_autocheckpoint=512; PRAGMA journal_size_limit=4194304; PRAGMA foreign_keys=ON;");
        Execute(db,"CREATE TABLE IF NOT EXISTS dirs(id INTEGER PRIMARY KEY,path TEXT NOT NULL UNIQUE,display TEXT NOT NULL,root TEXT NOT NULL,depth INTEGER NOT NULL); CREATE TABLE IF NOT EXISTS entries(id INTEGER PRIMARY KEY,dir INTEGER NOT NULL REFERENCES dirs(id) ON DELETE CASCADE,name TEXT NOT NULL,key TEXT NOT NULL,folder INTEGER NOT NULL,seen INTEGER NOT NULL,UNIQUE(dir,name)); CREATE INDEX IF NOT EXISTS entry_names ON entries(key); CREATE TABLE IF NOT EXISTS work(path TEXT PRIMARY KEY,display TEXT NOT NULL,root TEXT NOT NULL,depth INTEGER NOT NULL,maximum INTEGER NOT NULL,expand INTEGER NOT NULL DEFAULT 1,ready INTEGER NOT NULL DEFAULT 0); CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT); ");
        trigram=sqlite3_exec(db,"CREATE VIRTUAL TABLE IF NOT EXISTS names USING fts5(key,content='entries',content_rowid='id',tokenize='trigram',detail=none,columnsize=0);",nullptr,nullptr,nullptr)==SQLITE_OK;
        if(trigram){Execute(db,"CREATE TRIGGER IF NOT EXISTS entry_add AFTER INSERT ON entries BEGIN INSERT INTO names(rowid,key) VALUES(new.id,new.key); END; CREATE TRIGGER IF NOT EXISTS entry_delete AFTER DELETE ON entries BEGIN INSERT INTO names(names,rowid,key) VALUES('delete',old.id,old.key); END;");Statement exists(db,"SELECT value FROM meta WHERE key='trigram'");if(!exists.Row()){Execute(db,"INSERT INTO names(names) VALUES('rebuild'); INSERT OR REPLACE INTO meta VALUES('trigram','1');");}}
        {Statement count(db,"SELECT count(*) FROM entries");count.Row();std::lock_guard lock(mutex);status.entries=count.Number(0);status.trigram=trigram;status.ready=status.entries>0;}
        sqlite3_progress_handler(db,2000,[](void* context)->int{auto& self=*static_cast<Impl*>(context);if(self.stopping)return 1;if(!self.queryRunning)return 0;std::lock_guard lock(self.mutex);return self.requestVersion!=self.activeQueryVersion||GetTickCount64()>self.queryUntil;},this);
        ntfs=std::make_unique<NtfsIndex>(db,profileKey,snapshotOnly);ntfs->Open();
    }
    void Queue(const std::wstring& display,const std::wstring& root,int depth,int maximum,bool expand=true){
        auto key=Key(display);if(Within(key,profileKey))return;
        if(key==scanningKey){scanDirty=true;scanDirtyExpand|=expand;return;}
        Statement put(db,"INSERT OR IGNORE INTO work(path,display,root,depth,maximum,expand,ready) VALUES(?,?,?,?,?,?,?)");put.Text(1,key);put.Text(2,display);put.Text(3,root);put.Number(4,depth);put.Number(5,maximum);put.Number(6,expand);put.Number(7,expand?0:GetTickCount64()+750);put.Row();
        if(!sqlite3_changes(db)){Statement merge(db,"UPDATE work SET root=?,depth=?,maximum=?,expand=max(expand,?) WHERE path=? AND maximum>=0 AND (?<0 OR ?-?>maximum-depth)");merge.Text(1,root);merge.Number(2,depth);merge.Number(3,maximum);merge.Number(4,expand);merge.Text(5,key);merge.Number(6,maximum);merge.Number(7,maximum);merge.Number(8,depth);merge.Row();}
    }
    void DeleteTree(const std::wstring& key){
        Statement count(db,"SELECT count(*) FROM entries WHERE dir IN (SELECT id FROM dirs WHERE path=? OR path>=? AND path<?)");auto prefix=key+(key.ends_with(L"\\")?L"":L"\\");count.Text(1,key);count.Text(2,prefix);count.Text(3,prefix+L"\xffff");count.Row();auto removed=count.Number(0);
        Statement remove(db,"DELETE FROM dirs WHERE path=? OR path>=? AND path<?");remove.Text(1,key);remove.Text(2,prefix);remove.Text(3,prefix+L"\xffff");remove.Row();
        Statement work(db,"DELETE FROM work WHERE path=? OR path>=? AND path<?");work.Text(1,key);work.Text(2,prefix);work.Text(3,prefix+L"\xffff");work.Row();std::lock_guard lock(mutex);status.entries-=std::min<uint64_t>(status.entries,removed);if(CoveredPath(key))coveredEntries-=std::min<uint64_t>(coveredEntries,removed);
    }
    void Configure(const Config::FileSearch& config,bool clear){
        watches.clear();Execute(db,"BEGIN; DELETE FROM work;");
        if(clear){Execute(db,"DELETE FROM dirs;");std::lock_guard lock(mutex);status.entries=0;}
        auto roots=FileIndex::Roots(config);std::vector<std::wstring> obsolete;
        ntfs->Configure(config,roots,clear);fastRoute.clear();for(const auto& root:roots)if(ntfs->Covers(root))fastRoute+=root.path;
        {Statement dirs(db,"SELECT path FROM dirs");while(dirs.Row()){auto path=dirs.Text(0);if(std::none_of(roots.begin(),roots.end(),[&](const auto& root){auto key=Key(root.path);if(!Within(path,key))return false;if(root.depth<0)return true;auto relative=fs::path(path).lexically_relative(key);return (relative==L"."?0:std::distance(relative.begin(),relative.end()))<=root.depth;}))obsolete.push_back(path);}}
        if(config.enabled)for(const auto& path:obsolete)DeleteTree(path);
        for(const auto& root:roots){if(!config.enabled)break;
            auto watch=std::make_unique<Watch>();watch->root=root.path;watch->event=CreateEventW(nullptr,TRUE,FALSE,nullptr);watch->directory=CreateFileW(root.path.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);if(watch->event&&watch->directory!=INVALID_HANDLE_VALUE&&watch->Arm())watches.push_back(std::move(watch));
            if(!ntfs->Covers(root))Queue(root.path,Key(root.path),0,root.depth);
        }
        Execute(db,"COMMIT");started=GetTickCount64();{std::lock_guard lock(mutex);resolvedRoots=roots;status.enabled=config.enabled;status.paused=config.paused;status.busy=config.enabled&&!roots.empty();status.current.clear();status.error.clear();status.folders=status.skipped=status.scannedEntries=status.currentEntries=0;results.clear();resultQuery.clear();doneVersion=0;}
        Publish(true);
    }
    bool Interrupted(){std::lock_guard lock(mutex);return stopping||changed||requested.paused;}
    bool Scan(){
        std::wstring path,key,root;int depth=0,maximum=-1;bool expand=true;
        {Statement next(db,"SELECT path,display,root,depth,maximum,expand FROM work WHERE ready<=? LIMIT 1");next.Number(1,GetTickCount64());if(!next.Row())return false;key=next.Text(0);path=next.Text(1);root=next.Text(2);depth=static_cast<int>(next.Number(3));maximum=static_cast<int>(next.Number(4));expand=next.Number(5)!=0;}
        {std::lock_guard lock(mutex);status.current=path;status.currentEntries=0;status.busy=true;}
        auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){
            DWORD error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND||attributes!=INVALID_FILE_ATTRIBUTES)DeleteTree(key);else{Statement remove(db,"DELETE FROM work WHERE path=?");remove.Text(1,key);remove.Row();std::lock_guard lock(mutex);++status.skipped;}return true;
        }
        WIN32_FIND_DATAW item{};HANDLE listing=FindFirstFileExW((fs::path(path)/L"*").c_str(),FindExInfoBasic,&item,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
        if(listing==INVALID_HANDLE_VALUE&&GetLastError()!=ERROR_FILE_NOT_FOUND){Statement remove(db,"DELETE FROM work WHERE path=?");remove.Text(1,key);remove.Row();std::lock_guard lock(mutex);++status.skipped;return true;}
        scanningKey=key;scanDirty=scanDirtyExpand=false;struct ScanScope{Impl& owner;~ScanScope(){owner.scanningKey.clear();}}scope{*this};
        FILETIME stamp{};GetSystemTimePreciseAsFileTime(&stamp);const int64_t generation=(static_cast<int64_t>(stamp.dwHighDateTime)<<32)|stamp.dwLowDateTime;int64_t directory=0;bool complete=true;unsigned batch=0;Execute(db,"BEGIN");
        try{
            {Statement add(db,"INSERT OR IGNORE INTO dirs(path,display,root,depth) VALUES(?,?,?,?)");add.Text(1,key);add.Text(2,path);add.Text(3,root);add.Number(4,depth);add.Row();Statement get(db,"SELECT id FROM dirs WHERE path=?");get.Text(1,key);get.Row();directory=get.Number(0);}
            Statement add(db,"INSERT OR IGNORE INTO entries(dir,name,key,folder,seen) VALUES(?,?,?,?,?)"),seen(db,"UPDATE entries SET seen=?,folder=? WHERE dir=? AND name=?");
            if(listing!=INVALID_HANDLE_VALUE)do{
                if(wcscmp(item.cFileName,L".")==0||wcscmp(item.cFileName,L"..")==0)continue;
                bool folder=(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;std::wstring child;if(folder){child=(fs::path(path)/item.cFileName).wstring();if(Within(Key(child),profileKey))continue;}
                add.Number(1,directory);add.Text(2,item.cFileName);add.Text(3,Key(item.cFileName));add.Number(4,folder);add.Number(5,generation);add.Row();bool fresh=sqlite3_changes(db)!=0;{std::lock_guard lock(mutex);if(fresh){++status.entries;if(CoveredPath(key))++coveredEntries;}++status.scannedEntries;++status.currentEntries;}add.Reset();
                if(!fresh){seen.Number(1,generation);seen.Number(2,folder);seen.Number(3,directory);seen.Text(4,item.cFileName);seen.Row();seen.Reset();}
                if(folder&&(expand||fresh)&&!(item.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&(maximum<0||depth<maximum))Queue(child,root,depth+1,maximum);
                if(++batch%256==0){Execute(db,"COMMIT");Notifications();Publish();ProcessQuery(GetTickCount64()>queryUntil+1000);if(Interrupted()){complete=false;break;}Execute(db,"BEGIN");}
            }while(FindNextFileW(listing,&item));
            if(listing!=INVALID_HANDLE_VALUE){DWORD error=GetLastError();FindClose(listing);listing=INVALID_HANDLE_VALUE;if(complete&&error!=ERROR_NO_MORE_FILES)complete=false;}
            if(sqlite3_get_autocommit(db))Execute(db,"BEGIN");
            if(complete){
                std::vector<std::wstring> old;{Statement missing(db,"SELECT name FROM entries WHERE dir=? AND seen<>? AND folder=1");missing.Number(1,directory);missing.Number(2,generation);while(missing.Row())old.push_back(Key((fs::path(path)/missing.Text(0)).wstring()));}for(const auto& stale:old)DeleteTree(stale);
                Statement remove(db,"DELETE FROM entries WHERE dir=? AND seen<>?");remove.Number(1,directory);remove.Number(2,generation);remove.Row();{std::lock_guard lock(mutex);auto removed=static_cast<uint64_t>(sqlite3_changes(db));status.entries-=std::min(status.entries,removed);if(CoveredPath(key))coveredEntries-=std::min(coveredEntries,removed);++status.folders;}
                if(scanDirty){Statement again(db,"UPDATE work SET expand=?,ready=? WHERE path=?");again.Number(1,scanDirtyExpand);again.Number(2,GetTickCount64()+750);again.Text(3,key);again.Row();}
                else{Statement done(db,"DELETE FROM work WHERE path=?");done.Text(1,key);done.Row();}
            }
            Execute(db,"COMMIT");
        }catch(...){if(listing!=INVALID_HANDLE_VALUE)FindClose(listing);if(!sqlite3_get_autocommit(db))sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        Publish();return true;
    }
    void Notifications(){
        for(auto& watch:watches)if(WaitForSingleObject(watch->event,0)==WAIT_OBJECT_0){
            DWORD bytes=0;bool ok=GetOverlappedResult(watch->directory,&watch->overlap,&bytes,FALSE)!=FALSE;watch->armed=false;std::set<std::wstring> parents;
            if(ntfs->Observe(watch->root,{watch->buffer,bytes},!ok||!bytes)){watch->Arm();SetEvent(wake);continue;}
            if(ok&&bytes){DWORD offset=0;for(;;){if(offset+offsetof(FILE_NOTIFY_INFORMATION,FileName)>bytes)break;auto entry=reinterpret_cast<FILE_NOTIFY_INFORMATION*>(watch->buffer+offset);if(offset+offsetof(FILE_NOTIFY_INFORMATION,FileName)+entry->FileNameLength>bytes)break;std::wstring relative(entry->FileName,entry->FileNameLength/sizeof(wchar_t));parents.insert((fs::path(watch->root)/relative).parent_path().wstring());if(!entry->NextEntryOffset)break;offset+=entry->NextEntryOffset;}}
            if(parents.empty())parents.insert(watch->root);
            watch->Arm();Config::FileSearch config;{std::lock_guard lock(mutex);config=requested;}auto roots=FileIndex::Roots(config);
            for(const auto& parent:parents)for(const auto& root:roots){if(ntfs->Covers(root))continue;auto key=Key(root.path),path=Key(parent);if(!Within(path,key))continue;auto relative=fs::path(path).lexically_relative(key);int depth=relative==L"."?0:static_cast<int>(std::distance(relative.begin(),relative.end()));if(root.depth<0||depth<=root.depth)Queue(parent,key,depth,root.depth,bytes==0);}
        }
    }
    void ProcessQuery(bool refresh=false){
        std::wstring query;uint64_t version=0;bool enabled=false;{std::lock_guard lock(mutex);if(doneVersion==requestVersion&&!refresh)return;query=pendingQuery;version=requestVersion;enabled=requested.enabled;}
        std::vector<FileHit> found;auto began=GetTickCount64();queryRunning=true;activeQueryVersion=version;queryUntil=began+500;
        try{
            auto key=Key(query);if(enabled&&!key.empty()){
                bool infix=trigram&&key.size()>=3;std::string sql=infix?"SELECT e.name,d.display,e.folder FROM names n JOIN entries e ON e.id=n.rowid JOIN dirs d ON d.id=e.dir WHERE n.key GLOB ?":"SELECT e.name,d.display,e.folder FROM entries e JOIN dirs d ON d.id=e.dir WHERE e.key>=? AND e.key<?";
                auto fast=ntfs->Progress();if(!fast.drives.empty())sql+=" AND instr(?,upper(substr(d.root,1,1)))=0";sql+=" LIMIT 80";
                Statement search(db,sql.c_str());search.Text(1,infix?Glob(key):key);if(!infix)search.Text(2,key+L"\xffff");if(!fast.drives.empty())search.Text(infix?2:3,fast.drives);while(search.Row())found.push_back({search.Text(0),(fs::path(search.Text(1))/search.Text(0)).wstring(),search.Number(2)!=0});
                auto fastHits=ntfs->Search(key,[&]{if(stopping||GetTickCount64()>queryUntil)return true;std::lock_guard lock(mutex);return requestVersion!=version;});for(auto& hit:fastHits)found.push_back(std::move(hit));
            }
        }catch(...){if(!stopping){/* A superseded or time-bounded query keeps its bounded partial page. */}}
        queryRunning=false;std::stable_sort(found.begin(),found.end(),[&](const FileHit& a,const FileHit& b){auto x=Key(a.name),y=Key(b.name),key=Key(query);bool ax=x.starts_with(key),by=y.starts_with(key);if(ax!=by)return ax;return x.size()<y.size();});
        if(found.size()>80)found.resize(80);{std::lock_guard lock(mutex);if(version!=requestVersion)return;results=std::move(found);resultQuery=Key(query);doneVersion=version;status.queryMs=GetTickCount64()-began;}Publish(true);
    }
    void Loop(){
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);started=GetTickCount64();
        try{
            Open();for(;;){if(stopping)break;Config::FileSearch config;bool change=false,clear=false;{std::lock_guard lock(mutex);config=requested;change=changed;clear=rebuild;changed=rebuild=false;}
                if(change)Configure(config,clear);
                {std::unique_ptr<NtfsConnection> connection;std::wstring error;{std::lock_guard lock(mutex);connection=std::move(incoming);error=std::move(launchError);launchError.clear();}if(connection)ntfs->Connect(std::move(connection));if(!error.empty())ntfs->LaunchError(std::move(error));}
                bool fastWork=ntfs->Step();auto fast=ntfs->Progress();std::wstring route;for(const auto& root:resolvedRoots)if(ntfs->Covers(root))route+=root.path;
                if(route!=fastRoute){fastRoute=route;for(const auto& root:resolvedRoots){if(ntfs->Covers(root)){Statement remove(db,"DELETE FROM work WHERE root=?");remove.Text(1,Key(root.path));remove.Row();}else Queue(root.path,Key(root.path),0,root.depth);}}
                ProcessQuery(GetTickCount64()>queryUntil+1000);Notifications();
                bool scanning=config.enabled&&!config.paused&&Scan();
                if(scanning||fastWork){Publish();std::vector<HANDLE> waits{stop,wake};if(auto event=ntfs->Event())waits.push_back(event);DWORD delay=scanning?2:fast.indexed&&fast.busy?1:100;if(WaitForMultipleObjects(static_cast<DWORD>(waits.size()),waits.data(),FALSE,delay)==WAIT_OBJECT_0)break;continue;}
                bool wasBusy=false;{std::lock_guard lock(mutex);wasBusy=status.busy;status.busy=false;status.paused=config.paused;status.ready=true;status.current.clear();}if(wasBusy){bool saving=config.fastNtfs&&fast.cached;if(saving){ntfs->Finalizing(true);Publish(true);}sqlite3_wal_checkpoint_v2(db,nullptr,SQLITE_CHECKPOINT_PASSIVE,nullptr,nullptr);if(saving){ntfs->Finalizing(false);std::lock_guard lock(mutex);status.busy=false;}ProcessQuery(true);Publish(true);}
                DWORD timeout=config.enabled&&config.fastNtfs&&!config.paused?2000:INFINITE;if(config.enabled&&!config.paused){Statement due(db,"SELECT min(ready) FROM work");if(due.Row()&&sqlite3_column_type(due.value,0)!=SQLITE_NULL)timeout=static_cast<DWORD>(std::clamp<int64_t>(due.Number(0)-GetTickCount64(),1,750));}Publish();
                std::vector<HANDLE> waits{stop,wake};for(auto& watch:watches)if(watch->armed)waits.push_back(watch->event);DWORD event=WaitForMultipleObjects(static_cast<DWORD>(waits.size()),waits.data(),FALSE,timeout);if(event==WAIT_OBJECT_0||event==WAIT_FAILED)break;
            }
        }catch(const std::exception& error){std::lock_guard lock(mutex);if(!stopping)status.error=Wide(error.what());status.busy=false;}
        watches.clear();if(!stopping)Publish(true);ntfs.reset();if(db){sqlite3_progress_handler(db,0,nullptr,nullptr);sqlite3_close(db);db=nullptr;}finished=true;if(notify)PostMessageW(notify,message,0,0);SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);
    }
};

FileIndex::FileIndex(fs::path database,bool snapshotOnly):impl_(std::make_unique<Impl>(std::move(database),snapshotOnly)){}
FileIndex::~FileIndex(){Stop();}
void FileIndex::Start(const Config::FileSearch& config,HWND notify,UINT message){
    if(impl_->worker.joinable()&&!impl_->finished){Configure(config);return;}if(impl_->worker.joinable())Stop();impl_->stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);impl_->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!impl_->stop||!impl_->wake){Stop();return;}impl_->notify=notify;impl_->message=message;impl_->stopping=false;impl_->finished=false;{std::lock_guard lock(impl_->mutex);impl_->requested=config;impl_->changed=true;}impl_->worker=std::thread([this]{impl_->Loop();});
}
void FileIndex::RequestStop(){impl_->stopping=true;if(impl_->stop)SetEvent(impl_->stop);if(impl_->wake)SetEvent(impl_->wake);}
void FileIndex::Stop(){RequestStop();if(impl_->worker.joinable())impl_->worker.join();if(impl_->stop)CloseHandle(impl_->stop);if(impl_->wake)CloseHandle(impl_->wake);impl_->stop=impl_->wake=nullptr;}
void FileIndex::Configure(const Config::FileSearch& config,bool rebuild){{std::lock_guard lock(impl_->mutex);bool rootsChanged=impl_->requested.enabled!=config.enabled||impl_->requested.fastNtfs!=config.fastNtfs||impl_->requested.allFixedDrives!=config.allFixedDrives||impl_->requested.roots.size()!=config.roots.size();if(!rootsChanged)for(size_t i=0;i<config.roots.size();++i)rootsChanged|=config.roots[i].path!=impl_->requested.roots[i].path||config.roots[i].depth!=impl_->requested.roots[i].depth;if(config.allFixedDrives){auto roots=Roots(config);rootsChanged|=roots.size()!=impl_->resolvedRoots.size();if(!rootsChanged)for(size_t i=0;i<roots.size();++i)rootsChanged|=roots[i].path!=impl_->resolvedRoots[i].path||roots[i].depth!=impl_->resolvedRoots[i].depth;}impl_->requested=config;impl_->changed|=rootsChanged||rebuild;impl_->rebuild|=rebuild;impl_->status.paused=config.paused;if((rootsChanged||rebuild)&&config.enabled&&!config.paused)impl_->status.busy=true;}if(impl_->wake)SetEvent(impl_->wake);}
void FileIndex::Query(std::wstring query){{std::lock_guard lock(impl_->mutex);if(impl_->pendingQuery==query)return;impl_->pendingQuery=std::move(query);++impl_->requestVersion;}if(impl_->wake)SetEvent(impl_->wake);}
std::vector<FileHit> FileIndex::Results(std::wstring_view query)const{std::lock_guard lock(impl_->mutex);return impl_->resultQuery==Key(query)?impl_->results:std::vector<FileHit>{};}
FileIndexStatus FileIndex::Status()const{std::lock_guard lock(impl_->mutex);auto status=impl_->status;status.entries-=std::min(status.entries,impl_->coveredEntries);status.entries+=status.fastEntries;return status;}
bool FileIndex::EnableFast(HWND owner,bool elevate,std::wstring_view createJournals){std::wstring error;auto connection=NtfsConnection::Launch(owner,error,elevate,createJournals);bool success=connection!=nullptr;{std::lock_guard lock(impl_->mutex);impl_->incoming=std::move(connection);impl_->launchError=std::move(error);impl_->changed=true;}if(impl_->wake)SetEvent(impl_->wake);return success;}
}
