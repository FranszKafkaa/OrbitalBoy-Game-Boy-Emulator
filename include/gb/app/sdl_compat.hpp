#pragma once

#ifdef GBEMU_USE_SDL2

#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#elif __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#error "SDL2 headers not found. Install SDL2 or build with -DGBEMU_USE_SDL2=OFF."
#endif
#else
#include <SDL.h>
#endif

#ifdef GBEMU_USE_SDL2_IMAGE
#if defined(__has_include)
#if __has_include(<SDL_image.h>)
#include <SDL_image.h>
#elif __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#else
#error "SDL2_image headers not found."
#endif
#else
#include <SDL_image.h>
#endif
#endif

#endif
