#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

struct ImageProcessResult {
    bool ok;
    String message;
    uint16_t outWidth;
    uint16_t outHeight;
};

ImageProcessResult processBmpToStaff(const char *srcPath,
                                     const char *destPath,
                                     uint16_t outWidth,
                                     uint16_t maxHeight,
                                     bool rotateCw,
                                     uint16_t requestedHeight);

bool readImageList(const char *listPath, std::vector<String> &files);
bool writeImageList(const char *listPath, const std::vector<String> &files);
bool upsertImageInList(const char *listPath, const String &filename);
void listBmpFiles(std::vector<String> &files);
