#pragma once
#include "file_index.h"
#include "ntfs.h"
struct sqlite3;
namespace gv {
struct NtfsCachedVolume {
    wchar_t drive=0;ntfs::Volume identity;uint64_t generation=0,count=0,next=0,readMs=0,buildMs=0;
};
// SQLite owns the complete graph. Only a bounded page of paths is materialised.
class NtfsStore {
public:
    NtfsStore(sqlite3* database,std::wstring excludedProfile);
    ~NtfsStore();
    void Open();
    std::optional<NtfsCachedVolume> Cached(wchar_t drive);
    uint64_t Begin(wchar_t drive,const ntfs::Volume& volume);
    uint64_t Apply(wchar_t drive,uint64_t generation,std::span<const ntfs::Record> records,bool changes);
    // At most one 4096-row FTS batch; false when all names are indexed.
    bool Build(wchar_t drive,uint64_t generation,uint64_t& indexed);
    void Activate(wchar_t drive,uint64_t generation,uint64_t journal,uint64_t next,uint64_t readMs,uint64_t buildMs);
    void Cursor(wchar_t drive,uint64_t next);
    void ResetJournal(wchar_t drive);
    void AbandonStaging();
    void Forget(wchar_t drive);
    bool Cleanup();
    uint64_t Count(wchar_t drive,uint64_t generation);
    std::vector<FileHit> Search(std::wstring_view key,std::wstring_view drives,const std::function<bool()>& cancelled);
    bool Trigram()const;
    void CheckIntegrity();
    bool QueueChanges(const std::wstring& root,std::span<const uint8_t> notifications);
    bool HasPending(wchar_t drive);void DropPending(wchar_t drive);
    bool ApplyPending(wchar_t drive,uint64_t generation,uint64_t rootId);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
struct NtfsProgress {
    bool connected=false,busy=false,cached=false;uint64_t entries=0,readMs=0,buildMs=0,records=0,indexed=0,capacity=0,remainingMs=0,changes=0;
    std::wstring stage,note,drives,journalMissing;
};
class NtfsIndex {
public:
    NtfsIndex(sqlite3* database,std::wstring profile,bool snapshotOnly=false);
    ~NtfsIndex();
    void Open();
    void Configure(const Config::FileSearch& config,const std::vector<Config::FileRoot>& roots,bool rebuild);
    void Connect(std::unique_ptr<NtfsConnection> connection);
    // One bounded import/indexing step. Queries run between these steps.
    bool Step();
    bool Covers(const Config::FileRoot& root,bool completeOnly=false);
    std::vector<FileHit> Search(std::wstring_view key,const std::function<bool()>& cancelled);
    NtfsProgress Progress();
    HANDLE Event()const;
    bool Observe(const std::wstring& root,std::span<const uint8_t> notifications,bool overflow);
    void Finalizing(bool value);
    void LaunchError(std::wstring message);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
