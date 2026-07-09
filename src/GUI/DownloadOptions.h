#pragma once

#include <string>

struct DownloadOptions {
    int fileFormat = 0;
    int mediaMode = 0;
    int quality = 0;
    bool useCustomPath = false;
    std::string customPath;
};
