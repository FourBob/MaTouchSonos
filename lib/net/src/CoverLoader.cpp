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

#include "ImageOps.h"

namespace net {
namespace {

constexpr size_t kMaxDownloadBytes = 700 * 1024;
constexpr int kMaxDecodeSide = 1600;       // größere Bilder (nach JPEG-Verkleinerung) werden verworfen
constexpr uint16_t kDarkenFactor = 105;    // ≈ 41 % Helligkeit – Text bleibt lesbar
constexpr uint32_t kHttpTimeoutMs = 4000;  // HTTPS-Handshake kann ~1 s dauern

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
            const int scale = progressive ? 8 : app::img::chooseJpegScale(jpeg->getWidth(), jpeg->getHeight(), kCoverSize);
            if (progressive) Serial.println(F("COVER: progressives JPEG – nur unscharfe Vorschau möglich"));
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

/** Lädt `url` in einen neuen PSRAM-Puffer. @return Größe in Bytes, 0 bei Fehler. */
size_t download(const std::string& url, uint8_t*& data, std::string& why) {
    data = nullptr;
    const bool https = url.rfind("https://", 0) == 0;
    WiFiClient plain;
    WiFiClientSecure secure;
    if (https) secure.setInsecure();  // nur öffentliche Bilder, siehe CoverLoader.h

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("MaTouchSonos/1.0");
    const bool begun = https ? http.begin(secure, url.c_str()) : http.begin(plain, url.c_str());
    if (!begun) {
        why = "URL ungültig";
        return 0;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        why = "HTTP " + std::to_string(status);
        http.end();
        return 0;
    }
    const int announced = http.getSize();  // -1 bei „chunked“
    if (announced > static_cast<int>(kMaxDownloadBytes)) {
        why = "zu groß (" + std::to_string(announced / 1024) + " KB)";
        http.end();
        return 0;
    }
    const size_t capacity = announced > 0 ? static_cast<size_t>(announced) : kMaxDownloadBytes;
    data = static_cast<uint8_t*>(psramAlloc(capacity));
    if (!data) {
        why = "kein Speicher (Download)";
        http.end();
        return 0;
    }

    // writeToStream() beachtet Content-Length und „chunked“-Kodierung.
    BufferStream sink(data, capacity);
    const int written = http.writeToStream(&sink);
    http.end();
    if (written < 0 || sink.overflow() || sink.size() == 0 ||
        (announced > 0 && sink.size() != static_cast<size_t>(announced))) {
        why = sink.overflow() ? std::string("zu groß") : "unvollständig (" + std::to_string(sink.size()) + " Bytes, Code " +
                                                          std::to_string(written) + ")";
        heap_caps_free(data);
        data = nullptr;
        return 0;
    }
    return sink.size();
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
    const uint32_t start = millis();
    std::string why;
    uint8_t* data = nullptr;
    const size_t size = download(url, data, why);
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

    app::img::coverResize(img.pixels, img.width, img.height, target, kCoverSize, kCoverSize);
    heap_caps_free(img.pixels);
    app::img::darken(target, static_cast<size_t>(kCoverSize) * kCoverSize, kDarkenFactor);
    Serial.printf("COVER ok: %u KB, %dx%d, Download %lu ms, Aufbereitung %lu ms – %s\n",
                  static_cast<unsigned>(size / 1024), img.width, img.height,
                  static_cast<unsigned long>(downloaded - start), static_cast<unsigned long>(millis() - downloaded),
                  url.c_str());
    return true;
}

void task(void*) {
    uint32_t handledSeq = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

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
    // 12 KB Stack: TLS-Handshake (mbedTLS) braucht einiges.
    xTaskCreatePinnedToCore(task, "cover", 12288, nullptr, 0, &gTask, 0);
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
