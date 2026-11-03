#include "runtime.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define FONT_OK 0
#define FONT_ERR_INVALID_PARAMETER ((int32_t)0x80460002)
#define FONT_ERR_INVALID_MEMORY ((int32_t)0x80460003)
#define FONT_ERR_INVALID_LIBRARY ((int32_t)0x80460004)
#define FONT_ERR_INVALID_HANDLE ((int32_t)0x80460005)
#define FONT_ERR_INVALID_RENDERER ((int32_t)0x80460007)
#define FONT_ERR_NO_GLYPH ((int32_t)0x80460042)
#define FONT_ERR_NO_SURFACE ((int32_t)0x80460050)

typedef struct {
    uint32_t reserved[64];
} FontHandleOpaque;
typedef struct { uint16_t mem_kind, attr_bits; uint32_t region_size; void *region_base, *mspace_handle, *iface, *on_destroy, *destroy_ctx, *some_ctx1, *some_ctx2; } FontMem;
typedef struct {
    void *buffer; int32_t width_byte; int8_t pixel_size; uint8_t pad0, style_flag, pad2;
    int32_t width, height; uint32_t sc_x0, sc_y0, sc_x1, sc_y1; uint64_t reserved[11];
} FontSurface;
typedef struct { float width, height; struct { float x, y, advance; } horizontal, vertical; } FontMetrics;
typedef struct { float bearing_x, bearing_y, advance, stride; uint32_t width, height; } FontImageMetrics;
typedef struct {
    const void *stage; struct { uint8_t *address; uint32_t width_byte; uint8_t pixel_size, format; uint16_t pad; } image;
    struct { uint32_t x, y, w, h; } rect; FontImageMetrics metrics;
} FontRenderOutput;
typedef struct { uint16_t tag, pad; uint32_t flags, subfont_index; int32_t unique_id; const void *reserved2, *reserved1; } FontOpenParams;
typedef struct { uint32_t magic; } FontLibrary;
typedef struct { uint32_t magic; } FontRenderer;
typedef struct {
    FontHandleOpaque opaque;
    uint32_t magic;
    FT_Face face;
    float scale_w, scale_h;
    void *renderer;
} FontHandle;
typedef struct {
    uint8_t reserved_00[0x10];
    uintptr_t pixel_resolution, init, term, support;
    uint8_t reserved_30[0x08];
    uintptr_t open, close;
    uint8_t reserved_48[0x08];
    uintptr_t scale;
    uint8_t reserved_58[0x08];
    uintptr_t metric, glyphs_count;
    uint8_t reserved_70[0x08];
    uintptr_t glyph_index, set_char_dpi, set_char_default_dpi;
    uint8_t reserved_90[0x10];
    uintptr_t compute_layout, load_glyph_cached;
    uint8_t reserved_b0[0x08];
    uintptr_t get_glyph_metrics;
    uint8_t reserved_c0[0x20];
    uintptr_t apply_glyph_adjust;
    uint8_t reserved_e8[0x20];
    uintptr_t configure_glyph;
    uint8_t reserved_110[0x08];
    uintptr_t compute_layout_alt;
} FontFtLibraryTable;
typedef struct { uint32_t magic, size; uintptr_t create, destroy, query; } FontFtRendererTable;

_Static_assert(sizeof(FontMem) == 64, "Font memory ABI size");
_Static_assert(sizeof(FontSurface) == 128, "Font surface ABI size");
_Static_assert(sizeof(FontMetrics) == 32, "Font metrics ABI size");
_Static_assert(sizeof(FontImageMetrics) == 24, "Font image metrics ABI size");
_Static_assert(sizeof(FontRenderOutput) == 64, "Font render output ABI size");
_Static_assert(sizeof(FontFtLibraryTable) == 0x120, "FontFt library driver ABI size");

static pthread_mutex_t font_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t ft_once = PTHREAD_ONCE_INIT;
static FT_Library ft_library;
static FT_Error ft_init_error;
static FontLibrary font_library = {0x0f01};
static FontRenderer font_renderer = {0x0f03};
static FontFtLibraryTable ft_library_table;
static FontFtRendererTable ft_renderer_table = {0, 0x168, 0, 0, 0};
static _Atomic size_t library_creates, renderer_creates, memory_open_attempts, memory_open_successes, memory_open_failures;
static _Atomic size_t glyph_render_attempts, glyph_render_successes, glyph_render_blank, glyph_render_failed;
static _Atomic size_t surfaces_initialized;
static _Atomic uint32_t font_api_call_count;

#define LOG_FONT_CALL(fmt, ...) do { \
    uint32_t _call = atomic_fetch_add_explicit(&font_api_call_count, 1, memory_order_relaxed); \
    if (_call < 50) { \
        printf("Font API [#%02u]: " fmt "\n", _call + 1, ##__VA_ARGS__); \
        fflush(stdout); \
    } \
} while (0)

static void init_freetype(void) { ft_init_error = FT_Init_FreeType(&ft_library); }
static int32_t load_glyph(FontHandle *font, uint32_t code, int render) {
    if (!font || font->magic != 0x0f02 || !font->face || !code) return FONT_ERR_INVALID_HANDLE;
    FT_UInt width = (FT_UInt)(font->scale_w > 0.0f ? font->scale_w + 0.5f : 16.0f);
    FT_UInt height = (FT_UInt)(font->scale_h > 0.0f ? font->scale_h + 0.5f : 16.0f);
    if (!width) width = 1;
    if (!height) height = 1;
    FT_Error error = FT_Set_Pixel_Sizes(font->face, width, height);
    if (!error) error = FT_Load_Char(font->face, (FT_ULong)code, render ? FT_LOAD_RENDER : FT_LOAD_DEFAULT);
    return error ? FONT_ERR_NO_GLYPH : FONT_OK;
}
static void clear_output(FontMetrics *metrics, FontRenderOutput *result) {
    if (metrics) memset(metrics, 0, sizeof(*metrics));
    if (result) memset(result, 0, sizeof(*result));
}
static void metrics_from_face(FontHandle *font, FontMetrics *out) {
    FT_GlyphSlot slot = font->face->glyph;
    out->width = (float)slot->metrics.width / 64.0f;
    out->height = (float)slot->metrics.height / 64.0f;
    out->horizontal.x = (float)slot->metrics.horiBearingX / 64.0f;
    out->horizontal.y = (float)slot->metrics.horiBearingY / 64.0f;
    out->horizontal.advance = (float)slot->metrics.horiAdvance / 64.0f;
    out->vertical.x = (float)slot->metrics.vertBearingX / 64.0f;
    out->vertical.y = (float)slot->metrics.vertBearingY / 64.0f;
    out->vertical.advance = (float)slot->metrics.vertAdvance / 64.0f;
}

static ABI int32_t font_memory_init(FontMem *desc, void *region, uint32_t size, const void *iface, void *mspace, void *destroy, void *ctx) {
    if (!desc || (!iface && (!region || !size))) {
        LOG_FONT_CALL("sceFontMemoryInit(desc=%p, region=%p, size=%u, iface=%p) -> FONT_ERR_INVALID_PARAMETER", (void*)desc, region, size, iface);
        return FONT_ERR_INVALID_PARAMETER;
    }
    desc->mem_kind = 0x0f00; desc->attr_bits = 0; desc->region_size = size; desc->region_base = region;
    desc->mspace_handle = mspace; desc->iface = (void *)iface; desc->on_destroy = destroy; desc->destroy_ctx = ctx;
    desc->some_ctx1 = NULL; desc->some_ctx2 = mspace;
    LOG_FONT_CALL("sceFontMemoryInit(desc=%p, region=%p, size=%u, iface=%p) -> FONT_OK", (void*)desc, region, size, iface);
    return FONT_OK;
}
static ABI int32_t font_memory_term(FontMem *desc) {
    if (!desc || desc->mem_kind != 0x0f00) {
        LOG_FONT_CALL("sceFontMemoryTerm(desc=%p) -> FONT_ERR_INVALID_MEMORY", (void*)desc);
        return FONT_ERR_INVALID_MEMORY;
    }
    memset(desc, 0, sizeof(*desc));
    LOG_FONT_CALL("sceFontMemoryTerm(desc=%p) -> FONT_OK", (void*)desc);
    return FONT_OK;
}
static ABI int32_t font_library_create(const FontMem *memory, void *params, uint64_t edition, void **out) {
    (void)edition;
    if (out) *out = NULL;
    if (!memory || !params || !out) {
        LOG_FONT_CALL("sceFontCreateLibraryWithEdition(mem=%p, params=%p) -> FONT_ERR_INVALID_PARAMETER", (void*)memory, params);
        return FONT_ERR_INVALID_PARAMETER;
    }
    if (memory->mem_kind != 0x0f00 || !memory->iface) {
        LOG_FONT_CALL("sceFontCreateLibraryWithEdition(mem=%p, iface=%p) -> FONT_ERR_INVALID_MEMORY", (void*)memory, memory->iface);
        return FONT_ERR_INVALID_MEMORY;
    }
    pthread_once(&ft_once, init_freetype);
    if (ft_init_error) {
        LOG_FONT_CALL("sceFontCreateLibraryWithEdition: FT_Init_FreeType failed (0x%08x)", (uint32_t)ft_init_error);
        return FONT_ERR_INVALID_MEMORY;
    }
    *out = &font_library;
    atomic_fetch_add_explicit(&library_creates, 1, memory_order_relaxed);
    LOG_FONT_CALL("sceFontCreateLibraryWithEdition(mem=%p, edition=%llu) -> FONT_OK (*out=%p)", (void*)memory, (unsigned long long)edition, *out);
    return FONT_OK;
}
static ABI int32_t font_library_destroy(void **library) {
    if (!library || (*library && *library != &font_library)) {
        LOG_FONT_CALL("sceFontDestroyLibrary(lib=%p) -> FONT_ERR_INVALID_LIBRARY", library ? *library : NULL);
        return FONT_ERR_INVALID_LIBRARY;
    }
    *library = NULL;
    LOG_FONT_CALL("sceFontDestroyLibrary() -> FONT_OK");
    return FONT_OK;
}
static ABI int32_t font_renderer_create(const FontMem *memory, void *params, uint64_t edition, void **out) {
    (void)edition;
    if (out) *out = NULL;
    if (!memory || !params || !out || memory->mem_kind != 0x0f00) {
        LOG_FONT_CALL("sceFontCreateRendererWithEdition(mem=%p) -> FONT_ERR_INVALID_PARAMETER", (void*)memory);
        return FONT_ERR_INVALID_PARAMETER;
    }
    *out = &font_renderer;
    atomic_fetch_add_explicit(&renderer_creates, 1, memory_order_relaxed);
    LOG_FONT_CALL("sceFontCreateRendererWithEdition(mem=%p, edition=%llu) -> FONT_OK (*out=%p)", (void*)memory, (unsigned long long)edition, *out);
    return FONT_OK;
}
static ABI int32_t font_renderer_destroy(void **renderer) {
    if (!renderer || (*renderer && *renderer != &font_renderer)) {
        LOG_FONT_CALL("sceFontDestroyRenderer(ren=%p) -> FONT_ERR_INVALID_RENDERER", renderer ? *renderer : NULL);
        return FONT_ERR_INVALID_RENDERER;
    }
    *renderer = NULL;
    LOG_FONT_CALL("sceFontDestroyRenderer() -> FONT_OK");
    return FONT_OK;
}
static ABI int32_t font_open_memory(void *library, const void *data, uint32_t size, const FontOpenParams *params, FontHandle **out) {
    atomic_fetch_add_explicit(&memory_open_attempts, 1, memory_order_relaxed);
    if (out) *out = NULL;
    if (library != &font_library) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontOpenFontMemory(lib=%p) -> FONT_ERR_INVALID_LIBRARY", library);
        return FONT_ERR_INVALID_LIBRARY;
    }
    if (!data || !size || !out) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontOpenFontMemory(data=%p, size=%u) -> FONT_ERR_INVALID_PARAMETER", data, size);
        return FONT_ERR_INVALID_PARAMETER;
    }
    unsigned char magic4[4] = {0};
    if (size >= 4) memcpy(magic4, data, 4);
    char magic_ascii[5] = {0};
    for (int i = 0; i < 4; ++i) magic_ascii[i] = (magic4[i] >= 32 && magic4[i] <= 126) ? (char)magic4[i] : '.';
    magic_ascii[4] = '\0';

    pthread_once(&ft_once, init_freetype);
    if (ft_init_error) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontOpenFontMemory: FT_Init_FreeType failed (0x%08x)", (uint32_t)ft_init_error);
        return FONT_ERR_INVALID_MEMORY;
    }
    FontHandle *font = calloc(1, sizeof(*font));
    if (!font) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontOpenFontMemory: calloc failed");
        return (int32_t)0x80460010;
    }
    pthread_mutex_lock(&font_lock);
    FT_Error error = FT_New_Memory_Face(ft_library, data, (FT_Long)size,
                                        params ? (FT_Long)params->subfont_index : 0, &font->face);
    pthread_mutex_unlock(&font_lock);
    if (error) {
        free(font);
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontOpenFontMemory(size=%u, magic=[%02X %02X %02X %02X] '%s') -> FreeType REJECTED with error 0x%08x",
                      size, magic4[0], magic4[1], magic4[2], magic4[3], magic_ascii, (uint32_t)error);
        return (int32_t)0x80460036;
    }
    font->magic = 0x0f02; font->scale_w = 16.0f; font->scale_h = 16.0f;
    font->opaque.reserved[0] = 0x0f02;
    *out = font;
    atomic_fetch_add_explicit(&memory_open_successes, 1, memory_order_relaxed);
    LOG_FONT_CALL("sceFontOpenFontMemory(size=%u, magic=[%02X %02X %02X %02X] '%s', subfont=%u) -> ACCEPTED (face=%p, family='%s', glyphs=%ld) -> font=%p",
                  size, magic4[0], magic4[1], magic4[2], magic4[3], magic_ascii,
                  params ? params->subfont_index : 0,
                  (void*)font->face, font->face->family_name ? font->face->family_name : "unknown",
                  font->face->num_glyphs, (void*)font);
    return FONT_OK;
}
static ABI int32_t font_close(FontHandle *font) {
    LOG_FONT_CALL("sceFontCloseFont(font=%p) -> FONT_OK", (void*)font);
    if (!font || font->magic != 0x0f02) return FONT_OK;
    pthread_mutex_lock(&font_lock);
    if (font->face) FT_Done_Face(font->face);
    pthread_mutex_unlock(&font_lock);
    font->face = NULL; font->magic = 0; free(font);
    return FONT_OK;
}
static ABI int32_t font_bind_renderer(FontHandle *font, void *renderer) {
    if (!font || font->magic != 0x0f02) {
        LOG_FONT_CALL("sceFontBindRenderer(font=%p, ren=%p) -> FONT_ERR_INVALID_HANDLE", (void*)font, renderer);
        return FONT_ERR_INVALID_HANDLE;
    }
    if (renderer && renderer != &font_renderer) {
        LOG_FONT_CALL("sceFontBindRenderer(font=%p, ren=%p) -> FONT_ERR_INVALID_RENDERER", (void*)font, renderer);
        return FONT_ERR_INVALID_RENDERER;
    }
    font->renderer = renderer;
    LOG_FONT_CALL("sceFontBindRenderer(font=%p, ren=%p) -> FONT_OK", (void*)font, renderer);
    return FONT_OK;
}
static ABI int32_t font_unbind_renderer(FontHandle *font) {
    if (!font || font->magic != 0x0f02) {
        LOG_FONT_CALL("sceFontUnbindRenderer(font=%p) -> FONT_ERR_INVALID_HANDLE", (void*)font);
        return FONT_ERR_INVALID_HANDLE;
    }
    font->renderer = NULL;
    LOG_FONT_CALL("sceFontUnbindRenderer(font=%p) -> FONT_OK", (void*)font);
    return FONT_OK;
}
static ABI int32_t font_get_metrics(FontHandle *font, uint32_t code, FontMetrics *metrics) {
    if (!font || !metrics) {
        LOG_FONT_CALL("sceFontGetCharGlyphMetrics(font=%p, code=0x%X) -> FONT_ERR_INVALID_PARAMETER", (void*)font, code);
        return FONT_ERR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&font_lock);
    int32_t rc = load_glyph(font, code, 0);
    if (rc) {
        memset(metrics, 0, sizeof(*metrics));
        pthread_mutex_unlock(&font_lock);
        LOG_FONT_CALL("sceFontGetCharGlyphMetrics(font=%p, code=0x%X) -> load_glyph failed (0x%08x)", (void*)font, code, (uint32_t)rc);
        return rc;
    }
    metrics_from_face(font, metrics);
    pthread_mutex_unlock(&font_lock);
    char ch_str[8] = {0};
    if (code >= 32 && code <= 126) snprintf(ch_str, sizeof(ch_str), "'%c'", (char)code);
    else snprintf(ch_str, sizeof(ch_str), "U+%04X", code);
    LOG_FONT_CALL("sceFontGetCharGlyphMetrics(font=%p, code=%s) -> FONT_OK (w=%.1f, h=%.1f, h_adv=%.1f)",
                  (void*)font, ch_str, metrics->width, metrics->height, metrics->horizontal.advance);
    return FONT_OK;
}
static ABI int32_t font_get_kerning(FontHandle *font, uint32_t left, uint32_t right, void *kerning) {
    if (!font || font->magic != 0x0f02 || !kerning) {
        LOG_FONT_CALL("sceFontGetKerning(font=%p, left=0x%X, right=0x%X) -> FONT_ERR_INVALID_PARAMETER", (void*)font, left, right);
        return FONT_ERR_INVALID_PARAMETER;
    }
    struct { float x, y, px, py; } *out = kerning;
    memset(out, 0, sizeof(*out));
    if (!FT_HAS_KERNING(font->face)) {
        LOG_FONT_CALL("sceFontGetKerning(font=%p, left=0x%X, right=0x%X) -> no kerning table", (void*)font, left, right);
        return FONT_OK;
    }
    pthread_mutex_lock(&font_lock);
    FT_UInt a = FT_Get_Char_Index(font->face, left), b = FT_Get_Char_Index(font->face, right);
    FT_Vector delta = {0, 0};
    FT_Error error = FT_Get_Kerning(font->face, a, b, FT_KERNING_DEFAULT, &delta);
    pthread_mutex_unlock(&font_lock);
    if (error) {
        LOG_FONT_CALL("sceFontGetKerning(font=%p, left=0x%X, right=0x%X) -> FONT_ERR_NO_GLYPH", (void*)font, left, right);
        return FONT_ERR_NO_GLYPH;
    }
    out->x = (float)delta.x / 64.0f; out->y = (float)delta.y / 64.0f;
    LOG_FONT_CALL("sceFontGetKerning(font=%p, left=0x%X, right=0x%X) -> FONT_OK (dx=%.2f, dy=%.2f)", (void*)font, left, right, out->x, out->y);
    return FONT_OK;
}
static ABI int32_t font_set_scale(FontHandle *font, float w, float h) {
    if (!font || font->magic != 0x0f02) {
        LOG_FONT_CALL("sceFontSetScalePixel(font=%p, w=%.2f, h=%.2f) -> FONT_ERR_INVALID_HANDLE", (void*)font, w, h);
        return FONT_ERR_INVALID_HANDLE;
    }
    if (!(w > 0.0f) || !(h > 0.0f) || !isfinite(w) || !isfinite(h)) {
        LOG_FONT_CALL("sceFontSetScalePixel(font=%p, w=%.2f, h=%.2f) -> FONT_ERR_INVALID_PARAMETER", (void*)font, w, h);
        return FONT_ERR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&font_lock); font->scale_w = w; font->scale_h = h; pthread_mutex_unlock(&font_lock);
    LOG_FONT_CALL("sceFontSetScalePixel(font=%p, w=%.2f, h=%.2f) -> FONT_OK", (void*)font, w, h);
    return FONT_OK;
}
static ABI int32_t font_get_scale(FontHandle *font, float *w, float *h) {
    if (!font || font->magic != 0x0f02) {
        LOG_FONT_CALL("sceFontGetScalePixel(font=%p) -> FONT_ERR_INVALID_HANDLE", (void*)font);
        return FONT_ERR_INVALID_HANDLE;
    }
    if (!w || !h) {
        LOG_FONT_CALL("sceFontGetScalePixel(font=%p) -> FONT_ERR_INVALID_PARAMETER", (void*)font);
        return FONT_ERR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&font_lock); *w = font->scale_w; *h = font->scale_h; pthread_mutex_unlock(&font_lock);
    LOG_FONT_CALL("sceFontGetScalePixel(font=%p) -> FONT_OK (*w=%.2f, *h=%.2f)", (void*)font, *w, *h);
    return FONT_OK;
}
static ABI int32_t font_setup_scale(FontHandle *font, float w, float h) {
    int32_t rc = font_set_scale(font, w, h);
    LOG_FONT_CALL("sceFontSetupRenderScalePixel(font=%p, w=%.2f, h=%.2f) -> rc=0x%08x", (void*)font, w, h, (uint32_t)rc);
    return rc;
}
static ABI int32_t font_attach_cache(void *library, void *buffer, uint32_t size) {
    (void)buffer; (void)size;
    int32_t rc = (library == &font_library ? FONT_OK : FONT_ERR_INVALID_LIBRARY);
    LOG_FONT_CALL("sceFontAttachDeviceCacheBuffer(lib=%p, buf=%p, size=%u) -> rc=0x%08x", library, buffer, size, (uint32_t)rc);
    return rc;
}
static ABI int32_t font_support_external(void *library, uint32_t max_fonts, uint32_t formats) {
    (void)max_fonts; (void)formats;
    int32_t rc = (library == &font_library ? FONT_OK : FONT_ERR_INVALID_LIBRARY);
    LOG_FONT_CALL("sceFontSupportExternalFonts(lib=%p, max_fonts=%u, fmt=0x%x) -> rc=0x%08x", library, max_fonts, formats, (uint32_t)rc);
    return rc;
}
static ABI int32_t font_support_system(void *library) {
    int32_t rc = (library == &font_library ? FONT_OK : FONT_ERR_INVALID_LIBRARY);
    LOG_FONT_CALL("sceFontSupportSystemFonts(lib=%p) -> rc=0x%08x", library, (uint32_t)rc);
    return rc;
}
static ABI void font_surface_init(FontSurface *surface, void *buffer, int width_byte, int pixel_size, int width, int height) {
    atomic_fetch_add_explicit(&surfaces_initialized, 1, memory_order_relaxed);
    if (!surface) {
        LOG_FONT_CALL("sceFontRenderSurfaceInit(surface=NULL)");
        return;
    }
    surface->buffer = buffer; surface->width_byte = width_byte; surface->pixel_size = (int8_t)pixel_size;
    surface->pad0 = surface->style_flag = surface->pad2 = 0; surface->width = width > 0 ? width : 0;
    surface->height = height > 0 ? height : 0; surface->sc_x0 = surface->sc_y0 = 0;
    surface->sc_x1 = (uint32_t)surface->width; surface->sc_y1 = (uint32_t)surface->height;

    static _Atomic int surf_log_count = 0;
    int cur_surf_log = atomic_fetch_add_explicit(&surf_log_count, 1, memory_order_relaxed);
    if (cur_surf_log < 20) {
        printf("Font Surface Init [#%02d]: surf=%p, buf=%p, stride=%d, bpp=%d, w=%d, h=%d, clip=[0,0..%u,%u]\n",
               cur_surf_log + 1, (void*)surface, buffer, width_byte, pixel_size, width, height, surface->sc_x1, surface->sc_y1);
        fflush(stdout);
    }
    LOG_FONT_CALL("sceFontRenderSurfaceInit(surf=%p, buf=%p, stride=%d, bpp=%d, w=%d, h=%d)",
                  (void*)surface, buffer, width_byte, pixel_size, width, height);
}
static ABI int32_t font_render_glyph(FontHandle *font, uint32_t code, FontSurface *surface, float x, float y, FontMetrics *metrics, FontRenderOutput *result) {
    atomic_fetch_add_explicit(&glyph_render_attempts, 1, memory_order_relaxed);
    clear_output(metrics, result);
    if (!font || font->magic != 0x0f02) {
        atomic_fetch_add_explicit(&glyph_render_failed, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontRenderCharGlyphImageHorizontal(font=%p, code=0x%X) -> FONT_ERR_INVALID_HANDLE", (void*)font, code);
        return FONT_ERR_INVALID_HANDLE;
    }
    if (!surface || !surface->buffer || !metrics || !result || surface->width_byte <= 0 ||
        surface->width <= 0 || surface->height <= 0 || (surface->pixel_size != 1 && surface->pixel_size != 4)) {
        atomic_fetch_add_explicit(&glyph_render_failed, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontRenderCharGlyphImageHorizontal(surf=%p, code=0x%X) -> FONT_ERR_NO_SURFACE", (void*)surface, code);
        return FONT_ERR_NO_SURFACE;
    }
    pthread_mutex_lock(&font_lock);
    int32_t rc = load_glyph(font, code, 1);
    if (rc) {
        pthread_mutex_unlock(&font_lock);
        atomic_fetch_add_explicit(&glyph_render_failed, 1, memory_order_relaxed);
        LOG_FONT_CALL("sceFontRenderCharGlyphImageHorizontal(font=%p, code=0x%X) -> load_glyph failed (0x%08x)", (void*)font, code, (uint32_t)rc);
        return rc;
    }
    FT_GlyphSlot slot = font->face->glyph;
    metrics_from_face(font, metrics);
    FT_Bitmap *bitmap = &slot->bitmap;
    int bpp = surface->pixel_size;
    int dx = (int)floorf(x) + slot->bitmap_left;
    int dy = (int)floorf(y) - slot->bitmap_top;
    int x0 = (int)surface->sc_x0, y0 = (int)surface->sc_y0;
    int x1 = (int)surface->sc_x1, y1 = (int)surface->sc_y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > surface->width) x1 = surface->width;
    if (y1 > surface->height) y1 = surface->height;
    int left = dx > x0 ? dx : x0, top = dy > y0 ? dy : y0;
    int right = dx + (int)bitmap->width < x1 ? dx + (int)bitmap->width : x1;
    int bottom = dy + (int)bitmap->rows < y1 ? dy + (int)bitmap->rows : y1;
    int pixels_drawn = 0;

    int pitch = (int)bitmap->pitch;
    size_t abs_pitch = (size_t)abs(pitch);
    size_t max_surface_bytes = (size_t)surface->height * (size_t)surface->width_byte;

    for (int py = top; py < bottom; ++py) {
        if (py < 0 || py >= surface->height) continue;
        int sy = py - dy;
        if (sy < 0 || sy >= (int)bitmap->rows) continue;

        size_t row_idx = (pitch >= 0) ? (size_t)sy : (size_t)((int)bitmap->rows - 1 - sy);
        const uint8_t *row = bitmap->buffer + row_idx * abs_pitch;

        for (int px = left; px < right; ++px) {
            if (px < 0 || px >= surface->width) continue;
            int sx = px - dx;
            if (sx < 0 || sx >= (int)bitmap->width) continue;

            uint8_t cov;
            if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                if ((size_t)(sx >> 3) >= abs_pitch) continue;
                cov = (row[sx >> 3] & (0x80u >> (sx & 7))) ? 255 : 0;
            } else {
                if ((size_t)sx >= abs_pitch) continue;
                cov = row[sx];
            }

            size_t dst_offset = (size_t)py * (size_t)surface->width_byte + (size_t)px * (size_t)bpp;
            if (dst_offset + (size_t)bpp > max_surface_bytes) {
                static _Atomic int oob_warn_count = 0;
                if (atomic_fetch_add_explicit(&oob_warn_count, 1, memory_order_relaxed) < 10) {
                    printf("Runtime: Font surface write out of bounds! offset=%zu limit=%zu (px=%d, py=%d, surf=%dx%d, stride=%d, bpp=%d)\n",
                           dst_offset + (size_t)bpp, max_surface_bytes, px, py, surface->width, surface->height, surface->width_byte, bpp);
                    fflush(stdout);
                }
                continue;
            }

            uint8_t *dst = (uint8_t *)surface->buffer + dst_offset;
            for (int c = 0; c < bpp; ++c) dst[c] = cov;
            if (cov > 0) ++pixels_drawn;
        }
    }
    result->stage = NULL; result->image.address = (uint8_t *)surface->buffer;
    result->image.width_byte = (uint32_t)surface->width_byte; result->image.pixel_size = (uint8_t)bpp;
    result->image.format = result->image.pad = 0;
    result->rect.x = (uint32_t)(left > 0 ? left : 0); result->rect.y = (uint32_t)(top > 0 ? top : 0);
    result->rect.w = (uint32_t)(right > left ? right - left : 0); result->rect.h = (uint32_t)(bottom > top ? bottom - top : 0);
    result->image.address += (size_t)result->rect.y * (size_t)surface->width_byte + (size_t)result->rect.x * (size_t)bpp;
    result->metrics.bearing_x = (float)slot->bitmap_left; result->metrics.bearing_y = (float)slot->bitmap_top;
    result->metrics.advance = (float)slot->advance.x / 64.0f; result->metrics.stride = result->metrics.advance;
    result->metrics.width = bitmap->width; result->metrics.height = bitmap->rows;
    pthread_mutex_unlock(&font_lock);

    if (bitmap->width == 0 || bitmap->rows == 0 || pixels_drawn == 0) {
        atomic_fetch_add_explicit(&glyph_render_blank, 1, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&glyph_render_successes, 1, memory_order_relaxed);
    }

    static _Atomic int glyph_render_log_count = 0;
    int cur_glyph_log = atomic_fetch_add_explicit(&glyph_render_log_count, 1, memory_order_relaxed);
    if (cur_glyph_log < 50) {
        char char_display[8] = {0};
        if (code >= 32 && code <= 126) snprintf(char_display, sizeof(char_display), "'%c'", (char)code);
        else snprintf(char_display, sizeof(char_display), "U+%04X", code);
        printf("Font Glyph Render [#%02d]: font=%p('%s') code=%s surf=[buf=%p stride=%d bpp=%d size=%dx%d clip=(%d,%d..%d,%d)] pos=(%.1f,%.1f) ft_bitmap=[size=%ux%u pitch=%d mode=%d] -> update=[(%u,%u) %ux%u] img_addr=%p drawn=%d (%s)\n",
               cur_glyph_log + 1, (void*)font, (font->face && font->face->family_name) ? font->face->family_name : "unknown",
               char_display, surface->buffer, surface->width_byte, bpp, surface->width, surface->height,
               x0, y0, x1, y1, x, y,
               bitmap->width, bitmap->rows, bitmap->pitch, bitmap->pixel_mode,
               result->rect.x, result->rect.y, result->rect.w, result->rect.h,
               (void*)result->image.address, pixels_drawn, pixels_drawn > 0 ? "OK" : "BLANK");
        fflush(stdout);
    }

    char char_display_log[8] = {0};
    if (code >= 32 && code <= 126) snprintf(char_display_log, sizeof(char_display_log), "'%c'", (char)code);
    else snprintf(char_display_log, sizeof(char_display_log), "U+%04X", code);

    LOG_FONT_CALL("sceFontRenderCharGlyphImageHorizontal(code=%s, x=%.1f, y=%.1f, surf={bpp=%d, stride=%d, %dx%d}, clip=[%d,%d..%d,%d]) -> rect={%u,%u,%ux%u}, bmp=%ux%u, drawn_pixels=%d -> %s",
                  char_display_log, x, y, bpp, surface->width_byte, surface->width, surface->height,
                  x0, y0, x1, y1, result->rect.x, result->rect.y, result->rect.w, result->rect.h,
                  bitmap->width, bitmap->rows, pixels_drawn,
                  pixels_drawn > 0 ? "OK" : "BLANK");
    return FONT_OK;
}
static ABI const void *font_select_library(int value);
static ABI const void *font_select_renderer(int value);
static ABI uint32_t ft_pixel_resolution(void) { return 0x40; }
static ABI int32_t ft_driver_init(const void *memory, void *library) {
    return memory && library ? FONT_OK : FONT_ERR_INVALID_PARAMETER;
}
static ABI int32_t ft_driver_term(void *library) { return library ? FONT_OK : FONT_ERR_INVALID_PARAMETER; }
static ABI int32_t ft_driver_support(void *library, uint32_t formats) {
    (void)formats; return library ? FONT_OK : FONT_ERR_INVALID_PARAMETER;
}
static ABI int32_t ft_driver_open(void *library, uint32_t mode, const void *data, uint32_t size,
                                  uint32_t subfont, uint32_t unique, void **out) {
    (void)mode; (void)unique;
    atomic_fetch_add_explicit(&memory_open_attempts, 1, memory_order_relaxed);
    if (out) *out = NULL;
    if (!library || !data || !size || !out) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("FontFt driver open(data=%p, size=%u) -> FONT_ERR_INVALID_PARAMETER", data, size);
        return FONT_ERR_INVALID_PARAMETER;
    }
    unsigned char magic4[4] = {0};
    if (size >= 4) memcpy(magic4, data, 4);
    char magic_ascii[5] = {0};
    for (int i = 0; i < 4; ++i) magic_ascii[i] = (magic4[i] >= 32 && magic4[i] <= 126) ? (char)magic4[i] : '.';
    magic_ascii[4] = '\0';

    pthread_once(&ft_once, init_freetype);
    if (ft_init_error) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("FontFt driver open: FT_Init_FreeType failed (0x%08x)", (uint32_t)ft_init_error);
        return FONT_ERR_INVALID_MEMORY;
    }
    FontHandle *font = calloc(1, sizeof(*font));
    if (!font) {
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("FontFt driver open: calloc failed");
        return (int32_t)0x80460010;
    }
    pthread_mutex_lock(&font_lock);
    FT_Error error = FT_New_Memory_Face(ft_library, data, (FT_Long)size, (FT_Long)subfont, &font->face);
    pthread_mutex_unlock(&font_lock);
    if (error) {
        free(font);
        atomic_fetch_add_explicit(&memory_open_failures, 1, memory_order_relaxed);
        LOG_FONT_CALL("FontFt driver open(size=%u, magic=[%02X %02X %02X %02X] '%s') -> FreeType REJECTED with error 0x%08x",
                      size, magic4[0], magic4[1], magic4[2], magic4[3], magic_ascii, (uint32_t)error);
        return (int32_t)0x80460036;
    }
    font->magic = 0x0f02; font->scale_w = font->scale_h = 16.0f; font->opaque.reserved[0] = 0x0f02;
    *out = font;
    atomic_fetch_add_explicit(&memory_open_successes, 1, memory_order_relaxed);
    LOG_FONT_CALL("FontFt driver open(size=%u, magic=[%02X %02X %02X %02X] '%s', subfont=%u) -> ACCEPTED (face=%p, family='%s', glyphs=%ld) -> font=%p",
                  size, magic4[0], magic4[1], magic4[2], magic4[3], magic_ascii,
                  subfont, (void*)font->face, font->face->family_name ? font->face->family_name : "unknown",
                  font->face->num_glyphs, (void*)font);
    return FONT_OK;
}
static ABI int32_t ft_driver_close(void *object, uint32_t flags) {
    (void)flags; FontHandle *font = object;
    if (!font || font->magic != 0x0f02) return FONT_ERR_INVALID_HANDLE;
    pthread_mutex_lock(&font_lock); if (font->face) FT_Done_Face(font->face); pthread_mutex_unlock(&font_lock);
    font->face = NULL; font->magic = 0; free(font); return FONT_OK;
}
static ABI int32_t ft_driver_scale(void *object, uint16_t *units, float *scale) {
    FontHandle *font = object;
    if (!font || font->magic != 0x0f02) return FONT_ERR_INVALID_HANDLE;
    if (units) *units = font->face->units_per_EM;
    if (scale) *scale = 1.0f;
    return FONT_OK;
}
static ABI int32_t ft_driver_metric(void *object, uint32_t metric_id, uint16_t *out) {
    (void)metric_id; FontHandle *font = object;
    if (!font || font->magic != 0x0f02 || !out) return FONT_ERR_INVALID_PARAMETER;
    *out = (uint16_t)font->face->units_per_EM; return FONT_OK;
}
static ABI int32_t ft_driver_glyph_count(void *object, uint32_t *out) {
    FontHandle *font = object;
    if (!font || font->magic != 0x0f02 || !out) return FONT_ERR_INVALID_PARAMETER;
    *out = (uint32_t)font->face->num_glyphs; return FONT_OK;
}
static ABI int32_t ft_driver_glyph_index(void *object, uint32_t code, uint32_t *out) {
    FontHandle *font = object;
    if (!font || font->magic != 0x0f02 || !out) return FONT_ERR_INVALID_PARAMETER;
    *out = FT_Get_Char_Index(font->face, code); return *out ? FONT_OK : FONT_ERR_NO_GLYPH;
}
static ABI int32_t ft_driver_set_char(void *object, uint32_t dpi_x, uint32_t dpi_y, float w,
                                      float h, float *out_w, float *out_h) {
    (void)dpi_x; (void)dpi_y;
    int32_t rc = font_set_scale(object, w, h);
    if (!rc) { if (out_w) *out_w = w; if (out_h) *out_h = h; }
    return rc;
}
static ABI int32_t ft_driver_set_char_default(void *object, float w, float h, float *out_w, float *out_h) {
    int32_t rc = font_set_scale(object, w, h);
    if (!rc) { if (out_w) *out_w = w; if (out_h) *out_h = h; }
    return rc;
}
static ABI int32_t ft_driver_layout(void *object, const void *style, uint8_t (*words)[16]) {
    if (!object || !style || !words) return FONT_ERR_INVALID_PARAMETER;
    memset(words, 0, 16); return FONT_OK;
}
static ABI int32_t ft_driver_load_glyph(void *object, uint32_t glyph, int32_t mode, uint64_t *words) {
    (void)mode; FontHandle *font = object;
    if (!font || font->magic != 0x0f02 || !words) return FONT_ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&font_lock); FT_Error error = FT_Load_Glyph(font->face, glyph, FT_LOAD_DEFAULT); pthread_mutex_unlock(&font_lock);
    *words = 0; return error ? FONT_ERR_NO_GLYPH : FONT_OK;
}
static ABI int32_t ft_driver_glyph_metrics(void *object, uint32_t *optional, uint8_t mode,
                                           uint8_t *params, FontMetrics *metrics) {
    (void)optional; (void)mode; (void)params; FontHandle *font = object;
    if (!font || font->magic != 0x0f02 || !metrics) return FONT_ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&font_lock); metrics_from_face(font, metrics); pthread_mutex_unlock(&font_lock);
    return FONT_OK;
}
static ABI int32_t ft_driver_glyph_adjust(void *object, uint32_t p2, uint32_t glyph, int32_t p4,
                                          int32_t p5, uint32_t *inout) {
    (void)p2; (void)glyph; (void)p4; (void)p5;
    return object && inout ? FONT_OK : FONT_ERR_INVALID_PARAMETER;
}
static ABI int32_t ft_driver_configure_glyph(void *object, uint32_t *params, int32_t mode, uint32_t *state) {
    (void)mode; return object && params && state ? FONT_OK : FONT_ERR_INVALID_PARAMETER;
}
static ABI int32_t ft_renderer_create(void *renderer) {
    if (!renderer) return FONT_ERR_INVALID_RENDERER;
    uint8_t *bytes = renderer; memset(bytes, 0, 0x168); *(void **)(bytes + 0x120) = bytes;
    *(void **)(bytes + 0x140) = renderer; return FONT_OK;
}
static ABI int32_t ft_renderer_destroy(void *renderer) {
    if (!renderer) return FONT_ERR_INVALID_RENDERER;
    *(void **)((uint8_t *)renderer + 0x140) = NULL; return FONT_OK;
}
static ABI uint64_t ft_renderer_query(void *renderer, uint8_t *params, int64_t *out, uint8_t (*vec)[16]) {
    (void)renderer; (void)params; if (out) *out = 0; if (vec) memset(vec, 0, 16); return 0;
}
static ABI const void *font_select_library(int value) {
    LOG_FONT_CALL("sceFontSelectLibraryFt(val=%d) -> %p", value, value == 0 ? &ft_library_table : NULL);
    if (value != 0) return NULL;
    memset(&ft_library_table, 0, sizeof(ft_library_table));
    ft_library_table.pixel_resolution = (uintptr_t)ft_pixel_resolution;
    ft_library_table.init = (uintptr_t)ft_driver_init; ft_library_table.term = (uintptr_t)ft_driver_term;
    ft_library_table.support = (uintptr_t)ft_driver_support; ft_library_table.open = (uintptr_t)ft_driver_open;
    ft_library_table.close = (uintptr_t)ft_driver_close; ft_library_table.scale = (uintptr_t)ft_driver_scale;
    ft_library_table.metric = (uintptr_t)ft_driver_metric; ft_library_table.glyphs_count = (uintptr_t)ft_driver_glyph_count;
    ft_library_table.glyph_index = (uintptr_t)ft_driver_glyph_index; ft_library_table.set_char_dpi = (uintptr_t)ft_driver_set_char;
    ft_library_table.set_char_default_dpi = (uintptr_t)ft_driver_set_char_default;
    ft_library_table.compute_layout = (uintptr_t)ft_driver_layout; ft_library_table.compute_layout_alt = (uintptr_t)ft_driver_layout;
    ft_library_table.load_glyph_cached = (uintptr_t)ft_driver_load_glyph; ft_library_table.get_glyph_metrics = (uintptr_t)ft_driver_glyph_metrics;
    ft_library_table.apply_glyph_adjust = (uintptr_t)ft_driver_glyph_adjust; ft_library_table.configure_glyph = (uintptr_t)ft_driver_configure_glyph;
    return &ft_library_table;
}
static ABI const void *font_select_renderer(int value) {
    LOG_FONT_CALL("sceFontSelectRendererFt(val=%d) -> %p", value, value == 0 ? &ft_renderer_table : NULL);
    if (value != 0) return NULL;
    ft_renderer_table.create = (uintptr_t)ft_renderer_create; ft_renderer_table.destroy = (uintptr_t)ft_renderer_destroy;
    ft_renderer_table.query = (uintptr_t)ft_renderer_query; return &ft_renderer_table;
}

static const RuntimeExport exports[] = {
    {"sceFontAttachDeviceCacheBuffer", font_attach_cache}, {"sceFontBindRenderer", font_bind_renderer},
    {"sceFontCloseFont", font_close}, {"sceFontCreateLibraryWithEdition", font_library_create},
    {"sceFontCreateRendererWithEdition", font_renderer_create}, {"sceFontDestroyLibrary", font_library_destroy},
    {"sceFontDestroyRenderer", font_renderer_destroy}, {"sceFontGetCharGlyphMetrics", font_get_metrics},
    {"sceFontGetKerning", font_get_kerning}, {"sceFontGetScalePixel", font_get_scale},
    {"sceFontMemoryInit", font_memory_init}, {"sceFontMemoryTerm", font_memory_term},
    {"sceFontOpenFontMemory", font_open_memory}, {"sceFontRenderCharGlyphImageHorizontal", font_render_glyph},
    {"sceFontRenderSurfaceInit", font_surface_init}, {"sceFontSetScalePixel", font_set_scale},
    {"sceFontSetupRenderScalePixel", font_setup_scale}, {"sceFontSupportExternalFonts", font_support_external},
    {"sceFontSupportSystemFonts", font_support_system}, {"sceFontUnbindRenderer", font_unbind_renderer},
    {"sceFontSelectLibraryFt", font_select_library}, {"sceFontSelectRendererFt", font_select_renderer},
};
uintptr_t runtime_font_resolve(const char *name) {
    static int disabled = -1;
    if (disabled == -1) {
        const char *env = getenv("LIAN_NO_FONT_HLE");
        disabled = (env && (!strcmp(env, "1") || !strcmp(env, "true") || !strcmp(env, "yes"))) ? 1 : 0;
        if (disabled) {
            printf("Runtime: LIAN_NO_FONT_HLE=1 active (font HLE disabled, falling back to dummy stubs)\n");
            fflush(stdout);
        }
    }
    if (disabled) return 0;
    return RUNTIME_LOOKUP(exports, name);
}
void runtime_font_report(void) {
    size_t libs = (size_t)atomic_load_explicit(&library_creates, memory_order_relaxed);
    size_t rens = (size_t)atomic_load_explicit(&renderer_creates, memory_order_relaxed);
    size_t surfs = (size_t)atomic_load_explicit(&surfaces_initialized, memory_order_relaxed);
    size_t opens_ok = (size_t)atomic_load_explicit(&memory_open_successes, memory_order_relaxed);
    size_t opens_fail = (size_t)atomic_load_explicit(&memory_open_failures, memory_order_relaxed);
    size_t glyphs_ok = (size_t)atomic_load_explicit(&glyph_render_successes, memory_order_relaxed);
    size_t glyphs_blank = (size_t)atomic_load_explicit(&glyph_render_blank, memory_order_relaxed);
    size_t glyphs_fail = (size_t)atomic_load_explicit(&glyph_render_failed, memory_order_relaxed);
    size_t glyph_attempts = (size_t)atomic_load_explicit(&glyph_render_attempts, memory_order_relaxed);

    printf("Runtime: font libraries created=%zu, renderers created=%zu, surfaces initialized=%zu\n", libs, rens, surfs);
    printf("Runtime: font opens ok=%zu, failed=%zu (total attempts=%zu)\n", opens_ok, opens_fail, opens_ok + opens_fail);
    printf("Runtime: glyph renders ok=%zu, blank=%zu, failed=%zu (total attempts=%zu)\n", glyphs_ok, glyphs_blank, glyphs_fail, glyph_attempts);
}
