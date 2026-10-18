// ZYC0076 — ESP32-CAM: puente Bluetooth (mando FF01) -> Arduino UNO + WiFi OTA
// El UNO corre el sketch del kit: paquetes de 3 bytes 0xA5 <orden> 0x5A @115200.
#include "sdkconfig.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Update.h>

// ---------------------------------------------------------------- enlace UNO
#define UNO_HDR 0xA5
#define UNO_TAIL 0x5A

enum : uint8_t {
    O_STOP = 0, O_FWD = 92, O_BACK = 163, O_LEFT = 149, O_RIGHT = 106,
    O_UPLEFT = 20, O_DOWNLEFT = 129, O_UPRIGHT = 72, O_DOWNRIGHT = 34,
    O_CCW = 172, O_CW = 83,
    O_M1 = 25, O_M2 = 26, O_M3 = 27, O_M4 = 28,
    O_SVL = 230, O_SVR = 231, O_SVC = 232
};

static inline void unoSend(uint8_t order) {
    uint8_t p[3] = {UNO_HDR, order, UNO_TAIL};
    Serial.write(p, 3);
}

// ---------------------------------------------------------------------- red
struct Net { const char* ssid; const char* pass; };
static const Net NETS[] = {
    {"INFINITUM181E", "zKYRN3FFC7"},
    {"INFINITUM181E_EXT", "chonaverijas"},
    {"TP-Link_BF0C", "Chonaverijas02"},
};

#define OTA_HOST "zyc0076"
#define OTA_PASS "zyc0076"

// ------------------------------------------------------------------- estado
ControllerPtr myControllers[BP32_MAX_GAMEPADS];
static uint8_t g_order = O_STOP;

static inline void unoOrder(uint8_t o) {
    if (o == g_order) return;
    g_order = o;
    unoSend(o);
}

void onConnectedController(ControllerPtr ctl) {
    for (auto& c : myControllers) if (!c) { c = ctl; return; }
}
void onDisconnectedController(ControllerPtr ctl) {
    for (auto& c : myControllers) if (c == ctl) c = nullptr;
    unoOrder(O_STOP);
}

// ------------------------------------------------------------- mando -> orden
#define DEAD 200  // ejes en -511..512

void processGamepad(ControllerPtr ctl) {
    int lx = ctl->axisX(), ly = ctl->axisY(), rx = ctl->axisRX();
    uint8_t o;

    if (ctl->x())                o = O_M2;   // X  -> modelo 2 (obstaculos)
    else if (ctl->y())           o = O_M3;   // Y  -> modelo 3 (seguir)
    else if (ctl->brake() > 500) o = O_M4;   // LT -> modelo 4 (linea)
    else if (ctl->b())           o = O_M1;   // B  -> modelo 1 (manual)
    else if (ctl->a())           o = O_STOP; // A  -> parar
    else if (ly < -DEAD)         o = O_FWD;
    else if (ly > DEAD)          o = O_BACK;
    else if (lx < -DEAD)         o = O_CCW;
    else if (lx > DEAD)          o = O_CW;
    else if (rx < -DEAD)         o = O_LEFT;
    else if (rx > DEAD)          o = O_RIGHT;
    else if (ctl->l1())          o = O_SVL;  // LB -> servo izquierda
    else if (ctl->r1())          o = O_SVR;  // RB -> servo derecha
    else if (ctl->throttle() > 500) o = O_SVC; // RT -> centrar servo
    else                         o = O_STOP;

    unoOrder(o);
}

void processControllers() {
    bool any = false;
    for (auto c : myControllers) {
        if (c && c->isConnected() && c->hasData() && c->isGamepad()) {
            processGamepad(c);
            any = true;
        }
    }
    if (!any) unoOrder(O_STOP);
}

// ----------------------------------------------------------------- wifi/ota
static void wifiConnect() {
    WiFi.mode(WIFI_STA);
    for (const auto& n : NETS) {
        WiFi.begin(n.ssid, n.pass);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
        if (WiFi.status() == WL_CONNECTED) return;
        WiFi.disconnect();
    }
}

static void otaBegin() {
    ArduinoOTA.setHostname(OTA_HOST);
    ArduinoOTA.setPassword(OTA_PASS);
    ArduinoOTA.begin();
}

// ------------------------------------------------------------ OTA por HTTP
// El PC sube el binario al ESP32 (POST /update). No requiere conexion entrante.
static WebServer httpServer(80);

static void httpUpdateDone() {
    httpServer.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(300);
    ESP.restart();
}

static void httpUpdateUpload() {
    HTTPUpload& up = httpServer.upload();
    if (up.status == UPLOAD_FILE_START) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) Update.printError(Serial);
    }
}

static void httpOtaBegin() {
    httpServer.on("/update", HTTP_POST, httpUpdateDone, httpUpdateUpload);
    httpServer.begin();
}

// --------------------------------------------------------------- arduino api
void setup() {
    Serial.begin(115200);
    unoSend(O_STOP);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);
    BP32.enableBLEService(false);

    wifiConnect();
    if (WiFi.status() == WL_CONNECTED) { otaBegin(); httpOtaBegin(); }
}

void loop() {
    if (BP32.update()) processControllers();

    if (WiFi.status() == WL_CONNECTED) { ArduinoOTA.handle(); httpServer.handleClient(); }

    // reafirma la ultima orden (el UNO la mantiene en bucle)
    static unsigned long last = 0;
    if (millis() - last > 200) { last = millis(); unoSend(g_order); }

    delay(20);
}
