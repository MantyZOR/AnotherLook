#pragma once
#include "core.h"
namespace gv {
fs::path SelectDataDirectory(const fs::path& executable,const fs::path& localAppData,bool installed);
fs::path DefaultDataDirectory();
bool IsInstalledApplication();
void RememberPortableProfile(const fs::path& directory);
bool MigratePortableProfile(const fs::path& destination,std::wstring& error);
bool ImportProfile(const fs::path& source,const fs::path& destination,std::wstring& error);
}
