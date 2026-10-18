#pragma once
#include "platform.h"

namespace gv {
// Only launch targets and names are persisted. Window handles, icons and browser
// state never enter the catalogue. All disk work belongs to the launcher's worker.
class AppIndex {
public:
    explicit AppIndex(fs::path file):file_(std::move(file)){}
    bool Load();
    bool Save()const;
    void ReplaceCatalogue(std::vector<Program> programs);
    bool Learn(const Program& program);
    std::vector<Program> Entries()const;
    bool RefreshDue()const;
    static std::optional<Program> Discover(std::wstring query);
    static std::wstring Key(std::wstring value);
private:
    fs::path file_;
    std::vector<Program> catalogue_,learned_;
    uint64_t updated_=0;
};
}
