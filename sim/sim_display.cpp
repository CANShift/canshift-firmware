#include "sim_display.h"

#include <SDL.h>
#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

SDL_Window *s_window = nullptr;
SDL_Renderer *s_renderer = nullptr;
SDL_Texture *s_texture = nullptr;
int s_width = 0;
int s_height = 0;
bool s_quit = false;
bool s_screenshot = false;

constexpr size_t kKeyQueueSize = 8;
int s_keyQueue[kKeyQueueSize] = {};
size_t s_keyHead = 0;
size_t s_keyTail = 0;

lv_disp_draw_buf_t s_drawBuf;
lv_color_t *s_buf1 = nullptr;
uint32_t *s_frame = nullptr;

bool s_mouseDown = false;
int s_mouseX = 0;
int s_mouseY = 0;

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            const lv_color_t c = *pixels++;
            s_frame[y * s_width + x] = 0xFF000000u | (lv_color_to32(c) & 0x00FFFFFFu);
        }
    }
    if (lv_disp_flush_is_last(drv)) {
        SDL_UpdateTexture(s_texture, nullptr, s_frame, s_width * 4);
        SDL_RenderClear(s_renderer);
        SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
        SDL_RenderPresent(s_renderer);
    }
    lv_disp_flush_ready(drv);
}

void pointerReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point.x = static_cast<lv_coord_t>(s_mouseX);
    data->point.y = static_cast<lv_coord_t>(s_mouseY);
    data->state = s_mouseDown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace

namespace SimDisplay {

bool init(int width, int height, int zoom) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    s_width = width;
    s_height = height;

    s_window = SDL_CreateWindow("CANShift sim", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width * zoom, height * zoom, 0);
    if (!s_window)
        return false;
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
    if (!s_renderer)
        return false;
    SDL_RenderSetLogicalSize(s_renderer, width, height);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  width, height);
    if (!s_texture)
        return false;

    s_frame = static_cast<uint32_t *>(calloc(static_cast<size_t>(width) * height, 4));

    const uint32_t bufPixels = static_cast<uint32_t>(width) * 40;
    s_buf1 = static_cast<lv_color_t *>(malloc(bufPixels * sizeof(lv_color_t)));
    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, nullptr, bufPixels);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = static_cast<lv_coord_t>(width);
    dispDrv.ver_res = static_cast<lv_coord_t>(height);
    dispDrv.draw_buf = &s_drawBuf;
    dispDrv.flush_cb = flushCb;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = pointerReadCb;
    lv_indev_drv_register(&indevDrv);

    return true;
}

void pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                s_quit = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                s_mouseDown = true;
                s_mouseX = ev.button.x;
                s_mouseY = ev.button.y;
                break;
            case SDL_MOUSEBUTTONUP:
                s_mouseDown = false;
                break;
            case SDL_MOUSEMOTION:
                s_mouseX = ev.motion.x;
                s_mouseY = ev.motion.y;
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    s_quit = true;
                } else if (ev.key.keysym.sym == SDLK_s) {
                    s_screenshot = true;
                } else if ((s_keyTail + 1) % kKeyQueueSize != s_keyHead) {
                    s_keyQueue[s_keyTail] = ev.key.keysym.sym;
                    s_keyTail = (s_keyTail + 1) % kKeyQueueSize;
                }
                break;
            default:
                break;
        }
    }
}

int pollKey() {
    if (s_keyHead == s_keyTail)
        return 0;
    const int key = s_keyQueue[s_keyHead];
    s_keyHead = (s_keyHead + 1) % kKeyQueueSize;
    return key;
}

bool quitRequested() {
    return s_quit;
}

bool screenshotRequested() {
    const bool req = s_screenshot;
    s_screenshot = false;
    return req;
}

void writeScreenshot(const char *path) {
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(s_frame, s_width, s_height, 32,
                                                           s_width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!surf)
        return;
    SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    printf("screenshot written: %s\n", path);
}

} // namespace SimDisplay
