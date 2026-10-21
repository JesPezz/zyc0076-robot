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
#define LED_PIN 4   // flash/iluminacion del ESP32-CAM, activo en HIGH
#define UNO_RST_PIN 15  // cable al RESET del UNO: la OTA del UNO lo pulsa aqui
                        // (la serigrafia del cabezal no coincide: el pin que
                        //  reinicia de verdad es el 15, comprobado con /unoprobe)

enum : uint8_t {
    O_STOP = 0, O_FWD = 92, O_BACK = 163, O_LEFT = 149, O_RIGHT = 106,
    O_UPLEFT = 20, O_DOWNLEFT = 129, O_UPRIGHT = 72, O_DOWNRIGHT = 34,
    O_CCW = 172, O_CW = 83,
    O_M1 = 25, O_M2 = 26, O_M3 = 27, O_M4 = 28,
    O_SVL = 230, O_SVR = 231, O_SVC = 232,
    O_UNOOTA = 254          // el UNO salta a su bootloader (optiboot) para programarse
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
    // aviso luminoso: 3 destellos del LED de iluminacion (inicio y fin de la OTA)
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH); delay(150);
        digitalWrite(LED_PIN, LOW);  delay(150);
    }
}

// ---------------------------------------------------------------------- red
struct Net { const char* ssid; const char* pass; };
static const Net NETS[] = {
    {"INFINITUM181E", "zKYRN3FFC7"},
    {"INFINITUM181E_EXT", "chonaverijas"},  // repetidor de casa: solo cuando esta al alcance
    {"TP-Link_BF0C", "Chonaverijas02"},
};

#define OTA_HOST "zyc0076"
#define OTA_PASS "zyc0076"

// ------------------------------------------------------------------- estado
ControllerPtr myControllers[BP32_MAX_GAMEPADS];
static uint8_t g_order = O_STOP;
static unsigned long g_remoteMs = 0;  // ultima orden recibida por WiFi
static bool g_unoBusy = false;        // OTA del UNO en curso: no enviar nada al UART0

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
    if (!any && !g_unoBusy && millis() - g_remoteMs > 700) unoOrder(O_STOP);
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
    Serial.println("STR: entrada");
    WiFiClient client = camServer.client();
    client.setNoDelay(true);
    client.print(F("HTTP/1.1 200 OK\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Type: multipart/x-mixed-replace; boundary=" PART_BOUNDARY "\r\n\r\n"));
    int n = 0;
    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { Serial.println("STR: fb NULL"); break; }
        if (n == 0) Serial.printf("STR: fb len=%u fmt=%u %ux%u\n", (unsigned)fb->len, fb->format, fb->width, fb->height);
        uint8_t* jpg = nullptr;
        size_t len = 0;
        bool ok = frame2jpg(fb, 16, &jpg, &len);  // JPEG por software (RGB565); q16 = frames ~30% menores
        esp_camera_fb_return(fb);
        if (n == 0) Serial.printf("STR: frame2jpg=%d len=%u\n", (int)ok, (unsigned)len);
        n++;
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
    // La antena del ESP32-CAM dentro del robot llega muy justa: se elige la red
    // conocida con mejor senal, no la primera de la tabla (un repetidor cercano
    // puede ir mejor que el router).
    const int N = sizeof(NETS) / sizeof(NETS[0]);
    bool used[8] = {};
    int idx[8];
    int cnt = 0;
    int n = WiFi.scanNetworks();
    while (cnt < N) {
        int best = -1, bestRssi = -999;
        for (int i = 0; i < n; i++)
            for (int k = 0; k < N && k < 8; k++)
                if (!used[k] && WiFi.SSID(i) == NETS[k].ssid && WiFi.RSSI(i) > bestRssi) {
                    best = k;
                    bestRssi = WiFi.RSSI(i);
                }
        if (best < 0) break;
        used[best] = true;
        idx[cnt++] = best;
    }
    WiFi.scanDelete();
    for (int k = 0; k < N && k < 8; k++)
        if (!used[k]) idx[cnt++] = k;  // las que no se ven, al final
    for (int j = 0; j < cnt; j++) {
        const Net& n2 = NETS[idx[j]];
        WiFi.begin(n2.ssid, n2.pass);
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
#define FW_VERSION "1.26"
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
// Diagnostico del TLS: separa "no llego" (TCP) de "no negocio" (handshake) y saca
// el error de mbedTLS. Se llama antes y despues de inicializar la camara: si el
// handshake solo falla despues, el culpable es el heap que deja la camara.
static void tlsProbe() {
    WiFiClient plain;
    bool tcp = plain.connect("api.github.com", 443);
    Serial.printf("TLS: TCP=%d heap=%u max=%u\n", (int)tcp, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    if (tcp) plain.stop();

    WiFiClientSecure c;
    c.setInsecure();
    unsigned long t0 = millis();
    bool ok = c.connect("api.github.com", 443);
    char err[160] = {0};
    int e = c.lastError(err, sizeof(err));
    Serial.printf("TLS: handshake=%d en %lums err=%d [%s] heap=%u max=%u\n",
                  (int)ok, millis() - t0, e, err, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    if (ok) c.stop();
}

// GET a la API de GitHub con reintentos. Justo despues de asociarse el DNS o la
// ruta pueden no estar listos y GET() devuelve -1 (que es lo mismo para DNS roto
// que para TCP rechazado), asi que se resuelve el nombre antes y se reintenta.
static bool githubGet(const String& url, String& body) {
    for (int intento = 0; intento < 3; intento++) {
        IPAddress ip;
        if (!WiFi.hostByName("api.github.com", ip)) {
            Serial.printf("OTA: no resuelve api.github.com (intento %d)\n", intento + 1);
            delay(800);
            continue;
        }
        WiFiClientSecure client;
        client.setInsecure();  // ponytail: no valida el certificado (robot personal); pinear la CA si hace falta
        HTTPClient https;
        if (!https.begin(client, url)) { delay(800); continue; }
        https.addHeader("User-Agent", "zyc0076");
        https.addHeader("Accept", "application/vnd.github+json");
        int code = https.GET();
        if (code == HTTP_CODE_OK) {
            body = https.getString();
            https.end();
            return true;
        }
        Serial.printf("OTA: GitHub HTTP %d (intento %d)\n", code, intento + 1);
        https.end();
        delay(800);
    }
    return false;
}

static String releaseBinUrl() {
    static const char TAG_KEY[] = "\"tag_name\":\"";
    static const char URL_KEY[] = "\"browser_download_url\":\"";

    String body;
    String api = String("https://api.github.com/repos/") + GH_REPO + "/releases/latest";
    if (!githubGet(api, body)) return "";

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
    BP32.enableNewBluetoothConnections(false);  // el escaneo BT se come aire del WiFi
    String url = releaseBinUrl();
    if (url.isEmpty()) {
        BP32.enableNewBluetoothConnections(true);
        return;
    }

    Serial.printf("OTA: actualizando desde %s\n", url.c_str());
    unoSend(O_STOP);  // el UNO mantiene la ultima orden: parar antes del bloqueo
    g_order = O_STOP;
    gestoOta();       // aviso: empieza la actualizacion

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, url)) {
        Serial.println("OTA: no se pudo abrir la descarga");
        BP32.enableNewBluetoothConnections(true);
        gestoOta();
        return;
    }
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = https.GET();
    int len = https.getSize();
    if (code != HTTP_CODE_OK || len <= 0) {
        Serial.printf("OTA: descarga HTTP %d, tamano %d\n", code, len);
        https.end();
        BP32.enableNewBluetoothConnections(true);
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
    BP32.enableNewBluetoothConnections(true);  // se acabo la descarga: vuelve el escaneo

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

// Sonar: el UNO manda "D:<cm>" por la misma UART de las ordenes (solo en manual).
static int g_distCm = -1;
static int g_line[3] = {-1, -1, -1};   // sensores de linea A0/A1/A2 del UNO
static void sonarRead() {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            if (line.startsWith("D:")) g_distCm = line.substring(2).toInt();
            else if (line.startsWith("L:")) {
                int a, b, d;
                if (sscanf(line.c_str() + 2, "%d,%d,%d", &a, &b, &d) == 3) {
                    g_line[0] = a; g_line[1] = b; g_line[2] = d;
                }
            }
            line = "";
        } else if (c != '\r' && line.length() < 24) {
            line += c;
        }
    }
}

static void handleDist() {
    uiServer.send(200, "text/plain", String(g_distCm));
}

// 43,512,610,498: distancia en cm y las tres lecturas de linea (A0,A1,A2)
static void handleSens() {
    char buf[48];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%d", g_distCm, g_line[0], g_line[1], g_line[2]);
    uiServer.send(200, "text/plain", buf);
}

static void handleCtrl() {
    if (!uiServer.hasArg("o")) { uiServer.send(400, "text/plain", "falta o"); return; }
    int o = uiServer.arg("o").toInt();
    if (!validOrder(o)) { uiServer.send(400, "text/plain", "orden invalida"); return; }
    g_remoteMs = millis();
    unoOrder((uint8_t)o);
    uiServer.send(200, "text/plain", "ok");
}

// GET /rst?ms=5000 -> mantiene el RESET del UNO en bajo (comprobar el cable con
// el polimetro). Sin argumento, 5 s.
static void handleRst() {
    int ms = uiServer.hasArg("ms") ? uiServer.arg("ms").toInt() : 5000;
    if (ms < 100 || ms > 20000) ms = 5000;
    pinMode(UNO_RST_PIN, OUTPUT);
    digitalWrite(UNO_RST_PIN, LOW);
    uiServer.send(200, "text/plain", "reset del UNO en bajo " + String(ms) + " ms");
    delay(ms);
    pinMode(UNO_RST_PIN, INPUT);
}

// GET /info -> red, IP y senal: para saber a que red se engancha el robot
static void handleInfo() {
    uiServer.send(200, "text/plain",
                  WiFi.SSID() + " " + WiFi.localIP().toString() + " " + String(WiFi.RSSI()) + " dBm");
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

// Programar el UNO por WiFi (Fase 3): para entrar a su bootloader hay que pulsar su
// RESET, y el escudo puede o no llevar esa linea. Solo el bootloader (optiboot)
// contesta al sync STK500 con 0x14 0x10, asi que probar los GPIO libres es concluyente.
static const uint8_t RST_CAND[] = {12, 13, 14, 15, 2};

static bool bootloaderSync(uint8_t pin, int* ms) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(50);                                  // pulso de RESET
    pinMode(pin, INPUT);                        // se suelta: el pull-up del UNO lo sube
    while (Serial.available()) Serial.read();   // tira lo que quede en la linea
    Serial.write(0x30); Serial.write(0x20);     // STK_GET_SYNC
    Serial.flush();
    unsigned long t0 = millis();
    int st = 0;
    while (millis() - t0 < 600) {
        if (!Serial.available()) { delay(1); continue; }
        uint8_t b = Serial.read();
        if (b == 0x14) st = 1;                  // INSYNC
        else if (st == 1 && b == 0x10) { *ms = (int)(millis() - t0); return true; }
        else st = 0;
    }
    return false;
}

// GET /unoprobe -> que GPIO (si alguno) resetea el UNO
// ------------------------------------------ OTA del UNO (STK500v1 por UART0)
// El sketch del UNO lleva la orden 254: imprime "254", se resetea por watchdog y
// su bootloader (optiboot) escucha ~1 s. Aqui se le habla como avrdude: sync,
// load address y prog page de 128 B. Nada de cables: el enlace serie ya existe.
static int stkRead(uint8_t* buf, int n, unsigned ms) {
    unsigned long t0 = millis();
    int got = 0;
    while (got < n && millis() - t0 < ms) {
        if (Serial.available()) buf[got++] = (uint8_t)Serial.read();
        else delay(1);
    }
    return got;
}

// respuesta esperada de optiboot: INSYNC (0x14) + OK (0x10)
static bool stkCmd(const uint8_t* cmd, int len) {
    Serial.write(cmd, len);
    Serial.flush();
    uint8_t r[2];
    if (stkRead(r, 2, 400) != 2) return false;
    return r[0] == 0x14 && r[1] == 0x10;
}

// Bytes que llegan del UNO mientras se busca el bootloader (para el mensaje de error)
static int g_unoRx = 0;

static bool unoEntrarBootloader() {
    g_unoBusy = true;
    g_unoRx = 0;
    // Misma secuencia que /unoprobe, que es la que entra de verdad: parar el
    // keepalive, dejar que el UNO se calle y solo entonces pulsar el RESET.
    // Pulsando en frio (como hacia antes) optiboot no contesta nunca.
    unoOrder(O_STOP);
    delay(50);
    unoSend(O_STOP);
    while (Serial.available()) Serial.read();
    unsigned long t0 = millis();
    while (millis() - t0 < 500) {
        if (Serial.available()) Serial.read();
        else delay(1);
    }
    int ms = 0;
    bool ok = bootloaderSync(UNO_RST_PIN, &ms);
    if (!ok) {  // por si el cable acabo en otro GPIO: la serigrafia del cabezal enganya
        for (unsigned i = 0; i < sizeof(RST_CAND); i++) {
            if (RST_CAND[i] == UNO_RST_PIN) continue;
            if (bootloaderSync(RST_CAND[i], &ms)) { ok = true; break; }
        }
    }
    if (!ok) { g_unoBusy = false; return false; }
    delay(20);
    while (Serial.available()) Serial.read();       // el 0x10 (OK) que sigue al INSYNC
    const uint8_t prog[2] = {0x50, 0x20};          // enter progmode
    return stkCmd(prog, 2);
}

static unsigned g_unoFallo = 0;   // ultima pagina intentada (para el mensaje de error)

static bool unoPagina(unsigned addr, const uint8_t* data) {
    g_unoFallo = addr;
    uint8_t cmd[4] = {0x55, (uint8_t)((addr >> 1) & 0xFF), (uint8_t)((addr >> 9) & 0xFF), 0x20};
    if (!stkCmd(cmd, 4)) return false;
    const uint8_t hdr[3] = {0x64, 0x00, 0x80};     // prog page 128 B: DOS bytes de longitud (STK500v1)
    Serial.write(hdr, 3);
    Serial.write((uint8_t)'F');
    Serial.write(data, 128);
    Serial.write((uint8_t)0x20);
    Serial.flush();
    uint8_t r[2];
    if (stkRead(r, 2, 2000) != 2) return false;
    return r[0] == 0x14 && r[1] == 0x10;
}

static int hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parsea el .hex de Arduino (Intel HEX) y graba pagina a pagina, sin buffer grande.
static bool unoProgramarHex(const String& hex) {
    uint8_t page[128];
    int fill = 0;
    unsigned pageAddr = 0;
    bool eof = false;
    const char* p = hex.c_str();
    size_t i = 0, n = hex.length();
    while (!eof && i + 10 < n) {
        if (p[i] != ':') { i++; continue; }
        int len = hexNib(p[i + 1]) * 16 + hexNib(p[i + 2]);
        unsigned addr = (hexNib(p[i + 3]) << 12) | (hexNib(p[i + 4]) << 8) |
                        (hexNib(p[i + 5]) << 4) | hexNib(p[i + 6]);
        int type = hexNib(p[i + 7]) * 16 + hexNib(p[i + 8]);
        i += 9;
        if (type == 1) { eof = true; break; }      // fin de fichero
        if (type != 0) { i += len * 2 + 2; continue; }
        for (int k = 0; k < len; k++, i += 2) {
            if (fill == 0) pageAddr = addr - (addr % 128);
            page[fill++] = (uint8_t)(hexNib(p[i]) * 16 + hexNib(p[i + 1]));
            if (fill == 128) {
                if (!unoPagina(pageAddr, page)) return false;
                fill = 0;
            }
        }
        i += 2;                                    // checksum
    }
    if (fill) {                                    // ultima pagina incompleta
        memset(page + fill, 0xFF, 128 - fill);
        if (!unoPagina(pageAddr, page)) return false;
    }
    return eof;
}

static String g_unoHex;

static void unoOtaUpload() {
    HTTPUpload& up = uiServer.upload();
    if (up.status == UPLOAD_FILE_START) {
        g_unoHex = "";
        g_unoHex.reserve(24000);                   // el .hex ronda los 18 KB
    } else if (up.status == UPLOAD_FILE_WRITE) {
        g_unoHex.concat((const char*)up.buf, up.currentSize);
    }
}

static void unoOtaDone() {
    if (!g_unoHex.length()) { uiServer.send(400, "text/plain", "sin fichero"); return; }
    String res = "no entra al bootloader (rx=" + String(g_unoRx) + ")";
    if (unoEntrarBootloader()) {
        res = "FALLO al programar (pagina 0x" + String(g_unoFallo, HEX) + ")";
        if (unoProgramarHex(g_unoHex)) {
            const uint8_t leave[2] = {0x51, 0x20};  // leave progmode: arranca el sketch nuevo
            res = stkCmd(leave, 2) ? "OK" : "programado, pero no arranca";
        }
    }
    g_unoBusy = false;
    g_unoHex = "";
    uiServer.send(200, "text/plain", res);
}

static void handleUnoProbe() {
    unoOrder(O_STOP);   // el UNO va a reiniciarse: que no quede marcha puesta
    delay(50);
    unoSend(O_STOP);
    // El UNO imprime "order" por cada paquete que recibe y nosotros mandamos uno
    // cada 200 ms: si aqui no llega nada, el ESP32 no esta oyendo al UNO y el
    // resultado de abajo no seria fiable.
    while (Serial.available()) Serial.read();
    unsigned long t0 = millis();
    unsigned rx = 0;
    while (millis() - t0 < 500) {
        if (Serial.available()) { Serial.read(); rx++; } else delay(1);
    }
    String res = "rx del UNO: " + String(rx) + " bytes en 500 ms\n";
    res += "linea RESET del UNO:\n";
    for (unsigned i = 0; i < sizeof(RST_CAND); i++) {
        int ms = 0;
        if (bootloaderSync(RST_CAND[i], &ms)) {
            res += "GPIO" + String(RST_CAND[i]) + " SI (sync en " + String(ms) + " ms)\n";
            uiServer.send(200, "text/plain", res);
            return;
        }
        res += "GPIO" + String(RST_CAND[i]) + " no\n";
    }
    res += "NINGUNA (hace falta un cable GPIO->RESET del UNO)\n";
    uiServer.send(200, "text/plain", res);
}

// Sonda del camino de vuelta UNO->ESP32: el UNO imprime `order` por cada paquete
// valido y el keepalive manda uno cada 200 ms, asi que el GPIO que este conectado
// a su TX vera flancos. Se miran los GPIO libres (16/17 los usa la PSRAM en QUAD).
static volatile unsigned g_edges = 0;
static void IRAM_ATTR cuentaFlanco() { g_edges++; }

static void handleRxScan() {
    String res = "flancos en 400 ms (el UNO habla cada 200 ms):\n";
    for (unsigned i = 0; i < sizeof(RST_CAND); i++) {
        uint8_t p = RST_CAND[i];
        g_edges = 0;
        pinMode(p, INPUT);  // sin pull-up: no cargamos la linea del UNO
        attachInterrupt(digitalPinToInterrupt(p), cuentaFlanco, FALLING);
        unsigned long t0 = millis();
        while (millis() - t0 < 400) delay(1);
        detachInterrupt(digitalPinToInterrupt(p));
        res += "GPIO" + String(p) + ": " + String(g_edges) + " flancos\n";
    }
    res += "GPIO3 (RX del UART0): " + String(Serial.available()) + " bytes en buffer\n";
    uiServer.send(200, "text/plain", res);
}

// Sonda del CONTENIDO: el UNO imprime `order` (30 0D 0A = "0\r\n") por cada paquete
// valido; nosotros mandamos A5 00 5A. Distingue "me oye el UNO" de "me oigo yo mismo".
static volatile unsigned g_e14 = 0, g_e2 = 0;
static void IRAM_ATTR c14() { g_e14++; }
static void IRAM_ATTR c2() { g_e2++; }

static void handleRxBytes() {
    String res = "";
    pinMode(14, INPUT);
    pinMode(2, INPUT);
    attachInterrupt(digitalPinToInterrupt(14), c14, FALLING);
    attachInterrupt(digitalPinToInterrupt(2), c2, FALLING);
    // A) en reposo: el keepalive no corre (este handler bloquea loop()), asi que
    //    cualquier flanco aqui NO lo causan nuestros paquetes.
    g_e14 = 0; g_e2 = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 400) delay(1);
    res += "reposo 400 ms: GPIO14=" + String(g_e14) + " GPIO2=" + String(g_e2) + "\n";

    // B) un unico paquete al UNO
    while (Serial.available()) Serial.read();
    g_e14 = 0; g_e2 = 0;
    unoOrder(O_STOP);
    delay(10);
    unoSend(O_STOP);
    String hex = "";
    t0 = millis();
    while (millis() - t0 < 400) {
        while (Serial.available()) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", (uint8_t)Serial.read());
            hex += b;
        }
        delay(1);
    }
    detachInterrupt(digitalPinToInterrupt(14));
    detachInterrupt(digitalPinToInterrupt(2));
    res += "tras 1 paquete (A5 00 5A): GPIO14=" + String(g_e14) + " GPIO2=" + String(g_e2) + "\n";
    res += "bytes UART0 (hex): " + (hex.length() ? hex : String("(ninguno)")) + "\n";
    uiServer.send(200, "text/plain", res);
}

// OTA manual: el PC sube el binario con `curl -F firmware=@firmware.bin /update`.
static void httpUpdateDone() {
    BP32.enableNewBluetoothConnections(true);   // si falla, que el mando pueda engancharse
    uiServer.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(300);
    ESP.restart();
}

static void httpUpdateUpload() {
    HTTPUpload& up = uiServer.upload();
    if (up.status == UPLOAD_FILE_START) {
        // sin escaneo BT: la radio es la misma y el aire se reparte
        BP32.enableNewBluetoothConnections(false);
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
    uiServer.on("/dist", HTTP_GET, handleDist);
    uiServer.on("/sens", HTTP_GET, handleSens);
    uiServer.on("/rst", HTTP_GET, handleRst);
    uiServer.on("/ledon", HTTP_GET, handleLed);
    uiServer.on("/ledoff", HTTP_GET, handleLed);
    uiServer.on("/sleep", HTTP_GET, handleSleep);
    uiServer.on("/unoprobe", HTTP_GET, handleUnoProbe);
    uiServer.on("/info", HTTP_GET, handleInfo);
    uiServer.on("/rxscan", HTTP_GET, handleRxScan);
    uiServer.on("/rxbytes", HTTP_GET, handleRxBytes);
    uiServer.on("/updateuno", HTTP_POST, unoOtaDone, unoOtaUpload);
    uiServer.on("/update", HTTP_POST, httpUpdateDone, httpUpdateUpload);
    uiServer.begin();
}

// --------------------------------------------------------------- arduino api
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // arranca apagado: se enciende desde la pagina (LUZ)
    unoSend(O_STOP);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);
    BP32.enableBLEService(false);

    wifiConnect();  // primero la red: lwip debe estar listo antes de la tarea de la camara
    if (WiFi.status() == WL_CONNECTED) {
        tlsProbe();         // antes de la camara: aqui sobra heap
        otaBegin();
        httpBegin();
        otaDesdeRelease();  // busca release solo al arrancar (antes de la camara: el TLS necesita heap)
    }
    g_camOk = cameraInit();
    if (g_camOk) camBegin();
    if (WiFi.status() == WL_CONNECTED) tlsProbe();  // despues de la camara: comparar
}

void loop() {
    if (BP32.update()) processControllers();
    sonarRead();  // distancia del UNO (linea "D:<cm>")

    if (WiFi.status() == WL_CONNECTED) { ArduinoOTA.handle(); uiServer.handleClient(); }

    // reafirma la ultima orden (el UNO la mantiene en bucle)
    static unsigned long last = 0;
    if (!g_unoBusy && millis() - last > 200) { last = millis(); unoSend(g_order); }

    delay(10);
}
