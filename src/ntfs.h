#pragma once
#include "core.h"
#include <cstdint>
#include <memory>
#include <span>

namespace gv {
// The helper accepts only a local NTFS drive letter and journal cursors, never
// a database path. Routine sessions are read-only. Explicit setup can create a
// missing OS journal with fixed parameters, once for the approved drive letters.
namespace ntfs {
constexpr uint32_t Protocol = 1, Magic = 0x47564e54, MaxPacket = 128 * 1024;
enum class Packet : uint32_t { Volume=1, Records=2, Done=3 };
enum class Operation : uint32_t { Snapshot=1, Changes=2, Exit=3, Probe=4, SnapshotWithoutJournal=5 };
struct Header { uint32_t magic=Magic,version=Protocol,type=0,bytes=0; };
struct Request { uint32_t magic=Magic,version=Protocol,operation=0,drive=0;uint64_t journal=0,next=0; };
struct Volume {
    uint64_t root=0,serial=0,journal=0,first=0,next=0,capacity=0;
    wchar_t guid[64]{};
};
struct Done { uint64_t next=0,readMs=0,records=0;uint32_t error=0,reserved=0; };
struct Record { uint64_t id=0,parent=0,usn=0;uint32_t reason=0,attributes=0;std::wstring name; };
// Parses documented V2 records, including the leading 8-byte continuation.
// No pointers into the untrusted packet survive this call.
bool ParseRecords(std::span<const uint8_t> bytes,std::vector<Record>& records,uint64_t& next,std::wstring& error);
bool ValidDrive(wchar_t drive);
bool SameExecutable(DWORD process);
int RunHelper(std::wstring_view pipe,DWORD parent,std::wstring createJournals={});
}

class NtfsConnection {
public:
    static std::unique_ptr<NtfsConnection> Launch(HWND owner,std::wstring& error,bool elevate=true,std::wstring_view createJournals={});
    ~NtfsConnection();
    // Non-blocking connection and framed receive. 0=pending, 1=packet, -1=error.
    int Poll(ntfs::Packet& type,std::vector<uint8_t>& payload,std::wstring& error);
    bool Send(const ntfs::Request& request,std::wstring& error);
    bool Connected()const{return connected_;}
    HANDLE Event()const{return event_;}
private:
    NtfsConnection()=default;
    HANDLE pipe_=INVALID_HANDLE_VALUE,process_{},event_{};OVERLAPPED overlap_{};
    bool connected_=false,connecting_=false,reading_=false,body_=false;
    ntfs::Header header_{};std::vector<uint8_t> buffer_;DWORD have_=0,want_=sizeof(ntfs::Header);
    ULONGLONG launched_=0;
};
}
