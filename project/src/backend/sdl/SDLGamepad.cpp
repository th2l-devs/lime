#include "SDLGamepad.h"


namespace lime {


	std::map<int, SDL_Gamepad*> gameControllers;


	bool SDLGamepad::Connect (SDL_JoystickID id) {

		if (gameControllers.find (id) != gameControllers.end ())
			return true;

		if (!SDL_IsGamepad (id))
			return false;

		SDL_Gamepad *gameController = SDL_OpenGamepad (id);
		if (gameController == nullptr)
			return false;

		gameControllers[id] = gameController;

		return true;

	}


	bool SDLGamepad::Disconnect (SDL_JoystickID id) {

		auto it = gameControllers.find (id);
		if (it == gameControllers.end ())
			return false;

		SDL_CloseGamepad (it->second);
		gameControllers.erase (it);

		return true;

	}


	void Gamepad::AddMapping (const char* content) {

		SDL_AddGamepadMapping (content);

	}


	const char* Gamepad::GetDeviceGUID (int id) {

		auto it = gameControllers.find (id);
		if (it == gameControllers.end ())
			return nullptr;

		char* guid = new char[64];
		SDL_GUIDToString (SDL_GetGamepadGUIDForID (id), guid, 64);
		return guid;

	}


	const char* Gamepad::GetDeviceName (int id) {

		auto it = gameControllers.find (id);
		if (it == gameControllers.end ())
			return nullptr;

		return SDL_GetGamepadName (it->second);

	}


	void Gamepad::Rumble (int id, double lowFrequencyRumble, double highFrequencyRumble, int duration) {

		auto it = gameControllers.find (id);
		if (it == gameControllers.end ())
			return;

		if (highFrequencyRumble < 0.0f)
			highFrequencyRumble = 0.0f;
		else if (highFrequencyRumble > 1.0f)
			highFrequencyRumble = 1.0f;

		if (lowFrequencyRumble < 0.0f)
			lowFrequencyRumble = 0.0f;
		else if (lowFrequencyRumble > 1.0f)
			lowFrequencyRumble = 1.0f;

		SDL_RumbleGamepad (it->second, lowFrequencyRumble * 0xFFFF, highFrequencyRumble * 0xFFFF, duration);

	}


}
