#include "SDLApplication.h"
#include "SDLGamepad.h"
#include "SDLJoystick.h"
#include "SDLDisplay.h"
#include <system/System.h>

#ifdef HX_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef EMSCRIPTEN
#include "emscripten.h"
#endif

#if defined (LIME_FIX_FREEZE_WINDOW) && defined (HX_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// timeBeginPeriod/timeEndPeriod (winmm) - WIN32_LEAN_AND_MEAN omits mmsystem.h,
// so pull in the multimedia timer API explicitly. winmm.lib is already linked.
#include <timeapi.h>
#endif


namespace lime {


	AutoGCRoot* Application::callback = 0;
	SDLApplication* SDLApplication::currentApplication = 0;

	const int analogAxisDeadZone = 1000;
	std::map<int, std::map<int, int> > gamepadsAxisMap;
	bool inBackground = false;


	SDLApplication::SDLApplication () {

		SDL_SetHint (SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)
		// SDL3 makes the process per-monitor DPI aware by default, SDL2 left it unaware.
		// Keep the SDL2 behaviour (lime defaults to allow-high-dpi="false" on Windows),
		// otherwise scaled displays render at native resolution and use far more memory.
		// Can still be overridden with the SDL_WINDOWS_DPI_AWARENESS environment variable.
		SDL_SetHintWithPriority ("SDL_WINDOWS_DPI_AWARENESS", "unaware", SDL_HINT_DEFAULT);
		#endif

		initFlags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR;
		#if defined(LIME_MOJOAL) || defined(LIME_OPENALSOFT)
		initFlags |= SDL_INIT_AUDIO;
		#endif

		if (!SDL_Init (initFlags)) {

			printf ("Could not initialize SDL: %s.\n", SDL_GetError ());

		}

		SDL_SetLogPriority (SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN);

		currentApplication = this;

		active = false;
		accelerometer = 0;

		#ifdef LIME_FIX_FREEZE_WINDOW
		lastWatchedEventTimestamp = 0;
		modalTimerActive = false;
		#endif

		framePeriod = (Uint64)(SDL_NS_PER_SECOND / 60);
		lastUpdate = SDL_GetTicksNS ();

		SDL_SetEventEnabled (SDL_EVENT_DROP_FILE, true);
		InitSensors ();

		#ifdef HX_MACOS
		CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL (CFBundleGetMainBundle ());
		char path[PATH_MAX];

		if (CFURLGetFileSystemRepresentation (resourcesURL, TRUE, (UInt8 *)path, PATH_MAX)) {

			chdir (path);

		}

		CFRelease (resourcesURL);
		#endif

	}


	SDLApplication::~SDLApplication () {

		CloseSensors ();

	}


	void SDLApplication::InitSensors () {

		int count = 0;
		SDL_SensorID* sensors = SDL_GetSensors (&count);

		if (sensors) {

			for (int i = 0; i < count; i++) {

				if (SDL_GetSensorTypeForID (sensors[i]) == SDL_SENSOR_ACCEL) {

					accelerometer = SDL_OpenSensor (sensors[i]);

					if (accelerometer) {

						break;

					}

				}

			}

			SDL_free (sensors);

		}

	}


	void SDLApplication::CloseSensors () {

		if (accelerometer) {

			SDL_CloseSensor (accelerometer);
			accelerometer = 0;

		}

	}


	int SDLApplication::Exec () {

		Init ();

		#ifdef EMSCRIPTEN
		emscripten_cancel_main_loop ();
		emscripten_set_main_loop (UpdateFrame, 0, 0);
		emscripten_set_main_loop_timing (EM_TIMING_RAF, 1);
		#endif

		#if defined(IPHONE) || defined(EMSCRIPTEN)

		return 0;

		#else

		while (active) {

			Update ();

		}

		return Quit ();

		#endif

	}




	void SDLApplication::HandleEvent (SDL_Event* event) {

		#if defined(IPHONE) || defined(EMSCRIPTEN)

		int top = 0;
		gc_set_top_of_stack(&top,false);

		#endif

		switch (event->type) {

			case SDL_EVENT_WILL_ENTER_BACKGROUND:

				inBackground = true;

				windowEvent.type = WINDOW_DEACTIVATE;
				WindowEvent::Dispatch (&windowEvent);
				break;

			case SDL_EVENT_DID_ENTER_FOREGROUND:

				windowEvent.type = WINDOW_ACTIVATE;
				WindowEvent::Dispatch (&windowEvent);

				inBackground = false;
				lastUpdate = SDL_GetTicksNS ();
				nextUpdate = lastUpdate;
				break;

			case SDL_EVENT_CLIPBOARD_UPDATE:

				ProcessClipboardEvent (event);
				break;

			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
			case SDL_EVENT_GAMEPAD_ADDED:
			case SDL_EVENT_GAMEPAD_REMOVED:

				ProcessGamepadEvent (event);
				break;

			case SDL_EVENT_DISPLAY_ORIENTATION:

				// this is the orientation of what is rendered, which
				// may not exactly match the orientation of the device,
				// if the app was locked to portrait or landscape.
				orientationEvent.type = DISPLAY_ORIENTATION_CHANGE;
				orientationEvent.orientation = event->display.data1;
				orientationEvent.display = SDLDisplay::GetIndex (event->display.displayID);
				OrientationEvent::Dispatch (&orientationEvent);
				break;

			case SDL_EVENT_DROP_FILE:

				ProcessDropEvent (event);
				break;

			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:

				ProcessTouchEvent (event);
				break;

			case SDL_EVENT_JOYSTICK_AXIS_MOTION:
			case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			case SDL_EVENT_JOYSTICK_BUTTON_UP:
			case SDL_EVENT_JOYSTICK_HAT_MOTION:
			case SDL_EVENT_JOYSTICK_ADDED:
			case SDL_EVENT_JOYSTICK_REMOVED:

				ProcessJoystickEvent (event);
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:

				ProcessKeyEvent (event);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:

				ProcessMouseEvent (event);
				break;

			#ifndef EMSCRIPTEN
			case SDL_EVENT_RENDER_DEVICE_RESET:

				renderEvent.type = RENDER_CONTEXT_LOST;
				RenderEvent::Dispatch (&renderEvent);

				renderEvent.type = RENDER_CONTEXT_RESTORED;
				RenderEvent::Dispatch (&renderEvent);

				renderEvent.type = RENDER;
				break;
			#endif

			case SDL_EVENT_SENSOR_UPDATE:

				ProcessSensorEvent (event);
				break;

			case SDL_EVENT_TEXT_INPUT:
			case SDL_EVENT_TEXT_EDITING:

				ProcessTextEvent (event);
				break;

			case SDL_EVENT_WINDOW_MOUSE_ENTER:
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			case SDL_EVENT_WINDOW_SHOWN:
			case SDL_EVENT_WINDOW_HIDDEN:
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			case SDL_EVENT_WINDOW_MAXIMIZED:
			case SDL_EVENT_WINDOW_MINIMIZED:
			case SDL_EVENT_WINDOW_RESTORED:

				ProcessWindowEvent (event);
				break;

			case SDL_EVENT_WINDOW_MOVED:

				#ifdef LIME_FIX_FREEZE_WINDOW
				// Already dispatched by WindowEventWatcher during a modal loop
				if (event->window.timestamp <= lastWatchedEventTimestamp) break;
				#endif

				ProcessWindowEvent (event);
				break;

			case SDL_EVENT_WINDOW_EXPOSED:
			case SDL_EVENT_WINDOW_RESIZED:

				#ifdef LIME_FIX_FREEZE_WINDOW
				if (event->window.timestamp <= lastWatchedEventTimestamp) break;
				#endif

				ProcessWindowEvent (event);

				if (!inBackground) {

					RenderEvent::Dispatch (&renderEvent);

				}

				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {

				ProcessWindowEvent (event);

				// Avoid handling SDL_EVENT_QUIT if in response to window.close
				SDL_Event nextEvent;

				if (SDL_PollEvent (&nextEvent)) {

					if (nextEvent.type != SDL_EVENT_QUIT) {

						HandleEvent (&nextEvent);

					}

				}

				break;

			}

			case SDL_EVENT_QUIT:

				active = false;
				break;

			default:

				break;

		}

	}


	void SDLApplication::Init () {

		active = true;
		lastUpdate = SDL_GetTicksNS ();
		nextUpdate = lastUpdate;

		#ifdef LIME_FIX_FREEZE_WINDOW
		// Raise the Windows timer resolution to 1ms. Without this, the WM_TIMER SDL uses to
		// keep the window alive during the modal move/resize loop is clamped to the default
		// ~15.6ms scheduler tick. Paired with timeEndPeriod in Quit().
		#ifdef HX_WINDOWS
		timeBeginPeriod (1);

		// Keep rendering during the modal move/size/menu loop, including a stationary
		// hold which produces no window events, by ticking our own TIMERPROC for its
		// duration (SDL3 forwards WM_ENTERSIZEMOVE and every modal-loop message here).
		SDL_SetWindowsMessageHook ((SDL_WindowsMessageHook)WindowsMessageHook, this);
		#endif

		SDL_AddEventWatch (WindowEventWatcher, this);
		#endif

	}


	void SDLApplication::ProcessClipboardEvent (SDL_Event* event) {

		if (ClipboardEvent::callback) {

			clipboardEvent.type = CLIPBOARD_UPDATE;

			ClipboardEvent::Dispatch (&clipboardEvent);

		}

	}


	void SDLApplication::ProcessDropEvent (SDL_Event* event) {

		if (DropEvent::callback && event->drop.data) {

			// SDL3 owns the dropped path, DropEvent::Dispatch copies it
			dropEvent.type = DROP_FILE;
			dropEvent.file = (vbyte*)event->drop.data;

			DropEvent::Dispatch (&dropEvent);
			dropEvent.file = 0;

		}

	}


	void SDLApplication::ProcessGamepadEvent (SDL_Event* event) {

		if (GamepadEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_GAMEPAD_AXIS_MOTION:

					if (gamepadsAxisMap[event->gaxis.which].empty ()) {

						gamepadsAxisMap[event->gaxis.which][event->gaxis.axis] = event->gaxis.value;

					} else if (gamepadsAxisMap[event->gaxis.which][event->gaxis.axis] == event->gaxis.value) {

						break;

					}

					gamepadEvent.type = GAMEPAD_AXIS_MOVE;
					gamepadEvent.axis = event->gaxis.axis;
					gamepadEvent.id = event->gaxis.which;

					if (event->gaxis.value > -analogAxisDeadZone && event->gaxis.value < analogAxisDeadZone) {

						if (gamepadsAxisMap[event->gaxis.which][event->gaxis.axis] != 0) {

							gamepadsAxisMap[event->gaxis.which][event->gaxis.axis] = 0;
							gamepadEvent.axisValue = 0;
							GamepadEvent::Dispatch (&gamepadEvent);

						}

						break;

					}

					gamepadsAxisMap[event->gaxis.which][event->gaxis.axis] = event->gaxis.value;
					gamepadEvent.axisValue = event->gaxis.value / (event->gaxis.value > 0 ? 32767.0 : 32768.0);

					GamepadEvent::Dispatch (&gamepadEvent);
					break;

				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:

					gamepadEvent.type = GAMEPAD_BUTTON_DOWN;
					gamepadEvent.button = event->gbutton.button;
					gamepadEvent.id = event->gbutton.which;

					GamepadEvent::Dispatch (&gamepadEvent);
					break;

				case SDL_EVENT_GAMEPAD_BUTTON_UP:

					gamepadEvent.type = GAMEPAD_BUTTON_UP;
					gamepadEvent.button = event->gbutton.button;
					gamepadEvent.id = event->gbutton.which;

					GamepadEvent::Dispatch (&gamepadEvent);
					break;

				case SDL_EVENT_GAMEPAD_ADDED:

					if (SDLGamepad::Connect (event->gdevice.which)) {

						gamepadEvent.type = GAMEPAD_CONNECT;
						gamepadEvent.id = event->gdevice.which;

						GamepadEvent::Dispatch (&gamepadEvent);

					}

					break;

				case SDL_EVENT_GAMEPAD_REMOVED:

					gamepadEvent.type = GAMEPAD_DISCONNECT;
					gamepadEvent.id = event->gdevice.which;

					GamepadEvent::Dispatch (&gamepadEvent);
					SDLGamepad::Disconnect (event->gdevice.which);
					gamepadsAxisMap.erase (event->gdevice.which);
					break;

			}

		}

	}


	void SDLApplication::ProcessJoystickEvent (SDL_Event* event) {

		if (JoystickEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_JOYSTICK_AXIS_MOTION:

					joystickEvent.type = JOYSTICK_AXIS_MOVE;
					joystickEvent.index = event->jaxis.axis;
					joystickEvent.x = event->jaxis.value / (event->jaxis.value > 0 ? 32767.0 : 32768.0);
					joystickEvent.id = event->jaxis.which;

					JoystickEvent::Dispatch (&joystickEvent);
					break;

				case SDL_EVENT_JOYSTICK_BUTTON_DOWN:

					joystickEvent.type = JOYSTICK_BUTTON_DOWN;
					joystickEvent.index = event->jbutton.button;
					joystickEvent.id = event->jbutton.which;

					JoystickEvent::Dispatch (&joystickEvent);
					break;

				case SDL_EVENT_JOYSTICK_BUTTON_UP:

					joystickEvent.type = JOYSTICK_BUTTON_UP;
					joystickEvent.index = event->jbutton.button;
					joystickEvent.id = event->jbutton.which;

					JoystickEvent::Dispatch (&joystickEvent);
					break;

				case SDL_EVENT_JOYSTICK_HAT_MOTION:

					joystickEvent.type = JOYSTICK_HAT_MOVE;
					joystickEvent.index = event->jhat.hat;
					joystickEvent.eventValue = event->jhat.value;
					joystickEvent.id = event->jhat.which;

					JoystickEvent::Dispatch (&joystickEvent);
					break;

				case SDL_EVENT_JOYSTICK_ADDED:

					if (SDLJoystick::Connect (event->jdevice.which)) {

						joystickEvent.type = JOYSTICK_CONNECT;
						joystickEvent.id = event->jdevice.which;

						JoystickEvent::Dispatch (&joystickEvent);

					}
					break;

				case SDL_EVENT_JOYSTICK_REMOVED:

					joystickEvent.type = JOYSTICK_DISCONNECT;
					joystickEvent.id = event->jdevice.which;

					JoystickEvent::Dispatch (&joystickEvent);
					SDLJoystick::Disconnect (event->jdevice.which);
					break;

			}

		}

	}


	void SDLApplication::ProcessKeyEvent (SDL_Event* event) {

		if (KeyEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_KEY_DOWN: keyEvent.type = KEY_DOWN; break;
				case SDL_EVENT_KEY_UP: keyEvent.type = KEY_UP; break;

			}

			keyEvent.keyCode = event->key.key;
			keyEvent.modifier = event->key.mod;
			keyEvent.windowID = event->key.windowID;

			if (keyEvent.type == KEY_DOWN) {

				if (keyEvent.keyCode == SDLK_CAPSLOCK) keyEvent.modifier |= SDL_KMOD_CAPS;
				if (keyEvent.keyCode == SDLK_LALT) keyEvent.modifier |= SDL_KMOD_LALT;
				if (keyEvent.keyCode == SDLK_LCTRL) keyEvent.modifier |= SDL_KMOD_LCTRL;
				if (keyEvent.keyCode == SDLK_LGUI) keyEvent.modifier |= SDL_KMOD_LGUI;
				if (keyEvent.keyCode == SDLK_LSHIFT) keyEvent.modifier |= SDL_KMOD_LSHIFT;
				if (keyEvent.keyCode == SDLK_MODE) keyEvent.modifier |= SDL_KMOD_MODE;
				if (keyEvent.keyCode == SDLK_NUMLOCKCLEAR) keyEvent.modifier |= SDL_KMOD_NUM;
				if (keyEvent.keyCode == SDLK_RALT) keyEvent.modifier |= SDL_KMOD_RALT;
				if (keyEvent.keyCode == SDLK_RCTRL) keyEvent.modifier |= SDL_KMOD_RCTRL;
				if (keyEvent.keyCode == SDLK_RGUI) keyEvent.modifier |= SDL_KMOD_RGUI;
				if (keyEvent.keyCode == SDLK_RSHIFT) keyEvent.modifier |= SDL_KMOD_RSHIFT;

			}

			KeyEvent::Dispatch (&keyEvent);

		}

	}


	void SDLApplication::ProcessMouseEvent (SDL_Event* event) {

		if (MouseEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_MOUSE_MOTION:

					mouseEvent.type = MOUSE_MOVE;
					mouseEvent.x = event->motion.x;
					mouseEvent.y = event->motion.y;
					mouseEvent.movementX = event->motion.xrel;
					mouseEvent.movementY = event->motion.yrel;
					mouseEvent.windowID = event->motion.windowID;
					break;

				case SDL_EVENT_MOUSE_BUTTON_DOWN:

					SDL_CaptureMouse (true);

					mouseEvent.type = MOUSE_DOWN;
					mouseEvent.button = event->button.button - 1;
					mouseEvent.x = event->button.x;
					mouseEvent.y = event->button.y;
					mouseEvent.clickCount = event->button.clicks;
					mouseEvent.windowID = event->button.windowID;
					break;

				case SDL_EVENT_MOUSE_BUTTON_UP:

					SDL_CaptureMouse (false);

					mouseEvent.type = MOUSE_UP;
					mouseEvent.button = event->button.button - 1;
					mouseEvent.x = event->button.x;
					mouseEvent.y = event->button.y;
					mouseEvent.clickCount = event->button.clicks;
					mouseEvent.windowID = event->button.windowID;
					break;

				case SDL_EVENT_MOUSE_WHEEL: {

					// Match SDL2 behaviour: report whole scroll "ticks" only
					int wheelX = event->wheel.integer_x;
					int wheelY = event->wheel.integer_y;

					if (wheelX == 0 && wheelY == 0) {

						return;

					}

					mouseEvent.type = MOUSE_WHEEL;

					if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {

						mouseEvent.x = -wheelX;
						mouseEvent.y = -wheelY;

					} else {

						mouseEvent.x = wheelX;
						mouseEvent.y = wheelY;

					}

					mouseEvent.windowID = event->wheel.windowID;
					break;

				}

			}

			MouseEvent::Dispatch (&mouseEvent);

		}

	}


	void SDLApplication::ProcessSensorEvent (SDL_Event* event) {

		if (SensorEvent::callback && accelerometer && event->sensor.which == SDL_GetSensorID (accelerometer)) {

			// Lime registers the accelerometer as sensor 0 and expects values
			// in units of standard gravity (as reported by SDL2's joystick emulation)
			sensorEvent.type = SENSOR_ACCELEROMETER;
			sensorEvent.id = 0;
			sensorEvent.x = event->sensor.data[0] / SDL_STANDARD_GRAVITY;
			sensorEvent.y = event->sensor.data[1] / SDL_STANDARD_GRAVITY;
			sensorEvent.z = event->sensor.data[2] / SDL_STANDARD_GRAVITY;

			SensorEvent::Dispatch (&sensorEvent);

		}

	}


	void SDLApplication::ProcessTextEvent (SDL_Event* event) {

		if (TextEvent::callback) {

			const char* text = "";

			switch (event->type) {

				case SDL_EVENT_TEXT_INPUT:

					textEvent.type = TEXT_INPUT;
					text = event->text.text;
					textEvent.windowID = event->text.windowID;
					break;

				case SDL_EVENT_TEXT_EDITING:

					textEvent.type = TEXT_EDIT;
					text = event->edit.text;
					textEvent.start = event->edit.start;
					textEvent.length = event->edit.length;
					textEvent.windowID = event->edit.windowID;
					break;

			}

			if (!text) text = "";

			if (textEvent.text) {

				free (textEvent.text);

			}

			textEvent.text = (vbyte*)malloc (strlen (text) + 1);
			strcpy ((char*)textEvent.text, text);

			TextEvent::Dispatch (&textEvent);

		}

	}


	void SDLApplication::ProcessTouchEvent (SDL_Event* event) {

		if (TouchEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_FINGER_MOTION:

					touchEvent.type = TOUCH_MOVE;
					break;

				case SDL_EVENT_FINGER_DOWN:

					touchEvent.type = TOUCH_START;
					break;

				case SDL_EVENT_FINGER_UP:
				case SDL_EVENT_FINGER_CANCELED:

					touchEvent.type = TOUCH_END;
					break;

			}

			touchEvent.x = event->tfinger.x;
			touchEvent.y = event->tfinger.y;
			touchEvent.id = (int)event->tfinger.fingerID;
			touchEvent.dx = event->tfinger.dx;
			touchEvent.dy = event->tfinger.dy;
			touchEvent.pressure = event->tfinger.pressure;
			touchEvent.device = (int)event->tfinger.touchID;

			TouchEvent::Dispatch (&touchEvent);

		}

	}


	void SDLApplication::ProcessWindowEvent (SDL_Event* event) {

		if (WindowEvent::callback) {

			switch (event->type) {

				case SDL_EVENT_WINDOW_SHOWN: windowEvent.type = WINDOW_SHOW; break;
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED: windowEvent.type = WINDOW_CLOSE; break;
				case SDL_EVENT_WINDOW_HIDDEN: windowEvent.type = WINDOW_HIDE; break;
				case SDL_EVENT_WINDOW_MOUSE_ENTER: windowEvent.type = WINDOW_ENTER; break;
				case SDL_EVENT_WINDOW_FOCUS_GAINED: windowEvent.type = WINDOW_FOCUS_IN; break;
				case SDL_EVENT_WINDOW_FOCUS_LOST: windowEvent.type = WINDOW_FOCUS_OUT; break;
				case SDL_EVENT_WINDOW_MOUSE_LEAVE: windowEvent.type = WINDOW_LEAVE; break;
				case SDL_EVENT_WINDOW_MAXIMIZED: windowEvent.type = WINDOW_MAXIMIZE; break;
				case SDL_EVENT_WINDOW_MINIMIZED: windowEvent.type = WINDOW_MINIMIZE; break;
				case SDL_EVENT_WINDOW_EXPOSED: windowEvent.type = WINDOW_EXPOSE; break;

				case SDL_EVENT_WINDOW_MOVED:

					windowEvent.type = WINDOW_MOVE;
					windowEvent.x = event->window.data1;
					windowEvent.y = event->window.data2;
					break;

				case SDL_EVENT_WINDOW_RESIZED:

					windowEvent.type = WINDOW_RESIZE;
					windowEvent.width = event->window.data1;
					windowEvent.height = event->window.data2;
					break;

				case SDL_EVENT_WINDOW_RESTORED: windowEvent.type = WINDOW_RESTORE; break;

			}

			windowEvent.windowID = event->window.windowID;
			WindowEvent::Dispatch (&windowEvent);

		}

	}


	int SDLApplication::Quit () {

		applicationEvent.type = EXIT;
		ApplicationEvent::Dispatch (&applicationEvent);

		#ifdef LIME_FIX_FREEZE_WINDOW
		SDL_RemoveEventWatch (WindowEventWatcher, this);
		#ifdef HX_WINDOWS
		SDL_SetWindowsMessageHook (NULL, NULL);
		timeEndPeriod (1);
		#endif
		#endif

		CloseSensors ();
		SDL_QuitSubSystem (initFlags);
		SDL_Quit ();

		return 0;

	}


	void SDLApplication::RegisterWindow (SDLWindow *window) {

		#ifdef IPHONE
		SDL_SetiOSAnimationCallback (window->sdlWindow, 1, UpdateFrame, NULL);
		#endif

	}


	void SDLApplication::SetFrameRate (double frameRate) {

		// A frame rate of zero (or less) removes the frame cap
		framePeriod = frameRate > 0 ? (Uint64)(SDL_NS_PER_SECOND / frameRate) : 0;

	}


	bool SDLApplication::Update () {

		SDL_Event event;

		while (SDL_PollEvent (&event)) {

			HandleEvent (&event);

			if (!active)
				return active;

		}

		if (!inBackground) {

			Uint64 currentUpdate = SDL_GetTicksNS ();

			#if !defined (IPHONE) && !defined (EMSCRIPTEN)
			// iOS and HTML5 are paced by the display, elsewhere cap the frame rate here.
			// Fixed schedule: nextUpdate advances one period per frame so work outside the
			// wait (poll/dispatch/render/swap) can't drift the rate down; maxBehind caps
			// catch-up so resuming from a stall doesn't burst frames at unlimited rate.
			if (framePeriod > 0) {

				Uint64 maxBehind = framePeriod * 4;

				nextUpdate += framePeriod;

				if (currentUpdate > nextUpdate && (currentUpdate - nextUpdate) > maxBehind) {

					nextUpdate = currentUpdate;

				}

				if (currentUpdate < nextUpdate) {

					System::GCEnterBlocking ();
					SDL_DelayPrecise (nextUpdate - currentUpdate);
					System::GCExitBlocking ();
					currentUpdate = SDL_GetTicksNS ();

				}

			} else {

				nextUpdate = currentUpdate;

			}
			#endif

			RenderFrame (currentUpdate);

		}

		return active;

	}


	void SDLApplication::RenderFrame (Uint64 currentUpdate) {

		applicationEvent.type = UPDATE;
		applicationEvent.deltaTime = (double)(currentUpdate - lastUpdate) / SDL_NS_PER_MS;
		lastUpdate = currentUpdate;

		ApplicationEvent::Dispatch (&applicationEvent);
		RenderEvent::Dispatch (&renderEvent);

	}


	#ifdef LIME_FIX_FREEZE_WINDOW

	// While the user drags or resizes a window on Windows, the OS runs a modal
	// message loop inside SDL_PollEvent, so Update () never returns and the
	// application freezes. SDL still reports window events through event
	// watchers during that loop, so dispatch them and render from here.
	bool SDLCALL SDLApplication::WindowEventWatcher (void* userdata, SDL_Event* event) {

		SDLApplication* application = (SDLApplication*)userdata;

		switch (event->type) {

			case SDL_EVENT_WINDOW_EXPOSED:

				// SDL's own modal loop timer reports EXPOSED with data1 == 1. Our timer
				// already renders during the modal loop, so don't render twice.
				if (application->modalTimerActive && event->window.data1 == 1) return true;
				break;

			case SDL_EVENT_WINDOW_MOVED:
			case SDL_EVENT_WINDOW_RESIZED:
				break;

			default:
				return true;

		}

		if (!application->active || inBackground || !SDL_IsMainThread ()) {

			return true;

		}

		Uint64 currentUpdate = SDL_GetTicksNS ();

		application->ProcessWindowEvent (event);
		application->lastWatchedEventTimestamp = event->window.timestamp;
		application->RenderFrame (currentUpdate);
		application->nextUpdate = currentUpdate;

		// The return value of an event watcher is ignored, HandleEvent skips
		// the queued copy of this event using lastWatchedEventTimestamp
		return true;

	}


	#ifdef HX_WINDOWS

	// Arbitrary, unlikely-to-collide timer id for the modal-loop render timer.
	static const UINT_PTR LIME_MODAL_TIMER_ID = 0x4C494D45; // 'LIME'

	// Called directly by DispatchMessage in the modal loop, so it works even with a subclassed WndProc.
	void __stdcall SDLApplication::ModalRenderTimerProc (void* hWnd, unsigned int message, uintptr_t idTimer, unsigned long dwTime) {

		SDLApplication* application = currentApplication;

		if (idTimer == LIME_MODAL_TIMER_ID && application && application->active && !inBackground) {

			Uint64 currentUpdate = SDL_GetTicksNS ();
			application->RenderFrame (currentUpdate);
			application->nextUpdate = currentUpdate;

		}

	}


	bool SDLCALL SDLApplication::WindowsMessageHook (void* userdata, void* msgPtr) {

		SDLApplication* application = (SDLApplication*)userdata;
		MSG* msg = (MSG*)msgPtr;

		switch (msg->message) {

			case WM_ENTERSIZEMOVE:
			case WM_ENTERMENULOOP:

				if (SetTimer (msg->hwnd, LIME_MODAL_TIMER_ID, USER_TIMER_MINIMUM, (TIMERPROC)&SDLApplication::ModalRenderTimerProc)) {

					application->modalTimerActive = true;

				}
				break;

			case WM_EXITSIZEMOVE:
			case WM_EXITMENULOOP:

				KillTimer (msg->hwnd, LIME_MODAL_TIMER_ID);
				application->modalTimerActive = false;
				break;

		}

		// Always let SDL process the message as usual
		return true;

	}

	#endif

	#endif


	void SDLApplication::UpdateFrame () {

		#ifdef EMSCRIPTEN
		System::GCTryExitBlocking ();
		#endif

		currentApplication->Update ();

		#ifdef EMSCRIPTEN
		System::GCTryEnterBlocking ();
		#endif

	}


	void SDLApplication::UpdateFrame (void*) {

		UpdateFrame ();

	}


	Application* CreateApplication () {

		return new SDLApplication ();

	}


}


#ifdef ANDROID
int SDL_main (int argc, char *argv[]) { return 0; }
#endif
