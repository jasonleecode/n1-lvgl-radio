#include <SDL2/SDL.h>

SDL_Window *__real_SDL_CreateWindow(const char *title,int x,int y,int w,int h,Uint32 flags);

// LVGL's installed SDL driver initially creates a visible 100%-size window.
// Wrap only this executable's creation call; map the window after layout/zoom.
SDL_Window *__wrap_SDL_CreateWindow(const char *title,int x,int y,int w,int h,Uint32 flags) {
    return __real_SDL_CreateWindow(title,x,y,w,h,(flags & ~SDL_WINDOW_SHOWN)|SDL_WINDOW_HIDDEN);
}
