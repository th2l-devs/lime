#ifndef LIME_SDL_APPLICATION_H
#define LIME_SDL_APPLICATION_H


#include <SDL3/SDL.h>
#include <app/Application.h>
#include <app/ApplicationEvent.h>
#include <graphics/RenderEvent.h>
#include <system/ClipboardEvent.h>
#include <system/OrientationEvent.h>
#include <system/SensorEvent.h>
#include <ui/DropEvent.h>
#include <ui/GamepadEvent.h>
#include <ui/JoystickEvent.h>
#include <ui/KeyEvent.h>
#include <ui/MouseEvent.h>
#include <ui/TextEvent.h>
#include <ui/TouchEvent.h>
#include <ui/WindowEvent.h>
#include "SDLWindow.h"


namespace lime {


	class SDLApplication : public Application {

		public:

			SDLApplication ();
			~SDLApplication ();

			virtual int Exec ();
			virtual void Init ();
			virtual int Quit ();
			virtual void SetFrameRate (double frameRate);
			virtual bool Update ();

			void RegisterWindow (SDLWindow *window);

		private:

			void HandleEvent (SDL_Event* event);
			void ProcessClipboardEvent (SDL_Event* event);
			void ProcessDropEvent (SDL_Event* event);
			void ProcessGamepadEvent (SDL_Event* event);
			void ProcessJoystickEvent (SDL_Event* event);
			void ProcessKeyEvent (SDL_Event* event);
			void ProcessMouseEvent (SDL_Event* event);
			void ProcessSensorEvent (SDL_Event* event);
			void ProcessTextEvent (SDL_Event* event);
			void ProcessTouchEvent (SDL_Event* event);
			void ProcessWindowEvent (SDL_Event* event);
			void InitSensors ();
			void CloseSensors ();
			void RenderFrame (Uint64 currentUpdate);

			#ifdef LIME_FIX_FREEZE_WINDOW
			static bool SDLCALL WindowEventWatcher (void* userdata, SDL_Event* event);
			Uint64 lastWatchedEventTimestamp;
			#endif

			static void UpdateFrame ();
			static void UpdateFrame (void*);

			static SDLApplication* currentApplication;

			bool active;
			SDL_InitFlags initFlags;
			ApplicationEvent applicationEvent;
			ClipboardEvent clipboardEvent;
			Uint64 lastUpdate;
			Uint64 framePeriod;
			SDL_Sensor* accelerometer;
			DropEvent dropEvent;
			GamepadEvent gamepadEvent;
			JoystickEvent joystickEvent;
			KeyEvent keyEvent;
			MouseEvent mouseEvent;
			OrientationEvent orientationEvent;
			RenderEvent renderEvent;
			SensorEvent sensorEvent;
			TextEvent textEvent;
			TouchEvent touchEvent;
			WindowEvent windowEvent;

	};


}


#endif