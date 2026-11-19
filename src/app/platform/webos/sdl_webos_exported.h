#pragma once

/*
 * Own variant: build fix for third_party/ss4s/modules/webos/smp/src/smp_resource_webos5.c.
 *
 * That file calls SDL_webOSGetRefreshRate / SDL_webOS*ExportedWindow through <SDL.h>. Only the
 * webOS SDL2 port (webosbrew SDL-webOS, in the toolchain sysroot) declares them; sdl2-compat
 * (ext_sdl2_compat, built during the build) does not. Which SDL.h the file sees depends on whether
 * ext_sdl2_compat has installed its headers yet, so the build passed or failed by build order
 * (GCC 14+: implicit declaration is an error). At run time sdl2_ndl_shim.c provides them.
 *
 * Force-included into ss4s-module-smp-webos (CMakeLists.txt). The declarations below copy the
 * webOS port's (SDL-webOS, branch webOS-2.32.x, include/SDL_system.h) and are only made when the
 * SDL in use is sdl2-compat (patch level 50 and up), so they never clash with the port's own.
 */

#include <SDL_version.h>

#if SDL_MAJOR_VERSION == 2 && SDL_PATCHLEVEL >= 50

#include <SDL_stdinc.h>
#include <SDL_rect.h>

typedef enum {
    SDL_WEBOS_EXPORTED_WINDOW_TYPE_VIDEO = 0,
    SDL_WEBOS_EXPORTED_WINDOW_TYPE_SUBTITLE = 1,
    SDL_WEBOS_EXPORTED_WINDOW_TYPE_TRANSPARENT = 2,
    SDL_WEBOS_EXPORTED_WINDOW_TYPE_OPAQUE = 3,
} SDL_webOSExportedWindowType;

extern DECLSPEC SDL_bool SDLCALL SDL_webOSGetRefreshRate(int *rate);

extern DECLSPEC const char *SDLCALL SDL_webOSCreateExportedWindow(SDL_webOSExportedWindowType type);

extern DECLSPEC SDL_bool SDLCALL SDL_webOSSetExportedWindow(const char *windowId, SDL_Rect *src, SDL_Rect *dst);

extern DECLSPEC void SDLCALL SDL_webOSDestroyExportedWindow(const char *windowId);

#endif
