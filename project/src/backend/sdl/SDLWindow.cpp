#include "SDLWindow.h"
#include "SDLCursor.h"
#include "SDLDisplay.h"
#include "SDLApplication.h"
#include "../../graphics/opengl/OpenGL.h"
#include "../../graphics/opengl/OpenGLBindings.h"

#include <cstring>

#if defined (HX_WINDOWS) && !defined (HX_WINRT)
#include <windows.h>
#undef CreateWindow
#endif


// Desktop GL targets that negotiate an explicit context version. Windows and Linux both reach GL
// through the same dynamic extension loader (DYNAMIC_OGL, see graphics/opengl/OpenGL.h) and both
// expose compatibility profiles above 2.1, so the same negotiation applies to each.
//
// Deliberately excluded:
//   ANGLE and Raspberry Pi already request a GLES context a few lines below, and would fight
//   with this one.
//   macOS caps its compatibility profile at 2.1 - anything higher is core-only there, which the
//   renderer cannot use (see LIME_GL_CONTEXT_PROFILE) - so negotiation could only ever fail
//   through every rung and land back on the default it starts from.
#if (defined (HX_WINDOWS) || defined (HX_LINUX)) && !defined (NATIVE_TOOLKIT_SDL_ANGLE) && !defined (RASPBERRYPI)
#define LIME_GL_NEGOTIATE_CONTEXT 1
#endif


#ifdef LIME_GL_NEGOTIATE_CONTEXT

// The GL context desktop Windows and Linux ask for. Each of these may be overridden at build
// time, but read the caveats first - they are not independent knobs.
//
// LIME_GL_CONTEXT_PROFILE defaults to compatibility rather than core on purpose. Core profile
// removes the default vertex array object (VAO 0), so a draw call with no VAO bound is invalid,
// and it drops support for GLSL 1.10/1.20. OpenFL's renderer depends on both of those: it never
// calls glGenVertexArrays/glBindVertexArray anywhere, and all of its shaders are unversioned
// GLSL 1.10 written with `attribute`/`varying`. Requesting core today produces a context in
// which every draw call and every shader compile fails on a driver that enforces the profile
// strictly - and appears to work on one that does not, which is worse. Switching to core is a
// renderer migration, not a context flag.
//
// LIME_GL_CONTEXT_VERSION_* is only the first version tried. SDLWindow negotiates downward if the
// driver refuses, so raising it is safe and lowering it only skips attempts.
#ifndef LIME_GL_CONTEXT_PROFILE
#define LIME_GL_CONTEXT_PROFILE SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
#endif

#ifndef LIME_GL_CONTEXT_VERSION_MAJOR
#define LIME_GL_CONTEXT_VERSION_MAJOR 4
#endif

#ifndef LIME_GL_CONTEXT_VERSION_MINOR
#define LIME_GL_CONTEXT_VERSION_MINOR 5
#endif

#endif


namespace lime {


	static Cursor currentCursor = DEFAULT;

	SDL_Cursor* SDLCursor::arrowCursor = 0;
	SDL_Cursor* SDLCursor::crosshairCursor = 0;
	SDL_Cursor* SDLCursor::moveCursor = 0;
	SDL_Cursor* SDLCursor::pointerCursor = 0;
	SDL_Cursor* SDLCursor::resizeNESWCursor = 0;
	SDL_Cursor* SDLCursor::resizeNSCursor = 0;
	SDL_Cursor* SDLCursor::resizeNWSECursor = 0;
	SDL_Cursor* SDLCursor::resizeWECursor = 0;
	SDL_Cursor* SDLCursor::textCursor = 0;
	SDL_Cursor* SDLCursor::waitCursor = 0;
	SDL_Cursor* SDLCursor::waitArrowCursor = 0;


	static PixelFormat GetLimePixelFormat (SDL_PixelFormat format) {

		switch (format) {

			case SDL_PIXELFORMAT_ARGB8888:

				return ARGB32;

			case SDL_PIXELFORMAT_BGRA8888:
			case SDL_PIXELFORMAT_BGRX8888:

				return BGRA32;

			default:

				return RGBA32;

		}

	}


	static SDL_PixelFormat GetSDLPixelFormat (PixelFormat format) {

		switch (format) {

			case ARGB32: return SDL_PIXELFORMAT_ARGB8888;
			case BGRA32: return SDL_PIXELFORMAT_BGRA8888;
			default: return SDL_PIXELFORMAT_RGBA8888;

		}

	}


	SDLWindow::SDLWindow (Application* application, int width, int height, int flags, const char* title) {

		sdlTexture = 0;
		sdlRenderer = 0;
		sdlWindow = 0;
		context = 0;

		contextWidth = 0;
		contextHeight = 0;

		currentApplication = application;
		this->flags = flags;

		SDL_WindowFlags sdlWindowFlags = 0;

		if (flags & WINDOW_FLAG_FULLSCREEN) sdlWindowFlags |= SDL_WINDOW_FULLSCREEN;
		if (flags & WINDOW_FLAG_RESIZABLE) sdlWindowFlags |= SDL_WINDOW_RESIZABLE;
		if (flags & WINDOW_FLAG_BORDERLESS) sdlWindowFlags |= SDL_WINDOW_BORDERLESS;
		if (flags & WINDOW_FLAG_HIDDEN) sdlWindowFlags |= SDL_WINDOW_HIDDEN;
		if (flags & WINDOW_FLAG_MINIMIZED) sdlWindowFlags |= SDL_WINDOW_MINIMIZED;
		if (flags & WINDOW_FLAG_MAXIMIZED) sdlWindowFlags |= SDL_WINDOW_MAXIMIZED;
		if (flags & WINDOW_FLAG_ALLOW_HIGHDPI) sdlWindowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

		#ifndef EMSCRIPTEN
		if (flags & WINDOW_FLAG_ALWAYS_ON_TOP) sdlWindowFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
		#endif

		#if !defined(EMSCRIPTEN) && !defined(LIME_SWITCH)
		SDL_SetHint (SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "0");
		SDL_SetHint (SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
		SDL_SetHint (SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
		SDL_SetHint (SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
		#endif

		if (flags & WINDOW_FLAG_HARDWARE) {

			sdlWindowFlags |= SDL_WINDOW_OPENGL;

			#if defined (HX_WINDOWS) && defined (NATIVE_TOOLKIT_SDL_ANGLE)
			SDL_SetHint (SDL_HINT_OPENGL_ES_DRIVER, "1");
			SDL_SetHint (SDL_HINT_VIDEO_WIN_D3DCOMPILER, "d3dcompiler_47.dll");
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
			#endif

			#ifdef LIME_GL_NEGOTIATE_CONTEXT
			// Ask for a specific GL version instead of accepting whatever the driver hands back.
			// Without this, entry points gated on 4.x - glBufferStorage, direct state access,
			// glTextureBarrier, glMultiDraw*Indirect - may resolve to null with nothing to
			// explain why. LIME_GL_CONTEXT_VERSION_* is negotiated down at context creation
			// below, so this is the version to try first, not a requirement.
			//
			// Compatibility, not core, and deliberately so: see LIME_GL_CONTEXT_PROFILE above.
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, LIME_GL_CONTEXT_PROFILE);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, LIME_GL_CONTEXT_VERSION_MAJOR);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, LIME_GL_CONTEXT_VERSION_MINOR);
			#endif

			#if defined (RASPBERRYPI)
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
			SDL_SetHint (SDL_HINT_RENDER_DRIVER, "opengles2");
			#endif

			#if defined (IPHONE) || defined (APPLETV) || defined (ANDROID)
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 0);
			#endif

			if (flags & WINDOW_FLAG_DEPTH_BUFFER) {

				SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 32 - ((flags & WINDOW_FLAG_STENCIL_BUFFER) ? 8 : 0));

			}

			if (flags & WINDOW_FLAG_STENCIL_BUFFER) {

				SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);

			}

			if (flags & WINDOW_FLAG_HW_AA_HIRES) {

				SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 1);
				SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, 4);

			} else if (flags & WINDOW_FLAG_HW_AA) {

				SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 1);
				SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, 2);

			}

			if (flags & WINDOW_FLAG_COLOR_DEPTH_32_BIT) {

				SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
				SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
				SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
				SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, 8);

			} else {

				SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 5);
				SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 6);
				SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 5);

			}

		}

		sdlWindow = SDL_CreateWindow (title, width, height, sdlWindowFlags);

		if (!sdlWindow) {

			printf ("Could not create SDL window: %s.\n", SDL_GetError ());
			return;

		}

		#if defined (HX_WINDOWS) && !defined (HX_WINRT)

		HINSTANCE handle = ::GetModuleHandle (nullptr);
		HICON icon = ::LoadIcon (handle, MAKEINTRESOURCE (1));

		if (icon != nullptr) {

			HWND hwnd = (HWND)SDL_GetPointerProperty (SDL_GetWindowProperties (sdlWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

			if (hwnd != nullptr) {

				#ifdef _WIN64
				::SetClassLongPtr (hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon));
				#else
				::SetClassLong (hwnd, GCL_HICON, reinterpret_cast<LONG>(icon));
				#endif

			}

		}

		#endif

		if (flags & WINDOW_FLAG_HARDWARE) {

			context = SDL_GL_CreateContext (sdlWindow);

			#if defined (IPHONE) || defined (APPLETV) || defined (ANDROID)
			if (!context) {

				// Fall back to OpenGL ES 2 on older devices
				SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
				context = SDL_GL_CreateContext (sdlWindow);

			}
			#endif

			#ifdef LIME_GL_NEGOTIATE_CONTEXT
			if (!context) {

				// Negotiate downward rather than failing outright. The version and profile
				// attributes are consumed at context creation, not at window creation, so the
				// window built above stays valid and only the context needs retrying.
				static const int fallbackVersions[][2] = { { 4, 1 }, { 3, 3 } };

				for (int i = 0; !context && i < 2; i++) {

					SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, LIME_GL_CONTEXT_PROFILE);
					SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, fallbackVersions[i][0]);
					SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, fallbackVersions[i][1]);

					context = SDL_GL_CreateContext (sdlWindow);

				}

				if (!context) {

					// SDL's own defaults: no profile, 2.1 - the worst case is exactly the
					// behavior from before any version was pinned.
					SDL_GL_SetAttribute (SDL_GL_CONTEXT_PROFILE_MASK, 0);
					SDL_GL_SetAttribute (SDL_GL_CONTEXT_MAJOR_VERSION, 2);
					SDL_GL_SetAttribute (SDL_GL_CONTEXT_MINOR_VERSION, 1);

					context = SDL_GL_CreateContext (sdlWindow);

				}

			}
			#endif

			if (context && SDL_GL_MakeCurrent (sdlWindow, context)) {

				SDL_GL_SetSwapInterval ((flags & WINDOW_FLAG_VSYNC) ? 1 : 0);

				OpenGLBindings::Init ();

				#ifndef LIME_GLES

				int version = 0;
				glGetIntegerv (GL_MAJOR_VERSION, &version);

				if (version == 0) {

					float versionScan = 0;
					sscanf ((const char*)glGetString (GL_VERSION), "%f", &versionScan);
					version = versionScan;

				}

				if (version < 2 && !strstr ((const char*)glGetString (GL_VERSION), "OpenGL ES")) {

					SDL_GL_DestroyContext (context);
					context = 0;

				}

				#ifdef LIME_GL_NEGOTIATE_CONTEXT
				if (context && version < LIME_GL_CONTEXT_VERSION_MAJOR) {

					// The negotiation above settled for less than was asked for. Say so once,
					// rather than leaving version-gated entry points to fail later with no
					// explanation. The version is also readable from Haxe as `GL.version`.
					printf ("Requested OpenGL %d.%d, got %s. Features gated on OpenGL %d.x are unavailable.\n",
						LIME_GL_CONTEXT_VERSION_MAJOR, LIME_GL_CONTEXT_VERSION_MINOR,
						(const char*)glGetString (GL_VERSION), LIME_GL_CONTEXT_VERSION_MAJOR);

				}
				#endif

				#elif defined(IPHONE) || defined(APPLETV)

				SDL_PropertiesID props = SDL_GetWindowProperties (sdlWindow);
				OpenGLBindings::defaultFramebuffer = (int)SDL_GetNumberProperty (props, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
				OpenGLBindings::defaultRenderbuffer = (int)SDL_GetNumberProperty (props, SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER, 0);

				if (!OpenGLBindings::defaultFramebuffer) {

					glGetIntegerv (GL_FRAMEBUFFER_BINDING, &OpenGLBindings::defaultFramebuffer);
					glGetIntegerv (GL_RENDERBUFFER_BINDING, &OpenGLBindings::defaultRenderbuffer);

				}

				#endif

			} else if (context) {

				SDL_GL_DestroyContext (context);
				context = 0;

			}

		}

		if (!context) {

			sdlRenderer = SDL_CreateRenderer (sdlWindow, SDL_SOFTWARE_RENDERER);

		}

		if (context || sdlRenderer) {

			((SDLApplication*)currentApplication)->RegisterWindow (this);

		} else {

			printf ("Could not create SDL renderer: %s.\n", SDL_GetError ());

		}

		#if !defined (IPHONE) && !defined (APPLETV) && !defined (ANDROID) && !defined (EMSCRIPTEN)
		// SDL2 enabled text input by default on desktop platforms, SDL3 does not.
		// Keep the previous behaviour so TEXT_INPUT events keep arriving.
		SDL_StartTextInput (sdlWindow);
		#endif

	}


	SDLWindow::~SDLWindow () {

		if (sdlTexture) {

			SDL_DestroyTexture (sdlTexture);
			sdlTexture = 0;

		}

		if (sdlRenderer) {

			SDL_DestroyRenderer (sdlRenderer);
			sdlRenderer = 0;

		} else if (context) {

			SDL_GL_DestroyContext (context);
			context = 0;

		}

		if (sdlWindow) {

			SDL_DestroyWindow (sdlWindow);
			sdlWindow = 0;

		}

	}


	void SDLWindow::Alert (const char* message, const char* title) {

		SDL_FlashWindow (sdlWindow, SDL_FLASH_UNTIL_FOCUSED);

		if (message) {

			SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_INFORMATION, title, message, sdlWindow);

		}

	}


	void SDLWindow::Close () {

		if (sdlWindow) {

			SDL_DestroyWindow (sdlWindow);
			sdlWindow = 0;

		}

	}


	bool SDLWindow::SetVisible (bool visible) {

		if (visible) {

			SDL_ShowWindow (sdlWindow);

		} else {

			SDL_HideWindow (sdlWindow);

		}

		return !(SDL_GetWindowFlags (sdlWindow) & SDL_WINDOW_HIDDEN);

	}


	void SDLWindow::ContextFlip () {

		if (context && !sdlRenderer) {

			SDL_GL_SwapWindow (sdlWindow);

		} else if (sdlRenderer) {

			SDL_RenderPresent (sdlRenderer);

		}

	}


	void* SDLWindow::ContextLock (bool useCFFIValue) {

		if (sdlRenderer) {

			int width;
			int height;

			SDL_GetCurrentRenderOutputSize (sdlRenderer, &width, &height);

			if (width != contextWidth || height != contextHeight) {

				if (sdlTexture) {

					SDL_DestroyTexture (sdlTexture);

				}

				sdlTexture = SDL_CreateTexture (sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);

				contextWidth = width;
				contextHeight = height;

			}

			void *pixels;
			int pitch;

			if (useCFFIValue) {

				if (SDL_LockTexture (sdlTexture, NULL, &pixels, &pitch)) {

					value result = alloc_empty_object ();
					alloc_field (result, val_id ("width"), alloc_int (contextWidth));
					alloc_field (result, val_id ("height"), alloc_int (contextHeight));
					alloc_field (result, val_id ("pixels"), alloc_float ((uintptr_t)pixels));
					alloc_field (result, val_id ("pitch"), alloc_int (pitch));
					return result;

				} else {

					return alloc_null ();

				}

			} else {

				const int id_width = hl_hash_utf8 ("width");
				const int id_height = hl_hash_utf8 ("height");
				const int id_pixels = hl_hash_utf8 ("pixels");
				const int id_pitch = hl_hash_utf8 ("pitch");

				if (SDL_LockTexture (sdlTexture, NULL, &pixels, &pitch)) {

					vdynamic* result = (vdynamic*)hl_alloc_dynobj();
					hl_dyn_seti (result, id_width, &hlt_i32, contextWidth);
					hl_dyn_seti (result, id_height, &hlt_i32, contextHeight);
					hl_dyn_setd (result, id_pixels, (uintptr_t)pixels);
					hl_dyn_seti (result, id_pitch, &hlt_i32, pitch);
					return result;

				} else {

					return 0;

				}

			}

		} else {

			if (useCFFIValue) {

				return alloc_null ();

			} else {

				return 0;

			}

		}

	}


	void SDLWindow::ContextMakeCurrent () {

		if (sdlWindow && context) {

			SDL_GL_MakeCurrent (sdlWindow, context);

		}

	}


	void SDLWindow::ContextUnlock () {

		if (sdlTexture) {

			SDL_UnlockTexture (sdlTexture);
			SDL_RenderClear (sdlRenderer);
			SDL_RenderTexture (sdlRenderer, sdlTexture, NULL, NULL);

		}

	}


	void SDLWindow::Focus () {

		SDL_RaiseWindow (sdlWindow);

	}


	void* SDLWindow::GetContext () {

		return context;

	}


	const char* SDLWindow::GetContextType () {

		if (context) {

			return "opengl";

		} else if (sdlRenderer) {

			const char* name = SDL_GetRendererName (sdlRenderer);

			if (name && std::strcmp (name, SDL_SOFTWARE_RENDERER) == 0) {

				return "software";

			} else {

				return "opengl";

			}

		}

		return "none";

	}


	int SDLWindow::GetDisplay () {

		return SDLDisplay::GetIndex (SDL_GetDisplayForWindow (sdlWindow));

	}


	void SDLWindow::GetDisplayMode (DisplayMode* displayMode) {

		const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode (sdlWindow);

		if (!mode) {

			mode = SDL_GetDesktopDisplayMode (SDL_GetDisplayForWindow (sdlWindow));

		}

		if (!mode) {

			return;

		}

		displayMode->width = mode->w;
		displayMode->height = mode->h;
		displayMode->pixelFormat = GetLimePixelFormat (mode->format);
		displayMode->refreshRate = (int)(mode->refresh_rate + 0.5f);

	}


	int SDLWindow::GetHeight () {

		int width;
		int height;

		SDL_GetWindowSize (sdlWindow, &width, &height);

		return height;

	}


	uint32_t SDLWindow::GetID () {

		return SDL_GetWindowID (sdlWindow);

	}


	bool SDLWindow::GetMouseLock () {

		return SDL_GetWindowRelativeMouseMode (sdlWindow);

	}


	float SDLWindow::GetOpacity () {

		float opacity = SDL_GetWindowOpacity (sdlWindow);
		return opacity < 0 ? 1.0f : opacity;

	}


	double SDLWindow::GetScale () {

		float density = SDL_GetWindowPixelDensity (sdlWindow);
		return density > 0 ? density : 1.0;

	}


	bool SDLWindow::GetTextInputEnabled () {

		return SDL_TextInputActive (sdlWindow);

	}


	int SDLWindow::GetWidth () {

		int width;
		int height;

		SDL_GetWindowSize (sdlWindow, &width, &height);

		return width;

	}


	int SDLWindow::GetX () {

		int x;
		int y;

		SDL_GetWindowPosition (sdlWindow, &x, &y);

		return x;

	}


	int SDLWindow::GetY () {

		int x;
		int y;

		SDL_GetWindowPosition (sdlWindow, &x, &y);

		return y;

	}


	void SDLWindow::Move (int x, int y) {

		SDL_SetWindowPosition (sdlWindow, x, y);

	}


	void SDLWindow::ReadPixels (ImageBuffer *buffer, Rectangle *rect) {

		if (sdlRenderer) {

			SDL_Rect bounds = { 0, 0, 0, 0 };

			if (rect) {

				bounds.x = rect->x;
				bounds.y = rect->y;
				bounds.w = rect->width;
				bounds.h = rect->height;

			} else {

				SDL_GetCurrentRenderOutputSize (sdlRenderer, &bounds.w, &bounds.h);

			}

			SDL_Surface* surface = SDL_RenderReadPixels (sdlRenderer, &bounds);

			if (surface) {

				buffer->Resize (surface->w, surface->h, 32);
				SDL_ConvertPixels (surface->w, surface->h, surface->format, surface->pixels, surface->pitch, SDL_PIXELFORMAT_ABGR8888, buffer->data->buffer->b, buffer->Stride ());
				SDL_DestroySurface (surface);

			}

		} else if (context) {

			// TODO

		}

	}


	void SDLWindow::Resize (int width, int height) {

		SDL_SetWindowSize (sdlWindow, width, height);

	}


	void SDLWindow::SetMinimumSize (int width, int height) {

		SDL_SetWindowMinimumSize (sdlWindow, width, height);

	}


	void SDLWindow::SetMaximumSize (int width, int height) {

		SDL_SetWindowMaximumSize (sdlWindow, width, height);

	}


	bool SDLWindow::SetBorderless (bool borderless) {

		SDL_SetWindowBordered (sdlWindow, !borderless);

		return borderless;

	}


	static SDL_Cursor* GetSystemCursor (SDL_Cursor** cache, SDL_SystemCursor id) {

		if (!*cache) {

			*cache = SDL_CreateSystemCursor (id);

		}

		return *cache;

	}


	void SDLWindow::SetCursor (Cursor cursor) {

		if (cursor != currentCursor) {

			if (currentCursor == HIDDEN) {

				SDL_ShowCursor ();

			}

			switch (cursor) {

				case HIDDEN:

					SDL_HideCursor ();
					break;

				case CROSSHAIR:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::crosshairCursor, SDL_SYSTEM_CURSOR_CROSSHAIR));
					break;

				case MOVE:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::moveCursor, SDL_SYSTEM_CURSOR_MOVE));
					break;

				case POINTER:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::pointerCursor, SDL_SYSTEM_CURSOR_POINTER));
					break;

				case RESIZE_NESW:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::resizeNESWCursor, SDL_SYSTEM_CURSOR_NESW_RESIZE));
					break;

				case RESIZE_NS:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::resizeNSCursor, SDL_SYSTEM_CURSOR_NS_RESIZE));
					break;

				case RESIZE_NWSE:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::resizeNWSECursor, SDL_SYSTEM_CURSOR_NWSE_RESIZE));
					break;

				case RESIZE_WE:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::resizeWECursor, SDL_SYSTEM_CURSOR_EW_RESIZE));
					break;

				case TEXT:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::textCursor, SDL_SYSTEM_CURSOR_TEXT));
					break;

				case WAIT:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::waitCursor, SDL_SYSTEM_CURSOR_WAIT));
					break;

				case WAIT_ARROW:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::waitArrowCursor, SDL_SYSTEM_CURSOR_PROGRESS));
					break;

				default:

					SDL_SetCursor (GetSystemCursor (&SDLCursor::arrowCursor, SDL_SYSTEM_CURSOR_DEFAULT));
					break;

			}

			currentCursor = cursor;

		}

	}


	void SDLWindow::SetDisplayMode (DisplayMode* displayMode) {

		SDL_DisplayID displayID = SDL_GetDisplayForWindow (sdlWindow);
		SDL_DisplayMode mode;

		if (!SDL_GetClosestFullscreenDisplayMode (displayID, displayMode->width, displayMode->height, (float)displayMode->refreshRate, true, &mode)) {

			// Fall back to an exact mode description when no close match exists
			SDL_zero (mode);
			mode.displayID = displayID;
			mode.format = GetSDLPixelFormat (displayMode->pixelFormat);
			mode.w = displayMode->width;
			mode.h = displayMode->height;
			mode.refresh_rate = (float)displayMode->refreshRate;

		}

		if (SDL_SetWindowFullscreenMode (sdlWindow, &mode)) {

			if (SDL_GetWindowFlags (sdlWindow) & SDL_WINDOW_FULLSCREEN) {

				SDL_SetWindowFullscreen (sdlWindow, true);

			}

		}

	}


	bool SDLWindow::SetFullscreen (bool fullscreen) {

		if (!SDL_SetWindowFullscreen (sdlWindow, fullscreen)) {

			// report what the window actually ended up as, not what was requested -
			// a failed call here previously still reported success to Haxe
			printf ("Could not set fullscreen: %s.\n", SDL_GetError ());
			return (SDL_GetWindowFlags (sdlWindow) & SDL_WINDOW_FULLSCREEN) != 0;

		}

		return fullscreen;

	}


	void SDLWindow::SetIcon (ImageBuffer *imageBuffer) {

		SDL_PixelFormat format = SDL_GetPixelFormatForMasks (imageBuffer->bitsPerPixel, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		SDL_Surface *surface = SDL_CreateSurfaceFrom (imageBuffer->width, imageBuffer->height, format, imageBuffer->data->buffer->b, imageBuffer->Stride ());

		if (surface) {

			SDL_SetWindowIcon (sdlWindow, surface);
			SDL_DestroySurface (surface);

		}

	}


	bool SDLWindow::SetMaximized (bool maximized) {

		if (maximized) {

			SDL_MaximizeWindow (sdlWindow);

		} else {

			SDL_RestoreWindow (sdlWindow);

		}

		return maximized;

	}


	bool SDLWindow::SetMinimized (bool minimized) {

		if (minimized) {

			SDL_MinimizeWindow (sdlWindow);

		} else {

			SDL_RestoreWindow (sdlWindow);

		}

		return minimized;

	}


	void SDLWindow::SetMouseLock (bool mouseLock) {

		SDL_SetWindowRelativeMouseMode (sdlWindow, mouseLock);

	}


	void SDLWindow::SetOpacity (float opacity) {

		SDL_SetWindowOpacity (sdlWindow, opacity);

	}


	bool SDLWindow::SetResizable (bool resizable) {

		#ifndef EMSCRIPTEN

		SDL_SetWindowResizable (sdlWindow, resizable);

		return (SDL_GetWindowFlags (sdlWindow) & SDL_WINDOW_RESIZABLE);

		#else

		return resizable;

		#endif

	}


	void SDLWindow::SetTextInputEnabled (bool enabled) {

		if (enabled) {

			SDL_StartTextInput (sdlWindow);

		} else {

			SDL_StopTextInput (sdlWindow);

		}

	}


	void SDLWindow::SetTextInputRect (Rectangle * rect) {

		SDL_Rect bounds = { 0, 0, 0, 0 };

		if (rect) {

			bounds.x = rect->x;
			bounds.y = rect->y;
			bounds.w = rect->width;
			bounds.h = rect->height;

		}

		SDL_SetTextInputArea (sdlWindow, &bounds, 0);

	}


	const char* SDLWindow::SetTitle (const char* title) {

		SDL_SetWindowTitle (sdlWindow, title);

		return title;

	}


	bool SDLWindow::SetVSync (bool vsync) {

		return SDL_GL_SetSwapInterval (vsync ? 1 : 0) && vsync;

	}


	void SDLWindow::WarpMouse (int x, int y) {

		SDL_WarpMouseInWindow (sdlWindow, (float)x, (float)y);

	}


	Window* CreateWindow (Application* application, int width, int height, int flags, const char* title) {

		return new SDLWindow (application, width, height, flags, title);

	}


}
