#pragma once

#include <string>

namespace sonos {

/**
 * Minimale XML-Hilfsfunktionen für SOAP-Antworten von Sonos.
 *
 * Bewusst kein vollständiger XML-Parser: Sonos-Antworten sind klein und flach
 * strukturiert. Namespace-Präfixe (z. B. <u:CurrentVolume>) werden ignoriert.
 */
namespace xml {

/**
 * Inhalt des ersten Elements mit dem lokalen Namen `name` (ohne Namespace-Präfix).
 * @param found wird auf true gesetzt, wenn das Element existiert (auch wenn leer).
 * @return Rohinhalt ohne Entity-Dekodierung.
 */
std::string findElement(const std::string& doc, const std::string& name, bool* found = nullptr);

/** Ersetzt & < > " ' durch Entities – für Werte, die in SOAP-Anfragen eingesetzt werden. */
std::string escape(const std::string& text);

/** Dekodiert &amp; &lt; &gt; &quot; &apos; sowie numerische Entities (&#39; &#x27;). */
std::string unescape(const std::string& text);

}  // namespace xml
}  // namespace sonos
