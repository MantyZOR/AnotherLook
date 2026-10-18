#include "ntfs.h"
#include <winioctl.h>
#include <shellapi.h>
#include <sddl.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

namespace gv {
namespace {
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):h(value){}
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::wstring WinError(DWORD code){return L"Ошибка Windows "+std::to_wstring(code);}
std::wstring Executable(DWORD pid){Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));wchar_t path[32768]{};DWORD size=32768;return p.h!=INVALID_HANDLE_VALUE&&p.h&&QueryFullProcessImageNameW(p.h,0,path,&size)?std::wstring(path,size):L"";}
bool ReadAll(HANDLE h,void* data,DWORD bytes){auto p=static_cast<uint8_t*>(data);while(bytes){DWORD read=0;if(!ReadFile(h,p,bytes,&read,nullptr)||!read)return false;p+=read;bytes-=read;}return true;}
bool WriteAll(HANDLE h,const void* data,DWORD bytes){auto p=static_cast<const uint8_t*>(data);while(bytes){DWORD written=0;if(!WriteFile(h,p,bytes,&written,nullptr)||!written)return false;p+=written;bytes-=written;}return true;}
bool WritePacket(HANDLE pipe,ntfs::Packet type,const void* data,DWORD bytes){ntfs::Header header;header.type=static_cast<uint32_t>(type);header.bytes=bytes;return bytes<=ntfs::MaxPacket&&WriteAll(pipe,&header,sizeof(header))&&(!bytes||WriteAll(pipe,data,bytes));}
bool PipeName(std::wstring_view pipe){constexpr std::wstring_view prefix=L"\\\\.\\pipe\\GoodView.Ntfs.";return pipe.starts_with(prefix)&&pipe.size()<160&&std::all_of(pipe.begin()+prefix.size(),pipe.end(),[](wchar_t c){return c==L'-'||c==L'{'||c==L'}'||c==L'.'||c>=L'0'&&c<=L'9'||c>=L'a'&&c<=L'f'||c>=L'A'&&c<=L'F';});}
}

namespace ntfs {
bool ValidDrive(wchar_t drive){
    if(drive<L'A'||drive>L'Z')return false;wchar_t root[]{drive,L':',L'\\',0},format[32]{};
    auto type=GetDriveTypeW(root);return (type==DRIVE_FIXED||type==DRIVE_REMOVABLE)&&GetVolumeInformationW(root,nullptr,0,nullptr,nullptr,nullptr,format,32)&&_wcsicmp(format,L"NTFS")==0;
}
bool SameExecutable(DWORD process){auto ours=Executable(GetCurrentProcessId()),other=Executable(process);return !ours.empty()&&!other.empty()&&_wcsicmp(ours.c_str(),other.c_str())==0;}
bool ParseRecords(std::span<const uint8_t> bytes,std::vector<Record>& records,uint64_t& next,std::wstring& error){
    records.clear();error.clear();if(bytes.size()<sizeof(uint64_t)||bytes.size()>MaxPacket){error=L"Некорректный размер пакета USN";return false;}
    std::memcpy(&next,bytes.data(),sizeof(next));size_t offset=sizeof(next);
    while(offset<bytes.size()){
        constexpr size_t minimum=offsetof(USN_RECORD_V2,FileName);
        if(bytes.size()-offset<minimum){error=L"Обрезанная запись USN";return false;}
        USN_RECORD_V2 record{};std::memcpy(&record,bytes.data()+offset,minimum);
        if(record.MajorVersion!=2){error=L"Неподдерживаемая версия записи USN";return false;}
        if(record.RecordLength<minimum||record.RecordLength>bytes.size()-offset||record.RecordLength%8||record.FileNameOffset<minimum||record.FileNameOffset%2||record.FileNameLength%2||!record.FileNameLength||record.FileNameLength>510||static_cast<size_t>(record.FileNameOffset)+record.FileNameLength>record.RecordLength){error=L"Повреждённые границы записи USN";return false;}
        Record value;value.id=record.FileReferenceNumber;value.parent=record.ParentFileReferenceNumber;value.usn=record.Usn;value.reason=record.Reason;value.attributes=record.FileAttributes;
        value.name.resize(record.FileNameLength/2);std::memcpy(value.name.data(),bytes.data()+offset+record.FileNameOffset,record.FileNameLength);
        if(value.name.find_first_of(L"\\/:")!=std::wstring::npos||value.name.find(L'\0')!=std::wstring::npos){error=L"Недопустимое имя в записи USN";return false;}
        records.push_back(std::move(value));offset+=record.RecordLength;
    }return true;
}

int RunHelper(std::wstring_view pipeName,DWORD parent,std::wstring createJournals){
    if(!PipeName(pipeName)||!parent||!SameExecutable(parent)||createJournals.size()>26||!std::all_of(createJournals.begin(),createJournals.end(),[](wchar_t c){return c>=L'A'&&c<=L'Z';}))return ERROR_INVALID_PARAMETER;
    Handle parentHandle(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,parent));if(!parentHandle.h||parentHandle.h==INVALID_HANDLE_VALUE)return ERROR_INVALID_PARAMETER;
    Handle token;TOKEN_ELEVATION elevation{};DWORD returned=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h)||!GetTokenInformation(token.h,TokenElevation,&elevation,sizeof(elevation),&returned)||!elevation.TokenIsElevated)return ERROR_ELEVATION_REQUIRED;
    std::wstring name(pipeName);Handle pipe(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr));if(pipe.h==INVALID_HANDLE_VALUE)return GetLastError();
    ULONG server=0;if(!GetNamedPipeServerProcessId(pipe.h,&server)||server!=parent)return ERROR_ACCESS_DENIED;
    // A vanished parent must never leave an elevated idle process behind.
    std::thread([parentHandle=parentHandle.h]{WaitForSingleObject(parentHandle,INFINITE);ExitProcess(0);}).detach();
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    std::vector<uint8_t> buffer(MaxPacket);Request request;
    while(ReadAll(pipe.h,&request,sizeof(request))){
        if(request.magic!=Magic||request.version!=Protocol)return ERROR_INVALID_DATA;
        auto operation=static_cast<Operation>(request.operation);if(operation==Operation::Exit)return 0;bool ignoreJournal=operation==Operation::SnapshotWithoutJournal;if(ignoreJournal)operation=Operation::Snapshot;
        if((operation!=Operation::Snapshot&&operation!=Operation::Changes&&operation!=Operation::Probe)||!ValidDrive(static_cast<wchar_t>(request.drive)))return ERROR_INVALID_PARAMETER;
        wchar_t root[]{static_cast<wchar_t>(request.drive),L':',L'\\',0};std::wstring path=L"\\\\.\\"+std::wstring(root,2);
        Handle volume(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));Done done;
        if(operation==Operation::Probe){MFT_ENUM_DATA_V0 cursor{};cursor.HighUsn=std::numeric_limits<USN>::max();DWORD count=0;auto began=GetTickCount64();BOOL ok=volume.h!=INVALID_HANDLE_VALUE&&DeviceIoControl(volume.h,FSCTL_ENUM_USN_DATA,&cursor,sizeof(cursor),buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr);done.error=ok?0:GetLastError();done.records=count;done.readMs=GetTickCount64()-began;if(!WritePacket(pipe.h,Packet::Done,&done,sizeof(done)))return ERROR_BROKEN_PIPE;continue;}
        USN_JOURNAL_DATA_V0 journal{};DWORD bytes=0;auto ioBegan=GetTickCount64();
        if(volume.h==INVALID_HANDLE_VALUE)done.error=GetLastError();
        else if(!DeviceIoControl(volume.h,FSCTL_QUERY_USN_JOURNAL,nullptr,0,&journal,sizeof(journal),&bytes,nullptr))done.error=GetLastError();
        // This separate, explicit setup permission comes from a settings action
        // or command-line consent. Routine helper sessions remain read-only.
        // Recheck through the setup handle before creating a missing journal.
        if(done.error==ERROR_JOURNAL_NOT_ACTIVE&&createJournals.find(static_cast<wchar_t>(request.drive))!=std::wstring::npos){
            Handle setup(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
            if(setup.h==INVALID_HANDLE_VALUE)done.error=GetLastError();
            else if(DeviceIoControl(setup.h,FSCTL_QUERY_USN_JOURNAL,nullptr,0,&journal,sizeof(journal),&bytes,nullptr))done.error=0;
            else if(GetLastError()==ERROR_JOURNAL_NOT_ACTIVE){CREATE_USN_JOURNAL_DATA settings{64ULL*1024*1024,8ULL*1024*1024};if(DeviceIoControl(setup.h,FSCTL_CREATE_USN_JOURNAL,&settings,sizeof(settings),nullptr,0,&bytes,nullptr)&&DeviceIoControl(setup.h,FSCTL_QUERY_USN_JOURNAL,nullptr,0,&journal,sizeof(journal),&bytes,nullptr))done.error=0;else done.error=GetLastError();}
            else done.error=GetLastError();
            std::erase(createJournals,static_cast<wchar_t>(request.drive));
        }
        if(!done.error)std::erase(createJournals,static_cast<wchar_t>(request.drive));
        // MFT enumeration is available even when the optional USN journal is off.
        // That mode is kept current by ordinary directory notifications.
        if(done.error==ERROR_JOURNAL_NOT_ACTIVE&&operation==Operation::Snapshot){done.error=0;journal={};}
        if(ignoreJournal&&!done.error)journal={};
        if(!done.error&&operation==Operation::Changes&&(request.journal!=journal.UsnJournalID||request.next<static_cast<uint64_t>(std::max(journal.FirstUsn,journal.LowestValidUsn))||request.next>static_cast<uint64_t>(journal.NextUsn)))done.error=ERROR_JOURNAL_ENTRY_DELETED;
        done.readMs+=GetTickCount64()-ioBegan;
        if(!done.error){
            Volume info;info.journal=journal.UsnJournalID;info.first=std::max(journal.FirstUsn,journal.LowestValidUsn);info.next=journal.NextUsn;GetVolumeNameForVolumeMountPointW(root,info.guid,64);
            Handle directory(CreateFileW(root,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr));BY_HANDLE_FILE_INFORMATION identity{};
            if(directory.h==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(directory.h,&identity))done.error=GetLastError();
            else info.root=(static_cast<uint64_t>(identity.nFileIndexHigh)<<32)|identity.nFileIndexLow;
            NTFS_VOLUME_DATA_BUFFER data{};ioBegan=GetTickCount64();
            if(DeviceIoControl(volume.h,FSCTL_GET_NTFS_VOLUME_DATA,nullptr,0,&data,sizeof(data),&bytes,nullptr)){info.serial=data.VolumeSerialNumber.QuadPart;if(data.BytesPerFileRecordSegment)info.capacity=data.MftValidDataLength.QuadPart/data.BytesPerFileRecordSegment;}
            done.readMs+=GetTickCount64()-ioBegan;
            if(!done.error&&!WritePacket(pipe.h,Packet::Volume,&info,sizeof(info)))return ERROR_BROKEN_PIPE;
            if(!done.error&&operation==Operation::Snapshot){
                MFT_ENUM_DATA_V0 cursor{};cursor.HighUsn=std::numeric_limits<USN>::max();
                for(;;){
                    ioBegan=GetTickCount64();BOOL ok=DeviceIoControl(volume.h,FSCTL_ENUM_USN_DATA,&cursor,sizeof(cursor),buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr);DWORD error=ok?0:GetLastError();done.readMs+=GetTickCount64()-ioBegan;
                    if(!ok){if(error!=ERROR_HANDLE_EOF)done.error=error;break;}
                    if(bytes<8){done.error=ERROR_INVALID_DATA;break;}uint64_t next=0;std::memcpy(&next,buffer.data(),8);if(next<=cursor.StartFileReferenceNumber){done.error=ERROR_INVALID_DATA;break;}cursor.StartFileReferenceNumber=next;
                    if(!WritePacket(pipe.h,Packet::Records,buffer.data(),bytes))return ERROR_BROKEN_PIPE;
                    // Count safely without allocating a second copy of every name.
                    for(size_t off=8;off+8<=bytes;){DWORD length=0;std::memcpy(&length,buffer.data()+off,4);if(length<8||length>bytes-off){done.error=ERROR_INVALID_DATA;break;}++done.records;off+=length;}
                    if(done.error)break;
                }done.next=info.next;
            }else if(!done.error){
                READ_USN_JOURNAL_DATA_V0 cursor{};cursor.StartUsn=request.next;cursor.UsnJournalID=request.journal;
                // Only namespace changes enter the index. Disk writes and access-time
                // changes do not generate SQLite writes or UI refreshes.
                cursor.ReasonMask=USN_REASON_FILE_CREATE|USN_REASON_FILE_DELETE|USN_REASON_RENAME_OLD_NAME|USN_REASON_RENAME_NEW_NAME|USN_REASON_HARD_LINK_CHANGE;
                done.next=request.next;
                while(done.next<info.next){
                    ioBegan=GetTickCount64();BOOL ok=DeviceIoControl(volume.h,FSCTL_READ_USN_JOURNAL,&cursor,sizeof(cursor),buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr);DWORD error=ok?0:GetLastError();done.readMs+=GetTickCount64()-ioBegan;
                    if(!ok){done.error=error;break;}if(bytes<8){done.error=ERROR_INVALID_DATA;break;}
                    uint64_t next=0;std::memcpy(&next,buffer.data(),8);if(next<done.next){done.error=ERROR_INVALID_DATA;break;}
                    if(bytes>8&&!WritePacket(pipe.h,Packet::Records,buffer.data(),bytes))return ERROR_BROKEN_PIPE;
                    for(size_t off=8;off+8<=bytes;){DWORD length=0;std::memcpy(&length,buffer.data()+off,4);if(length<8||length>bytes-off){done.error=ERROR_INVALID_DATA;break;}++done.records;off+=length;}
                    if(next==done.next||done.error)break;done.next=next;cursor.StartUsn=next;
                }
            }
        }
        if(!WritePacket(pipe.h,Packet::Done,&done,sizeof(done)))return ERROR_BROKEN_PIPE;
    }return 0;
}
}

std::unique_ptr<NtfsConnection> NtfsConnection::Launch(HWND owner,std::wstring& error,bool elevate,std::wstring_view createJournals){
    if(createJournals.size()>26||!std::all_of(createJournals.begin(),createJournals.end(),[](wchar_t c){return c>=L'A'&&c<=L'Z';})){error=L"Некорректный список дисков NTFS";return {};}
    error.clear();auto connection=std::unique_ptr<NtfsConnection>(new NtfsConnection);auto name=L"\\\\.\\pipe\\GoodView.Ntfs."+NewId();
    Handle token;DWORD bytes=0;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h)){error=WinError(GetLastError());return {};}
    GetTokenInformation(token.h,TokenUser,nullptr,0,&bytes);std::vector<uint8_t> user(bytes);if(!bytes||!GetTokenInformation(token.h,TokenUser,user.data(),bytes,&bytes)){error=WinError(GetLastError());return {};}
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid)){error=WinError(GetLastError());return {};}
    std::wstring acl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")(A;;GA;;;BA)(A;;GA;;;SY)";LocalFree(sid);PSECURITY_DESCRIPTOR security=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&security,nullptr)){error=WinError(GetLastError());return {};}
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),security,FALSE};
    connection->pipe_=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,ntfs::MaxPacket,ntfs::MaxPacket,0,&attributes);LocalFree(security);
    connection->event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(connection->pipe_==INVALID_HANDLE_VALUE||!connection->event_){error=WinError(GetLastError());return {};}
    connection->overlap_.hEvent=connection->event_;BOOL connected=ConnectNamedPipe(connection->pipe_,&connection->overlap_);DWORD last=GetLastError();
    if(!connected&&last!=ERROR_IO_PENDING&&last!=ERROR_PIPE_CONNECTED){error=WinError(last);return {};}
    connection->connecting_=!connected&&last==ERROR_IO_PENDING;if(!connection->connecting_)SetEvent(connection->event_);
    auto args=L"--ntfs-helper \""+name+L"\" "+std::to_wstring(GetCurrentProcessId());if(!createJournals.empty())args+=L" --create-journal "+std::wstring(createJournals);auto exe=Executable(GetCurrentProcessId());
    SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;launch.hwnd=owner;launch.lpVerb=elevate?L"runas":L"open";launch.lpFile=exe.c_str();launch.lpParameters=args.c_str();launch.nShow=SW_HIDE;
    if(!ShellExecuteExW(&launch)||!launch.hProcess){DWORD code=GetLastError();error=code==ERROR_CANCELLED?L"Доступ к MFT не разрешён. Продолжен обычный обход.":WinError(code);return {};}
    connection->process_=launch.hProcess;connection->launched_=GetTickCount64();connection->buffer_.resize(sizeof(ntfs::Header));return connection;
}
NtfsConnection::~NtfsConnection(){
    if(pipe_!=INVALID_HANDLE_VALUE){CancelIoEx(pipe_,nullptr);if(event_&&(reading_||connecting_)){DWORD bytes=0;GetOverlappedResult(pipe_,&overlap_,&bytes,TRUE);}DisconnectNamedPipe(pipe_);CloseHandle(pipe_);}
    // Only our own small read-only helper is terminated if it fails to exit on EOF.
    if(process_){if(WaitForSingleObject(process_,1500)==WAIT_TIMEOUT)TerminateProcess(process_,0);CloseHandle(process_);}if(event_)CloseHandle(event_);
}
bool NtfsConnection::Send(const ntfs::Request& request,std::wstring& error){
    if(!connected_){error=L"Помощник MFT не подключён";return false;}
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED write{};write.hEvent=event.h;DWORD bytes=0;BOOL ok=WriteFile(pipe_,&request,sizeof(request),&bytes,&write);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(event.h,500)!=WAIT_OBJECT_0){CancelIoEx(pipe_,&write);GetOverlappedResult(pipe_,&write,&bytes,TRUE);error=L"Помощник MFT не отвечает";return false;}ok=GetOverlappedResult(pipe_,&write,&bytes,FALSE);}
    if(!ok||bytes!=sizeof(request)){error=WinError(GetLastError());return false;}return true;
}
int NtfsConnection::Poll(ntfs::Packet& type,std::vector<uint8_t>& payload,std::wstring& error){
    if(!connected_){
        if(WaitForSingleObject(event_,0)!=WAIT_OBJECT_0){if(GetTickCount64()-launched_>15000||WaitForSingleObject(process_,0)==WAIT_OBJECT_0){error=L"Не удалось подключить помощник MFT";return -1;}return 0;}
        DWORD bytes=0;BOOL ok=!connecting_||GetOverlappedResult(pipe_,&overlap_,&bytes,FALSE);if(!ok&&GetLastError()!=ERROR_PIPE_CONNECTED){error=WinError(GetLastError());return -1;}
        ULONG client=0;if(!GetNamedPipeClientProcessId(pipe_,&client)||client!=GetProcessId(process_)||!ntfs::SameExecutable(client)){error=L"Не совпадает процесс помощника MFT";return -1;}
        Handle token;TOKEN_ELEVATION elevation{};DWORD size=0;
        if(!OpenProcessToken(process_,TOKEN_QUERY,&token.h)||!GetTokenInformation(token.h,TokenElevation,&elevation,sizeof(elevation),&size)||!elevation.TokenIsElevated){error=L"Помощнику MFT нужны повышенные права";return -1;}
        connected_=true;connecting_=false;
    }
    for(int pass=0;pass<4;++pass){
        DWORD bytes=0;if(reading_){if(WaitForSingleObject(event_,0)!=WAIT_OBJECT_0)return 0;if(!GetOverlappedResult(pipe_,&overlap_,&bytes,FALSE)){error=WinError(GetLastError());return -1;}reading_=false;}
        else{ResetEvent(event_);overlap_={};overlap_.hEvent=event_;BOOL ok=ReadFile(pipe_,buffer_.data()+have_,want_-have_,&bytes,&overlap_);if(!ok){if(GetLastError()==ERROR_IO_PENDING){reading_=true;return 0;}error=WinError(GetLastError());return -1;}}
        if(!bytes){error=L"Соединение с MFT закрыто";return -1;}have_+=bytes;if(have_!=want_)continue;
        if(!body_){std::memcpy(&header_,buffer_.data(),sizeof(header_));if(header_.magic!=ntfs::Magic||header_.version!=ntfs::Protocol||header_.bytes>ntfs::MaxPacket||header_.type<1||header_.type>3){error=L"Неверный протокол MFT";return -1;}body_=true;have_=0;want_=header_.bytes;buffer_.resize(want_);if(want_)continue;}
        type=static_cast<ntfs::Packet>(header_.type);payload=std::move(buffer_);buffer_.resize(sizeof(ntfs::Header));body_=false;have_=0;want_=sizeof(ntfs::Header);return 1;
    }return 0;
}
}
