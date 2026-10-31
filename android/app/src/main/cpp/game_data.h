#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <stdexcept>
#include <android/log.h>

inline std::string resolveGameRoot() {
    const char* env = std::getenv("NFSU2_DATA_DIR");
    if (env && *env && access(env, R_OK) == 0) {
        return env;
    }
    const char* candidates[] = {
        "/sdcard/NFSU2",
        "/storage/emulated/0/NFSU2",
        "/sdcard/nfsu2",
        "/storage/emulated/0/nfsu2"
    };
    for (const char* dir : candidates) {
        std::string s1 = std::string(dir) + "/SPEED2.EXE";
        std::string s2 = std::string(dir) + "/speed2.exe";
        if (access(s1.c_str(), R_OK) == 0 || access(s2.c_str(), R_OK) == 0) {
            return dir;
        }
    }
    for (const char* dir : candidates) {
        if (access(dir, R_OK) == 0) {
            return dir;
        }
    }
    return "/sdcard/NFSU2";
}

inline const std::string& getGameRoot() {
    static std::string root = resolveGameRoot();
    return root;
}

#define kGameRoot (getGameRoot().c_str())

inline void checkGameData() {
    const std::string root = getGameRoot();
    std::string exePath = root + "/SPEED2.EXE";
    FILE* exe = std::fopen(exePath.c_str(), "rb");
    if (!exe) {
        exePath = root + "/speed2.exe";
        exe = std::fopen(exePath.c_str(), "rb");
    }
    if (!exe) throw std::runtime_error("Cannot read " + root + "/SPEED2.EXE; check files and storage permission");
    unsigned char magic[2]{};
    const size_t read = std::fread(magic, 1, 2, exe);
    std::fclose(exe);
    struct stat info{};
    if (read != 2 || magic[0] != 'M' || magic[1] != 'Z' ||
        stat(exePath.c_str(), &info) || info.st_size != 4800512)
        throw std::runtime_error("SPEED2.EXE does not match expected image size/header");
    for (const char* name : {"CARS", "CREDITS", "FRONTEND", "GLOBAL", "LANGUAGES", "MOVIES",
                             "NIS", "SDATA", "SOUND", "SUBTITLES", "TRACKS"}) {
        auto path = root + "/" + name;
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            DIR* parent = opendir(root.c_str());
            if (parent) {
                while (auto* entry = readdir(parent)) {
                    if (strcasecmp(entry->d_name, name) == 0) {
                        path = root + "/" + entry->d_name;
                        dir = opendir(path.c_str());
                        break;
                    }
                }
                closedir(parent);
            }
        }
        if (!dir) throw std::runtime_error("Cannot read game directory: " + path);
        unsigned files = 0;
        while (auto* entry = readdir(dir)) if (entry->d_name[0] != '.') ++files;
        closedir(dir);
        if (!files) throw std::runtime_error("Empty game directory: " + path);
        __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Data directory %s: %u entries", name, files);
    }
    __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Game data readable at %s; executable header/size OK (guest not running)", root.c_str());
}
