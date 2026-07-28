#pragma once

#include <string>

#include "gb/app/app_options.hpp"

namespace gb::frontend {

struct SessionPaths {
    std::string state;
    std::string legacyState;
    std::string batteryRam;
    std::string controls;
    std::string cheats;
    std::string palette;
    std::string rtc;
    std::string filters;
    std::string captureDirectory;
};

struct NetworkOptions {
    std::string linkConnect;
    int linkHostPort = 0;
    std::string netplayConnect;
    int netplayHostPort = 0;
    int netplayDelayFrames = 0;
};

struct RunLabOptions {
    bool enabled = false;
    std::string statePath;
    std::string commandQueuePath;
};

struct RealtimeOptions {
    int scale = 4;
    int audioBufferSamples = 1024;
    SessionPaths paths;
    NetworkOptions network;
    RunLabOptions runLab;
};

RealtimeOptions makeRealtimeOptions(const AppOptions& appOptions);

} // namespace gb::frontend
