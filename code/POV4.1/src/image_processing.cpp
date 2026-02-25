#include "image_processing.h"
#include <vector>

static uint16_t read16(File &file) {
    uint16_t value = file.read();
    value |= static_cast<uint16_t>(file.read()) << 8;
    return value;
}

static uint32_t read32(File &file) {
    uint32_t value = file.read();
    value |= static_cast<uint32_t>(file.read()) << 8;
    value |= static_cast<uint32_t>(file.read()) << 16;
    value |= static_cast<uint32_t>(file.read()) << 24;
    return value;
}

static void write16(File &file, uint16_t value) {
    file.write(static_cast<uint8_t>(value & 0xff));
    file.write(static_cast<uint8_t>((value >> 8) & 0xff));
}

static void write32(File &file, uint32_t value) {
    file.write(static_cast<uint8_t>(value & 0xff));
    file.write(static_cast<uint8_t>((value >> 8) & 0xff));
    file.write(static_cast<uint8_t>((value >> 16) & 0xff));
    file.write(static_cast<uint8_t>((value >> 24) & 0xff));
}

ImageProcessResult processBmpToStaff(const char *srcPath,
                                     const char *destPath,
                                     uint16_t outWidth,
                                     uint16_t maxHeight,
                                     bool rotateCw,
                                     uint16_t requestedHeight) {
    ImageProcessResult result{false, "", 0, 0};
    File inFile = LittleFS.open(srcPath, "r");
    if (!inFile) {
        result.message = "Failed to open upload";
        return result;
    }

    if (read16(inFile) != 0x4D42) {
        result.message = "Not a BMP";
        inFile.close();
        return result;
    }

    read32(inFile);
    read32(inFile);
    uint32_t dataOffset = read32(inFile);
    read32(inFile);
    int32_t inWidth = static_cast<int32_t>(read32(inFile));
    int32_t inHeightSigned = static_cast<int32_t>(read32(inFile));
    if (read16(inFile) != 1) {
        result.message = "Invalid BMP planes";
        inFile.close();
        return result;
    }
    uint16_t bitDepth = read16(inFile);
    uint32_t compression = read32(inFile);

    if (bitDepth != 24 || compression != 0) {
        result.message = "BMP must be 24-bit uncompressed";
        inFile.close();
        return result;
    }

    bool topDown = false;
    int32_t inHeight = inHeightSigned;
    if (inHeightSigned < 0) {
        topDown = true;
        inHeight = -inHeightSigned;
    }

    if (inWidth <= 0 || inHeight <= 0) {
        result.message = "Invalid BMP size";
        inFile.close();
        return result;
    }

    uint32_t inRowSize = (static_cast<uint32_t>(inWidth) * 3 + 3) & ~3U;
    uint32_t outRowSize = (static_cast<uint32_t>(outWidth) * 3 + 3) & ~3U;
    if (outRowSize == 0) {
        result.message = "Invalid output width";
        inFile.close();
        return result;
    }

    uint16_t maxHeightSafe = static_cast<uint16_t>(maxHeight);
    uint32_t maxFromBuffer = 64000 / outRowSize;
    if (maxFromBuffer < maxHeightSafe) {
        maxHeightSafe = static_cast<uint16_t>(maxFromBuffer);
    }

    uint32_t virtualWidth = rotateCw ? static_cast<uint32_t>(inHeight) : static_cast<uint32_t>(inWidth);
    uint32_t virtualHeight = rotateCw ? static_cast<uint32_t>(inWidth) : static_cast<uint32_t>(inHeight);

    uint32_t outHeight = requestedHeight > 0
                             ? requestedHeight
                             : static_cast<uint32_t>((static_cast<float>(outWidth) * virtualWidth) / virtualHeight + 0.5f);
    if (outHeight == 0) {
        outHeight = 1;
    }
    if (outHeight > maxHeightSafe) {
        outHeight = maxHeightSafe;
    }

    uint32_t outSize = outRowSize * outHeight;
    if (outSize == 0 || outSize > 64000) {
        result.message = "Output too large";
        inFile.close();
        return result;
    }

    File outFile = LittleFS.open(destPath, "w");
    if (!outFile) {
        result.message = "Failed to open output";
        inFile.close();
        return result;
    }

    uint32_t fileSize = 54 + outSize;
    write16(outFile, 0x4D42);
    write32(outFile, fileSize);
    write32(outFile, 0);
    write32(outFile, 54);
    write32(outFile, 40);
    write32(outFile, outWidth);
    write32(outFile, outHeight);
    write16(outFile, 1);
    write16(outFile, 24);
    write32(outFile, 0);
    write32(outFile, outSize);
    write32(outFile, 2835);
    write32(outFile, 2835);
    write32(outFile, 0);
    write32(outFile, 0);

    std::vector<uint8_t> rowBuffer(inRowSize);
    std::vector<uint8_t> outRowBuffer(outRowSize);

    int32_t cachedSrcY = -1;
    for (int32_t outY = static_cast<int32_t>(outHeight) - 1; outY >= 0; outY--) {
        memset(outRowBuffer.data(), 0, outRowSize);
        for (uint32_t outX = 0; outX < outWidth; outX++) {
            uint32_t rotX = (outX * virtualWidth) / outWidth;
            uint32_t rotY = (static_cast<uint32_t>(outY) * virtualHeight) / outHeight;
            if (rotX >= virtualWidth) {
                rotX = virtualWidth - 1;
            }
            if (rotY >= virtualHeight) {
                rotY = virtualHeight - 1;
            }

            uint32_t srcX = rotateCw ? rotY : rotX;
            uint32_t srcY = rotateCw ? (static_cast<uint32_t>(inHeight) - 1 - rotX) : rotY;
            if (srcX >= static_cast<uint32_t>(inWidth) || srcY >= static_cast<uint32_t>(inHeight)) {
                continue;
            }

            if (static_cast<int32_t>(srcY) != cachedSrcY) {
                int32_t fileRow = topDown ? static_cast<int32_t>(srcY) : (inHeight - 1 - static_cast<int32_t>(srcY));
                uint32_t offset = dataOffset + static_cast<uint32_t>(fileRow) * inRowSize;
                if (!inFile.seek(offset)) {
                    result.message = "Failed to seek BMP";
                    inFile.close();
                    outFile.close();
                    LittleFS.remove(destPath);
                    return result;
                }
                size_t readCount = inFile.read(rowBuffer.data(), inRowSize);
                if (readCount != inRowSize) {
                    result.message = "Failed to read BMP";
                    inFile.close();
                    outFile.close();
                    LittleFS.remove(destPath);
                    return result;
                }
                cachedSrcY = static_cast<int32_t>(srcY);
            }

            uint32_t srcPos = srcX * 3;
            uint32_t outPos = outX * 3;
            outRowBuffer[outPos + 0] = rowBuffer[srcPos + 0];
            outRowBuffer[outPos + 1] = rowBuffer[srcPos + 1];
            outRowBuffer[outPos + 2] = rowBuffer[srcPos + 2];
        }

        size_t written = outFile.write(outRowBuffer.data(), outRowSize);
        if (written != outRowSize) {
            result.message = "Failed to write BMP";
            inFile.close();
            outFile.close();
            LittleFS.remove(destPath);
            return result;
        }
    }

    inFile.close();
    outFile.close();

    result.ok = true;
    result.message = "ok";
    result.outWidth = outWidth;
    result.outHeight = static_cast<uint16_t>(outHeight);
    return result;
}

bool readImageList(const char *listPath, std::vector<String> &files) {
    files.clear();
    File file = LittleFS.open(listPath, "r");
    if (!file) {
        return false;
    }
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }
        int spaceIndex = line.indexOf(' ');
        int tabIndex = line.indexOf('\t');
        int splitIndex = -1;
        if (spaceIndex >= 0 && tabIndex >= 0) {
            splitIndex = (spaceIndex < tabIndex) ? spaceIndex : tabIndex;
        } else if (spaceIndex >= 0) {
            splitIndex = spaceIndex;
        } else if (tabIndex >= 0) {
            splitIndex = tabIndex;
        }
        String filename = (splitIndex >= 0) ? line.substring(0, splitIndex) : line;
        filename.trim();
        if (filename.length() > 0) {
            files.push_back(filename);
        }
    }
    file.close();
    return true;
}

bool writeImageList(const char *listPath, const std::vector<String> &files) {
    File file = LittleFS.open(listPath, "w");
    if (!file) {
        return false;
    }
    for (const auto &name : files) {
        file.print(name);
        file.print('\n');
    }
    file.close();
    return true;
}

bool upsertImageInList(const char *listPath, const String &filename) {
    std::vector<String> files;
    readImageList(listPath, files);
    String clean = filename;
    if (!clean.startsWith("/")) {
        clean = "/" + clean;
    }
    std::vector<String> updated;
    for (const auto &name : files) {
        if (name != clean) {
            updated.push_back(name);
        }
    }
    updated.push_back(clean);
    return writeImageList(listPath, updated);
}

void listBmpFiles(std::vector<String> &files) {
    files.clear();
    File root = LittleFS.open("/");
    if (!root) {
        return;
    }
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".bmp")) {
            files.push_back(name);
        }
        file = root.openNextFile();
    }
    root.close();
}
