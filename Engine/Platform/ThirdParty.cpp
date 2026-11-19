// ThirdParty.cpp
// The single-file libraries the engine uses are "implemented" here, once:
//   stb_image        reads PNG and JPEG files
//   stb_image_write  writes PNG files (screenshots)
//   miniaudio        plays sound and decodes WAV, FLAC and MP3 files
// Their warnings are not ours to fix, so they are silenced for this file only.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Weverything"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
