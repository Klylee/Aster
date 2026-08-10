// ============================================================================
// stb_image_write 实现 TU
// ----------------------------------------------------------------------------
// 仅用于向 tinyexr 的 STB zlib 路径（EnvironmentMap.cpp 里的
// TINYEXR_USE_STB_ZLIB=1）提供 stbi_zlib_compress 符号（写入路径用）。
// 解码路径的 stbi_zlib_decode_buffer 已由 Texture.cpp（stb_image）提供。
// imgui_draw.cpp 里的同段代码被 #if 0 屏蔽，因此不会产生重复符号。
// ============================================================================
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
