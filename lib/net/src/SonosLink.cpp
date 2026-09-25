#include "SonosLink.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "AVTransport.h"
#include "AlbumArt.h"
#include "CoverLoader.h"
#include "Favorites.h"
#include "NowPlaying.h"
#include "RenderingControl.h"
#include "Soap.h"
#include "Topology.h"

namespace net {
namespace {

constexpr uint16_t kSonosPort = 1400;
constexpr uint32_t kHttpConnectTimeoutMs = 1000;
constexpr uint16_t kHttpTimeoutMs = 1200;        // Speaker antwortet normal in 30–60 ms
constexpr uint32_t kPollIntervalMs = 1500;       // Zustand, Titel, Lautstärke abfragen
constexpr uint32_t kPollAfterCommandMs = 400;    // nach einem Befehl schnell bestätigen
constexpr uint32_t kQuickRetryMs = 500;          // nach einem einzelnen Aussetzer
constexpr uint32_t kRetryIntervalMs = 5000;      // wenn der Speaker als nicht erreichbar gilt
constexpr uint32_t kTopologyIntervalMs = 30000;  // Räume/Gruppen neu lesen
constexpr uint32_t kDiscoveryRetryMs = 5000;     // Anlage nicht gefunden: erneut suchen
constexpr uint32_t kSsdpWaitMs = 1500;
constexpr uint16_t kSsdpLocalPort = 50505;
constexpr int kFailuresBeforeError = 3;          // so viele Aussetzer in Folge bis zur Fehlermeldung
constexpr uint32_t kWifiCheckMs = 500;
constexpr int kUpnpTransitionNotAvailable = 701;
constexpr uint16_t kBrowseTimeoutMs = 3000;      // Favoritenliste kann einige 10 KB groß sein
constexpr int kFavoritesPageSize = 50;
constexpr uint32_t kFavoritesMinIntervalMs = 10000;  // Favoriten höchstens so oft neu lesen

const char* gSsid = nullptr;
const char* gPassword = nullptr;
std::string gStartRoomName;
std::string gInitialRoomUuid;
std::string gFallbackIp;

struct Command {
    Transport type;
    int value;  // Seek: Zielposition in Sekunden
};
struct RoomRequest {
    char uuid[40];
};
struct FavoriteRequest {
    int index;
    char title[64];
};

QueueHandle_t gVolumeQueue = nullptr;     // int, Länge 1 (xQueueOverwrite)
QueueHandle_t gTransportQueue = nullptr;  // Command, Länge 4
QueueHandle_t gRoomQueue = nullptr;       // RoomRequest, Länge 1 (xQueueOverwrite)
QueueHandle_t gEventQueue = nullptr;      // Event, Länge 16
QueueHandle_t gFavoriteQueue = nullptr;   // FavoriteRequest, Länge 1 (xQueueOverwrite)
std::atomic<bool> gFavoritesRefresh{false};

SemaphoreHandle_t gNowPlayingMutex = nullptr;
NowPlayingInfo gNowPlaying;  // geschützt durch gNowPlayingMutex
SemaphoreHandle_t gRoomsMutex = nullptr;
RoomsInfo gRooms;            // geschützt durch gRoomsMutex
SemaphoreHandle_t gFavoritesMutex = nullptr;
FavoritesInfo* gFavorites = nullptr;  // PSRAM, geschützt durch gFavoritesMutex
FavoritesInfo* gFavoritesScratch = nullptr;  // PSRAM, nur die Task: neue Liste wird hier aufgebaut

/** Wie strlcpy, schneidet aber nie mitten in einem UTF-8-Zeichen ab (sonst zeigt LVGL Müll). */
void copyUtf8(char* dst, const std::string& src, size_t size) {
    size_t n = src.size() < size - 1 ? src.size() : size - 1;
    if (n < src.size()) {
        while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) --n;  // Folgebyte: zurück zum Anfang
    }
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

void post(Event::Type type, int value = 0, const char* text = "") {
    Event e{};
    e.type = type;
    e.value = value;
    strlcpy(e.text, text, sizeof(e.text));
    if (xQueueSend(gEventQueue, &e, 0) != pdTRUE) {
        log_w("Event-Queue voll, Meldung verworfen");
    }
}

// ---------------------------------------------------------------------------
// HTTP/SOAP
// ---------------------------------------------------------------------------

/** Führt eine SOAP-Anfrage an `ip` aus. body enthält die Antwort (auch im Fehlerfall). */
sonos::SoapResult call(const std::string& ip, const sonos::SoapRequest& req, std::string& body,
                       uint16_t timeoutMs = kHttpTimeoutMs) {
    HTTPClient http;
    http.setConnectTimeout(kHttpConnectTimeoutMs);
    http.setTimeout(timeoutMs);
    http.setReuse(false);

    const String url = String("http://") + ip.c_str() + ":" + kSonosPort + req.path.c_str();
    if (!http.begin(url)) {
        body.clear();
        return sonos::evaluateResponse(-1, body);
    }
    http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
    http.addHeader("SOAPACTION", req.soapAction.c_str());

    const uint32_t start = millis();
    const int status = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(req.body.data())), req.body.size());
    body = status > 0 ? std::string(http.getString().c_str()) : std::string();
    http.end();

    const sonos::SoapResult result = sonos::evaluateResponse(status, body);
    log_d("SOAP %s -> %d (%lu ms)", req.soapAction.c_str(), status, static_cast<unsigned long>(millis() - start));
    return result;
}

/** Wie call(), aber bei einem Verbindungsfehler (Aussetzer) sofort ein zweites Mal. */
sonos::SoapResult callWithRetry(const std::string& ip, const sonos::SoapRequest& req, std::string& body,
                                uint16_t timeoutMs = kHttpTimeoutMs) {
    sonos::SoapResult r = call(ip, req, body, timeoutMs);
    if (!r.ok && r.httpStatus <= 0) {
        Serial.printf("SONOS Aussetzer (%s) – wiederhole\n", r.error.c_str());
        r = call(ip, req, body, timeoutMs);
    }
    return r;
}

// ---------------------------------------------------------------------------
// Suche (SSDP) und Topologie
// ---------------------------------------------------------------------------

/** SSDP-Suche: IP-Adressen aller antwortenden Sonos-Speaker. */
std::vector<std::string> ssdpSearch() {
    std::vector<std::string> ips;
    WiFiUDP udp;
    if (!udp.begin(kSsdpLocalPort)) return ips;

    const std::string request = sonos::ssdp::buildSearchRequest();
    const IPAddress multicast(239, 255, 255, 250);
    for (int i = 0; i < 2; ++i) {  // UDP kann verloren gehen: zweimal senden
        udp.beginPacket(multicast, sonos::ssdp::kPort);
        udp.write(reinterpret_cast<const uint8_t*>(request.data()), request.size());
        udp.endPacket();
    }

    char buf[1024];
    const uint32_t start = millis();
    while (millis() - start < kSsdpWaitMs) {
        const int len = udp.parsePacket();
        if (len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        const int n = udp.read(buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        std::string location;
        if (!sonos::ssdp::parseSearchResponse(std::string(buf, n), location)) continue;
        const std::string ip = sonos::topology::ipFromLocation(location);
        if (!ip.empty() && std::find(ips.begin(), ips.end(), ip) == ips.end()) ips.push_back(ip);
    }
    udp.stop();
    Serial.printf("SSDP: %u Sonos-Speaker gefunden\n", static_cast<unsigned>(ips.size()));
    return ips;
}

/** Topologie von einem beliebigen Speaker lesen. */
bool fetchTopology(const std::string& ip, std::vector<sonos::ZoneGroup>& groups) {
    std::string body;
    const sonos::SoapResult r = callWithRetry(ip, sonos::topology::getZoneGroupState(), body);
    std::vector<sonos::ZoneGroup> parsed;
    if (!r.ok || !sonos::topology::parseZoneGroupState(body, parsed) || parsed.empty()) return false;
    groups = std::move(parsed);
    return true;
}

void publishRooms(const std::vector<sonos::ZoneGroup>& groups, int selected) {
    xSemaphoreTake(gRoomsMutex, portMAX_DELAY);
    gRooms.version++;
    gRooms.count = static_cast<int>(groups.size() < kMaxRooms ? groups.size() : kMaxRooms);
    gRooms.selected = selected < gRooms.count ? selected : -1;
    for (int i = 0; i < gRooms.count; ++i) {
        RoomEntry& e = gRooms.rooms[i];
        strlcpy(e.uuid, groups[i].coordinatorUuid.c_str(), sizeof(e.uuid));
        strlcpy(e.name, groups[i].name.c_str(), sizeof(e.name));
        strlcpy(e.display, groups[i].displayName().c_str(), sizeof(e.display));
        e.memberCount = static_cast<uint8_t>(groups[i].members.size());
    }
    xSemaphoreGive(gRoomsMutex);
}

// ---------------------------------------------------------------------------
// Verbindung zum aktiven Raum
// ---------------------------------------------------------------------------

/** Zustand der Verbindung innerhalb der Task. */
struct Link {
    // Anlage
    std::vector<sonos::ZoneGroup> groups;
    std::string preferredUuid;  // gewählter Raum (Koordinator-UUID zum Zeitpunkt der Wahl)
    bool startNamePending = true;  // bevorzugter Raum (SONOS_ROOM) noch nicht angewendet
    int roomIndex = -1;
    std::string targetIp;       // Koordinator des aktiven Raums
    bool targetIsGroup = false;
    bool needDiscovery = true;
    uint32_t nextDiscoveryAt = 0;
    uint32_t nextTopologyAt = 0;

    // Abfragen
    bool synced = false;            // Zustand und Lautstärke sind bekannt
    bool speakerFailing = false;    // Fehler wurde an die UI gemeldet
    int consecutiveFailures = 0;
    uint32_t nextPollAt = 0;
    std::string lastTrackUri;       // für GetMediaInfo nur bei Quellenwechsel
    sonos::MediaInfo media;
    bool hasMedia = false;

    // Favoriten
    bool favoritesLoaded = false;
    uint32_t favoritesTriedAt = 0;  // 0 = noch nie

    bool hasTarget() const { return !targetIp.empty(); }

    /** Kontakt fehlgeschlagen. Einzelne Aussetzer still wiederholen, erst dann melden und neu suchen. */
    void failed(const char* what, const std::string& error) {
        ++consecutiveFailures;
        Serial.printf("SONOS %s FEHLER (%d/%d): %s\n", what, consecutiveFailures, kFailuresBeforeError,
                      error.c_str());
        if (consecutiveFailures < kFailuresBeforeError) {
            nextPollAt = millis() + kQuickRetryMs;
            return;
        }
        synced = false;
        needDiscovery = true;  // vielleicht hat der Speaker eine neue IP
        nextPollAt = millis() + kRetryIntervalMs;
        if (!speakerFailing) {
            speakerFailing = true;
            post(Event::Type::SpeakerError, 0, error.c_str());
        }
    }

    void succeeded() {
        consecutiveFailures = 0;
        if (speakerFailing) {
            speakerFailing = false;
            post(Event::Type::SpeakerOk);
        }
    }

    /** Aktiven Raum aus Wunsch-UUID und Topologie bestimmen; bei Wechsel neu synchronisieren. */
    void resolveTarget() {
        if (groups.empty()) return;
        // Beim Start einmalig: bevorzugter Raum aus secrets.h (SONOS_ROOM) hat Vorrang.
        if (startNamePending) {
            startNamePending = false;
            const int byName = sonos::topology::findGroupByName(groups, gStartRoomName);
            if (byName >= 0) {
                preferredUuid = groups[byName].coordinatorUuid;
            } else if (!gStartRoomName.empty()) {
                Serial.printf("RAUM „%s“ (SONOS_ROOM) nicht gefunden – nehme den zuletzt gewählten\n",
                              gStartRoomName.c_str());
            }
        }
        int idx = preferredUuid.empty() ? -1 : sonos::topology::findGroupOf(groups, preferredUuid);
        if (idx < 0 && !gFallbackIp.empty()) idx = sonos::topology::findGroupByIp(groups, gFallbackIp);
        if (idx < 0) idx = 0;
        if (preferredUuid.empty()) preferredUuid = groups[idx].coordinatorUuid;

        const sonos::ZoneGroup& g = groups[idx];
        const bool changed = g.coordinatorIp != targetIp || idx != roomIndex || g.isGroup() != targetIsGroup;
        roomIndex = idx;
        publishRooms(groups, roomIndex);
        if (!changed) return;

        targetIp = g.coordinatorIp;
        targetIsGroup = g.isGroup();
        synced = false;
        consecutiveFailures = 0;
        lastTrackUri.clear();
        hasMedia = false;
        nextPollAt = millis();
        Serial.printf("RAUM %s (Koordinator %s%s)\n", g.displayName().c_str(), targetIp.c_str(),
                      targetIsGroup ? ", Gruppe" : "");
        post(Event::Type::RoomChanged, roomIndex, g.displayName().c_str());
    }

    /** Anlage suchen: zuerst bekannte Adressen, dann SSDP. */
    bool discover() {
        post(Event::Type::Discovering);
        std::vector<std::string> candidates;
        if (!targetIp.empty()) candidates.push_back(targetIp);
        if (!gFallbackIp.empty()) candidates.push_back(gFallbackIp);
        for (const auto& g : groups) {
            for (const auto& m : g.members) candidates.push_back(m.ip);
        }
        for (const auto& ip : candidates) {
            if (fetchTopology(ip, groups)) return true;
        }
        for (const auto& ip : ssdpSearch()) {
            if (fetchTopology(ip, groups)) return true;
        }
        return false;
    }
};

void publishNowPlaying(const sonos::NowPlaying& np, uint32_t fetchedAt) {
    xSemaphoreTake(gNowPlayingMutex, portMAX_DELAY);
    gNowPlaying.version++;
    gNowPlaying.kind = static_cast<uint8_t>(np.kind);
    strlcpy(gNowPlaying.title, np.title.c_str(), sizeof(gNowPlaying.title));
    strlcpy(gNowPlaying.subtitle, np.subtitle.c_str(), sizeof(gNowPlaying.subtitle));
    strlcpy(gNowPlaying.album, np.album.c_str(), sizeof(gNowPlaying.album));
    strlcpy(gNowPlaying.albumArtUri, np.albumArtUri.c_str(), sizeof(gNowPlaying.albumArtUri));
    gNowPlaying.durationSec = np.durationSec;
    gNowPlaying.positionSec = np.positionSec;
    gNowPlaying.fetchedAtMs = fetchedAt;
    xSemaphoreGive(gNowPlayingMutex);
}

/** Lautstärke lesen: Einzelraum per RenderingControl, Gruppe per GroupRenderingControl. */
bool readVolume(Link& link, int& volume, std::string& error) {
    std::string body;
    sonos::SoapResult r;
    bool parsed = false;
    if (link.targetIsGroup) {
        // Snapshot, damit spätere Änderungen proportional auf die Mitglieder verteilt werden.
        call(link.targetIp, sonos::grouprendering::snapshotGroupVolume(), body);
        r = call(link.targetIp, sonos::grouprendering::getGroupVolume(), body);
        parsed = r.ok && sonos::grouprendering::parseGetGroupVolume(body, volume);
    } else {
        r = call(link.targetIp, sonos::rendering::getVolume(), body);
        parsed = r.ok && sonos::rendering::parseGetVolume(body, volume);
    }
    if (!parsed) error = r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error;
    return parsed;
}

/** Wiedergabezustand, Titel und Lautstärke abfragen. */
void poll(Link& link) {
    std::string body;

    sonos::SoapResult r = call(link.targetIp, sonos::avtransport::getTransportInfo(), body);
    sonos::TransportState state = sonos::TransportState::Unknown;
    if (!r.ok || !sonos::avtransport::parseTransportInfo(body, state)) {
        link.failed("GetTransportInfo", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    r = call(link.targetIp, sonos::avtransport::getPositionInfo(), body);
    const uint32_t fetchedAt = millis();
    sonos::PositionInfo pos;
    if (!r.ok || !sonos::parsePositionInfo(body, pos)) {
        link.failed("GetPositionInfo", r.ok ? std::string("Unerwartete Antwort vom Speaker") : r.error);
        return;
    }

    // Quelle gewechselt: GetMediaInfo liefert u. a. den Sendernamen bei Radio. Bei leerer
    // TrackURI jedes Mal – sie bleibt z. B. beim Wechsel auf den TV-Eingang mancher Geräte leer.
    if (pos.trackUri != link.lastTrackUri || pos.trackUri.empty() || !link.hasMedia) {
        r = call(link.targetIp, sonos::avtransport::getMediaInfo(), body);
        link.hasMedia = r.ok && sonos::parseMediaInfo(body, link.media);
        link.lastTrackUri = pos.trackUri;
    }

    int volume = 0;
    std::string error;
    if (!readVolume(link, volume, error)) {
        link.failed(link.targetIsGroup ? "GetGroupVolume" : "GetVolume", error);
        return;
    }

    link.succeeded();
    const sonos::NowPlaying np = sonos::buildNowPlaying(pos, link.hasMedia ? &link.media : nullptr);
    if (!link.synced) {
        Serial.printf("SONOS verbunden: Zustand %d, Lautstärke %d, „%s“\n", static_cast<int>(state), volume,
                      np.title.c_str());
    }
    link.synced = true;
    link.nextPollAt = millis() + kPollIntervalMs;
    publishNowPlaying(np, fetchedAt);
    // Cover: Kandidaten je nach Dienst; unveränderte Kandidaten ignoriert der Lader selbst.
    CoverLoader::request(sonos::art::candidates(np, link.targetIp));
    post(Event::Type::TransportState, static_cast<int>(state));
    post(Event::Type::SpeakerVolume, volume);
}

void sendVolume(Link& link, int volume) {
    std::string body;
    const sonos::SoapResult r =
        callWithRetry(link.targetIp,
                      link.targetIsGroup ? sonos::grouprendering::setGroupVolume(volume)
                                         : sonos::rendering::setVolume(volume),
                      body);
    if (r.ok) {
        Serial.printf("SONOS Set%sVolume %d ok\n", link.targetIsGroup ? "Group" : "", volume);
    } else {
        link.failed("SetVolume", r.error);
    }
}

sonos::SoapRequest requestFor(const Command& command) {
    switch (command.type) {
        case Transport::Play: return sonos::avtransport::play();
        case Transport::Pause: return sonos::avtransport::pause();
        case Transport::Next: return sonos::avtransport::next();
        case Transport::Previous: return sonos::avtransport::previous();
        case Transport::Seek: return sonos::avtransport::seek(command.value);
    }
    return sonos::avtransport::play();
}

const char* nameOf(Transport command) {
    switch (command) {
        case Transport::Play: return "Play";
        case Transport::Pause: return "Pause";
        case Transport::Next: return "Next";
        case Transport::Previous: return "Previous";
        case Transport::Seek: return "Seek";
    }
    return "?";
}

void sendTransport(Link& link, const Command& command) {
    const char* name = nameOf(command.type);
    std::string body;
    sonos::SoapResult r = callWithRetry(link.targetIp, requestFor(command), body);

    // Manche Quellen (z. B. Radio-Streams) können nicht pausieren – dann stoppen.
    if (!r.ok && command.type == Transport::Pause && r.upnpErrorCode == kUpnpTransitionNotAvailable) {
        Serial.println(F("SONOS Pause nicht möglich – sende Stop"));
        name = "Stop";
        r = callWithRetry(link.targetIp, sonos::avtransport::stop(), body);
    }

    if (r.ok) {
        if (command.type == Transport::Seek) {
            Serial.printf("SONOS Seek %s ok\n", sonos::time::toUpnp(command.value).c_str());
        } else {
            Serial.printf("SONOS %s ok\n", name);
        }
        link.nextPollAt = millis() + kPollAfterCommandMs;
    } else if (r.httpStatus <= 0) {
        post(Event::Type::TransportError, 0, r.error.c_str());
        link.failed(name, r.error);  // Verbindungsproblem
    } else {
        // Der Speaker ist erreichbar, lehnt aber ab (z. B. Radio ohne „Nächster“).
        post(Event::Type::TransportError, r.upnpErrorCode, r.error.c_str());
        Serial.printf("SONOS %s abgelehnt: %s\n", name, r.error.c_str());
        link.nextPollAt = millis() + kPollAfterCommandMs;
    }
}

// ---------------------------------------------------------------------------
// Favoriten (Schritt 7)
// ---------------------------------------------------------------------------

/** Favoritenliste lesen (seitenweise) und für die UI veröffentlichen. */
bool loadFavorites(Link& link) {
    if (!gFavorites || !gFavoritesScratch) return false;
    FavoritesInfo& fresh = *gFavoritesScratch;
    fresh.count = 0;
    std::string body;
    int start = 0;
    int total = 0;
    int hidden = 0;
    const uint32_t began = millis();
    do {
        const sonos::SoapResult r =
            callWithRetry(link.targetIp, sonos::favorites::browse(start, kFavoritesPageSize), body, kBrowseTimeoutMs);
        int seen = 0;
        const bool parsed = r.ok && sonos::favorites::parseBrowse(
                                        body,
                                        [&](int position, sonos::Favorite&& f) {
                                            ++seen;
                                            if (f.title.empty() || fresh.count >= kMaxFavorites) return;
                                            // Reine Verknüpfungen (z. B. Podcast-Seiten) haben keine Adresse –
                                            // gar nicht erst anbieten statt beim Drücken einen Fehler zu zeigen.
                                            if (sonos::favorites::playMethod(f) == sonos::PlayMethod::Unsupported) {
                                                ++hidden;
                                                return;
                                            }
                                            FavoriteEntry& e = fresh.items[fresh.count++];
                                            copyUtf8(e.title, f.title, sizeof(e.title));
                                            copyUtf8(e.detail, f.description, sizeof(e.detail));
                                            e.position = static_cast<int16_t>(start + position);
                                        },
                                        total);
        if (!parsed) {
            Serial.printf("FAVORITEN lesen fehlgeschlagen: %s\n",
                          r.ok ? "unerwartete Antwort" : r.error.c_str());
            return false;
        }
        if (seen == 0) break;
        start += seen;
    } while (start < total && fresh.count < kMaxFavorites);

    xSemaphoreTake(gFavoritesMutex, portMAX_DELAY);
    const uint32_t version = gFavorites->version + 1;
    memcpy(gFavorites, &fresh, sizeof(FavoritesInfo));
    gFavorites->version = version;
    gFavorites->loaded = true;
    xSemaphoreGive(gFavoritesMutex);
    Serial.printf("FAVORITEN: %d geladen, %d nicht abspielbar ausgeblendet (%lu ms)%s\n", fresh.count, hidden,
                  static_cast<unsigned long>(millis() - began), total > kMaxFavorites ? " – Liste gekürzt" : "");
    return true;
}

void favoriteFailed(const char* title, const char* reason) {
    Serial.printf("FAVORIT „%s“ FEHLER: %s\n", title, reason);
    post(Event::Type::FavoriteFailed, 0, reason);
}

/** Einen Schritt beim Starten ausführen; bei Fehler melden. */
bool favoriteStep(Link& link, const char* title, const char* step, const sonos::SoapRequest& req) {
    std::string body;
    const sonos::SoapResult r = callWithRetry(link.targetIp, req, body);
    if (r.ok) return true;
    char reason[64];
    if (r.upnpErrorCode == 800) {
        snprintf(reason, sizeof(reason), "Speaker ist nicht Gruppen-Koordinator");
    } else if (r.upnpErrorCode != 0) {
        snprintf(reason, sizeof(reason), "Abgelehnt (%s, Fehler %d)", step, r.upnpErrorCode);
    } else {
        snprintf(reason, sizeof(reason), "%s: %s", step, r.error.c_str());
    }
    favoriteFailed(title, reason);
    if (r.httpStatus <= 0) link.failed(step, r.error);
    return false;
}

/**
 * Favorit abspielen: Adresse und Metadaten einzeln nachladen, dann
 *  - Radio/Line-In/TV: SetAVTransportURI + Play
 *  - Playlist/Album/Titel: Warteschlange leeren, Favorit anhängen, Warteschlange abspielen
 */
void startFavorite(Link& link, const FavoriteRequest& req) {
    if (!gFavorites) return;
    FavoriteEntry entry{};
    bool known = false;
    xSemaphoreTake(gFavoritesMutex, portMAX_DELAY);
    if (req.index >= 0 && req.index < gFavorites->count && strcmp(gFavorites->items[req.index].title, req.title) == 0) {
        entry = gFavorites->items[req.index];
        known = true;
    }
    xSemaphoreGive(gFavoritesMutex);
    if (!known) {
        favoriteFailed(req.title, "Favoriten haben sich geändert – bitte neu wählen");
        gFavoritesRefresh = true;
        return;
    }

    std::string body;
    sonos::SoapResult r = callWithRetry(link.targetIp, sonos::favorites::browse(entry.position, 1), body, kBrowseTimeoutMs);
    sonos::Favorite fav;
    bool found = false;
    int total = 0;
    if (r.ok) {
        sonos::favorites::parseBrowse(
            body,
            [&](int, sonos::Favorite&& f) {
                char title[sizeof(entry.title)];
                copyUtf8(title, f.title, sizeof(title));
                if (!found && strcmp(title, entry.title) == 0) {
                    fav = std::move(f);
                    found = true;
                }
            },
            total);
    }
    if (!found) {
        favoriteFailed(entry.title, r.ok ? "Favoriten haben sich geändert – bitte neu wählen" : r.error.c_str());
        if (r.ok) gFavoritesRefresh = true;
        if (r.httpStatus <= 0) link.failed("Browse", r.error);
        return;
    }

    const sonos::PlayMethod method = sonos::favorites::playMethod(fav);
    Serial.printf("FAVORIT „%s“ (%s): %s\n", entry.title,
                  method == sonos::PlayMethod::Direct  ? "direkt"
                  : method == sonos::PlayMethod::Queue ? "über die Warteschlange"
                                                       : "nicht abspielbar",
                  fav.uri.c_str());
    if (method == sonos::PlayMethod::Unsupported) {
        favoriteFailed(entry.title, "Dieser Favorit lässt sich hier nicht abspielen");
        return;
    }

    if (method == sonos::PlayMethod::Direct) {
        if (!favoriteStep(link, entry.title, "SetAVTransportURI", sonos::avtransport::setAVTransportURI(fav.uri, fav.metadata)))
            return;
    } else {
        if (link.roomIndex < 0 || link.roomIndex >= static_cast<int>(link.groups.size())) {
            favoriteFailed(entry.title, "Kein Raum gewählt");
            return;
        }
        const std::string queue = sonos::avtransport::queueUri(link.groups[link.roomIndex].coordinatorUuid);
        if (!favoriteStep(link, entry.title, "RemoveAllTracksFromQueue", sonos::avtransport::removeAllTracksFromQueue()) ||
            !favoriteStep(link, entry.title, "AddURIToQueue", sonos::avtransport::addURIToQueue(fav.uri, fav.metadata)) ||
            !favoriteStep(link, entry.title, "SetAVTransportURI", sonos::avtransport::setAVTransportURI(queue, ""))) {
            return;
        }
        // Zum ersten Titel – schlägt fehl, wenn die Warteschlange schon dort steht; egal.
        call(link.targetIp, sonos::avtransport::seekTrack(1), body);
    }
    if (!favoriteStep(link, entry.title, "Play", sonos::avtransport::play())) return;

    Serial.printf("FAVORIT „%s“ läuft\n", entry.title);
    post(Event::Type::FavoriteStarted, 0, entry.title);
    link.lastTrackUri.clear();  // neue Quelle: GetMediaInfo neu lesen (Sendername)
    link.hasMedia = false;
    link.nextPollAt = millis() + kPollAfterCommandMs;
}

void task(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("MaTouchSonos");
    WiFi.setSleep(false);  // geringere Latenz; Gerät hängt ohnehin am USB-Netzteil
    WiFi.setAutoReconnect(true);

    post(Event::Type::WifiConnecting);
    WiFi.begin(gSsid, gPassword);

    bool wifiUp = false;
    uint32_t lastWifiCheck = 0;
    Link link;
    link.preferredUuid = gInitialRoomUuid;

    for (;;) {
        const uint32_t now = millis();

        // --- WLAN-Zustand überwachen -----------------------------------------
        if (now - lastWifiCheck >= kWifiCheckMs) {
            lastWifiCheck = now;
            const bool connected = WiFi.status() == WL_CONNECTED;
            if (connected && !wifiUp) {
                wifiUp = true;
                link.synced = false;
                link.consecutiveFailures = 0;
                link.nextPollAt = now;
                link.nextTopologyAt = now;  // Topologie gleich neu lesen (IPs können sich geändert haben)
                post(Event::Type::WifiConnected, 0, WiFi.localIP().toString().c_str());
                Serial.printf("WLAN verbunden, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            } else if (!connected && wifiUp) {
                wifiUp = false;
                link.synced = false;
                post(Event::Type::WifiLost);
                Serial.println(F("WLAN verloren – verbinde neu"));
            }
        }

        if (!wifiUp) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Anlage suchen (Start, nach wiederholten Fehlern) --------------------
        if (link.needDiscovery || link.groups.empty()) {
            if (static_cast<int32_t>(now - link.nextDiscoveryAt) < 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (link.discover()) {
                link.needDiscovery = false;
                link.nextTopologyAt = millis() + kTopologyIntervalMs;
                link.resolveTarget();
            } else {
                link.nextDiscoveryAt = millis() + kDiscoveryRetryMs;
                post(Event::Type::NoSpeakers);
                Serial.println(F("Keine Sonos-Anlage gefunden – neuer Versuch in 5 s"));
                continue;
            }
        }

        // --- Raumwahl der UI -----------------------------------------------------
        RoomRequest room;
        if (xQueueReceive(gRoomQueue, &room, 0) == pdTRUE) {
            link.preferredUuid = room.uuid;
            link.resolveTarget();
        }

        // --- Topologie regelmäßig auffrischen (Gruppen/IPs geändert?) -------------
        if (static_cast<int32_t>(now - link.nextTopologyAt) >= 0) {
            link.nextTopologyAt = now + kTopologyIntervalMs;
            if (fetchTopology(link.targetIp, link.groups)) link.resolveTarget();
        }

        if (!link.hasTarget()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // --- Befehle der UI (Lautstärke zuerst: sie fühlt sich am direktesten an) ----
        int volume;
        if (xQueueReceive(gVolumeQueue, &volume, 0) == pdTRUE && link.synced) {
            sendVolume(link, volume);
        }
        Command command;
        if (xQueueReceive(gTransportQueue, &command, pdMS_TO_TICKS(20)) == pdTRUE && link.synced) {
            sendTransport(link, command);
        }

        FavoriteRequest favorite;
        if (xQueueReceive(gFavoriteQueue, &favorite, 0) == pdTRUE) {
            if (link.synced) startFavorite(link, favorite);
            else favoriteFailed(favorite.title, "Speaker noch nicht verbunden");
        }

        // --- Regelmäßig abfragen (Start, Änderungen aus der App, Erholung nach Fehlern) --
        if (static_cast<int32_t>(millis() - link.nextPollAt) >= 0) {
            poll(link);
        }

        // --- Favoriten: nach dem Verbinden einmal, danach auf Wunsch der UI ---------------
        if (link.synced) {
            const bool refresh = gFavoritesRefresh.exchange(false);
            const uint32_t t = millis();
            if ((!link.favoritesLoaded || refresh) &&
                (link.favoritesTriedAt == 0 || t - link.favoritesTriedAt >= kFavoritesMinIntervalMs)) {
                link.favoritesTriedAt = t == 0 ? 1 : t;
                if (loadFavorites(link)) link.favoritesLoaded = true;
            }
        }
    }
}

void enqueue(const Command& command) {
    if (gTransportQueue && xQueueSend(gTransportQueue, &command, 0) != pdTRUE) {
        log_w("Transport-Queue voll, Befehl verworfen");
    }
}

}  // namespace

void SonosLink::begin(const char* ssid, const char* password, const char* startRoomName,
                      const char* savedRoomUuid, const char* fallbackIp) {
    gSsid = ssid;
    gPassword = password;
    gStartRoomName = startRoomName ? startRoomName : "";
    gInitialRoomUuid = savedRoomUuid ? savedRoomUuid : "";
    gFallbackIp = fallbackIp ? fallbackIp : "";
    gVolumeQueue = xQueueCreate(1, sizeof(int));
    gTransportQueue = xQueueCreate(4, sizeof(Command));
    gRoomQueue = xQueueCreate(1, sizeof(RoomRequest));
    gFavoriteQueue = xQueueCreate(1, sizeof(FavoriteRequest));
    gEventQueue = xQueueCreate(16, sizeof(Event));
    gNowPlayingMutex = xSemaphoreCreateMutex();
    gRoomsMutex = xSemaphoreCreateMutex();
    gFavoritesMutex = xSemaphoreCreateMutex();
    // Favoritenlisten (je ~10 KB) in den PSRAM – interner RAM ist knapp.
    gFavorites = static_cast<FavoritesInfo*>(ps_calloc(1, sizeof(FavoritesInfo)));
    gFavoritesScratch = static_cast<FavoritesInfo*>(ps_calloc(1, sizeof(FavoritesInfo)));
    if (!gFavorites || !gFavoritesScratch) Serial.println(F("FAVORITEN: kein PSRAM – Favoriten deaktiviert"));
    // Kern 0 (dort läuft auch der WLAN-Stack), UI bleibt auf Kern 1.
    // 16 KB Stack: XML-Auswertung von Titel-Metadaten und Topologie (Antwort ~15 KB bei 13 Geräten).
    xTaskCreatePinnedToCore(task, "sonos", 16384, nullptr, 1, nullptr, 0);
}

void SonosLink::setVolume(int volume) {
    if (gVolumeQueue) xQueueOverwrite(gVolumeQueue, &volume);
}

void SonosLink::transport(Transport command) { enqueue(Command{command, 0}); }

void SonosLink::seek(int positionSec) { enqueue(Command{Transport::Seek, positionSec}); }

void SonosLink::selectRoom(const char* uuid) {
    if (!gRoomQueue) return;
    RoomRequest r{};
    strlcpy(r.uuid, uuid, sizeof(r.uuid));
    xQueueOverwrite(gRoomQueue, &r);
}

void SonosLink::refreshFavorites() { gFavoritesRefresh = true; }

void SonosLink::playFavorite(int index, const char* title) {
    if (!gFavoriteQueue) return;
    FavoriteRequest r{};
    r.index = index;
    strlcpy(r.title, title, sizeof(r.title));
    xQueueOverwrite(gFavoriteQueue, &r);
}

bool SonosLink::takeFavorites(uint32_t lastVersion, FavoritesInfo& out) {
    if (!gFavoritesMutex || !gFavorites) return false;
    bool changed = false;
    xSemaphoreTake(gFavoritesMutex, portMAX_DELAY);
    if (gFavorites->version != lastVersion) {
        memcpy(&out, gFavorites, sizeof(FavoritesInfo));
        changed = true;
    }
    xSemaphoreGive(gFavoritesMutex);
    return changed;
}

bool SonosLink::pollEvent(Event& out) {
    return gEventQueue && xQueueReceive(gEventQueue, &out, 0) == pdTRUE;
}

bool SonosLink::takeNowPlaying(uint32_t lastVersion, NowPlayingInfo& out) {
    if (!gNowPlayingMutex) return false;
    bool changed = false;
    xSemaphoreTake(gNowPlayingMutex, portMAX_DELAY);
    if (gNowPlaying.version != lastVersion) {
        out = gNowPlaying;
        changed = true;
    }
    xSemaphoreGive(gNowPlayingMutex);
    return changed;
}

bool SonosLink::takeRooms(uint32_t lastVersion, RoomsInfo& out) {
    if (!gRoomsMutex) return false;
    bool changed = false;
    xSemaphoreTake(gRoomsMutex, portMAX_DELAY);
    if (gRooms.version != lastVersion) {
        out = gRooms;
        changed = true;
    }
    xSemaphoreGive(gRoomsMutex);
    return changed;
}

}  // namespace net
