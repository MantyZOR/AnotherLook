#include "ntfs_index.h"
#include <winsqlite/winsqlite3.h>
#include <winioctl.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace gv {
namespace {
std::wstring Letter(wchar_t c){return std::wstring(1,c);}
wchar_t FastDrive(const Config::FileRoot& root,bool validate=true){if(root.depth>=0||root.path.size()!=3||root.path[1]!=L':'||root.path[2]!=L'\\')return 0;wchar_t drive=static_cast<wchar_t>(towupper(root.path[0]));return !validate||ntfs::ValidDrive(drive)?drive:0;}
}

struct NtfsIndex::Impl {
    NtfsStore store;std::unique_ptr<NtfsConnection> connection;std::wstring wanted,failed,note,journalMissing,checked,refresh;bool available=false,enabled=false,paused=false,waiting=false,building=false,catchup=false,indexDone=false,snapshotOnly=false,finalizing=false;
    wchar_t current=0;size_t roundRobin=0;uint64_t generation=0,rootId=0,baseline=0,journal=0,started=0,buildStarted=0,readMs=0,buildMs=0,records=0,indexed=0,total=0,capacity=0,changes=0,nextPoll=0,refreshAfter=0;ntfs::Operation operation=ntfs::Operation::Snapshot;
    std::vector<uint8_t> payload;
    std::unordered_map<wchar_t,uint64_t> positions;uint64_t cursorSaved=GetTickCount64();bool requestHadNames=false;
    explicit Impl(sqlite3* db,std::wstring profile,bool only):store(db,std::move(profile)),snapshotOnly(only){}
    void Fail(std::wstring message){if(available)store.AbandonStaging();note=std::move(message);if(!connection)failed=wanted;else if(current&&failed.find(current)==std::wstring::npos)failed+=current;current=0;waiting=building=catchup=false;}
    bool Send(wchar_t drive,ntfs::Operation op,uint64_t id,uint64_t next){ntfs::Request request;request.operation=static_cast<uint32_t>(op==ntfs::Operation::Snapshot&&snapshotOnly?ntfs::Operation::SnapshotWithoutJournal:op);request.drive=drive;request.journal=id;request.next=op==ntfs::Operation::Changes&&positions.contains(drive)?std::max(next,positions.at(drive)):next;std::wstring error;if(!connection->Send(request,error)){connection.reset();Fail(error);return false;}current=drive;operation=op;waiting=true;requestHadNames=false;return true;}
    bool SameVolume(wchar_t drive,const NtfsCachedVolume& cached){wchar_t root[]{drive,L':',L'\\',0},guid[64]{};return GetVolumeNameForVolumeMountPointW(root,guid,64)&&_wcsicmp(guid,cached.identity.guid)==0;}
    void FlushPositions(){for(auto [drive,next]:positions)store.Cursor(drive,next);cursorSaved=GetTickCount64();}
    void Activate(uint64_t next){buildMs=GetTickCount64()-started;store.Activate(current,generation,journal,next,readMs,buildMs);if(checked.find(current)==std::wstring::npos)checked+=current;if(journal){store.DropPending(current);positions[current]=next;}current=0;building=catchup=false;nextPoll=GetTickCount64()+2000;note.clear();}
};
NtfsIndex::NtfsIndex(sqlite3* database,std::wstring profile,bool snapshotOnly):impl_(std::make_unique<Impl>(database,std::move(profile),snapshotOnly)){}
NtfsIndex::~NtfsIndex(){try{impl_->FlushPositions();}catch(...){/* Namespace batches already have durable cursors; idle progress is best effort. */}}
HANDLE NtfsIndex::Event()const{return impl_->connection?impl_->connection->Event():nullptr;}
void NtfsIndex::Finalizing(bool value){impl_->finalizing=value;}
void NtfsIndex::Open(){try{impl_->store.Open();impl_->available=true;}catch(const std::exception& error){impl_->note=L"MFT недоступен: "+Wide(error.what());}}
void NtfsIndex::Connect(std::unique_ptr<NtfsConnection> connection){auto& s=*impl_;s.FlushPositions();s.positions.clear();if(s.waiting||s.building)s.store.AbandonStaging();s.connection=std::move(connection);s.current=0;s.generation=s.journal=0;s.waiting=s.building=s.catchup=false;s.checked.clear();s.failed.clear();s.journalMissing.clear();s.note.clear();s.nextPoll=0;}
void NtfsIndex::LaunchError(std::wstring error){impl_->note=std::move(error);if(!impl_->connection)impl_->failed=impl_->wanted;}
void NtfsIndex::Configure(const Config::FileSearch& config,const std::vector<Config::FileRoot>& roots,bool rebuild){
    auto& s=*impl_;std::wstring wanted;if(s.available&&config.enabled&&config.fastNtfs)for(const auto& root:roots)if(auto drive=FastDrive(root);drive&&wanted.find(drive)==std::wstring::npos)wanted+=drive;
    if(!config.enabled||!config.fastNtfs||rebuild){s.connection.reset();if(s.waiting||s.building)s.store.AbandonStaging();s.waiting=s.building=s.catchup=false;s.current=0;}
    if(!config.enabled||!config.fastNtfs){s.journalMissing.clear();s.note.clear();}
    if(wanted!=s.wanted||rebuild){s.failed.clear();s.checked.clear();s.nextPoll=0;}
    s.wanted=wanted;s.enabled=config.enabled&&config.fastNtfs;s.paused=config.paused;
    for(auto drive:wanted){auto cached=s.store.Cached(drive);if(rebuild||cached&&!s.SameVolume(drive,*cached))s.store.Forget(drive);}
}
bool NtfsIndex::Covers(const Config::FileRoot& root,bool completeOnly){auto drive=FastDrive(root,false);auto& s=*impl_;if(!drive||s.wanted.find(drive)==std::wstring::npos||s.failed.find(drive)!=std::wstring::npos)return false;bool cached=s.store.Cached(drive).has_value();return completeOnly?cached:(cached||s.connection&&s.connection->Connected());}
bool NtfsIndex::Observe(const std::wstring& root,std::span<const uint8_t> notifications,bool overflow){
    auto& s=*impl_;wchar_t drive=FastDrive({root,-1},false);if(!drive||!Covers({root,-1}))return false;
    auto cached=s.store.Cached(drive);bool hasJournal=s.current==drive?s.journal!=0:cached&&cached->identity.journal!=0;if(hasJournal)return true;
    if(overflow||!s.store.QueueChanges(root,notifications)){if(s.refresh.find(drive)==std::wstring::npos)s.refresh+=drive;s.refreshAfter=GetTickCount64()+2000;s.store.DropPending(drive);}return true;
}
bool NtfsIndex::Step(){
    auto& s=*impl_;if(!s.enabled||!s.available||s.paused)return false;
    if(!s.connection){if(!s.paused)s.store.Cleanup();return false;}
    ntfs::Packet type{};std::wstring error;int received=s.connection->Poll(type,s.payload,error);
    if(received<0){s.connection.reset();s.Fail(std::move(error));return false;}
    if(received){
        if(!s.waiting){s.connection.reset();s.Fail(L"Неожиданный пакет MFT");return false;}
        if(type==ntfs::Packet::Volume){
            if(s.payload.size()!=sizeof(ntfs::Volume)){s.connection.reset();s.Fail(L"Неверная идентификация тома MFT");return false;}
            ntfs::Volume info{};std::memcpy(&info,s.payload.data(),sizeof(info));if(info.guid[63]!=0||!info.root){s.connection.reset();s.Fail(L"Неверные метаданные NTFS");return false;}
            wchar_t root[]{s.current,L':',L'\\',0},guid[64]{};if(!GetVolumeNameForVolumeMountPointW(root,guid,64)||_wcsicmp(guid,info.guid)){s.connection.reset();s.Fail(L"Том сменился во время чтения MFT");return false;}
            if(s.operation==ntfs::Operation::Snapshot){s.generation=s.store.Begin(s.current,info);s.rootId=info.root;s.journal=info.journal;s.baseline=info.next;s.capacity=info.capacity;if(!info.journal&&s.journalMissing.find(s.current)==std::wstring::npos)s.journalMissing+=s.current;}
        }else if(type==ntfs::Packet::Records){
            std::vector<ntfs::Record> records;uint64_t next=0;if(!s.generation||!ntfs::ParseRecords(s.payload,records,next,error)){s.connection.reset();s.Fail(error.empty()?L"Нет поколения MFT":error);return false;}
            s.store.Apply(s.current,s.generation,records,s.operation==ntfs::Operation::Changes);if(s.operation==ntfs::Operation::Snapshot)s.records+=records.size();else{s.changes+=records.size();s.requestHadNames|=!records.empty();}
        }else if(type==ntfs::Packet::Done){
            if(s.payload.size()!=sizeof(ntfs::Done)){s.connection.reset();s.Fail(L"Неверное завершение пакета MFT");return false;}
            ntfs::Done done{};std::memcpy(&done,s.payload.data(),sizeof(done));s.waiting=false;if(s.operation==ntfs::Operation::Snapshot||s.catchup)s.readMs+=done.readMs;
            if(done.error){
                if(done.error==ERROR_JOURNAL_ENTRY_DELETED||done.error==ERROR_JOURNAL_DELETE_IN_PROGRESS){s.positions.erase(s.current);s.store.ResetJournal(s.current);s.current=0;s.generation=0;s.catchup=false;s.note=L"Журнал USN сменился. Создаётся новый снимок MFT.";}
                else if(done.error==ERROR_JOURNAL_NOT_ACTIVE){if(s.journalMissing.find(s.current)==std::wstring::npos)s.journalMissing+=s.current;s.Fail(L"На "+Letter(s.current)+L": выключен USN. Подготовка NTFS включит журнал с целевым размером 64 МиБ. Пока используется обычный обход.");}
                else s.Fail(L"Том "+Letter(s.current)+L": ошибка "+std::to_wstring(done.error)+L". Используется обычный обход.");
            }else if(s.operation==ntfs::Operation::Snapshot){s.building=true;s.indexDone=false;s.buildStarted=GetTickCount64();s.indexed=0;s.total=s.store.Count(s.current,s.generation);}
            else if(s.catchup)s.Activate(done.next);
            else{s.positions[s.current]=done.next;if(s.requestHadNames)s.store.Cursor(s.current,done.next);if(GetTickCount64()-s.cursorSaved>=60000)s.FlushPositions();s.current=0;s.nextPoll=GetTickCount64()+2000;}
        }
    }
    // A pause retains at most one already received packet. The helper blocks on
    // the bounded pipe instead of allocating an unbounded record queue.
    if(s.building){
        if(!s.indexDone)s.indexDone=!s.store.Build(s.current,s.generation,s.indexed);if(s.indexDone){if(!s.journal){if(s.store.ApplyPending(s.current,s.generation,s.rootId))return true;s.Activate(0);}else{s.buildMs=GetTickCount64()-s.started;s.building=false;s.catchup=true;s.Send(s.current,ntfs::Operation::Changes,s.journal,s.baseline);}}return true;
    }
    if(s.waiting)return true;
    if(s.connection->Connected()&&GetTickCount64()>=s.nextPoll){
        // Missing volumes are built before routine journal polling.
        wchar_t drive=0;for(auto d:s.wanted)if(s.failed.find(d)==std::wstring::npos){auto cached=s.store.Cached(d);if(!cached||!cached->identity.journal&&s.checked.find(d)==std::wstring::npos||s.refresh.find(d)!=std::wstring::npos&&GetTickCount64()>=s.refreshAfter){drive=d;break;}}
        if(drive){std::erase(s.refresh,drive);s.positions.erase(drive);s.records=s.indexed=s.readMs=s.buildMs=s.capacity=0;s.generation=s.journal=0;s.started=GetTickCount64();s.Send(drive,ntfs::Operation::Snapshot,0,0);return true;}
        // Round robin across drives; every response is a durable cursor commit.
        for(size_t i=0;i<s.wanted.size();++i){auto d=s.wanted[s.roundRobin++%s.wanted.size()];if(s.failed.find(d)!=std::wstring::npos)continue;auto cached=s.store.Cached(d);if(!cached||!cached->identity.journal)continue;s.generation=cached->generation;s.journal=cached->identity.journal;s.Send(d,ntfs::Operation::Changes,s.journal,cached->next);return true;}
        s.nextPoll=GetTickCount64()+2000;
    }
    if(!s.waiting){for(auto d:s.wanted)if(auto cached=s.store.Cached(d);cached&&!cached->identity.journal&&s.store.ApplyPending(d,cached->generation,cached->identity.root))return true;s.store.Cleanup();}return s.waiting||s.building||!s.connection->Connected();
}
std::vector<FileHit> NtfsIndex::Search(std::wstring_view key,const std::function<bool()>& cancelled){auto drives=impl_->wanted;std::erase_if(drives,[&](wchar_t d){return impl_->failed.find(d)!=std::wstring::npos;});return impl_->available&&impl_->enabled?impl_->store.Search(key,drives,cancelled):std::vector<FileHit>{};}
NtfsProgress NtfsIndex::Progress(){
    auto& s=*impl_;NtfsProgress p;p.connected=s.connection&&s.connection->Connected();p.busy=s.enabled&&!s.paused&&(s.building||s.catchup||s.waiting&&s.operation==ntfs::Operation::Snapshot);p.readMs=s.readMs;p.buildMs=s.buildMs;p.records=s.records;p.indexed=s.indexed;p.capacity=s.capacity;p.changes=s.changes;p.note=s.note;p.journalMissing=s.journalMissing;
    for(auto drive:s.wanted){if(s.failed.find(drive)!=std::wstring::npos)continue;if(auto cached=s.store.Cached(drive)){p.entries+=cached->count;p.cached=true;p.drives+=drive;if(!p.buildMs){p.buildMs=cached->buildMs;p.readMs=cached->readMs;}}}
    if(s.building){p.stage=L"Поисковый индекс "+Letter(s.current)+L":";if(s.indexed>1024&&s.indexed<s.total)p.remainingMs=(s.total-s.indexed)*(GetTickCount64()-s.buildStarted)/s.indexed;}
    else if(s.waiting)p.stage=(s.operation==ntfs::Operation::Snapshot?L"Чтение MFT ":L"Изменения USN ")+Letter(s.current)+L":";
    else if(!s.journalMissing.empty()&&p.cached)p.stage=L"MFT готов. Изменения по уведомлениям Windows";
    else if(p.connected&&p.cached)p.stage=L"MFT готов. Обновление по USN";
    else if(p.connected)p.stage=L"MFT подключён. Выбери полный диск NTFS без ограничения глубины";
    else if(p.cached)p.stage=L"Кэш MFT. Для обновления подключи помощник";
    else p.stage=L"Обычный обход папок";
    if(s.finalizing){p.busy=true;p.stage=L"Сохранение готового индекса";p.remainingMs=0;}
    return p;
}
}
