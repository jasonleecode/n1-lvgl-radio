#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static Uint32 received;
static int width,height;
SDL_Window *__real_SDL_CreateWindow(const char *title,int x,int y,int w,int h,Uint32 flags) {
 (void)title;(void)x;(void)y;received=flags;width=w;height=h;return (SDL_Window*)(uintptr_t)1;
}
SDL_Window *__wrap_SDL_CreateWindow(const char *,int,int,int,int,Uint32);
int main(void) {
 assert(__wrap_SDL_CreateWindow("test",0,0,1040,800,SDL_WINDOW_RESIZABLE|SDL_WINDOW_SHOWN));
 assert(received&SDL_WINDOW_HIDDEN);assert(!(received&SDL_WINDOW_SHOWN));assert(received&SDL_WINDOW_RESIZABLE);assert(width==1040&&height==800);
 puts("PASS: initial SDL window is hidden while preserving all other flags");return 0;
}
