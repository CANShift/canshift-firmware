/**
 * lv_conf.h — LVGL 8.3 compile-time configuration
 * Tuned for ESP32 with 320x240 ILI9341 display
 *
 * Reference: https://github.com/lvgl/lvgl/blob/v8.3.11/lv_conf_template.h
 */

#if 1 /* Enable this file */

    #ifndef LV_CONF_H
        #define LV_CONF_H

        #include <stdint.h>
        #include "hardware_profile.h"
        #include "app_config.h"

        /*====================
   COLOR SETTINGS
 *====================*/

        /* Color depth: 1 (1 byte per pixel), 8, 16, 32 */
        #define LV_COLOR_DEPTH 16

        /* Swap the 2 bytes of RGB565 color — required for many SPI displays.
 * Set to 0 here because flushCallback already calls pushColors(.., true) which
 * does the swap itself. Two swaps = no swap = wrong byte order on this panel
 * (caused flicker/drift, see #40). Matches Elecrow's factory LVGL config. */
        #define LV_COLOR_16_SWAP 0

        /* Enable features to use with LV_COLOR_DEPTH = 32 */
        #define LV_COLOR_SCREEN_TRANSP 0

        /* Images pixels with this color will not be drawn. */
        #define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

        /*=========================
   MEMORY SETTINGS
 *=========================*/

        /* Size of the memory available for `lv_mem_alloc()` in bytes */
        #if APP_SIMULATION_MODE
            #define LV_MEM_SIZE                                                                    \
                (12U * 1024U) /* 12 KB — sim mode (CI compile check only, no runtime) */
        #else
            /* 64 KB — hotfix for v0.8.0 boot loop (issue #483). The previous
   96 KB ask succeeded against ~110 KB largest free block at boot, but
   left only ~14 KB headroom for the contiguous allocations LVGL still
   does inside lv_init() (display LL nodes, draw buffers, theme styles)
   and tripped LV_ASSERT_MALLOC → infinite halt → watchdog reboot.
   64 KB fits 3-4 of the 8 SPIFFS-loaded Orbitron sizes; FontManager
   degrades gracefully to the in-flash 14 px Medium fallback per size
   that fails to load, and the device stays usable. Allocated via
   malloc (LV_MEM_POOL_ALLOC), so this line consumes runtime heap, not
   bss. */
            #define LV_MEM_SIZE (64U * 1024U)
        #endif

        /* Set an address for the memory pool instead of allocating it as a global array.
   Can be in external SRAM too. */
        #define LV_MEM_ADR 0U

        /* Instead of an address give a memory allocator that will be called to get a
   memory pool for LVGL. E.g. my_malloc */
        #if LV_MEM_ADR == 0U
            #define LV_MEM_POOL_INCLUDE <stdlib.h>
            #define LV_MEM_POOL_ALLOC malloc
        #endif

        /*====================
   HAL SETTINGS
 *====================*/

        /* Default display refresh period. LVG will redraw changed areas with this period time */
        #define LV_DISP_DEF_REFR_PERIOD 20 /* ms — 50 fps */

        /* Input device read period in milliseconds — halved from 30 ms for
   snappier press → click latency on the resistive panel (issue #95, F2).
   The read callback is cheap (XPT2046 SPI transfer), so the higher poll
   rate has no measurable impact on the 27 MHz shared SPI bus. */
        #define LV_INDEV_DEF_READ_PERIOD 15 /* ms */

        /* Use a custom tick source that tells the elapsed time in milliseconds.
   It removes the need to manually update the tick with `lv_tick_inc()` */
        #define LV_TICK_CUSTOM 0

        /* Default Dot Per Inch.
   Used to initialize default sizes such as widgets sized, style paddings.
   Range: 80..1200 */
        #define LV_DPI_DEF 100

        /*=======================
   DRAW CONFIGURATION
 *=======================*/

        /* Required minimum level of complexity for precomputed data in draw descriptors */
        #define LV_DRAW_COMPLEX 1

        /* Allow buffering some shadow calculation.
   LV_SHADOW_CACHE_SIZE is the max. shadow size to buffer, where shadow size
   is `shadow_width + radius`. Caching has LV_SHADOW_CACHE_SIZE^2 RAM cost */
        #define LV_SHADOW_CACHE_SIZE 0

        /* Set number of maximally cached circle data (2 + 8 bytes per unit) */
        #define LV_CIRCLE_CACHE_SIZE 4

        /* Limit draw complexity — disable arcs for performance if needed */
        /* Keep at 1 for gauge/arc widgets */
        #define LV_USE_DRAW_MASKS 1

        /*====================
   GPU
 *====================*/

        /* Use ESP32-S3 PPA or similar accelerators if available */
        /* For ESP32 (non-S3), all disabled */
        #define LV_USE_GPU_STM32_DMA2D 0
        #define LV_USE_GPU_SDL 0

        /*==================
 * LOGGING
 *==================*/

        /* Enable the log module */
        #if APP_DEBUG_BUILD
            #define LV_USE_LOG 1
            #define LV_LOG_LEVEL LV_LOG_LEVEL_INFO
            #define LV_LOG_PRINTF 1
        #else
            #define LV_USE_LOG 0
        #endif

        /*=================
 * ASSERT
 *================*/

        /* Enable asserts if expr is false. */
        #define LV_USE_ASSERT_NULL 1
        #define LV_USE_ASSERT_MALLOC 1
        #define LV_USE_ASSERT_STYLE 0
        #define LV_USE_ASSERT_MEM_INTEGRITY 0
        #define LV_USE_ASSERT_OBJ 0

        /* Add a custom handler when assert happens e.g. to restart the MCU */
        #define LV_ASSERT_HANDLER_INCLUDE "diag/lvgl_assert.h"
        #define LV_ASSERT_HANDLER canshift_lvgl_assert_handler();

        /*==================
 *    FONT USAGE
 *==================*/

        /* Montserrat fonts — every upstream size is OFF. The dashboard now
   uses Orbitron (issue #431), shipped as runtime-loaded SPIFFS .bin files
   plus a single in-flash fallback. Montserrat is dead code, but the LVGL
   build defaults all these macros to 1, so we explicitly disable them. */
        #define LV_FONT_MONTSERRAT_8 0
        #define LV_FONT_MONTSERRAT_10 0
        #define LV_FONT_MONTSERRAT_12 0
        #define LV_FONT_MONTSERRAT_14 0
        #define LV_FONT_MONTSERRAT_16 0
        #define LV_FONT_MONTSERRAT_18 0
        #define LV_FONT_MONTSERRAT_20 0
        #define LV_FONT_MONTSERRAT_22 0
        #define LV_FONT_MONTSERRAT_24 0
        #define LV_FONT_MONTSERRAT_26 0
        #define LV_FONT_MONTSERRAT_28 0
        #define LV_FONT_MONTSERRAT_30 0
        #define LV_FONT_MONTSERRAT_32 0
        #define LV_FONT_MONTSERRAT_34 0
        #define LV_FONT_MONTSERRAT_36 0
        #define LV_FONT_MONTSERRAT_38 0
        #define LV_FONT_MONTSERRAT_40 0
        #define LV_FONT_MONTSERRAT_42 0
        #define LV_FONT_MONTSERRAT_44 0
        #define LV_FONT_MONTSERRAT_46 0
        #define LV_FONT_MONTSERRAT_48 0

        /* Demonstrate a font here if you need something other than normal range */
        #define LV_FONT_MONTSERRAT_12_SUBPX 0
        #define LV_FONT_MONTSERRAT_28_COMPRESSED 0
        #define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
        #define LV_FONT_SIMSUN_16_CJK 0

        /* Always enable this plain */
        #define LV_FONT_UNSCII_8 0
        #define LV_FONT_UNSCII_16 0

        /* All Orbitron sizes are lv_font_load()'d from SPIFFS at boot
   (issue #410, refreshed in #431). The only built-in font we keep in
   flash is the 14 px Medium variant — used by FontManager as a fallback
   when a runtime load fails (e.g. fresh-flash device without
   `pio run -t uploadfs`). Body lives in
   src/ui/fonts/lv_font_orbitron_medium_14_nk.c. */
        #define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_orbitron_medium_14_nk)

        /* Default font: pick the in-flash Orbitron Medium 14 we ship ourselves. */
        #define LV_FONT_DEFAULT &lv_font_orbitron_medium_14_nk

        /* Enable handling large font and/or fonts with a lot of characters.
   The compiler error message will hep to determine the need for it. */
        #define LV_FONT_FMT_TXT_LARGE 0

        /* Enables/disables support for compressed fonts. */
        #define LV_USE_FONT_COMPRESSED 0

        /* Enable subpixel rendering */
        #define LV_USE_FONT_SUBPX 0

        /*=================
 *  TEXT SETTINGS
 *=================*/

        /**
 * Select a character encoding for strings.
 * Your IDE or editor should have the same character encoding
 * - LV_TXT_ENC_UTF8
 * - LV_TXT_ENC_ASCII
 */
        #define LV_TXT_ENC LV_TXT_ENC_UTF8

        /* Can break (wrap) texts on these chars */
        #define LV_TXT_BREAK_CHARS " ,.;:-_"

        /* If a word is at least this long, will break wherever "prettiest" */
        #define LV_TXT_LINE_BREAK_LONG_LEN 0

        /* Minimum number of characters in a long word to put on a line before a break. */
        #define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3

        /* Minimum number of characters in a long word to put on a line after a break. */
        #define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

        /* The control character to use for signaling text recolor. */
        #define LV_TXT_COLOR_CMD "#"

        /* Support bidirectional texts. */
        #define LV_USE_BIDI 0

        /* Enable Arabic/Persian processing */
        #define LV_USE_ARABIC_PERSIAN_CHARS 0

    /*==================
 *  WIDGET USAGE
 *==================*/

        #define LV_USE_ARC 1 /* Gauge arcs */
        #define LV_USE_BAR 1 /* Progress bars */
        #define LV_USE_BTN 1 /* Buttons */
        #define LV_USE_BTNMATRIX 0
        #define LV_USE_CANVAS 0
        #define LV_USE_CHECKBOX 0
        #define LV_USE_DROPDOWN 0 /* Not needed for dash UI */
        #define LV_USE_IMG 1      /* Background images and icons */
        #define LV_USE_LABEL 1    /* Text labels */
        #define LV_USE_LINE 0     /* Unused — drop class to save flash */
        #define LV_USE_ROLLER 0
        #define LV_USE_SLIDER 1 /* Settings page brightness/contrast sliders */
        #define LV_USE_SWITCH 0
        #define LV_USE_TEXTAREA 0
        #define LV_USE_TABLE 0

    /*==================
 * EXTRA COMPONENTS
 *==================*/

        #define LV_USE_ANIMIMG 0
        #define LV_USE_CALENDAR 0
        #define LV_USE_CHART 0 /* Could use for graphs later */
        #define LV_USE_COLORWHEEL 0
        #define LV_USE_IMGBTN 0 /* Unused — top bar uses lv_img directly */
        #define LV_USE_KEYBOARD 0
        #define LV_USE_LED 0 /* Unused — warning indicators use LV_USE_IMG */
        #define LV_USE_LIST 0
        #define LV_USE_MENU 0
        #define LV_USE_METER 0 /* Unused — gauges use LV_USE_ARC + custom drawing */
        #define LV_USE_MSGBOX 0
        #define LV_USE_SPAN 0
        #define LV_USE_SPINBOX 0
        #define LV_USE_SPINNER 0
        #define LV_USE_TABVIEW 0
        #define LV_USE_TILEVIEW 0
        #define LV_USE_WIN 0

        /*=====================
 * THEME USAGE
 *=====================*/

        /* A simple, impressive and very complete theme */
        #define LV_USE_THEME_DEFAULT 1
        /* 0: Light mode; 1: Dark mode */
        #define LV_THEME_DEFAULT_DARK 1
        /* 1: Enable grow on press */
        #define LV_THEME_DEFAULT_GROW 0
        /* Default transition time in [ms] */
        #define LV_THEME_DEFAULT_TRANSITION_TIME 80

        /* A very simple theme that is a good starting point for a custom theme */
        #define LV_USE_THEME_BASIC 1

        /* A theme designed for monochrome displays */
        #define LV_USE_THEME_MONO 0

        /*==================
 * ANIMATIONS
 *==================*/

        /* Use LVGL animations (page transitions, gauge movement smoothing) */
        #define LV_USE_ANIMATION 1

        /*==================
 * FILESYSTEM
 *==================*/

/* LVGL file system for loading images from SPIFFS */
#define LV_USE_FS_IF   0    /* Not used — images are loaded via custom decoder */
#define LV_USE_PNG     0    /* PNG decoding — disabled to save memory; use BMP or raw arrays */
#define LV_USE_BMP     1    /* BMP support for background images */
#define LV_USE_SJPG    0    /* SJPG — disabled */
#define LV_USE_GIF     0    /* GIF — disabled */
#define LV_USE_QRCODE  0

        /*==================
 * OTHERS
 *==================*/

        /* Gesture detection */
        #define LV_USE_GESTURE_RECOGNITION 0

        /* 1: Show CPU usage and FPS count in the corner.
   Independently flippable from APP_DEBUG_BUILD via APP_PROFILE_UI so the
   `[env:debug-perf]` profile can render the on-screen overlay without the
   full debug log spam (issue #95, F7). */
        #define LV_USE_PERF_MONITOR 0
        #if APP_DEBUG_BUILD || (defined(APP_PROFILE_UI) && APP_PROFILE_UI)
            #undef LV_USE_PERF_MONITOR
            #define LV_USE_PERF_MONITOR 1
            #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
        #endif

        /* 1: Show the used memory and the memory fragmentation */
        #define LV_USE_MEM_MONITOR 0

        /* 1: Draw random colored rectangles over the redrawn areas */
        #define LV_USE_REFR_DEBUG 0

        /* Maximum buffer size to allocate for rotation.
   Only used if software rotation is enabled in the display driver. */
        #define LV_DISP_ROT_MAX_BUF (10U * 1024U)

    #endif /* LV_CONF_H */
#endif     /* Enable this file */
