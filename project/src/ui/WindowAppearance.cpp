#include <ui/Window.h>

#ifdef LIME_SDL
#include <SDL3/SDL.h>
#endif

#if defined(LIME_SDL) && defined(HX_WINDOWS) && !defined(HX_WINRT)
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#ifdef LIME_NATIVE_DESKTOP
#include <native_desktop/WindowsAppearance.h>
#endif
#endif

namespace lime {

#if defined(LIME_SDL) && defined(HX_MACOS)
	void SetCocoaWindowAppearance (SDL_Window* window, int appearance);
	int GetCocoaWindowAppearance (SDL_Window* window);
#endif

#if defined(LIME_SDL) && defined(HX_WINDOWS) && !defined(HX_WINRT)
	static const wchar_t* APPEARANCE_PROPERTY = L"Lime.WindowAppearance";
	static const UINT_PTR APPEARANCE_SUBCLASS = 0x4C494D45;

	static bool HighContrast () {
		HIGHCONTRASTW contrast = {};
		contrast.cbSize = sizeof (contrast);
		return SystemParametersInfoW (SPI_GETHIGHCONTRAST, sizeof (contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
	}

	static bool SystemDark () {
		// A missing preference uses the native light appearance on older Windows versions.
		DWORD light = 1, value = 1, length = sizeof (value), type = 0;
		HKEY key;
		if (RegOpenKeyExW (HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
			if (RegQueryValueExW (key, L"AppsUseLightTheme", NULL, &type, (LPBYTE)&value, &length) == ERROR_SUCCESS && type == REG_DWORD && length == sizeof (value)) light = value;
			RegCloseKey (key);
		}
		return light == 0;
	}

	static HMODULE DwmLibrary () {
		static HMODULE library = LoadLibraryW (L"dwmapi.dll");
		return library;
	}

	static void ApplyWindowsAppearance (HWND window, int appearance) {
		typedef HRESULT (WINAPI *SetAttribute) (HWND, DWORD, LPCVOID, DWORD);
		static SetAttribute set = DwmLibrary () ? (SetAttribute)GetProcAddress (DwmLibrary (), "DwmSetWindowAttribute") : NULL;
		BOOL dark = !HighContrast () && (appearance == 2 || (appearance == 0 && SystemDark ()));
		#ifdef LIME_NATIVE_DESKTOP
		native_desktop::SetWindowsMenuAppearance (window, appearance);
		#endif
		// DWMWA_USE_IMMERSIVE_DARK_MODE; older systems reject it without changing the window.
		if (set && SUCCEEDED (set (window, 20, &dark, sizeof (dark))))
			RedrawWindow (window, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
	}

	static LRESULT CALLBACK AppearanceWindowProc (HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
		if (message == WM_NCDESTROY) {
			RemovePropW (window, APPEARANCE_PROPERTY);
			RemoveWindowSubclass (window, AppearanceWindowProc, id);
		} else if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {
			HANDLE stored = GetPropW (window, APPEARANCE_PROPERTY);
			if (stored) ApplyWindowsAppearance (window, (int)(INT_PTR)stored - 1);
		}
		return DefSubclassProc (window, message, wParam, lParam);
	}
#endif


	void SetWindowAppearance (Window* window, int appearance) {
		if (!window || appearance < 0 || appearance > 2) return;
		#ifdef LIME_SDL
		SDL_Window* native = SDL_GetWindowFromID (window->GetID ());
		if (!native) return;
		#if defined(HX_MACOS)
		SetCocoaWindowAppearance (native, appearance);
		#else
		#if defined(HX_WINDOWS) && !defined(HX_WINRT)
		HWND handle = (HWND)SDL_GetPointerProperty (SDL_GetWindowProperties (native), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
		if (!handle) return;
		if (!SetWindowSubclass (handle, AppearanceWindowProc, APPEARANCE_SUBCLASS, 0)) return;
		if (!SetPropW (handle, APPEARANCE_PROPERTY, (HANDLE)(INT_PTR)(appearance + 1))) return;
		ApplyWindowsAppearance (handle, appearance);
		#else
		SDL_SetPointerProperty (SDL_GetWindowProperties (native), "Lime.WindowAppearance", (void*)(intptr_t)(appearance + 1));
		#endif
		#endif
		#endif
	}

	int GetWindowAppearance (Window* window) {
		#ifdef LIME_SDL
		if (!window) return 0;
		SDL_Window* native = SDL_GetWindowFromID (window->GetID ());
		if (!native) return 0;
		#if defined(HX_MACOS)
		return GetCocoaWindowAppearance (native);
		#else
		#if defined(HX_WINDOWS) && !defined(HX_WINRT)
		HWND handle = (HWND)SDL_GetPointerProperty (SDL_GetWindowProperties (native), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
		if (!handle) return 0;
		typedef HRESULT (WINAPI *GetAttribute) (HWND, DWORD, PVOID, DWORD);
		static GetAttribute get = DwmLibrary () ? (GetAttribute)GetProcAddress (DwmLibrary (), "DwmGetWindowAttribute") : NULL;
		BOOL dark = FALSE;
		return get && SUCCEEDED (get (handle, 20, &dark, sizeof (dark))) ? (dark ? 2 : 1) : 0;
		#else
		int stored = (int)(intptr_t)SDL_GetPointerProperty (SDL_GetWindowProperties (native), "Lime.WindowAppearance", NULL) - 1;
		return stored < 0 ? 0 : stored;
		#endif
		#endif
		#endif
		return 0;
	}
}
