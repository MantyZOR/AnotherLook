#include "tray.h"
#include "ui.h"
#include <wlanapi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <algorithm>
#include <functional>

namespace gv {
namespace {
struct WlanClient {
    HANDLE handle{};
    explicit WlanClient(bool enabled){DWORD version=0;if(enabled&&WlanOpenHandle(2,nullptr,&version,&handle)!=ERROR_SUCCESS)handle=nullptr;}
    ~WlanClient(){if(handle)WlanCloseHandle(handle,nullptr);}
};
struct WlanMemory {void* data{};~WlanMemory(){if(data)WlanFreeMemory(data);}};
struct DeviceSet {HDEVINFO handle=INVALID_HANDLE_VALUE;~DeviceSet(){if(handle!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(handle);}};
struct Device {DEVINST instance{};std::wstring name,letters;DWORD number{};};
constexpr GUID DiskInterface{0x53f56307,0xb6bf,0x11d0,{0x94,0xf2,0x00,0xa0,0xc9,0x1e,0xfb,0x8b}};
std::vector<Device> UsbDevices(){
    std::vector<Device> result;DeviceSet set;set.handle=SetupDiGetClassDevsW(&DiskInterface,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set.handle==INVALID_HANDLE_VALUE)return result;
    for(DWORD i=0;i<128;++i){
        SP_DEVICE_INTERFACE_DATA item{sizeof(item)};if(!SetupDiEnumDeviceInterfaces(set.handle,nullptr,&DiskInterface,i,&item))break;
        DWORD required=0;SetupDiGetDeviceInterfaceDetailW(set.handle,&item,nullptr,0,&required,nullptr);if(required<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||required>65536)continue;
        std::vector<BYTE> buffer(required);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA info{sizeof(info)};
        if(!SetupDiGetDeviceInterfaceDetailW(set.handle,&item,detail,required,nullptr,&info))continue;
        HANDLE disk=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(disk==INVALID_HANDLE_VALUE)continue;
        STORAGE_PROPERTY_QUERY query{};query.PropertyId=StorageDeviceProperty;query.QueryType=PropertyStandardQuery;
        BYTE storage[4096]{};DWORD returned=0;STORAGE_DEVICE_NUMBER number{};
        bool usb=DeviceIoControl(disk,IOCTL_STORAGE_QUERY_PROPERTY,&query,sizeof(query),storage,sizeof(storage),&returned,nullptr)&&returned>=sizeof(STORAGE_DEVICE_DESCRIPTOR)&&reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(storage)->BusType==BusTypeUsb;
        bool numbered=DeviceIoControl(disk,IOCTL_STORAGE_GET_DEVICE_NUMBER,nullptr,0,&number,sizeof(number),&returned,nullptr)!=FALSE;CloseHandle(disk);if(!usb||!numbered)continue;
        DEVINST removable=info.DevInst;bool ejectable=false;
        for(int level=0;level<6;++level){ULONG caps=0,size=sizeof(caps);if(CM_Get_DevNode_Registry_PropertyW(removable,CM_DRP_CAPABILITIES,nullptr,&caps,&size,0)==CR_SUCCESS&&(caps&CM_DEVCAP_REMOVABLE)){ejectable=true;break;}DEVINST parent{};if(CM_Get_Parent(&parent,removable,0)!=CR_SUCCESS)break;removable=parent;}
        if(!ejectable)continue;
        wchar_t name[512]{};if(!SetupDiGetDeviceRegistryPropertyW(set.handle,&info,SPDRP_FRIENDLYNAME,nullptr,reinterpret_cast<BYTE*>(name),sizeof(name),nullptr))SetupDiGetDeviceRegistryPropertyW(set.handle,&info,SPDRP_DEVICEDESC,nullptr,reinterpret_cast<BYTE*>(name),sizeof(name),nullptr);
        result.push_back({removable,name[0]?name:L"USB-накопитель",L"",number.DeviceNumber});
    }
    DWORD drives=GetLogicalDrives();for(int i=0;i<26;++i)if(drives&(1u<<i)){
        wchar_t root[]=L"A:\\";root[0]=static_cast<wchar_t>(L'A'+i);UINT kind=GetDriveTypeW(root);if(kind!=DRIVE_FIXED&&kind!=DRIVE_REMOVABLE)continue;
        wchar_t path[]=L"\\\\.\\A:";path[4]=static_cast<wchar_t>(L'A'+i);HANDLE volume=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(volume==INVALID_HANDLE_VALUE)continue;
        STORAGE_DEVICE_NUMBER number{};DWORD returned=0;bool ok=DeviceIoControl(volume,IOCTL_STORAGE_GET_DEVICE_NUMBER,nullptr,0,&number,sizeof(number),&returned,nullptr)!=FALSE;CloseHandle(volume);if(ok)for(auto& device:result)if(device.number==number.DeviceNumber){if(!device.letters.empty())device.letters+=L", ";device.letters+=static_cast<wchar_t>(L'A'+i);device.letters+=L":";}
    }
    // Multiple disks in one USB enclosure share one ejectable parent.
    std::vector<Device> unique;for(auto& device:result){auto found=std::find_if(unique.begin(),unique.end(),[&](const auto& other){return other.instance==device.instance;});if(found==unique.end())unique.push_back(std::move(device));else if(!device.letters.empty()){if(!found->letters.empty())found->letters+=L", ";found->letters+=device.letters;}}
    return unique;
}
}
bool IsNetworkTrayIcon(const GUID& guid){static constexpr GUID network{0x7820ae74,0x23e3,0x4229,{0x82,0xc1,0xe4,0x1c,0xb6,0x7d,0x5b,0x9c}};return IsEqualGUID(guid,network);}
bool IsDeviceTrayIcon(const GUID& guid){static constexpr GUID devices{0x7820ae78,0x23e3,0x4229,{0x82,0xc1,0xe4,0x1c,0xb6,0x7d,0x5b,0x9c}};return IsEqualGUID(guid,devices);}
void ShowTrayUtility(Application& app,HWND owner,POINT point,TrayUtility utility){
    HMENU menu=CreatePopupMenu();if(!menu)return;std::vector<std::function<void()>> actions;
    auto add=[&](std::wstring label,std::function<void()> action,UINT flags=MF_STRING){actions.push_back(std::move(action));AppendMenuW(menu,flags,static_cast<UINT_PTR>(actions.size()),label.c_str());};
    auto open=[&](const wchar_t* target){return [owner,target]{if(!LaunchTarget(target,L""))ShowNotice(owner,L"Системная команда",L"Не удалось открыть настройки Windows.");};};
    WlanClient wifi(utility==TrayUtility::Network);
    if(utility==TrayUtility::Network){
        WlanMemory interfaces;
        if(wifi.handle&&WlanEnumInterfaces(wifi.handle,nullptr,reinterpret_cast<PWLAN_INTERFACE_INFO_LIST*>(&interfaces.data))==ERROR_SUCCESS){
            auto list=static_cast<PWLAN_INTERFACE_INFO_LIST>(interfaces.data);
            for(DWORD i=0;i<std::min<DWORD>(list->dwNumberOfItems,8);++i){auto adapter=list->InterfaceInfo[i];WlanMemory networks;
                if(WlanGetAvailableNetworkList(wifi.handle,&adapter.InterfaceGuid,0,nullptr,reinterpret_cast<PWLAN_AVAILABLE_NETWORK_LIST*>(&networks.data))!=ERROR_SUCCESS)continue;
                auto available=static_cast<PWLAN_AVAILABLE_NETWORK_LIST>(networks.data);
                for(DWORD j=0;j<std::min<DWORD>(available->dwNumberOfItems,32);++j){auto network=available->Network[j];
                    auto name=Wide(std::string_view(reinterpret_cast<const char*>(network.dot11Ssid.ucSSID),std::min<ULONG>(network.dot11Ssid.uSSIDLength,DOT11_SSID_MAX_LENGTH)));if(name.empty())name=L"Скрытая сеть";
                    bool connected=(network.dwFlags&WLAN_AVAILABLE_NETWORK_CONNECTED)!=0;bool saved=(network.dwFlags&WLAN_AVAILABLE_NETWORK_HAS_PROFILE)!=0;
                    auto label=name+L" · "+std::to_wstring(network.wlanSignalQuality)+L"%"+(connected?L" · подключено":L"");
                    add(label,[&,adapter,network,connected,saved]{DWORD error=ERROR_SUCCESS;if(connected)error=WlanDisconnect(wifi.handle,&adapter.InterfaceGuid,nullptr);else if(saved){WLAN_CONNECTION_PARAMETERS parameters{};parameters.wlanConnectionMode=wlan_connection_mode_profile;parameters.strProfile=network.strProfileName;parameters.dot11BssType=network.dot11BssType;error=WlanConnect(wifi.handle,&adapter.InterfaceGuid,&parameters,nullptr);}else{LaunchTarget(L"ms-settings:network-wifi",L"");return;}if(error!=ERROR_SUCCESS)ShowNotice(owner,L"Wi-Fi",L"Windows не выполнила подключение. Открой настройки Wi-Fi и проверь профиль сети.");});
                }
            }
        }
        if(actions.empty())AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"Доступных сетей Wi-Fi нет");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add(L"Настройки Wi-Fi",open(L"ms-settings:network-wifi"));add(L"Ethernet",open(L"ms-settings:network-ethernet"));add(L"VPN",open(L"ms-settings:network-vpn"));add(L"Сеть и интернет",open(L"ms-settings:network-status"));
    }else{
        auto devices=UsbDevices();for(const auto& device:devices)add(L"Извлечь "+device.name+(device.letters.empty()?L"":L" ("+device.letters+L")"),[device,owner]{PNP_VETO_TYPE veto=PNP_VetoTypeUnknown;wchar_t reason[256]{};if(CM_Request_Device_EjectW(device.instance,&veto,reason,256,0)!=CR_SUCCESS)ShowNotice(owner,L"Безопасное извлечение",L"Windows не разрешила извлечение. Закрой файлы и приложения, использующие устройство, и попробуй ещё раз.");});
        if(devices.empty())AppendMenuW(menu,MF_STRING|MF_GRAYED,0,L"USB-накопителей для извлечения нет");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);add(L"Устройства USB",open(L"ms-settings:usb"));
    }
    app.BeginTrayMenu(GetCurrentProcessId());SetForegroundWindow(owner);UINT chosen=StyledMenu(menu,owner,point);DestroyMenu(menu);PostMessageW(owner,WM_NULL,0,0);
    if(chosen&&chosen<=actions.size())actions[chosen-1]();if(app.config.enabled&&!app.preview)app.chrome.Refresh();app.SyncPanels();
}
}
