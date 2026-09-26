#include "CoverLoader.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <new>

#include "AlbumArt.h"
#include "ImageOps.h"

// stb_image: nur die Deklarationen, die Umsetzung steckt in StbImage.cpp
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

namespace net {
namespace {

constexpr size_t kMaxDownloadBytes = 700 * 1024;
constexpr int kMaxDecodeSide = 1600;       // größere Bilder (nach JPEG-Verkleinerung) werden verworfen
// Progressive JPEGs voll dekodieren (stb_image) bis zu dieser Pixelzahl. Speicherbedarf ~10 Byte je
// Pixel im PSRAM (Koeffizienten + RGB), bei 800×800 also ~6 MB – darüber nur die 1/8-Vorschau.
constexpr int kMaxProgressivePixels = 640 * 640;
constexpr uint16_t kDarkenFactor = 105;    // ≈ 41 % Helligkeit – Text bleibt lesbar
constexpr uint32_t kHttpTimeoutMs = 4000;  // HTTPS-Handshake kann ~1 s dauern
// Bild-Proxy des Speakers (/getaa, Port 1400): Der Speaker lädt das Bild erst selbst beim Dienst –
// bei Podcasts (oft 3000 px) dauerte das im Geräte-Test länger als 4 s.
constexpr uint32_t kSpeakerArtTimeoutMs = 12000;
constexpr int kSonosPort = 1400;
constexpr uint32_t kKeepOpenMs = 60000;    // offene Verbindung so lange für den nächsten Titel behalten
constexpr int kMaxRedirects = 3;

// --- Auftrag (von der Sonos-Task gesetzt) -------------------------------------
SemaphoreHandle_t gRequestMutex = nullptr;
std::vector<std::string> gRequested;  // geschützt durch gRequestMutex
uint32_t gRequestSeq = 0;             // steigt bei jedem neuen Auftrag
TaskHandle_t gTask = nullptr;

// --- Ergebnis (Doppelpuffer) --------------------------------------------------
uint16_t* gBuffers[2] = {nullptr, nullptr};
std::atomic<uint32_t> gPublishedVersion{0};
std::atomic<int> gPublishedBuffer{-1};  // -1 = kein Cover
std::atomic<uint32_t> gAckedVersion{0};

// --- Dekodieren ---------------------------------------------------------------
struct DecodeTarget {
    uint16_t* pixels;
    int width;
    int height;
};

int onJpegBlock(JPEGDRAW* d) {
    auto* t = static_cast<DecodeTarget*>(d->pUser);
    for (int row = 0; row < d->iHeight; ++row) {
        const int y = d->y + row;
        if (y >= t->height) break;
        const int w = (d->x + d->iWidth > t->width) ? t->width - d->x : d->iWidth;
        if (w <= 0) continue;
        memcpy(t->pixels + static_cast<size_t>(y) * t->width + d->x, d->pPixels + row * d->iWidth, w * 2);
    }
    return 1;
}

PNG* gPng = nullptr;  // groß (~50 KB), deshalb einmalig im PSRAM

int onPngLine(PNGDRAW* d) {
    auto* t = static_cast<DecodeTarget*>(d->pUser);
    if (d->y >= t->height) return 1;
    // Transparenz (Senderlogos) auf Schwarz legen
    gPng->getLineAsRGB565(d, t->pixels + static_cast<size_t>(d->y) * t->width, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    return 1;
}

void* psramAlloc(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

/** Progressives JPEG vollständig dekodieren (stb_image). @return false → Aufrufer nimmt die 1/8-Vorschau. */
bool decodeProgressive(const uint8_t* data, size_t size, DecodeTarget& out, std::string& why) {
    int w = 0, h = 0, channels = 0;
    uint8_t* rgb = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 3);
    if (!rgb) {
        why = std::string("stb_image: ") + stbi_failure_reason();
        return false;
    }
    out.width = w;
    out.height = h;
    out.pixels = static_cast<uint16_t*>(psramAlloc(static_cast<size_t>(w) * h * 2));
    if (out.pixels) app::img::rgb888To565(rgb, out.pixels, static_cast<size_t>(w) * h);
    stbi_image_free(rgb);
    if (!out.pixels) why = "kein Speicher (Bild)";
    return out.pixels != nullptr;
}

/** Dekodiert `data` in einen neu angelegten RGB565-Puffer (Aufrufer gibt ihn frei). */
bool decode(uint8_t* data, size_t size, DecodeTarget& out, std::string& why) {
    const app::img::Format fmt = app::img::detectFormat(data, size);
    if (fmt == app::img::Format::Jpeg) {
        auto* jpeg = static_cast<JPEGDEC*>(psramAlloc(sizeof(JPEGDEC)));
        if (!jpeg) {
            why = "kein Speicher (JPEG)";
            return false;
        }
        new (jpeg) JPEGDEC();
        bool ok = false;
        if (jpeg->openRAM(data, static_cast<int>(size), onJpegBlock)) {
            // Progressive JPEGs dekodiert JPEGDEC nur als Vorschau in 1/8-Größe (erzwingt das intern) –
            // Puffer und Optionen müssen dazu passen, sonst landet ein Mini-Bild in der Ecke.
            const bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
            if (progressive && jpeg->getWidth() * jpeg->getHeight() <= kMaxProgressivePixels) {
                jpeg->close();
                jpeg->~JPEGDEC();
                heap_caps_free(jpeg);
                if (decodeProgressive(data, size, out, why)) return true;
                Serial.printf("COVER: progressives JPEG nicht voll dekodierbar (%s) – nehme die Vorschau\n", why.c_str());
                jpeg = static_cast<JPEGDEC*>(psramAlloc(sizeof(JPEGDEC)));
                if (!jpeg) {
                    why = "kein Speicher (JPEG)";
                    return false;
                }
                new (jpeg) JPEGDEC();
                if (!jpeg->openRAM(data, static_cast<int>(size), onJpegBlock)) {
                    why = "JPEG nicht lesbar";
                    jpeg->~JPEGDEC();
                    heap_caps_free(jpeg);
                    return false;
                }
            }
            const int scale = progressive ? 8 : app::img::chooseJpegScale(jpeg->getWidth(), jpeg->getHeight(), kCoverSize);
            if (progressive) Serial.println(F("COVER: großes progressives JPEG – nur unscharfe Vorschau (1/8)"));
            out.width = jpeg->getWidth() / scale;
            out.height = jpeg->getHeight() / scale;
            if (out.width > kMaxDecodeSide || out.height > kMaxDecodeSide || out.width <= 0 || out.height <= 0) {
                why = "Bild zu groß";
            } else if ((out.pixels = static_cast<uint16_t*>(psramAlloc(static_cast<size_t>(out.width) * out.height * 2)))) {
                memset(out.pixels, 0, static_cast<size_t>(out.width) * out.height * 2);
                jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
                jpeg->setUserPointer(&out);
                const int options = scale == 2 ? JPEG_SCALE_HALF : scale == 4 ? JPEG_SCALE_QUARTER
                                  : scale == 8 ? JPEG_SCALE_EIGHTH : 0;
                ok = jpeg->decode(0, 0, options) == 1;
                if (!ok) why = "JPEG-Fehler " + std::to_string(jpeg->getLastError());
            } else {
                why = "kein Speicher (Bild)";
            }
            jpeg->close();
        } else {
            why = "JPEG nicht lesbar";
        }
        jpeg->~JPEGDEC();
        heap_caps_free(jpeg);
        return ok;
    }
    if (fmt == app::img::Format::Png) {
        if (!gPng) {
            gPng = static_cast<PNG*>(psramAlloc(sizeof(PNG)));
            if (!gPng) {
                why = "kein Speicher (PNG)";
                return false;
            }
            new (gPng) PNG();
        }
        bool ok = false;
        if (gPng->openRAM(data, static_cast<int>(size), onPngLine) == PNG_SUCCESS) {
            out.width = gPng->getWidth();
            out.height = gPng->getHeight();
            if (out.width > kMaxDecodeSide || out.height > kMaxDecodeSide || out.width <= 0 || out.height <= 0) {
                why = "Bild zu groß";
            } else if ((out.pixels = static_cast<uint16_t*>(psramAlloc(static_cast<size_t>(out.width) * out.height * 2)))) {
                ok = gPng->decode(&out, 0) == PNG_SUCCESS;
                if (!ok) why = "PNG-Fehler " + std::to_string(gPng->getLastError());
            } else {
                why = "kein Speicher (Bild)";
            }
            gPng->close();
        } else {
            why = "PNG nicht lesbar";
        }
        return ok;
    }
    why = "unbekanntes Bildformat";
    return false;
}

// --- Herunterladen --------------------------------------------------------------

/** Nimmt die heruntergeladenen Bytes auf (PSRAM, feste Obergrenze). */
class BufferStream : public Stream {
public:
    BufferStream(uint8_t* buf, size_t capacity) : buf_(buf), capacity_(capacity) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* data, size_t len) override {
        if (size_ + len > capacity_) {
            overflow_ = true;
            len = capacity_ - size_;
        }
        memcpy(buf_ + size_, data, len);
        size_ += len;
        return len;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t size() const { return size_; }
    bool overflow() const { return overflow_; }

private:
    uint8_t* buf_;
    size_t capacity_;
    size_t size_ = 0;
    bool overflow_ = false;
};

// Offene Verbindungen (je eine für HTTP und HTTPS) bleiben nach dem Download bestehen:
// Beim nächsten Cover vom selben Server entfällt der Verbindungsaufbau – bei HTTPS
// (Spotify, TuneIn) spart das den TLS-Handshake. Nur die Cover-Task greift darauf zu.
struct Connection {
    WiFiClient* client;
    std::string key;        // „host:port“ der offenen Verbindung
    uint32_t lastUse = 0;
};
WiFiClient gPlainClient;
WiFiClientSecure gSecureClient;
Connection gPlain{&gPlainClient};
Connection gSecure{&gSecureClient};
HTTPClient gHttp;  // bleibt bestehen – der Destruktor würde die Verbindung schließen

void closeConnection(Connection& c) {
    c.client->stop();
    c.key.clear();
}

/** Schließt Verbindungen, die länger nicht gebraucht wurden (gibt v. a. den TLS-Speicher frei). */
void closeIdleConnections() {
    for (Connection* c : {&gPlain, &gSecure}) {
        if (!c->key.empty() && millis() - c->lastUse > kKeepOpenMs) closeConnection(*c);
    }
}

/** Zeiten eines Downloads fürs Log. */
struct DownloadStats {
    uint32_t connectMs = 0;   // Verbindungsaufbau inkl. TLS-Handshake
    uint32_t transferMs = 0;  // Anfrage bis letztes Byte
    bool reused = false;      // bestehende Verbindung genutzt
};

/**
 * Sorgt für eine offene Verbindung zum Server von `url`. Eine Verbindung zu einem anderen
 * Server wird vorher geschlossen – der HTTPClient würde sie sonst ungeprüft weiterverwenden.
 */
Connection* connectTo(const sonos::art::UrlParts& u, DownloadStats& stats, std::string& why) {
    Connection& c = u.https ? gSecure : gPlain;
    const std::string key = u.host + ":" + std::to_string(u.port);
    if (c.key == key && c.client->connected()) {
        stats.reused = true;
        return &c;
    }
    closeConnection(c);
    const uint32_t start = millis();
    if (!c.client->connect(u.host.c_str(), static_cast<uint16_t>(u.port), static_cast<int32_t>(kHttpTimeoutMs))) {
        why = "keine Verbindung zu " + u.host;
        return nullptr;
    }
    stats.connectMs += millis() - start;
    c.key = key;
    return &c;
}

/** Lädt `url` in einen neuen PSRAM-Puffer. @return Größe in Bytes, 0 bei Fehler. */
size_t download(const std::string& firstUrl, uint8_t*& data, DownloadStats& stats, std::string& why) {
    data = nullptr;
    std::string url = firstUrl;
    bool retried = false;

    for (int redirects = 0;;) {
        sonos::art::UrlParts parts;
        if (!sonos::art::splitUrl(url, parts)) {
            why = "URL ungültig";
            return 0;
        }
        Connection* conn = connectTo(parts, stats, why);
        if (!conn) return 0;
        // Lese-Timeout je Anfrage (auch bei wiederverwendeter Verbindung). Die Sekunden-Angabe für den
        // Client setzt sonst HTTPClient::connect(), das bei offener Verbindung übersprungen wird.
        const uint32_t timeoutMs = parts.port == kSonosPort ? kSpeakerArtTimeoutMs : kHttpTimeoutMs;
        gHttp.setTimeout(static_cast<uint16_t>(timeoutMs));
        conn->client->setTimeout((timeoutMs + 500) / 1000);

        if (!gHttp.begin(*conn->client, url.c_str())) {
            why = "URL ungültig";
            return 0;
        }
        const uint32_t requested = millis();
        const int status = gHttp.GET();
        if (status < 0 && stats.reused && !retried) {
            // Der Server hat die offene Verbindung inzwischen geschlossen – einmal neu verbinden.
            closeConnection(*conn);
            retried = true;
            stats.reused = false;
            continue;
        }
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            // Weiterleitungen selbst verfolgen: Der Server kann wechseln, die Verbindung dann auch.
            const std::string next = sonos::art::resolveRedirect(url, gHttp.getLocation().c_str());
            closeConnection(*conn);  // Antworttext der Weiterleitung nicht mitlesen
            if (next.empty() || ++redirects > kMaxRedirects) {
                why = "Weiterleitung nicht verfolgbar";
                return 0;
            }
            url = next;
            continue;
        }
        if (status != HTTP_CODE_OK) {
            why = "HTTP " + std::to_string(status);
            closeConnection(*conn);
            return 0;
        }

        const int announced = gHttp.getSize();  // -1 bei „chunked“
        if (announced > static_cast<int>(kMaxDownloadBytes)) {
            why = "zu groß (" + std::to_string(announced / 1024) + " KB)";
            closeConnection(*conn);
            return 0;
        }
        const size_t capacity = announced > 0 ? static_cast<size_t>(announced) : kMaxDownloadBytes;
        data = static_cast<uint8_t*>(psramAlloc(capacity));
        if (!data) {
            why = "kein Speicher (Download)";
            closeConnection(*conn);
            return 0;
        }

        // writeToStream() beachtet Content-Length und „chunked“-Kodierung.
        BufferStream sink(data, capacity);
        const int written = gHttp.writeToStream(&sink);
        stats.transferMs = millis() - requested;
        if (written < 0 || sink.overflow() || sink.size() == 0 ||
            (announced > 0 && sink.size() != static_cast<size_t>(announced))) {
            why = sink.overflow() ? std::string("zu groß")
                                  : "unvollständig (" + std::to_string(sink.size()) + " Bytes, Code " +
                                        std::to_string(written) + ")";
            closeConnection(*conn);
            heap_caps_free(data);
            data = nullptr;
            return 0;
        }
        // Antwort vollständig gelesen: end() lässt die Verbindung offen, sofern der Server
        // „keep-alive“ erlaubt (sonst schließt der HTTPClient sie selbst).
        gHttp.end();
        conn->lastUse = millis();
        if (!conn->client->connected()) conn->key.clear();
        return sink.size();
    }
}

// --- Task -------------------------------------------------------------------------

void publish(int buffer) {
    gPublishedBuffer.store(buffer);
    gPublishedVersion.fetch_add(1);
}

/** Wartet, bis die UI die zuletzt veröffentlichte Version übernommen hat (max. 2 s). */
void waitForAck() {
    const uint32_t start = millis();
    while (gAckedVersion.load() != gPublishedVersion.load() && millis() - start < 2000) vTaskDelay(pdMS_TO_TICKS(10));
}

bool loadInto(const std::string& url, uint16_t* target) {
    std::string why;
    uint8_t* data = nullptr;
    DownloadStats stats;
    const size_t size = download(url, data, stats, why);
    if (size == 0) {
        Serial.printf("COVER %s – Download fehlgeschlagen: %s\n", url.c_str(), why.c_str());
        return false;
    }
    const uint32_t downloaded = millis();

    DecodeTarget img{nullptr, 0, 0};
    const bool ok = decode(data, size, img, why);
    heap_caps_free(data);
    if (!ok) {
        if (img.pixels) heap_caps_free(img.pixels);
        Serial.printf("COVER %s – nicht darstellbar: %s\n", url.c_str(), why.c_str());
        return false;
    }

    const uint32_t decoded = millis();
    app::img::coverResize(img.pixels, img.width, img.height, target, kCoverSize, kCoverSize, kDarkenFactor);
    heap_caps_free(img.pixels);
    Serial.printf("COVER ok: %u KB, %dx%d, Verbindung %s, Übertragung %lu ms, Dekodieren %lu ms, "
                  "Skalieren %lu ms – %s\n",
                  static_cast<unsigned>(size / 1024), img.width, img.height,
                  stats.reused ? "wiederverwendet" : (std::to_string(stats.connectMs) + " ms").c_str(),
                  static_cast<unsigned long>(stats.transferMs), static_cast<unsigned long>(decoded - downloaded),
                  static_cast<unsigned long>(millis() - decoded), url.c_str());
    return true;
}

void task(void*) {
    gSecureClient.setInsecure();  // nur öffentliche Bilder, siehe CoverLoader.h
    gSecureClient.setHandshakeTimeout(kHttpTimeoutMs / 1000);
    gHttp.setReuse(true);
    gHttp.setConnectTimeout(kHttpTimeoutMs);
    gHttp.setTimeout(kHttpTimeoutMs);
    gHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // selbst verfolgt, siehe download()
    gHttp.setUserAgent("MaTouchSonos/1.0");

    uint32_t handledSeq = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        closeIdleConnections();

        xSemaphoreTake(gRequestMutex, portMAX_DELAY);
        const uint32_t seq = gRequestSeq;
        const std::vector<std::string> urls = gRequested;
        xSemaphoreGive(gRequestMutex);
        if (seq == handledSeq) continue;
        handledSeq = seq;

        if (urls.empty()) {
            publish(-1);
            continue;
        }
        if (WiFi.status() != WL_CONNECTED) {
            handledSeq = 0;  // später erneut versuchen
            continue;
        }

        waitForAck();
        const int shown = gPublishedBuffer.load();
        const int back = shown == 0 ? 1 : 0;

        bool loaded = false;
        for (const auto& url : urls) {
            if (gRequestSeq != seq) break;  // inzwischen neuer Titel – abbrechen
            if (loadInto(url, gBuffers[back])) {
                loaded = true;
                break;
            }
        }
        if (gRequestSeq != seq) continue;
        publish(loaded ? back : -1);
    }
}

}  // namespace

void CoverLoader::begin() {
    const size_t bytes = static_cast<size_t>(kCoverSize) * kCoverSize * 2;
    gBuffers[0] = static_cast<uint16_t*>(psramAlloc(bytes));
    gBuffers[1] = static_cast<uint16_t*>(psramAlloc(bytes));
    if (!gBuffers[0] || !gBuffers[1]) {
        Serial.println(F("COVER: kein PSRAM für die Bildpuffer – Cover deaktiviert"));
        return;
    }
    gRequestMutex = xSemaphoreCreateMutex();
    // Niedrigere Priorität als die Sonos-Task: Bedienung und Abfragen gehen vor.
    // 16 KB Stack: TLS-Handshake (mbedTLS) und stb_image brauchen einiges.
    xTaskCreatePinnedToCore(task, "cover", 16384, nullptr, 0, &gTask, 0);
}

void CoverLoader::request(const std::vector<std::string>& candidates) {
    if (!gRequestMutex) return;
    xSemaphoreTake(gRequestMutex, portMAX_DELAY);
    const bool changed = candidates != gRequested;
    if (changed) {
        gRequested = candidates;
        gRequestSeq++;
    }
    xSemaphoreGive(gRequestMutex);
    if (changed && gTask) xTaskNotifyGive(gTask);
}

bool CoverLoader::takeCover(uint32_t lastVersion, CoverFrame& out) {
    const uint32_t v = gPublishedVersion.load();
    if (v == lastVersion) return false;
    const int buffer = gPublishedBuffer.load();
    out.version = v;
    out.pixels = buffer >= 0 ? gBuffers[buffer] : nullptr;
    return true;
}

void CoverLoader::acknowledge(uint32_t version) { gAckedVersion.store(version); }

}  // namespace net
