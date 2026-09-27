#ifndef LIME_SDL_DISPLAY_H
#define LIME_SDL_DISPLAY_H


#include <SDL3/SDL.h>


namespace lime {


	// SDL3 identifies displays by an opaque SDL_DisplayID, while Lime exposes
	// zero-based display indices. These helpers convert between the two.

	class SDLDisplay {

		public:

			static SDL_DisplayID GetID (int index);
			static int GetIndex (SDL_DisplayID displayID);

	};


}


#endif
