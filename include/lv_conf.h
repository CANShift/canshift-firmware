/* Reference: https://github.com/lvgl/lvgl/blob/v8.3.11/lv_conf_template.h */
#if 1 

    #ifndef LV_CONF_H
        #define LV_CONF_H

        #include <stdint.h>
        #include "hardware_profile.h"
        #include "app_config.h"

        #define LV_COLOR_DEPTH 16

        #define LV_COLOR_16_SWAP 0

        #define LV_COLOR_SCREEN_TRANSP 0

        #define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

    #ifndef LV_MEM_SIZE

        #define LV_MEM_SIZE (80U * 1024U)
    #endif

        #define LV_MEM_ADR 0U

        #if LV_MEM_ADR == 0U
            #define LV_MEM_POOL_INCLUDE <stdlib.h>
            #define LV_MEM_POOL_ALLOC malloc
        #endif

        #define LV_DISP_DEF_REFR_PERIOD 20 

        #define LV_INDEV_DEF_READ_PERIOD 15 

        #define LV_TICK_CUSTOM 0

        #define LV_DPI_DEF 100

        #define LV_HOR_RES_MAX HW_DISPLAY_WIDTH
        #define LV_VER_RES_MAX HW_DISPLAY_HEIGHT

        #define LV_DRAW_COMPLEX 1

        #define LV_SHADOW_CACHE_SIZE 0

        #define LV_CIRCLE_CACHE_SIZE 4

        #define LV_USE_DRAW_MASKS 1

        #define LV_USE_GPU_STM32_DMA2D 0
        #define LV_USE_GPU_SDL 0

        #define LV_USE_LOG 1
        #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
        #define LV_LOG_PRINTF 1

        #define LV_USE_ASSERT_NULL 1
        #define LV_USE_ASSERT_MALLOC 1
        #define LV_USE_ASSERT_STYLE 0
        #define LV_USE_ASSERT_MEM_INTEGRITY 0
        #define LV_USE_ASSERT_OBJ 0

        #define LV_ASSERT_HANDLER_INCLUDE "diag/lvgl_assert.h"
        #define LV_ASSERT_HANDLER canshift_lvgl_assert_handler();

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

        #define LV_FONT_MONTSERRAT_12_SUBPX 0
        #define LV_FONT_MONTSERRAT_28_COMPRESSED 0
        #define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
        #define LV_FONT_SIMSUN_16_CJK 0

        #define LV_FONT_UNSCII_8 0
        #define LV_FONT_UNSCII_16 0

        #define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_jbmono_medium_14_nk)

        #define LV_FONT_DEFAULT &lv_font_jbmono_medium_14_nk

        #define LV_FONT_FMT_TXT_LARGE 0

        #define LV_USE_FONT_COMPRESSED 0

        #define LV_USE_FONT_SUBPX 0

        #define LV_TXT_ENC LV_TXT_ENC_UTF8

        #define LV_TXT_BREAK_CHARS " ,.;:-_"

        #define LV_TXT_LINE_BREAK_LONG_LEN 0

        #define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3

        #define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3

        #define LV_TXT_COLOR_CMD "#"

        #define LV_USE_BIDI 0

        #define LV_USE_ARABIC_PERSIAN_CHARS 0

        #define LV_USE_ARC 1 
        #define LV_USE_BAR 1 
        #define LV_USE_BTN 1 
        #define LV_USE_BTNMATRIX 0
        #define LV_USE_CANVAS 0
        #define LV_USE_CHECKBOX 0
        #define LV_USE_DROPDOWN 0 
        #define LV_USE_IMG 1      
        
        #define LV_IMG_CACHE_DEF_SIZE 24
        #define LV_USE_LABEL 1 
        #define LV_USE_LINE 1     
        #define LV_USE_ROLLER 0
        #define LV_USE_SLIDER 1 
        #define LV_USE_SWITCH 0
        #define LV_USE_TEXTAREA 0
        #define LV_USE_TABLE 0

        #define LV_USE_ANIMIMG 0
        #define LV_USE_CALENDAR 0
        #define LV_USE_CHART 0 
        #define LV_USE_COLORWHEEL 0
        #define LV_USE_IMGBTN 0 
        #define LV_USE_KEYBOARD 0
        #define LV_USE_LED 0 
        #define LV_USE_LIST 0
        #define LV_USE_MENU 0
        #define LV_USE_METER 0 
        #define LV_USE_MSGBOX 0
        #define LV_USE_SPAN 0
        #define LV_USE_SPINBOX 0
        #define LV_USE_SPINNER 0
        #define LV_USE_TABVIEW 0
        #define LV_USE_TILEVIEW 0
        #define LV_USE_WIN 0

        #define LV_USE_THEME_DEFAULT 1
        
        #define LV_THEME_DEFAULT_DARK 1
        
        #define LV_THEME_DEFAULT_GROW 0
        
        #define LV_THEME_DEFAULT_TRANSITION_TIME 80

        #define LV_USE_THEME_BASIC 1

        #define LV_USE_THEME_MONO 0

        #define LV_USE_ANIMATION 1

#define LV_USE_FS_IF   0    
#define LV_USE_PNG     0    
#define LV_USE_BMP     1    
#define LV_USE_SJPG    0    
#define LV_USE_GIF     0    
#define LV_USE_QRCODE  0

        #define LV_USE_GESTURE_RECOGNITION 0

        #define LV_USE_PERF_MONITOR 0
        #if APP_DEBUG_BUILD || (defined(APP_PROFILE_UI) && APP_PROFILE_UI)
            #undef LV_USE_PERF_MONITOR
            #define LV_USE_PERF_MONITOR 1
            #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
        #endif

        #define LV_USE_MEM_MONITOR 0

        #define LV_USE_REFR_DEBUG 0

        #define LV_DISP_ROT_MAX_BUF (10U * 1024U)

    #endif 
#endif     
