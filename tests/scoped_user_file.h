#pragma once
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

// UI smoke tests exercise the real save/settings actions while preserving the
// player's files. Keep a failed restoration's backup available for recovery.
class ScopedUserFile {
    std::filesystem::path original,backup;
    std::filesystem::file_time_type modified{};
    bool captured=false,existed=false,restored=false;
public:
    explicit ScopedUserFile(std::filesystem::path path):original(std::move(path)) {
        std::error_code error;existed=std::filesystem::exists(original,error);
        if(error)return;
        if(!existed){captured=true;return;}
        modified=std::filesystem::last_write_time(original,error);if(error)return;
        const auto temporary=std::filesystem::temp_directory_path(error);if(error)return;
        backup=temporary/("openra2-playtest-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            +"-"+original.filename().string());
        captured=std::filesystem::copy_file(original,backup,std::filesystem::copy_options::none,error) && !error;
    }
    ScopedUserFile(const ScopedUserFile&)=delete;
    ScopedUserFile& operator=(const ScopedUserFile&)=delete;
    bool ready() const{return captured;}
    bool restore() {
        if(restored)return true;
        if(!captured)return false;
        std::error_code error;
        if(existed) {
            std::filesystem::copy_file(backup,original,std::filesystem::copy_options::overwrite_existing,error);
            if(!error)std::filesystem::last_write_time(original,modified,error);
        } else std::filesystem::remove(original,error);
        if(error){std::fprintf(stderr,"PLAYTEST restore failed; backup retained: %s\n",backup.string().c_str());return false;}
        restored=true;if(existed)std::filesystem::remove(backup,error);
        return true;
    }
    ~ScopedUserFile(){if(captured && !restored)restore();}
};
