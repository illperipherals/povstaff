/*
Persistence of vision  staff project by shurik179
See https://github.com/shurik179/povstaff

Distibuted under MIT license, see LICENSE file in this directory.

For required libraries and support files, check
https://github.com/shurik179/povstaff/code

*/
//first, include all libraries

// C++ standard
#include <cstdarg>
#include <vector>

//NeoPixel
#include <Adafruit_NeoPixel.h>

//Filesystem-related
#include <FS.h>
#include <LittleFS.h>

//WiFi and webserver
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>

// BLE
#include <NimBLEDevice.h>

// Web file manager
// #include <detail/RequestHandlersImpl.h>
#include <ESPxWebFlMgr.h>

//our own custom library
#include <pov-esp32.h>
#include "LSM6.h"
#include "staff_config.h"



// Now, various constants and defines

const word filemanagerport = 8080;
const char *ssid = "POVSTAFFXXXX";
//when choosing a password, you must use at least 8 symbols
const char *password = "XXXXXXXX";
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "4.6"
#endif
const char *FW_VERSION = FW_VERSION_STR;

// POV Staff details
#define PIN_VSENSE 9
#define NUM_PIXELS STAFF_NUM_PIXELS


// frame rate. Instead of using constant frame rate per second, we will adjust
// depending on rotation speed

// how many degrees of staff turn between successive lines?
#define DEG_PER_LINE 1.0f
// where is the list of images stored?
static char imageListPath[] = "/imagelist.txt";
//Finally, some colors
#define RED 0xFF0000
#define GREEN 0x00FF00
#define BLUE 0x0000FF
#define YELLOW 0xFFFF00
#define PURPLE 0x8000FF


// Now, global objects

//the on-board NeoPixel
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);


ESPxWebFlMgr filemgr(filemanagerport); // we want a different port than the webserver
WiFiServer server(80);

LSM6 imu;
POV staff(NUM_PIXELS);
uint32_t lastIMUcheck = 0; //when did we last check IMU speed, in ms
uint32_t lastPause = 0;    //when did we last pause/unpause staff, in ms
uint32_t nextImageChange = 0;
float speed=0.0;           //staff rotation speed, in deg/s
static bool imuAvailable = false;
static float manualSpeed = 360.0f;
static bool imageLock = false;
static int currentImageIndex = 0;
static std::vector<String> imageNames;
static bool otaEnabled = false;
static bool oscEnabled = false;
static WiFiUDP oscUdp;
#ifdef POV_DEBUG
static bool imuDebugEnabled = true;
#else
static bool imuDebugEnabled = false;
#endif
static uint32_t lastImuDebug = 0;

static const char *bleServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleRxUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleTxUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleCurrentImageUuid = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleNextImageUuid = "6E400005-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *blePrevImageUuid = "6E400006-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleLockUuid = "6E400007-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleDebugUuid = "6E400008-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *bleLogUuid = "6E400009-B5A3-F393-E0A9-E50E24DCCA9E";
static NimBLECharacteristic *bleTxCharacteristic = NULL;
static NimBLECharacteristic *bleRxCharacteristic = NULL;
static NimBLECharacteristic *bleCurrentImageCharacteristic = NULL;
static NimBLECharacteristic *bleNextImageCharacteristic = NULL;
static NimBLECharacteristic *blePrevImageCharacteristic = NULL;
static NimBLECharacteristic *bleLockCharacteristic = NULL;
static NimBLECharacteristic *bleDebugCharacteristic = NULL;
static NimBLECharacteristic *bleLogCharacteristic = NULL;
static NimBLEServer *bleServer = NULL;
static String lastBleResponse;
static bool bleLogEnabled = false;
static uint32_t lastBleLogMs = 0;
static const uint32_t bleLogIntervalMs = 200;

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "changeme"
#endif

#ifndef OTA_WIFI_SSID
#define OTA_WIFI_SSID "YOUR_SSID"
#endif

#ifndef OTA_WIFI_PASSWORD
#define OTA_WIFI_PASSWORD "YOUR_PASSWORD"
#endif

#ifndef OSC_PORT
#define OSC_PORT 8000
#endif

static const char *otaWifiSsid = OTA_WIFI_SSID;
static const char *otaWifiPassword = OTA_WIFI_PASSWORD;

void setNewImageChange();

static void blinkStatus(uint32_t color, uint8_t times, uint16_t onMs, uint16_t offMs) {
    for (uint8_t i = 0; i < times; i++) {
        pixel.setPixelColor(0, color);
        pixel.show();
        delay(onMs);
        pixel.clear();
        pixel.show();
        delay(offMs);
    }
}

static uint32_t colorWheel(uint8_t pos) {
    if (pos < 85) {
        return ((255 - pos * 3) << 16) | (pos * 3);
    }
    if (pos < 170) {
        pos -= 85;
        return ((pos * 3) << 8) | (255 - pos * 3);
    }
    pos -= 170;
    return (pos * 3) << 16 | ((255 - pos * 3) << 8);
}

static void runRainbowReminder(uint8_t steps, uint16_t delayMs) {
    for (uint8_t step = 0; step < steps; step++) {
        for (uint16_t i = 0; i < NUM_PIXELS; i++) {
            uint8_t colorIndex = static_cast<uint8_t>((i * 256 / NUM_PIXELS) + step);
            staff.setPixel(i, colorWheel(colorIndex));
        }
        staff.show();
        delay(delayMs);
    }
}

static void logLine(const char *message) {
    Serial.println(message);

    if (!bleLogEnabled || !bleLogCharacteristic || !bleServer) {
        return;
    }
    if (bleServer->getConnectedCount() == 0) {
        return;
    }
    uint32_t now = millis();
    if (now - lastBleLogMs < bleLogIntervalMs) {
        return;
    }

    lastBleLogMs = now;
    bleLogCharacteristic->setValue(message);
    bleLogCharacteristic->notify();
}

static void logLinef(const char *fmt, ...) {
    char buffer[180];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    logLine(buffer);
}

static int imageCount() {
    return static_cast<int>(imageNames.size());
}

static String currentImageName() {
    if (imageNames.empty()) {
        return String();
    }
    if (currentImageIndex < 0 || currentImageIndex >= imageCount()) {
        return String();
    }
    return imageNames[static_cast<size_t>(currentImageIndex)];
}

static void notifyBle(const String &message) {
    if (!bleTxCharacteristic) {
        return;
    }
    bleTxCharacteristic->setValue(message.c_str());
    lastBleResponse = message;
    if (!bleTxCharacteristic->notify()) {
        Serial.print(F("BLE notify skipped: "));
        Serial.println(message);
    }
}

static void notifyStatus() {
    String status = "status paused=";
    status += (staff.paused ? "1" : "0");
    status += " lock=";
    status += (imageLock ? "1" : "0");
    status += " index=";
    status += currentImageIndex;
    status += " name=";
    status += currentImageName();
    notifyBle(status);
}

static void updateBleLock(bool shouldNotify) {
    if (!bleLockCharacteristic) {
        return;
    }
    bleLockCharacteristic->setValue(imageLock ? "1" : "0");
    if (shouldNotify) {
        bleLockCharacteristic->notify();
    }
}

static void updateBleCurrentImage(bool shouldNotify) {
    if (!bleCurrentImageCharacteristic) {
        return;
    }
    String value = String("index=") + currentImageIndex + " name=" + currentImageName();
    bleCurrentImageCharacteristic->setValue(value.c_str());
    if (shouldNotify) {
        bleCurrentImageCharacteristic->notify();
    }
}

static void notifyHelp() {
    notifyBle(F("help: cmds help,list,status,next,prev,pause,resume,lock,unlock; aliases n,p,s,l,u,run,stop; select index:<n>,name:<filename>; test speed:<deg_per_sec> (IMU-less); BLE debug char supports 0/1/on/off and log on/off; note pair/bond required"));
}

static String normalizeOscAddress(const char *address) {
    if (!address) {
        return String();
    }
    String normalized(address);
    while (normalized.startsWith("//")) {
        normalized.remove(0, 1);
    }
    return normalized;
}

static void resumeShow() {
    staff.paused = false;
    setNewImageChange();
}

static void pauseShow() {
    staff.paused = true;
    staff.blank();
}

static void handleOscMessage(OSCMessage &msg) {
    char address[64] = {0};
    msg.getAddress(address);
    String path = normalizeOscAddress(address);
    Serial.print(F("OSC: "));
    Serial.println(path);

    if (path == "/start") {
        if (msg.size() > 0 && (msg.isInt(0) || msg.isFloat(0))) {
            int index = msg.isInt(0) ? msg.getInt(0) : static_cast<int>(msg.getFloat(0));
            if (index > 0) {
                setImageByIndex(index - 1);
            }
        }
        resumeShow();
        return;
    }
    if (path == "/stop" || path == "/standby" || path == "/blackout") {
        pauseShow();
        return;
    }
    if (path == "/random") {
        if (!imageNames.empty()) {
            int count = imageCount();
            int nextIndex = random(count);
            if (count > 1 && nextIndex == currentImageIndex) {
                nextIndex = (nextIndex + 1) % count;
            }
            setImageByIndex(nextIndex);
        }
        return;
    }

    Serial.print(F("OSC unhandled: "));
    Serial.println(path);
}

static void handleOsc() {
    int packetSize = oscUdp.parsePacket();
    if (packetSize <= 0) {
        return;
    }

    OSCMessage msg;
    while (packetSize--) {
        msg.fill(oscUdp.read());
    }

    if (!msg.hasError()) {
        handleOscMessage(msg);
    }
}

static void setupOsc() {
    if (oscUdp.begin(OSC_PORT) == 0) {
        Serial.println(F("OSC UDP begin failed"));
        return;
    }
    oscEnabled = true;
    Serial.print(F("OSC listening on UDP port: "));
    Serial.println(OSC_PORT);
}

static void setupOta() {
    if (strcmp(otaWifiSsid, "YOUR_SSID") == 0) {
        logLine("OTA WiFi not configured");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(otaWifiSsid, otaWifiPassword);
    Serial.print(F("Connecting WiFi for OTA"));

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        logLine("OTA WiFi connect failed");
        return;
    }

    logLinef("OTA WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
    logLinef("OTA WiFi RSSI: %d", WiFi.RSSI());

    String hostname = String("POVStaff");
    if (!MDNS.begin(hostname.c_str())) {
        logLine("Error setting up mDNS responder");
        return;
    }
    logLinef("mDNS responder started: %s.local", hostname.c_str());
    MDNS.addService("arduino", "tcp", 3232);
    logLine("mDNS service added: arduino tcp 3232");
    ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        pixel.setPixelColor(0, BLUE);
        pixel.show();
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        uint8_t level = (total > 0) ? static_cast<uint8_t>((progress * 255UL) / total) : 0;
        pixel.setPixelColor(0, pixel.Color(0, 0, level));
        pixel.show();
    });
    ArduinoOTA.onEnd([]() {
        blinkStatus(GREEN, 3, 80, 80);
        pixel.clear();
        pixel.show();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        (void)error;
        blinkStatus(RED, 5, 80, 80);
        pixel.setPixelColor(0, RED);
        pixel.show();
    });
    logLine("Starting ArduinoOTA...");
    ArduinoOTA.begin();
    otaEnabled = true;
    Serial.print(F("OTA ready as: "));
    Serial.println(hostname);
    Serial.println(F("OTA port: 3232"));
    setupOsc();
}

static bool setImageByIndex(int index) {
    if (imageNames.empty()) {
        return false;
    }
    if (index < 0 || index >= imageCount()) {
        return false;
    }

    bool wasPaused = staff.paused;
    if (wasPaused) {
        staff.paused = false;
    }

    staff.firstImage();
    currentImageIndex = 0;
    for (int i = 0; i < index; i++) {
        staff.nextImage();
        currentImageIndex++;
    }

    if (wasPaused) {
        staff.paused = true;
        staff.blank();
    }

    setNewImageChange();
    updateBleCurrentImage(true);
    return true;
}

static bool advanceImage(int delta) {
    if (imageNames.empty()) {
        return false;
    }
    int count = imageCount();
    int nextIndex = (currentImageIndex + delta) % count;
    if (nextIndex < 0) {
        nextIndex += count;
    }
    return setImageByIndex(nextIndex);
}

static int loadImageList() {
    imageNames.clear();
    File file = LittleFS.open(imageListPath, "r");
    if (!file) {
        Serial.print(F("Failed to open image list: "));
        Serial.println(imageListPath);
        return 0;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        if (line.startsWith("#")) {
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
            imageNames.push_back(filename);
        }
    }

    file.close();
    currentImageIndex = 0;
    return imageCount();
}

static void handleBleCommand(const String &command) {
    String trimmed = command;
    trimmed.trim();
    if (trimmed.length() == 0) {
        return;
    }

    Serial.print(F("BLE command: "));
    Serial.println(trimmed);

    String lower = trimmed;
    lower.toLowerCase();

    if (lower == "next") {
        if (advanceImage(1)) {
            notifyBle(F("ok next"));
        } else {
            notifyBle(F("err no images"));
        }
    } else if (lower == "prev") {
        if (advanceImage(-1)) {
            notifyBle(F("ok prev"));
        } else {
            notifyBle(F("err no images"));
        }
    } else if (lower == "pause") {
        staff.paused = true;
        staff.blank();
        notifyBle(F("ok pause"));
    } else if (lower == "resume") {
        staff.paused = false;
        setNewImageChange();
        notifyBle(F("ok resume"));
    } else if (lower == "lock") {
        imageLock = true;
        updateBleLock(true);
        notifyBle(F("ok lock"));
    } else if (lower == "unlock") {
        imageLock = false;
        setNewImageChange();
        updateBleLock(true);
        notifyBle(F("ok unlock"));
    } else if (lower == "help") {
        notifyHelp();
    } else if (lower == "list") {
        if (imageNames.empty()) {
            notifyBle(F("err no images"));
            return;
        }
        for (size_t i = 0; i < imageNames.size(); i++) {
            String line = "image ";
            line += static_cast<int>(i);
            line += " ";
            line += imageNames[i];
            notifyBle(line);
        }
    } else if (lower.startsWith("index:")) {
        String value = trimmed.substring(6);
        value.trim();
        int index = value.toInt();
        if (setImageByIndex(index)) {
            notifyBle(F("ok index"));
        } else {
            notifyBle(F("err index"));
        }
    } else if (lower.startsWith("name:")) {
        String value = trimmed.substring(5);
        value.trim();
        if (value.length() == 0 || imageNames.empty()) {
            notifyBle(F("err name"));
            return;
        }
        bool found = false;
        for (size_t i = 0; i < imageNames.size(); i++) {
            if (imageNames[i] == value) {
                found = setImageByIndex(static_cast<int>(i));
                break;
            }
        }
        notifyBle(found ? F("ok name") : F("err name"));
    } else if (lower == "status") {
        notifyStatus();
    } else if (lower == "n") {
        handleBleCommand("next");
    } else if (lower == "p") {
        handleBleCommand("prev");
    } else if (lower == "s") {
        handleBleCommand("status");
    } else if (lower == "l") {
        handleBleCommand("lock");
    } else if (lower == "u") {
        handleBleCommand("unlock");
    } else if (lower == "run") {
        handleBleCommand("resume");
    } else if (lower == "stop") {
        handleBleCommand("pause");
    } else if (lower.startsWith("speed:")) {
        String value = trimmed.substring(6);
        value.trim();
        float newSpeed = value.toFloat();
        if (newSpeed <= 0.0f) {
            notifyBle(F("err speed"));
            return;
        }
        if (imuAvailable) {
            notifyBle(F("err speed imuon"));
            return;
        }
        manualSpeed = newSpeed;
        speed = manualSpeed;
        notifyBle(F("ok speed"));
    } else {
        notifyBle(F("err unknown"));
    }
}

class BleServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
        (void)server;
        (void)connInfo;
        Serial.println(F("BLE connected"));
        notifyStatus();
        updateBleCurrentImage(true);
        updateBleLock(true);
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
        (void)server;
        (void)connInfo;
        (void)reason;
        Serial.println(F("BLE disconnected"));
        NimBLEDevice::startAdvertising();
    }
};

class BleRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        if (!connInfo.isEncrypted()) {
            Serial.println(F("BLE write rejected: not encrypted"));
            NimBLEDevice::startSecurity(connInfo.getConnHandle());
            notifyBle(F("err not paired"));
            return;
        }
        std::string value = characteristic->getValue();
        if (value.empty()) {
            return;
        }
        handleBleCommand(String(value.c_str()));
    }
};

class BleActionCallbacks : public NimBLECharacteristicCallbacks {
  public:
    explicit BleActionCallbacks(const String &action) : actionName(action) {}

    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        (void)characteristic;
        if (!connInfo.isEncrypted()) {
            Serial.println(F("BLE write rejected: not encrypted"));
            NimBLEDevice::startSecurity(connInfo.getConnHandle());
            notifyBle(F("err not paired"));
            return;
        }
        if (actionName == "next") {
            handleBleCommand("next");
        } else if (actionName == "prev") {
            handleBleCommand("prev");
        } else if (actionName == "lock") {
            std::string value = characteristic->getValue();
            if (!value.empty() && (value[0] == '0' || value[0] == '1')) {
                imageLock = (value[0] == '1');
                updateBleLock(true);
                notifyBle(imageLock ? F("ok lock") : F("ok unlock"));
            } else {
                handleBleCommand("lock");
            }
        }
    }

  private:
    String actionName;
};

class BleTxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override {
        (void)characteristic;
        (void)connInfo;
        if (subValue) {
            logLine("BLE notifications enabled");
            notifyBle(F("ok subscribed"));
            if (lastBleResponse.length() > 0) {
                notifyBle(lastBleResponse);
            }
        } else {
            logLine("BLE notifications disabled");
        }
    }
};

class BleDebugCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
        (void)characteristic;
        if (!connInfo.isEncrypted()) {
            Serial.println(F("BLE write rejected: not encrypted"));
            NimBLEDevice::startSecurity(connInfo.getConnHandle());
            notifyBle(F("err not paired"));
            return;
        }

        std::string value = characteristic->getValue();
        String trimmed = String(value.c_str());
        trimmed.trim();
        trimmed.toLowerCase();

        if (trimmed.startsWith("log")) {
            String arg = trimmed.substring(3);
            arg.trim();
            if (arg.length() == 0) {
                bleLogEnabled = !bleLogEnabled;
            } else if (arg == "1" || arg == "on" || arg == "true") {
                bleLogEnabled = true;
            } else if (arg == "0" || arg == "off" || arg == "false") {
                bleLogEnabled = false;
            }
            notifyBle(bleLogEnabled ? F("ok log 1") : F("ok log 0"));
            return;
        }

        bool next = imuDebugEnabled;
        if (trimmed.length() == 0) {
            next = !imuDebugEnabled;
        } else if (trimmed == "1" || trimmed == "on" || trimmed == "true") {
            next = true;
        } else if (trimmed == "0" || trimmed == "off" || trimmed == "false") {
            next = false;
        } else {
            next = !imuDebugEnabled;
        }

        imuDebugEnabled = next;
        notifyBle(imuDebugEnabled ? F("ok debug 1") : F("ok debug 0"));
    }
};

static void setupBle() {
    String bleName = String("POVStaff");

    NimBLEDevice::init(bleName.c_str());
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_SC);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new BleServerCallbacks());

    NimBLEService *service = bleServer->createService(bleServiceUuid);
    bleTxCharacteristic = service->createCharacteristic(bleTxUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    bleRxCharacteristic = service->createCharacteristic(bleRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    bleCurrentImageCharacteristic = service->createCharacteristic(bleCurrentImageUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    bleNextImageCharacteristic = service->createCharacteristic(bleNextImageUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    blePrevImageCharacteristic = service->createCharacteristic(blePrevImageUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    bleLockCharacteristic = service->createCharacteristic(bleLockUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    bleDebugCharacteristic = service->createCharacteristic(bleDebugUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    bleLogCharacteristic = service->createCharacteristic(bleLogUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    bleRxCharacteristic->setCallbacks(new BleRxCallbacks());
    bleTxCharacteristic->setCallbacks(new BleTxCallbacks());
    bleNextImageCharacteristic->setCallbacks(new BleActionCallbacks("next"));
    blePrevImageCharacteristic->setCallbacks(new BleActionCallbacks("prev"));
    bleLockCharacteristic->setCallbacks(new BleActionCallbacks("lock"));
    bleDebugCharacteristic->setCallbacks(new BleDebugCallbacks());

    NimBLEDescriptor *currentName = bleCurrentImageCharacteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32);
    currentName->setValue("Current Image");
    NimBLEDescriptor *nextName = bleNextImageCharacteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32);
    nextName->setValue("Next Image");
    NimBLEDescriptor *prevName = blePrevImageCharacteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32);
    prevName->setValue("Previous Image");
    NimBLEDescriptor *lockName = bleLockCharacteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32);
    lockName->setValue("Image Lock");
    NimBLEDescriptor *logName = bleLogCharacteristic->createDescriptor("2901", NIMBLE_PROPERTY::READ, 32);
    logName->setValue("Debug Log");

    service->start();

    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData advData;
    advData.setName(bleName.c_str());
    advData.setCompleteServices(NimBLEUUID(bleServiceUuid));
    advertising->setAdvertisementData(advData);

    NimBLEAdvertisementData scanData;
    scanData.setName(bleName.c_str());
    scanData.setCompleteServices(NimBLEUUID(bleServiceUuid));
    advertising->setScanResponseData(scanData);
    advertising->start();

    Serial.print(F("BLE advertising as: "));
    Serial.println(bleName);
}



void setup() {
    // the usual Serial stuff....
    Serial.begin(115200);
    //deal with on-board neopixel
    pixel.begin();
    pixel.setPixelColor(0,GREEN);
    pixel.show();
    //initialize staff
    Wire.begin();
    staff.begin();
    delay(500);
    //measure battery voltage
    pinMode(PIN_VSENSE, INPUT);
    float voltage = analogReadMilliVolts(PIN_VSENSE)*0.001*2.0;
    staff.showValue(voltage/3.7);
    delay(2000);
    Serial.print("Firmware version: "); Serial.println(FW_VERSION);
    Serial.print("Voltage: "); Serial.println(voltage);
    //clear both the staff and neopixel
    staff.blank();
    pixel.clear();pixel.show();
    // Open LittleFS file system on the flash
    if ( !LittleFS.begin(true) ) {
        Serial.println("Error: filesystem does not exist. Please format LittleFS filesystem");
        while(1) {
            pixel.setPixelColor(0,RED);pixel.show();
            staff.blink();//blink red
        }
    }
    // start the imu
    imuAvailable = imu.init();
    if (!imuAvailable) {
        Serial.println("Failed to detect and initialize IMU!");
        pixel.setPixelColor(0,YELLOW);pixel.show();
        staff.blink(YELLOW);//blink yellow
        Serial.println("Continuing without IMU for testing");
    } else {
        imu.enableDefault();
        Serial.println("IMU Enabled");
        staff.blink(GREEN);
    }
    //now, let us check which mode are we in
    delay(1000);
    if (imuAvailable) {
        imu.read();
    }
    if (imuAvailable && imu.isHorizontal()){
        //we need to go into uploader mode!
        // let us start wifi, web server etc
        WiFi.softAP(ssid, password);
        IPAddress myIP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(myIP);
        if (!MDNS.begin("povstaff")) {
              Serial.println("Error setting up mDNS responder!");
              while(1) {
                  pixel.setPixelColor(0,PURPLE);pixel.show();
                  staff.blink(PURPLE);//blink red
              }
        }
        Serial.println("mDNS responder started");
        server.begin();
        MDNS.addService("http", "tcp", 80);
        Serial.println("Server started");
        setupWebserver();
        filemgr.begin();
        Serial.println("Filemanager started");
        //light up staff in green
        staff.blank();
        for (int i=0; 4*i<NUM_PIXELS;i++){
            staff.setPixel(4*i,GREEN); //set every 4th pixel green
        }
        staff.show();
        pixel.setPixelColor(0,GREEN); pixel.show();
        // start webserver loop
        while(1){
            filemgr.handleClient();
            loopWebServer();
            delay(1);
        }
    } else {
        //normal mode - showing files from the list
        loadImageList();
        staff.addImageList(imageListPath);
        Serial.println("imagelist added");
        pixel.setPixelColor(0,BLUE); pixel.show();
        staff.paused = true;
        if (!imuAvailable) {
            speed = manualSpeed;
        }
        setupOta();
        setupBle();
        updateBleCurrentImage(false);
        updateBleLock(false);
    }
}

//Note that loop will only be used in image show mode.
void loop() {
    if (otaEnabled) {
        ArduinoOTA.handle();
    }
    if (oscEnabled) {
        handleOsc();
    }
    //check IMU - do we need to pause the show?
    if (!imuAvailable) {
        if (!staff.paused) {
            float rotAngle = speed * staff.timeSinceUpdate() * 0.000001;
            if (rotAngle>DEG_PER_LINE) staff.showNextLine();
            if (!imageLock && millis()>nextImageChange) {
                staff.nextImage();
                if (!imageNames.empty()) {
                    currentImageIndex = (currentImageIndex + 1) % imageCount();
                    updateBleCurrentImage(true);
                }
                setNewImageChange();
            }
        }
        return;
    }

    if (millis()-lastIMUcheck > 50 ) {
        //let's check if staff is at rest. To avoid overloading the MCU, we only do it 20 times/sec.
        uint32_t now = millis();
        lastIMUcheck = now;
        imu.read();
        //also, get  rotation speed (in deg/s)
        speed = imu.getSpeed();
        if (imuDebugEnabled && (now - lastImuDebug >= 250)) {
            Serial.print(F("IMU speed="));
            Serial.print(speed);
            Serial.print(F(" a="));
            Serial.print(imu.a.x);
            Serial.print(F(","));
            Serial.print(imu.a.y);
            Serial.print(F(","));
            Serial.print(imu.a.z);
            Serial.print(F(" g="));
            Serial.print(imu.g.x);
            Serial.print(F(","));
            Serial.print(imu.g.y);
            Serial.print(F(","));
            Serial.print(imu.g.z);
            Serial.print(F(" v="));
            Serial.print(imu.isVertical() ? "1" : "0");
            Serial.print(F(" h="));
            Serial.println(imu.isHorizontal() ? "1" : "0");
            lastImuDebug = now;
        }
        bool atRest=(imu.isVertical() && (speed < 30));
        if (staff.paused && (now - lastPause>1000) && !atRest){
            //if staff was paused for more than 1 sec and is now moving again, resume show
            staff.paused = false;
            //move to new image in the list - unless we are just starting and haven't yet shown any images, which can be detected from lastPause value
            if (!imageLock) {
                if (lastPause == 0) {
                    staff.firstImage();
                    currentImageIndex = 0;
                    updateBleCurrentImage(true);
                } else  {
                    staff.nextImage();
                    if (!imageNames.empty()) {
                        currentImageIndex = (currentImageIndex + 1) % imageCount();
                        updateBleCurrentImage(true);
                    }
                }
            }
            //determine when we will need to change the image
            setNewImageChange();
            lastPause = now;
        } else if (!staff.paused && (now - lastPause>1000) && atRest){
            //staff has been active for more than a  second, and now is stopped
            staff.paused = true;
            staff.blank();
            lastPause = now;
        } else if (staff.paused && (now-lastPause>30000)){
            //blink every 30 seconds to remind the user
            runRainbowReminder(24, 30);
            lastPause = now;
        }
    }
    if (!staff.paused){
        float rotAngle = speed * staff.timeSinceUpdate() * 0.000001; //total rotation angle since last loop
        if (rotAngle>DEG_PER_LINE) staff.showNextLine();
        if (!imageLock && millis()>nextImageChange) {
            //time to switch to next image
            staff.nextImage();
            if (!imageNames.empty()) {
                currentImageIndex = (currentImageIndex + 1) % imageCount();
                updateBleCurrentImage(true);
            }
            setNewImageChange();
        }
    }
}

void setNewImageChange(){
    if (staff.currentDuration() == 0) {
        nextImageChange = ULONG_MAX; //maximal possible unsigned 32-bit int
    } else {
        nextImageChange=millis()+staff.currentDuration()*1000;
    }
}
