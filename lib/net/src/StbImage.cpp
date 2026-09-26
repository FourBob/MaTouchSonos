// stb_image (public domain / MIT, siehe third_party/stb_image.h) – nur für progressive JPEGs,
// die JPEGDEC lediglich als 1/8-Vorschau dekodieren kann (z. B. Amazon-Music-Cover vom Speaker).
// Normale JPEGs und PNGs laufen weiter über JPEGDEC bzw. PNGdec (schneller, weniger Speicher).

#include <esp_heap_caps.h>

// Alle Puffer in den PSRAM: Progressive JPEGs brauchen Koeffizienten für das ganze Bild.
#define STBI_MALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(ptr, size) heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(ptr) heap_caps_free(ptr)

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD           // kein SSE/NEON auf dem ESP32
#define STBI_NO_THREAD_LOCALS  // nur die Cover-Task dekodiert
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
