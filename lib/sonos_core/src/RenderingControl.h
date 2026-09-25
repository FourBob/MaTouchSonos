#pragma once

#include <string>

#include "Soap.h"

namespace sonos {

/**
 * RenderingControl – Lautstärke eines einzelnen Players.
 * (Gruppenlautstärke folgt in Schritt 8 über GroupRenderingControl.)
 */
namespace rendering {

constexpr int kMinVolume = 0;
constexpr int kMaxVolume = 100;

SoapRequest getVolume();
SoapRequest setVolume(int volume);  ///< Wert wird auf 0..100 begrenzt

/**
 * Liest <CurrentVolume> aus einer GetVolume-Antwort.
 * @return true bei Erfolg; `volume` enthält dann 0..100.
 */
bool parseGetVolume(const std::string& body, int& volume);

}  // namespace rendering
}  // namespace sonos
