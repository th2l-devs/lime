#include "SDLJoystick.h"


namespace lime {


	std::map<int, SDL_Joystick*> joysticks = std::map<int, SDL_Joystick*> ();


	bool SDLJoystick::Connect (SDL_JoystickID id) {

		if (joysticks.find (id) != joysticks.end ()) {

			return true;

		}

		SDL_Joystick* joystick = SDL_OpenJoystick (id);

		if (joystick) {

			joysticks[id] = joystick;
			return true;

		}

		return false;

	}


	bool SDLJoystick::Disconnect (SDL_JoystickID id) {

		auto it = joysticks.find (id);

		if (it != joysticks.end ()) {

			SDL_CloseJoystick (it->second);
			joysticks.erase (it);
			return true;

		}

		return false;

	}


	static SDL_Joystick* GetJoystick (int id) {

		auto it = joysticks.find (id);
		return it != joysticks.end () ? it->second : nullptr;

	}


	const char* Joystick::GetDeviceGUID (int id) {

		SDL_Joystick* joystick = GetJoystick (id);
		if (!joystick) return nullptr;

		char* guid = new char[64];
		SDL_GUIDToString (SDL_GetJoystickGUID (joystick), guid, 64);
		return guid;

	}


	const char* Joystick::GetDeviceName (int id) {

		SDL_Joystick* joystick = GetJoystick (id);
		return joystick ? SDL_GetJoystickName (joystick) : nullptr;

	}


	int Joystick::GetNumAxes (int id) {

		SDL_Joystick* joystick = GetJoystick (id);
		return joystick ? SDL_GetNumJoystickAxes (joystick) : 0;

	}


	int Joystick::GetNumButtons (int id) {

		SDL_Joystick* joystick = GetJoystick (id);
		return joystick ? SDL_GetNumJoystickButtons (joystick) : 0;

	}


	int Joystick::GetNumHats (int id) {

		SDL_Joystick* joystick = GetJoystick (id);
		return joystick ? SDL_GetNumJoystickHats (joystick) : 0;

	}


}
