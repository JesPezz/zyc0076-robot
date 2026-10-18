// ZYC0076 — ESP32-CAM: puente Bluetooth (mando FF01) -> UNO, control por WiFi,
// camara MJPEG y OTA por release de GitHub. El UNO corre el sketch del kit:
// paquetes de 3 bytes 0xA5 <orden> 0x5A @115200.
#include "sdkconfig.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "driver/ledc.h"
#include "esp_sleep.h"
#include "web.h"

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

// Aviso visible moviendo el servo: barre a un lado, al otro y vuelve al centro.
static void gestoOta() {
    for (int i = 0; i < 25; i++) { unoSend(O_SVL); delay(20); }
    for (int i = 0; i < 50; i++) { unoSend(O_SVR); delay(20); }
    for (int i = 0; i < 25; i++) { unoSend(O_SVL); delay(20); }
    unoSend(O_SVC);
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
static unsigned long g_remoteMs = 0;  // ultima orden recibida por WiFi

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
    // sin mando: solo paramos si nadie manda por WiFi (concesion de 700 ms)
    if (!any && millis() - g_remoteMs > 700) unoOrder(O_STOP);
}

// -------------------------------------------------------------------- camara
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22
#define LED_PIN          4  // flash/iluminacion del ESP32-CAM, activo en HIGH

#define PART_BOUNDARY "123456789000000000000987654321"

static WebServer uiServer(80);   // pagina + control + OTA
static WebServer camServer(81);  // solo el stream MJPEG

static bool g_camOk = false;

static bool cameraInit() {
    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer = LEDC_TIMER_0;
    c.pin_pwdn = PWDN_GPIO_NUM;
    c.pin_reset = RESET_GPIO_NUM;
    c.pin_xclk = XCLK_GPIO_NUM;
    c.pin_sccb_sda = SIOD_GPIO_NUM;
    c.pin_sccb_scl = SIOC_GPIO_NUM;
    c.pin_d7 = Y9_GPIO_NUM;
    c.pin_d6 = Y8_GPIO_NUM;
    c.pin_d5 = Y7_GPIO_NUM;
    c.pin_d4 = Y6_GPIO_NUM;
    c.pin_d3 = Y5_GPIO_NUM;
    c.pin_d2 = Y4_GPIO_NUM;
    c.pin_d1 = Y3_GPIO_NUM;
    c.pin_d0 = Y2_GPIO_NUM;
    c.pin_vsync = VSYNC_GPIO_NUM;
    c.pin_href = HREF_GPIO_NUM;
    c.pin_pclk = PCLK_GPIO_NUM;
    c.xclk_freq_hz = 20000000;
    c.pixel_format = PIXFORMAT_RGB565;  // el sensor GC2145 no tiene JPEG por hardware
    c.frame_size = FRAMESIZE_QVGA;   // 320x240: poco ancho de banda
    c.jpeg_quality = 12;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    return esp_camera_init(&c) == ESP_OK;
}

static void streamHandler() {
    WiFiClient client = camServer.client();
    client.setNoDelay(true);
    client.print(F("HTTP/1.1 200 OK\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Type: multipart/x-mixed-replace; boundary=" PART_BOUNDARY "\r\n\r\n"));
    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) break;
        uint8_t* jpg = nullptr;
        size_t len = 0;
        bool ok = frame2jpg(fb, 16, &jpg, &len);  // JPEG por software (RGB565); q16 = frames ~30% menores
        esp_camera_fb_return(fb);
        if (!ok) break;
        client.printf("--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                      PART_BOUNDARY, (unsigned)len);
        client.write(jpg, len);
        client.print("\r\n");
        free(jpg);
        if (!client.connected()) break;
        delay(1);
    }
    client.stop();
}

static void camTask(void*) {
    for (;;) {
        // sin WiFi no hay lwip: handleClient() antes de tcpip_init dispara un assert
        if (WiFi.status() == WL_CONNECTED) camServer.handleClient();
        delay(2);
    }
}

static void camBegin() {
    camServer.on("/stream", HTTP_GET, streamHandler);
    camServer.begin();
    xTaskCreatePinnedToCore(camTask, "cam", 8192, nullptr, 1, nullptr, 1);
}

// ----------------------------------------------------------------- wifi/ota
static void wifiConnect() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep anade ~100 ms de latencia al stream MJPEG
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

// ------------------------------------------------------------ OTA por release
// Al arrancar se consulta el ultimo release de GitHub; si su tag es mayor que
// FW_VERSION se descarga el asset GH_ASSET y se flashea. No hace falta cable ni
// subir nada al dispositivo: basta publicar un release nuevo.
#define FW_VERSION "1.4"
#define GH_REPO "JesPezz/zyc0076-robot"
#define GH_ASSET "firmware.bin"

// "v1.10" -> 1010, "1.0" -> 1000. Si el tag no parsea devuelve 0 (no actualiza).
static int versionNum(const String& v) {
    int maj = 0, min = 0;
    const char* s = v.c_str();
    if (*s == 'v' || *s == 'V') s++;
    sscanf(s, "%d.%d", &maj, &min);
    return maj * 1000 + min;
}

// Devuelve la URL del asset del ultimo release si es mas nuevo, o "" si no.
static String releaseBinUrl() {
    static const char TAG_KEY[] = "\"tag_name\":\"";
    static const char URL_KEY[] = "\"browser_download_url\":\"";

    WiFiClientSecure client;
    client.setInsecure();  // ponytail: no valida el certificado (robot personal); pinear la CA si hace falta

    HTTPClient https;
    String api = String("https://api.github.com/repos/") + GH_REPO + "/releases/latest";
    if (!https.begin(client, api)) return "";
    https.addHeader("User-Agent", "zyc0076");
    https.addHeader("Accept", "application/vnd.github+json");

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("OTA: GitHub HTTP %d\n", code);
        https.end();
        return "";
    }
    String body = https.getString();
    https.end();

    int p = body.indexOf(TAG_KEY);
    if (p < 0) return "";
    p += strlen(TAG_KEY);
    String tag = body.substring(p, body.indexOf('"', p));
    if (versionNum(tag) <= versionNum(FW_VERSION)) {
        Serial.printf("OTA: al dia (%s <= v%s)\n", tag.c_str(), FW_VERSION);
        return "";
    }

    for (int q = body.indexOf(URL_KEY); q >= 0; q = body.indexOf(URL_KEY, q + 1)) {
        q += strlen(URL_KEY);
        String u = body.substring(q, body.indexOf('"', q));
        if (u.endsWith(GH_ASSET)) return u;
    }
    Serial.printf("OTA: release %s sin %s\n", tag.c_str(), GH_ASSET);
    return "";
}

static void otaDesdeRelease() {
    String url = releaseBinUrl();
    if (url.isEmpty()) return;

    Serial.printf("OTA: actualizando desde %s\n", url.c_str());
    unoSend(O_STOP);  // el UNO mantiene la ultima orden: parar antes del bloqueo
    g_order = O_STOP;
    gestoOta();       // aviso: empieza la actualizacion

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, url)) {
        Serial.println("OTA: no se pudo abrir la descarga");
        gestoOta();
        return;
    }
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = https.GET();
    int len = https.getSize();
    if (code != HTTP_CODE_OK || len <= 0) {
        Serial.printf("OTA: descarga HTTP %d, tamano %d\n", code, len);
        https.end();
        gestoOta();
        return;
    }

    bool ok = false;
    if (Update.begin(len)) {
        size_t written = Update.writeStream(*https.getStreamPtr());
        ok = (written == (size_t)len) && Update.end();
        if (!ok) Serial.printf("OTA: escrito %u/%d (%s)\n", (unsigned)written, len, Update.errorString());
    } else {
        Serial.printf("OTA: Update.begin fallo (%s)\n", Update.errorString());
    }
    https.end();

    gestoOta();  // aviso: termino la actualizacion
    if (ok) {
        Serial.println("OTA: OK, reiniciando");
        delay(200);
        ESP.restart();
    }
}

// ----------------------------------- control por WiFi + OTA manual por HTTP
static bool validOrder(int o) {
    switch (o) {
        case O_STOP: case O_FWD: case O_BACK: case O_LEFT: case O_RIGHT:
        case O_UPLEFT: case O_DOWNLEFT: case O_UPRIGHT: case O_DOWNRIGHT:
        case O_CCW: case O_CW:
        case O_M1: case O_M2: case O_M3: case O_M4:
        case O_SVL: case O_SVR: case O_SVC:
            return true;
        default:
            return false;
    }
}

static void handleCtrl() {
    if (!uiServer.hasArg("o")) { uiServer.send(400, "text/plain", "falta o"); return; }
    int o = uiServer.arg("o").toInt();
    if (!validOrder(o)) { uiServer.send(400, "text/plain", "orden invalida"); return; }
    g_remoteMs = millis();
    unoOrder((uint8_t)o);
    uiServer.send(200, "text/plain", "ok");
}

static void handleRoot() {
    String html = FPSTR(PAGE_HTML);
    html.replace("%V%", FW_VERSION);  // la pagina muestra v1.4, v1.5, ...
    uiServer.send(200, "text/html", html);
}

// Iluminacion: /ledon y /ledoff (GET). Los mismos nombres que ya usaba robot.py.
static void handleLed() {
    bool on = uiServer.uri() == "/ledon";
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    uiServer.send(200, "text/plain", "ok");
}

// Reposo: /sleep?m=N para los motores y duerme el ESP32 N minutos (1..180, 10 por
// defecto). Al agotarse el temporizador reinicia solo: WiFi, camara y OTA ya van en
// setup(). Mientras duerme no responde: el deep sleep apaga la radio.
static void handleSleep() {
    int m = uiServer.hasArg("m") ? uiServer.arg("m").toInt() : 10;
    if (m < 1) m = 1;
    if (m > 180) m = 180;
    unoOrder(O_STOP);   // que el UNO no se quede con la ultima marcha
    delay(50);
    unoSend(O_STOP);
    digitalWrite(LED_PIN, LOW);
    char msg[24];
    snprintf(msg, sizeof(msg), "durmiendo %d min", m);
    uiServer.send(200, "text/plain", msg);
    uiServer.client().flush();
    delay(300);         // que salga la respuesta antes de apagar
    esp_sleep_enable_timer_wakeup((uint64_t)m * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

// OTA manual: el PC sube el binario con `curl -F firmware=@firmware.bin /update`.
static void httpUpdateDone() {
    uiServer.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(300);
    ESP.restart();
}

static void httpUpdateUpload() {
    HTTPUpload& up = uiServer.upload();
    if (up.status == UPLOAD_FILE_START) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) Update.printError(Serial);
    }
}

static void httpBegin() {
    uiServer.on("/", HTTP_GET, handleRoot);
    uiServer.on("/c", HTTP_GET, handleCtrl);
    uiServer.on("/ledon", HTTP_GET, handleLed);
    uiServer.on("/ledoff", HTTP_GET, handleLed);
    uiServer.on("/sleep", HTTP_GET, handleSleep);
    uiServer.on("/update", HTTP_POST, httpUpdateDone, httpUpdateUpload);
    uiServer.begin();
}

// --------------------------------------------------------------- arduino api
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    unoSend(O_STOP);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);
    BP32.enableBLEService(false);

    wifiConnect();  // primero la red: lwip debe estar listo antes de la tarea de la camara
    g_camOk = cameraInit();
    if (g_camOk) camBegin();
    if (WiFi.status() == WL_CONNECTED) {
        otaBegin();
        httpBegin();
        otaDesdeRelease();  // busca release solo al arrancar
    }
}

void loop() {
    if (BP32.update()) processControllers();

    if (WiFi.status() == WL_CONNECTED) { ArduinoOTA.handle(); uiServer.handleClient(); }

    // reafirma la ultima orden (el UNO la mantiene en bucle)
    static unsigned long last = 0;
    if (millis() - last > 200) { last = millis(); unoSend(g_order); }

    delay(10);
}
