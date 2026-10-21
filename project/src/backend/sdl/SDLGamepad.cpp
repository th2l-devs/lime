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


	static SDL_Gamepad* GetController (int id) {

		auto it = gameControllers.find (id);
		return it != gameControllers.end () ? it->second : nullptr;

	}


	static Uint16 ToRumble (double value) {

		if (value < 0.0) value = 0.0;
		if (value > 1.0) value = 1.0;
		return (Uint16)(value * 0xFFFF);

	}


	int Gamepad::GetType (int id) {

		SDL_Gamepad* controller = GetController (id);
		if (!controller) return 0;

		switch (SDL_GetGamepadType (controller)) {

			case SDL_GAMEPAD_TYPE_XBOX360: return 1;
			case SDL_GAMEPAD_TYPE_XBOXONE: return 2;
			case SDL_GAMEPAD_TYPE_PS3: return 3;
			case SDL_GAMEPAD_TYPE_PS4: return 4;
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return 5;
			case SDL_GAMEPAD_TYPE_PS5: return 7;
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return 11;
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return 12;
			case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return 13;
			default: return SDL_IsJoystickVirtual (id) ? 6 : 0;

		}

	}


	bool Gamepad::HasLED (int id) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_GetBooleanProperty (SDL_GetGamepadProperties (controller), SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);

	}


	bool Gamepad::HasRumbleTriggers (int id) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_GetBooleanProperty (SDL_GetGamepadProperties (controller), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false);

	}


	bool Gamepad::HasSensor (int id, int sensor) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_GamepadHasSensor (controller, (SDL_SensorType)sensor);

	}


	bool Gamepad::RumbleTriggers (int id, double leftRumble, double rightRumble, int duration) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_RumbleGamepadTriggers (controller, ToRumble (leftRumble), ToRumble (rightRumble), duration);

	}


	bool Gamepad::SetLED (int id, int red, int green, int blue) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_SetGamepadLED (controller, red, green, blue);

	}


	bool Gamepad::SetSensorEnabled (int id, int sensor, bool enabled) {

		SDL_Gamepad* controller = GetController (id);
		return controller && SDL_SetGamepadSensorEnabled (controller, (SDL_SensorType)sensor, enabled);

	}


	struct VirtualGamepadOutput {

		int led;
		int lowRumble;
		int highRumble;
		int leftRumble;
		int rightRumble;

	};


	static std::map<int, VirtualGamepadOutput*> virtualGamepads;


	static bool SDLCALL VirtualRumble (void* userdata, Uint16 lowRumble, Uint16 highRumble) {

		VirtualGamepadOutput* output = (VirtualGamepadOutput*)userdata;
		output->lowRumble = lowRumble;
		output->highRumble = highRumble;
		return true;

	}


	static bool SDLCALL VirtualRumbleTriggers (void* userdata, Uint16 leftRumble, Uint16 rightRumble) {

		VirtualGamepadOutput* output = (VirtualGamepadOutput*)userdata;
		output->leftRumble = leftRumble;
		output->rightRumble = rightRumble;
		return true;

	}


	static bool SDLCALL VirtualSetLED (void* userdata, Uint8 red, Uint8 green, Uint8 blue) {

		VirtualGamepadOutput* output = (VirtualGamepadOutput*)userdata;
		output->led = (red << 16) | (green << 8) | blue;
		return true;

	}


	int Gamepad::AttachVirtual () {

		VirtualGamepadOutput* output = new VirtualGamepadOutput ();
		output->led = -1;
		output->lowRumble = output->highRumble = output->leftRumble = output->rightRumble = -1;

		SDL_VirtualJoystickDesc desc;
		SDL_INIT_INTERFACE (&desc);
		desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
		desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		desc.name = "Lime Virtual Gamepad";
		desc.userdata = output;
		desc.Rumble = VirtualRumble;
		desc.RumbleTriggers = VirtualRumbleTriggers;
		desc.SetLED = VirtualSetLED;

		SDL_JoystickID id = SDL_AttachVirtualJoystick (&desc);

		if (id == 0 || !SDLGamepad::Connect (id)) {

			if (id != 0) SDL_DetachVirtualJoystick (id);
			delete output;
			return -1;

		}

		virtualGamepads[id] = output;
		return id;

	}


	void Gamepad::DetachVirtual (int id) {

		SDLGamepad::Disconnect (id);

		if (SDL_IsJoystickVirtual (id)) SDL_DetachVirtualJoystick (id);

		auto it = virtualGamepads.find (id);

		if (it != virtualGamepads.end ()) {

			delete it->second;
			virtualGamepads.erase (it);

		}

	}


	bool Gamepad::GetVirtualOutput (int id, int* led, int* lowRumble, int* highRumble, int* leftRumble, int* rightRumble) {

		auto it = virtualGamepads.find (id);
		if (it == virtualGamepads.end ()) return false;

		*led = it->second->led;
		*lowRumble = it->second->lowRumble;
		*highRumble = it->second->highRumble;
		*leftRumble = it->second->leftRumble;
		*rightRumble = it->second->rightRumble;
		return true;

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
