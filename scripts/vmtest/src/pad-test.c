/* NickelOS: what a game sees of the game controllers, through SDL2's game
 * controller API as most games use it, for testing controllers
 * (hw/pad-test.sh). It prints each controller that appears ("added NAME,
 * mapping yes|no"), its buttons ("button a down", "button a up") and large
 * axis moves ("axis leftx 32767"), and "removed" when it goes, for SECONDS.
 * Built against SDL2 and run where SDL2 is, as in Flatpak runtimes (Steam's
 * among them).
 *   pad-test SECONDS
 * Build: gcc -O2 -o pad-test pad-test.c $(pkg-config --cflags --libs sdl2) */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 20;
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
    printf("SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  printf("watching\n");
  fflush(stdout);
  Uint32 end = SDL_GetTicks() + (Uint32)(seconds * 1000);
  int last_axis[SDL_CONTROLLER_AXIS_MAX] = {0};
  while (SDL_GetTicks() < end) {
    SDL_Event e;
    while (SDL_WaitEventTimeout(&e, 100)) {
      switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED: {
          SDL_GameController* pad = SDL_GameControllerOpen(e.cdevice.which);
          printf("added %s, mapping %s\n",
                 pad ? SDL_GameControllerName(pad) : "?",
                 pad && SDL_GameControllerMapping(pad) ? "yes" : "no");
          break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
          printf("removed\n");
          break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
          printf("button %s %s\n",
                 SDL_GameControllerGetStringForButton(e.cbutton.button),
                 e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
          break;
        case SDL_CONTROLLERAXISMOTION: {
          /* Only crossings of the half-way marks, not every step. */
          int band = e.caxis.value > 16384 ? 1 : e.caxis.value < -16384 ? -1
                                                                        : 0;
          if (band != last_axis[e.caxis.axis]) {
            last_axis[e.caxis.axis] = band;
            printf("axis %s %d\n",
                   SDL_GameControllerGetStringForAxis(e.caxis.axis),
                   e.caxis.value);
          }
          break;
        }
      }
      fflush(stdout);
    }
  }
  printf("done\n");
  SDL_Quit();
  return 0;
}
