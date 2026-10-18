#include "ntfs_index.h"
#include <winsqlite/winsqlite3.h>
#include <winioctl.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace gv {
namespace {
struct Sql {
    sqlite3_stmt* s{};
    Sql(sqlite3* db,const std::string& sql){if(sqlite3_prepare_v2(db,sql.c_str(),-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Sql(){sqlite3_finalize(s);}
    void Text(int i,std::wstring_view value){auto utf=Utf8(value);if(sqlite3_bind_text(s,i,utf.data(),static_cast<int>(utf.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("NTFS text binding failed");}
    void Int(int i,uint64_t n){sqlite3_bind_int64(s,i,static_cast<int64_t>(n));}
    uint64_t Int(int i){return static_cast<uint64_t>(sqlite3_column_int64(s,i));}
    std::wstring Text(int i){const auto* p=sqlite3_column_text(s,i);return p?Wide(reinterpret_cast<const char*>(p)):L"";}
    bool Row(){int result=sqlite3_step(s);if(result!=SQLITE_ROW&&result!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));return result==SQLITE_ROW;}
    void Reset(){sqlite3_reset(s);sqlite3_clear_bindings(s);}
};
void Exec(sqlite3* db,const std::string& sql){if(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
std::string Nodes(uint64_t generation){return "fast_nodes_"+std::to_string(generation);}
std::string Names(uint64_t generation){return "fast_names_"+std::to_string(generation);}
std::wstring Letter(wchar_t c){return std::wstring(1,c);}
constexpr uint64_t OrdinalMask=0x0000ffffffffffffULL;
bool Within(std::wstring_view path,std::wstring_view root){return path==root||path.starts_with(root)&&((!root.empty()&&root.back()==L'\\')||(path.size()>root.size()&&path[root.size()]==L'\\'));}
std::wstring Glob(std::wstring_view key){std::wstring result=L"*";for(auto c:key){if(c==L'*')result+=L"[*]";else if(c==L'?')result+=L"[?]";else if(c==L'[')result+=L"[[]";else result+=c;}return result+L"*";}
bool Identity(const fs::path& path,uint64_t& id,DWORD& attributes,DWORD& error){
    HANDLE handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){error=GetLastError();return false;}BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(handle,&info)!=FALSE;error=ok?0:GetLastError();CloseHandle(handle);if(ok){id=(static_cast<uint64_t>(info.nFileIndexHigh)<<32)|info.nFileIndexLow;attributes=info.dwFileAttributes;}return ok;
}
}
struct NtfsStore::Impl {
    sqlite3* db;std::wstring excluded;bool trigram=false,cleanup=true;
    std::unordered_map<uint64_t,uint64_t> buildCursor;
    Impl(sqlite3* value,std::wstring profile):db(value),excluded(FileIndex::Key(profile)){}
    void Adjust(wchar_t drive,uint64_t generation,uint64_t added,uint64_t removed){
        Sql gen(db,"UPDATE fast_generations SET count=max(0,count+?-?) WHERE id=?");gen.Int(1,added);gen.Int(2,removed);gen.Int(3,generation);gen.Row();
        Sql volume(db,"UPDATE fast_volumes SET count=max(0,count+?-?) WHERE drive=? AND active=?");volume.Int(1,added);volume.Int(2,removed);volume.Text(3,Letter(drive));volume.Int(4,generation);volume.Row();
    }
    std::wstring Path(const NtfsCachedVolume& volume,uint64_t id,Sql& get,std::unordered_map<uint64_t,std::wstring>& cache,size_t& cacheBytes){
        uint64_t node=id;std::vector<std::pair<uint64_t,std::wstring>> parts;size_t chars=3;std::wstring path;
        for(size_t depth=0;depth<1024;++depth){
            if(node==volume.identity.root){path=std::wstring{volume.drive,L':',L'\\'};break;}if(auto found=cache.find(node);found!=cache.end()){path=found->second;break;}
            get.Int(1,node&OrdinalMask);bool exists=get.Row();auto sequence=exists?get.Int(0):0,parent=exists?get.Int(1):0;auto name=exists?get.Text(2):L"";get.Reset();
            if(!exists||sequence!=(node>>48)||parent==node||name.empty()||name==L"."||name==L".."||chars+name.size()+1>=32760)return {};chars+=name.size()+1;parts.emplace_back(node,std::move(name));node=parent;
        }
        if(path.empty())return {};
        for(auto it=parts.rbegin();it!=parts.rend();++it){if(path.size()+it->second.size()+1>=32760)return {};if(path.back()!=L'\\')path+=L'\\';path+=it->second;if(Within(FileIndex::Key(path),excluded))return {};
            if(cache.size()<256&&cacheBytes+path.size()*2<1024*1024&&!cache.contains(it->first)){cacheBytes+=path.size()*2;cache.emplace(it->first,path);}}
        return Within(FileIndex::Key(path),excluded)?std::wstring{}:path;
    }
};
NtfsStore::NtfsStore(sqlite3* database,std::wstring excludedProfile):impl_(std::make_unique<Impl>(database,std::move(excludedProfile))){}
NtfsStore::~NtfsStore()=default;
void NtfsStore::Open(){
    auto db=impl_->db;
    Exec(db,"CREATE TABLE IF NOT EXISTS fast_volumes(drive TEXT PRIMARY KEY,guid TEXT NOT NULL,serial INTEGER NOT NULL,root INTEGER NOT NULL,journal INTEGER NOT NULL,next INTEGER NOT NULL DEFAULT 0,active INTEGER NOT NULL DEFAULT 0,staging INTEGER NOT NULL DEFAULT 0,count INTEGER NOT NULL DEFAULT 0,read_ms INTEGER NOT NULL DEFAULT 0,build_ms INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS fast_generations(id INTEGER PRIMARY KEY AUTOINCREMENT,drive TEXT NOT NULL,count INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS fast_pending(drive TEXT NOT NULL,key TEXT NOT NULL,path TEXT NOT NULL,PRIMARY KEY(drive,key));UPDATE fast_volumes SET staging=0;");
    impl_->trigram=sqlite3_exec(db,"CREATE VIRTUAL TABLE temp.fast_fts_probe USING fts5(key,tokenize='trigram');DROP TABLE temp.fast_fts_probe;",nullptr,nullptr,nullptr)==SQLITE_OK;
    if(!impl_->trigram)throw std::runtime_error("This Windows SQLite runtime does not support NTFS trigram search");
}
bool NtfsStore::Trigram()const{return impl_->trigram;}
std::optional<NtfsCachedVolume> NtfsStore::Cached(wchar_t drive){
    Sql s(impl_->db,"SELECT guid,serial,root,journal,next,active,count,read_ms,build_ms FROM fast_volumes WHERE drive=? AND active<>0");s.Text(1,Letter(drive));if(!s.Row())return {};
    NtfsCachedVolume v;v.drive=drive;auto guid=s.Text(0);wcsncpy_s(v.identity.guid,guid.c_str(),_TRUNCATE);v.identity.serial=s.Int(1);v.identity.root=s.Int(2);v.identity.journal=s.Int(3);v.next=s.Int(4);v.generation=s.Int(5);v.count=s.Int(6);v.readMs=s.Int(7);v.buildMs=s.Int(8);return v;
}
uint64_t NtfsStore::Begin(wchar_t drive,const ntfs::Volume& volume){
    auto db=impl_->db;impl_->cleanup=true;uint64_t generation=0;
    Exec(db,"PRAGMA wal_autocheckpoint=0;");
    {Sql s(db,"INSERT INTO fast_generations(drive) VALUES(?)");s.Text(1,Letter(drive));s.Row();generation=sqlite3_last_insert_rowid(db);}
    // Ordinal MFT IDs arrive in disk order. The sequence stays in its own column
    // so ID reuse is validated, without a random secondary-index write per file.
    Exec(db,"CREATE TABLE "+Nodes(generation)+"(id INTEGER PRIMARY KEY,seq INTEGER NOT NULL,parent INTEGER NOT NULL,name TEXT NOT NULL,key TEXT NOT NULL,flags INTEGER NOT NULL,indexed INTEGER NOT NULL DEFAULT 0)");
    Sql s(db,"INSERT INTO fast_volumes(drive,guid,serial,root,journal,staging) VALUES(?,?,?,?,?,?) ON CONFLICT(drive) DO UPDATE SET guid=excluded.guid,serial=excluded.serial,root=excluded.root,staging=excluded.staging");s.Text(1,Letter(drive));s.Text(2,volume.guid);s.Int(3,volume.serial);s.Int(4,volume.root);s.Int(5,volume.journal);s.Int(6,generation);s.Row();return generation;
}
uint64_t NtfsStore::Apply(wchar_t drive,uint64_t generation,std::span<const ntfs::Record> records,bool changes){
    auto db=impl_->db;auto table=Nodes(generation);uint64_t added=0,removed=0;Exec(db,"BEGIN");
    try{
        Sql insert(db,"INSERT OR IGNORE INTO "+table+"(id,seq,parent,name,key,flags,indexed) VALUES(?,?,?,?,?,?,?)"),update(db,"UPDATE "+table+" SET seq=?,parent=?,name=?,key=?,flags=?,indexed=max(indexed,?) WHERE id=?"),erase(db,"DELETE FROM "+table+" WHERE id=? AND seq=?");
        for(const auto& record:records){
            auto ordinal=record.id&OrdinalMask;if(ordinal<16||record.name==L"."||record.name==L"..")continue;
            if(changes&&(record.reason&USN_REASON_FILE_DELETE)){erase.Int(1,ordinal);erase.Int(2,record.id>>48);erase.Row();removed+=sqlite3_changes(db);erase.Reset();continue;}
            if(changes&&(record.reason&USN_REASON_RENAME_OLD_NAME)&&!(record.reason&USN_REASON_RENAME_NEW_NAME))continue;
            auto key=FileIndex::Key(record.name);insert.Int(1,ordinal);insert.Int(2,record.id>>48);insert.Int(3,record.parent);insert.Text(4,record.name);insert.Text(5,key);insert.Int(6,record.attributes);insert.Int(7,changes?1:0);insert.Row();bool fresh=sqlite3_changes(db)!=0;insert.Reset();
            if(fresh)++added;else{update.Int(1,record.id>>48);update.Int(2,record.parent);update.Text(3,record.name);update.Text(4,key);update.Int(5,record.attributes);update.Int(6,changes?1:0);update.Int(7,ordinal);update.Row();update.Reset();}
        }impl_->Adjust(drive,generation,added,removed);Exec(db,"COMMIT");return added;
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
bool NtfsStore::Build(wchar_t,uint64_t generation,uint64_t& indexed){
    auto db=impl_->db;auto table=Nodes(generation),names=Names(generation);auto& cursors=impl_->buildCursor;
    if(!cursors.contains(generation)){
        // Sort once on disk after the sequential import. Maintaining these
        // structures per incoming name thrashes a small cache on large drives.
        Exec(db,"CREATE INDEX "+table+"_keys ON "+table+"(key);CREATE VIRTUAL TABLE "+names+" USING fts5(key,content='"+table+"',content_rowid='id',tokenize='trigram',detail=none,columnsize=0);");
        cursors.emplace(generation,0);
    }
    if(cursors.at(generation)==UINT64_MAX)return false;
    uint64_t last=0,count=0;{Sql s(db,"SELECT max(id),count(*) FROM(SELECT id FROM "+table+" WHERE id>? ORDER BY id LIMIT 4096)");s.Int(1,cursors.at(generation));s.Row();last=s.Int(0);count=s.Int(1);}
    if(!count){
        // All existing names are indexed now. Live changes can use triggers;
        // the bulk path avoids rewriting every node merely to fire a trigger.
        Exec(db,"CREATE TRIGGER "+table+"_add AFTER INSERT ON "+table+" BEGIN INSERT INTO "+names+"(rowid,key) VALUES(new.id,new.key);END;"
            "CREATE TRIGGER "+table+"_delete AFTER DELETE ON "+table+" BEGIN INSERT INTO "+names+"("+names+",rowid,key) VALUES('delete',old.id,old.key);END;"
            "CREATE TRIGGER "+table+"_before BEFORE UPDATE OF key ON "+table+" WHEN old.key<>new.key BEGIN INSERT INTO "+names+"("+names+",rowid,key) VALUES('delete',old.id,old.key);END;"
            "CREATE TRIGGER "+table+"_after AFTER UPDATE OF key ON "+table+" WHEN old.key<>new.key BEGIN INSERT INTO "+names+"(rowid,key) VALUES(new.id,new.key);END;");cursors.at(generation)=UINT64_MAX;return false;
    }
    Sql insert(db,"INSERT INTO "+names+"(rowid,key) SELECT id,key FROM "+table+" WHERE id>? AND id<=? ORDER BY id");insert.Int(1,cursors.at(generation));insert.Int(2,last);insert.Row();cursors.at(generation)=last;indexed+=count;return true;
}
uint64_t NtfsStore::Count(wchar_t,uint64_t generation){Sql s(impl_->db,"SELECT count FROM fast_generations WHERE id=?");s.Int(1,generation);return s.Row()?s.Int(0):0;}
void NtfsStore::Activate(wchar_t drive,uint64_t generation,uint64_t journal,uint64_t next,uint64_t readMs,uint64_t buildMs){
    impl_->cleanup=true;auto count=Count(drive,generation);{Sql s(impl_->db,"UPDATE fast_volumes SET active=?,staging=0,journal=?,next=?,count=?,read_ms=?,build_ms=? WHERE drive=?");s.Int(1,generation);s.Int(2,journal);s.Int(3,next);s.Int(4,count);s.Int(5,readMs);s.Int(6,buildMs);s.Text(7,Letter(drive));s.Row();}Exec(impl_->db,"PRAGMA wal_autocheckpoint=512;");
}
void NtfsStore::Cursor(wchar_t drive,uint64_t next){Sql s(impl_->db,"UPDATE fast_volumes SET next=? WHERE drive=? AND next<>?");s.Int(1,next);s.Text(2,Letter(drive));s.Int(3,next);s.Row();}
void NtfsStore::ResetJournal(wchar_t drive){Sql s(impl_->db,"UPDATE fast_volumes SET journal=0,next=0,staging=0 WHERE drive=?");s.Text(1,Letter(drive));s.Row();impl_->cleanup=true;}
void NtfsStore::AbandonStaging(){Exec(impl_->db,"UPDATE fast_volumes SET staging=0;PRAGMA wal_autocheckpoint=512;");impl_->cleanup=true;}
void NtfsStore::Forget(wchar_t drive){impl_->cleanup=true;Sql s(impl_->db,"DELETE FROM fast_volumes WHERE drive=?");s.Text(1,Letter(drive));s.Row();}
bool NtfsStore::Cleanup(){
    if(!impl_->cleanup)return false;uint64_t generation=0;{Sql s(impl_->db,"SELECT g.id FROM fast_generations g LEFT JOIN fast_volumes v ON g.drive=v.drive WHERE v.drive IS NULL OR(g.id<>v.active AND g.id<>v.staging) LIMIT 1");if(s.Row())generation=s.Int(0);}
    if(!generation){impl_->cleanup=false;return false;}auto db=impl_->db;Exec(db,"DROP TABLE IF EXISTS "+Names(generation)+";DROP TABLE IF EXISTS "+Nodes(generation)+";");Sql remove(db,"DELETE FROM fast_generations WHERE id=?");remove.Int(1,generation);remove.Row();impl_->buildCursor.erase(generation);return true;
}
std::vector<FileHit> NtfsStore::Search(std::wstring_view key,std::wstring_view drives,const std::function<bool()>& cancelled){
    std::vector<FileHit> hits;if(key.empty())return hits;
    for(auto drive:drives){if(cancelled()||hits.size()>=80)break;auto volume=Cached(drive);if(!volume)continue;auto table=Nodes(volume->generation),names=Names(volume->generation);bool infix=key.size()>=3;
        Sql search(impl_->db,infix?"SELECT n.id,n.seq,n.flags,n.name FROM "+names+" f JOIN "+table+" n ON n.id=f.rowid WHERE f.key GLOB ? LIMIT 2048":"SELECT id,seq,flags,name FROM "+table+" WHERE key>=? AND key<? LIMIT 2048");search.Text(1,infix?Glob(key):std::wstring(key));if(!infix)search.Text(2,std::wstring(key)+L"\xffff");
        Sql parent(impl_->db,"SELECT seq,parent,name FROM "+table+" WHERE id=?");std::unordered_map<uint64_t,std::wstring> cache;size_t cacheBytes=0;
        while(hits.size()<80&&!cancelled()&&search.Row()){auto id=search.Int(0)|(search.Int(1)<<48);auto path=impl_->Path(*volume,id,parent,cache,cacheBytes);if(!path.empty())hits.push_back({search.Text(3),std::move(path),(search.Int(2)&FILE_ATTRIBUTE_DIRECTORY)!=0});}
    }return hits;
}
void NtfsStore::CheckIntegrity(){Sql volumes(impl_->db,"SELECT active FROM fast_volumes WHERE active<>0");while(volumes.Row()){auto names=Names(volumes.Int(0));Exec(impl_->db,"INSERT INTO "+names+"("+names+",rank) VALUES('integrity-check',1)");}}

bool NtfsStore::QueueChanges(const std::wstring& root,std::span<const uint8_t> buffer){
    if(root.size()!=3||root[1]!=L':'||root[2]!=L'\\')return false;wchar_t drive=static_cast<wchar_t>(towupper(root[0]));auto db=impl_->db;bool valid=true;Exec(db,"BEGIN");
    try{Sql put(db,"INSERT OR REPLACE INTO fast_pending(drive,key,path) VALUES(?,?,?)");size_t offset=0;
        while(offset<buffer.size()){
            constexpr size_t minimum=offsetof(FILE_NOTIFY_INFORMATION,FileName);if(buffer.size()-offset<minimum){valid=false;break;}FILE_NOTIFY_INFORMATION record{};std::memcpy(&record,buffer.data()+offset,minimum);
            if(record.FileNameLength%2||record.FileNameLength>buffer.size()-offset-minimum||!record.FileNameLength){valid=false;break;}std::wstring relative(record.FileNameLength/2,L'\0');std::memcpy(relative.data(),buffer.data()+offset+minimum,record.FileNameLength);
            auto path=(fs::path(root)/relative).lexically_normal().wstring(),key=FileIndex::Key(path);if(relative.find(L'\0')!=std::wstring::npos||!Within(key,FileIndex::Key(root))){valid=false;break;}
            if(!Within(key,impl_->excluded)){put.Text(1,Letter(drive));put.Text(2,key);put.Text(3,path);put.Row();put.Reset();}
            if(!record.NextEntryOffset)break;if(record.NextEntryOffset<minimum+record.FileNameLength||record.NextEntryOffset>buffer.size()-offset){valid=false;break;}offset+=record.NextEntryOffset;
        }
        Sql count(db,"SELECT count(*) FROM fast_pending WHERE drive=?");count.Text(1,Letter(drive));count.Row();if(count.Int(0)>8192)valid=false;Exec(db,"COMMIT");
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
    if(!valid)DropPending(drive);return valid;
}
void NtfsStore::DropPending(wchar_t drive){Sql s(impl_->db,"DELETE FROM fast_pending WHERE drive=?");s.Text(1,Letter(drive));s.Row();}
bool NtfsStore::HasPending(wchar_t drive){Sql s(impl_->db,"SELECT 1 FROM fast_pending WHERE drive=? LIMIT 1");s.Text(1,Letter(drive));return s.Row();}
bool NtfsStore::ApplyPending(wchar_t drive,uint64_t generation,uint64_t rootId){
    struct Pending {std::wstring key,path;uint64_t parent=0;bool remove=false;};std::vector<Pending> pending;
    {Sql take(impl_->db,"SELECT key,path FROM fast_pending WHERE drive=? ORDER BY key LIMIT 64");take.Text(1,Letter(drive));while(take.Row())pending.push_back({take.Text(0),take.Text(1)});}if(pending.empty())return false;
    std::vector<ntfs::Record> records;std::unordered_map<std::wstring,uint64_t> parents;
    for(auto& item:pending){auto path=fs::path(item.path),parent=path.parent_path();DWORD attrs=0,error=0;uint64_t parentId=0,id=0;if(parent.wstring().size()==3)parentId=rootId;else if(auto found=parents.find(parent.wstring());found!=parents.end())parentId=found->second;else if(Identity(parent,parentId,attrs,error))parents.emplace(parent.wstring(),parentId);
        if(!parentId)continue;item.parent=parentId;if(Identity(path,id,attrs,error))records.push_back({id,parentId,0,USN_REASON_FILE_CREATE,attrs,path.filename().wstring()});else if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)item.remove=true;
    }
    if(!records.empty())Apply(drive,generation,records,true);auto db=impl_->db;Exec(db,"BEGIN");
    try{Sql erase(db,"DELETE FROM "+Nodes(generation)+" WHERE parent=? AND key=? AND name=?"),done(db,"DELETE FROM fast_pending WHERE drive=? AND key=?");uint64_t removed=0;
        for(const auto& item:pending){if(item.remove){auto name=fs::path(item.path).filename().wstring();erase.Int(1,item.parent);erase.Text(2,FileIndex::Key(name));erase.Text(3,name);erase.Row();removed+=sqlite3_changes(db);erase.Reset();}done.Text(1,Letter(drive));done.Text(2,item.key);done.Row();done.Reset();}
        impl_->Adjust(drive,generation,0,removed);Exec(db,"COMMIT");
    }catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}return true;
}
}
