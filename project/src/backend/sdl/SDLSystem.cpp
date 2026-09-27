#include <graphics/PixelFormat.h>
#include <math/Rectangle.h>
#include <system/Clipboard.h>
#include <system/Display.h>
#include <system/DisplayMode.h>
#include <system/JNI.h>
#include <system/System.h>

#ifdef HX_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef HX_WINDOWS
#include <shlobj.h>
#include <stdio.h>
//#include <io.h>
//#include <fcntl.h>
#ifdef __MINGW32__
#ifndef CSIDL_MYDOCUMENTS
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#endif
#ifndef SHGFP_TYPE_CURRENT
#define SHGFP_TYPE_CURRENT 0
#endif
#endif
#if UNICODE
#define WIN_StringToUTF8(S) SDL_iconv_string("UTF-8", "UTF-16LE", (char *)(S), (SDL_wcslen(S)+1)*sizeof(WCHAR))
#define WIN_UTF8ToString(S) (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", (char *)(S), SDL_strlen(S)+1)
#else
#define WIN_StringToUTF8(S) SDL_iconv_string("UTF-8", "ASCII", (char *)(S), (SDL_strlen(S)+1))
#define WIN_UTF8ToString(S) SDL_iconv_string("ASCII", "UTF-8", (char *)(S), SDL_strlen(S)+1)
#endif
#endif

#include <SDL3/SDL.h>
#include <string>
#include <cstring>
#include "SDLDisplay.h"

#include <locale>
#include <codecvt>

using wstring_convert = std::wstring_convert<std::codecvt_utf8<wchar_t>>;


namespace lime {


	static int id_bounds;
	static int id_currentMode;
	static int id_dpi;
	static int id_height;
	static int id_name;
	static int id_orientation;
	static int id_pixelFormat;
	static int id_refreshRate;
	static int id_supportedModes;
	static int id_width;
	static int id_safeArea;
	static bool init = false;


	const char* Clipboard::GetText () {

		// SDL3 returns memory owned by the caller, keep the last result alive
		// so the previous (SDL2) contract of a borrowed string is preserved
		static char* text = 0;

		if (text) {

			SDL_free (text);

		}

		text = SDL_GetClipboardText ();
		return text;

	}


	bool Clipboard::HasText () {

		return SDL_HasClipboardText ();

	}


	bool Clipboard::SetText (const char* text) {

		return SDL_SetClipboardText (text);

	}


	void *JNI::GetEnv () {

		#ifdef ANDROID
		return SDL_GetAndroidJNIEnv ();
		#else
		return 0;
		#endif

	}


	bool System::GetAllowScreenTimeout () {

		return SDL_ScreenSaverEnabled ();

	}


	std::wstring* System::GetDirectory (SystemDirectory type, const char* company, const char* title) {

		std::wstring* result = 0;
		System::GCEnterBlocking ();

		switch (type) {

			case APPLICATION: {

				const char* path = SDL_GetBasePath ();

				if (path != nullptr) {

					wstring_convert converter;
					result = new std::wstring (converter.from_bytes(path));

				}

				break;

			}

			case APPLICATION_STORAGE: {

				char* path = SDL_GetPrefPath (company, title);

				if (path != nullptr) {

        			wstring_convert converter;
					result = new std::wstring (converter.from_bytes(path));
					SDL_free (path);

				}

				break;

			}

			case DESKTOP: {

				#if defined (HX_WINRT)

				Windows::Storage::StorageFolder^ folder = Windows::Storage::KnownFolders::HomeGroup;
				result = new std::wstring (folder->Path->Data ());

				#elif defined (HX_WINDOWS)

				WCHAR folderPath[MAX_PATH] = L"";
				SHGetFolderPathW (NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, folderPath);
				result = new std::wstring (folderPath);

				#elif defined (IPHONE)

				result = System::GetIOSDirectory (type);

				#elif !defined (ANDROID)

				char const* home = getenv ("HOME");

				if (home != NULL) {

					std::string path = std::string (home) + std::string ("/Desktop");
					wstring_convert converter;
					result = new std::wstring (converter.from_bytes(path));

				}

				#endif
				break;

			}

			case DOCUMENTS: {

				#if defined (HX_WINRT)

				Windows::Storage::StorageFolder^ folder = Windows::Storage::KnownFolders::DocumentsLibrary;
				result = new std::wstring (folder->Path->Data ());

				#elif defined (HX_WINDOWS)

				WCHAR folderPath[MAX_PATH] = L"";
				SHGetFolderPathW (NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, folderPath);
				result = new std::wstring (folderPath);

				#elif defined (IPHONE)

				result = System::GetIOSDirectory (type);

				#elif defined (ANDROID)

				result = new std::wstring (L"/mnt/sdcard/Documents");

				#else

				char const* home = getenv ("HOME");

				if (home != NULL) {

					std::string path = std::string (home) + std::string ("/Documents");
					wstring_convert converter;
					result = new std::wstring (converter.from_bytes(path));

				}

				#endif
				break;

			}

			case FONTS: {

				#if defined (HX_WINRT)

				// TODO

				#elif defined (HX_WINDOWS)

				WCHAR folderPath[MAX_PATH] = L"";
				SHGetFolderPathW (NULL, CSIDL_FONTS, NULL, SHGFP_TYPE_CURRENT, folderPath);
				result = new std::wstring (folderPath);

				#elif defined (HX_MACOS)

				result = new std::wstring (L"/Library/Fonts");

				#elif defined (IPHONE)

				result = new std::wstring (L"/System/Library/Fonts");

				#elif defined (ANDROID)

				result = new std::wstring (L"/system/fonts");

				#elif defined (BLACKBERRY)

				result = new std::wstring (L"/usr/fonts/font_repository/monotype");

				#else

				result = new std::wstring (L"/usr/share/fonts/truetype");

				#endif
				break;

			}

			case USER: {

				#if defined (HX_WINRT)

				Windows::Storage::StorageFolder^ folder = Windows::Storage::ApplicationData::Current->RoamingFolder;
				result = new std::wstring (folder->Path->Data ());

				#elif defined (HX_WINDOWS)

				WCHAR folderPath[MAX_PATH] = L"";
				SHGetFolderPathW (NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, folderPath);
				result = new std::wstring (folderPath);

				#elif defined (IPHONE)

				result = System::GetIOSDirectory (type);

				#elif defined (ANDROID)

				result = new std::wstring (L"/mnt/sdcard");

				#else

				char const* home = getenv ("HOME");

				if (home != NULL) {

					std::string path = std::string (home);
					wstring_convert converter;
					result = new std::wstring (converter.from_bytes(path));

				}

				#endif
				break;

			}

		}

		System::GCExitBlocking ();
		return result;

	}


	SDL_DisplayID SDLDisplay::GetID (int index) {

		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays (&count);
		SDL_DisplayID result = 0;

		if (displays) {

			if (index >= 0 && index < count) {

				result = displays[index];

			}

			SDL_free (displays);

		}

		return result;

	}


	int SDLDisplay::GetIndex (SDL_DisplayID displayID) {

		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays (&count);
		int result = 0;

		if (displays) {

			for (int i = 0; i < count; i++) {

				if (displays[i] == displayID) {

					result = i;
					break;

				}

			}

			SDL_free (displays);

		}

		return result;

	}


	static void ReadDisplayMode (const SDL_DisplayMode* displayMode, DisplayMode* mode) {

		if (!displayMode) {

			mode->width = 0;
			mode->height = 0;
			mode->pixelFormat = RGBA32;
			mode->refreshRate = 0;
			return;

		}

		mode->width = displayMode->w;
		mode->height = displayMode->h;

		switch (displayMode->format) {

			case SDL_PIXELFORMAT_ARGB8888:

				mode->pixelFormat = ARGB32;
				break;

			case SDL_PIXELFORMAT_BGRA8888:
			case SDL_PIXELFORMAT_BGRX8888:

				mode->pixelFormat = BGRA32;
				break;

			default:

				mode->pixelFormat = RGBA32;

		}

		mode->refreshRate = (int)(displayMode->refresh_rate + 0.5f);

	}


	static float GetDisplayDPI (SDL_DisplayID displayID) {

		// SDL3 no longer reports physical DPI, derive it from the content scale
		#if defined (ANDROID)
		const float baseDPI = 160.0f;
		#elif defined (IPHONE)
		const float baseDPI = 163.0f;
		#elif defined (HX_MACOS)
		const float baseDPI = 72.0f;
		#else
		const float baseDPI = 96.0f;
		#endif

		#ifndef EMSCRIPTEN
		float scale = SDL_GetDisplayContentScale (displayID);

		if (scale > 0.0f) {

			return baseDPI * scale;

		}
		#endif

		return 72.0f;

	}


	void* System::GetDisplay (bool useCFFIValue, int id) {

		SDL_DisplayID displayID = SDLDisplay::GetID (id);

		if (useCFFIValue) {

			if (!init) {

				id_bounds = val_id ("bounds");
				id_currentMode = val_id ("currentMode");
				id_dpi = val_id ("dpi");
				id_height = val_id ("height");
				id_name = val_id ("name");
				id_orientation = val_id ("orientation");
				id_pixelFormat = val_id ("pixelFormat");
				id_refreshRate = val_id ("refreshRate");
				id_supportedModes = val_id ("supportedModes");
				id_width = val_id ("width");
				id_safeArea = val_id ("safeArea");
				init = true;

			}

			if (displayID == 0) {

				return alloc_null ();

			}

			value display = alloc_empty_object ();
			const char* displayName = SDL_GetDisplayName (displayID);
			alloc_field (display, id_name, alloc_string (displayName ? displayName : ""));

			SDL_Rect bounds = { 0, 0, 0, 0 };
			SDL_GetDisplayBounds (displayID, &bounds);
			alloc_field (display, id_bounds, Rectangle (bounds.x, bounds.y, bounds.w, bounds.h).Value ());

			Rectangle safeAreaInsets;
			Display::GetSafeAreaInsets(id, &safeAreaInsets);
			alloc_field (display, id_safeArea,
				Rectangle (bounds.x + safeAreaInsets.x,
					bounds.y + safeAreaInsets.y,
					bounds.w - safeAreaInsets.x - safeAreaInsets.width,
					bounds.h - safeAreaInsets.y - safeAreaInsets.height).Value ());

			alloc_field (display, id_dpi, alloc_float (GetDisplayDPI (displayID)));

			SDL_DisplayOrientation orientation = SDL_GetCurrentDisplayOrientation (displayID);
			alloc_field (display, id_orientation, alloc_int (orientation));

			DisplayMode mode;

			ReadDisplayMode (SDL_GetDesktopDisplayMode (displayID), &mode);
			alloc_field (display, id_currentMode, (value)mode.Value ());

			int numDisplayModes = 0;
			SDL_DisplayMode** displayModes = SDL_GetFullscreenDisplayModes (displayID, &numDisplayModes);

			if (!displayModes) numDisplayModes = 0;

			value supportedModes = alloc_array (numDisplayModes);

			for (int i = 0; i < numDisplayModes; i++) {

				ReadDisplayMode (displayModes[i], &mode);
				val_array_set_i (supportedModes, i, (value)mode.Value ());

			}

			SDL_free (displayModes);

			alloc_field (display, id_supportedModes, supportedModes);
			return display;

		} else {

			const int id_bounds = hl_hash_utf8 ("bounds");
			const int id_currentMode = hl_hash_utf8 ("currentMode");
			const int id_dpi = hl_hash_utf8 ("dpi");
			const int id_height = hl_hash_utf8 ("height");
			const int id_name = hl_hash_utf8 ("name");
			const int id_orientation = hl_hash_utf8 ("orientation");
			const int id_pixelFormat = hl_hash_utf8 ("pixelFormat");
			const int id_refreshRate = hl_hash_utf8 ("refreshRate");
			const int id_supportedModes = hl_hash_utf8 ("supportedModes");
			const int id_width = hl_hash_utf8 ("width");
			const int id_safeArea = hl_hash_utf8 ("safeArea");
			const int id_x = hl_hash_utf8 ("x");
			const int id_y = hl_hash_utf8 ("y");

			if (displayID == 0) {

				return 0;

			}

			vdynamic* display = (vdynamic*)hl_alloc_dynobj ();

			const char* displayName = SDL_GetDisplayName (displayID);
			if (!displayName) displayName = "";
			char* _displayName = (char*)malloc(strlen(displayName) + 1);
			strcpy (_displayName, displayName);
			hl_dyn_setp (display, id_name, &hlt_bytes, _displayName);

			SDL_Rect bounds = { 0, 0, 0, 0 };
			SDL_GetDisplayBounds (displayID, &bounds);

			vdynamic* _bounds = (vdynamic*)hl_alloc_dynobj ();
			hl_dyn_seti (_bounds, id_x, &hlt_i32, bounds.x);
			hl_dyn_seti (_bounds, id_y, &hlt_i32, bounds.y);
			hl_dyn_seti (_bounds, id_width, &hlt_i32, bounds.w);
			hl_dyn_seti (_bounds, id_height, &hlt_i32, bounds.h);

			hl_dyn_setp (display, id_bounds, &hlt_dynobj, _bounds);

			Rectangle safeAreaInsets;
			Display::GetSafeAreaInsets(id, &safeAreaInsets);
			vdynamic* _safeArea = (vdynamic*)hl_alloc_dynobj ();
			hl_dyn_seti (_safeArea, id_x, &hlt_i32, bounds.x + safeAreaInsets.x);
			hl_dyn_seti (_safeArea, id_y, &hlt_i32, bounds.y + safeAreaInsets.y);
			hl_dyn_seti (_safeArea, id_width, &hlt_i32, bounds.w - safeAreaInsets.x - safeAreaInsets.width);
			hl_dyn_seti (_safeArea, id_height, &hlt_i32, bounds.h - safeAreaInsets.y - safeAreaInsets.height);

			hl_dyn_setp (display, id_safeArea, &hlt_dynobj, _safeArea);

			hl_dyn_setf (display, id_dpi, GetDisplayDPI (displayID));

			SDL_DisplayOrientation orientation = SDL_GetCurrentDisplayOrientation (displayID);
			hl_dyn_seti (display, id_orientation, &hlt_i32, orientation);

			DisplayMode mode;
			ReadDisplayMode (SDL_GetDesktopDisplayMode (displayID), &mode);

			vdynamic* _displayMode = (vdynamic*)hl_alloc_dynobj ();
			hl_dyn_seti (_displayMode, id_height, &hlt_i32, mode.height);
			hl_dyn_seti (_displayMode, id_pixelFormat, &hlt_i32, mode.pixelFormat);
			hl_dyn_seti (_displayMode, id_refreshRate, &hlt_i32, mode.refreshRate);
			hl_dyn_seti (_displayMode, id_width, &hlt_i32, mode.width);
			hl_dyn_setp (display, id_currentMode, &hlt_dynobj, _displayMode);

			int numDisplayModes = 0;
			SDL_DisplayMode** displayModes = SDL_GetFullscreenDisplayModes (displayID, &numDisplayModes);

			if (!displayModes) numDisplayModes = 0;

			hl_varray* supportedModes = (hl_varray*)hl_alloc_array (&hlt_dynobj, numDisplayModes);
			vdynamic** supportedModesData = hl_aptr (supportedModes, vdynamic*);

			for (int i = 0; i < numDisplayModes; i++) {

				ReadDisplayMode (displayModes[i], &mode);

				vdynamic* _displayMode = (vdynamic*)hl_alloc_dynobj ();
				hl_dyn_seti (_displayMode, id_height, &hlt_i32, mode.height);
				hl_dyn_seti (_displayMode, id_pixelFormat, &hlt_i32, mode.pixelFormat);
				hl_dyn_seti (_displayMode, id_refreshRate, &hlt_i32, mode.refreshRate);
				hl_dyn_seti (_displayMode, id_width, &hlt_i32, mode.width);

				*supportedModesData++ = _displayMode;

			}

			SDL_free (displayModes);

			hl_dyn_setp (display, id_supportedModes, &hlt_array, supportedModes);
			return display;

		}

	}


	int System::GetNumDisplays () {

		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays (&count);
		SDL_free (displays);
		return count;

	}


	double System::GetTimer () {

		return (double)SDL_GetTicksNS () / SDL_NS_PER_MS;

	}


	double System::GetPerformanceCounter () {

		return SDL_GetPerformanceCounter ();

	}


	double System::GetPerformanceFrequency () {

		return SDL_GetPerformanceFrequency ();

	}


	bool System::SetAllowScreenTimeout (bool allow) {

		if (allow) {

			SDL_EnableScreenSaver ();

		} else {

			SDL_DisableScreenSaver ();

		}

		return allow;

	}


	int System::GetDisplayOrientation (int displayIndex) {

		switch (SDL_GetCurrentDisplayOrientation (SDLDisplay::GetID (displayIndex))) {

			case SDL_ORIENTATION_LANDSCAPE: return 1;
			case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return 2;
			case SDL_ORIENTATION_PORTRAIT: return 3;
			case SDL_ORIENTATION_PORTRAIT_FLIPPED: return 4;
			default: return 0;

		}

	}


	#ifndef HX_WINDOWS

	// SDL3 removed SDL_RWFromFP, so wrap a stdio FILE* in an SDL_IOStream
	// and expose it through SDL_PROP_IOSTREAM_STDIO_FILE_POINTER.

	static Sint64 SDLCALL stdio_size (void* userdata) {

		FILE* fp = (FILE*)userdata;
		long pos = ::ftell (fp);
		if (pos < 0 || ::fseek (fp, 0, SEEK_END) != 0) return -1;
		long size = ::ftell (fp);
		::fseek (fp, pos, SEEK_SET);
		return size;

	}


	static Sint64 SDLCALL stdio_seek (void* userdata, Sint64 offset, SDL_IOWhence whence) {

		FILE* fp = (FILE*)userdata;
		int stdioWhence = whence == SDL_IO_SEEK_CUR ? SEEK_CUR : (whence == SDL_IO_SEEK_END ? SEEK_END : SEEK_SET);

		if (::fseek (fp, (long)offset, stdioWhence) != 0) {

			SDL_SetError ("Error seeking in datastream");
			return -1;

		}

		return ::ftell (fp);

	}


	static size_t SDLCALL stdio_read (void* userdata, void* ptr, size_t size, SDL_IOStatus* status) {

		FILE* fp = (FILE*)userdata;
		size_t bytes = ::fread (ptr, 1, size, fp);

		if (bytes == 0) {

			*status = ::ferror (fp) ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;

		}

		return bytes;

	}


	static size_t SDLCALL stdio_write (void* userdata, const void* ptr, size_t size, SDL_IOStatus* status) {

		FILE* fp = (FILE*)userdata;
		size_t bytes = ::fwrite (ptr, 1, size, fp);

		if (bytes == 0 && ::ferror (fp)) {

			*status = SDL_IO_STATUS_ERROR;

		}

		return bytes;

	}


	static bool SDLCALL stdio_flush (void* userdata, SDL_IOStatus* status) {

		return ::fflush ((FILE*)userdata) == 0;

	}


	static bool SDLCALL stdio_close (void* userdata) {

		return ::fclose ((FILE*)userdata) == 0;

	}


	static SDL_IOStream* IOFromFP (FILE* fp) {

		if (!fp) return NULL;

		SDL_IOStreamInterface iface;
		SDL_INIT_INTERFACE (&iface);
		iface.size = stdio_size;
		iface.seek = stdio_seek;
		iface.read = stdio_read;
		iface.write = stdio_write;
		iface.flush = stdio_flush;
		iface.close = stdio_close;

		SDL_IOStream* stream = SDL_OpenIO (&iface, fp);

		if (!stream) {

			::fclose (fp);
			return NULL;

		}

		SDL_SetPointerProperty (SDL_GetIOProperties (stream), SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, fp);
		return stream;

	}

	#endif


	FILE* FILE_HANDLE::getFile () {

		#ifndef HX_WINDOWS

		return (FILE*)SDL_GetPointerProperty (SDL_GetIOProperties ((SDL_IOStream*)handle), SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, NULL);

		#else

		return (FILE*)handle;

		#endif

	}


	int FILE_HANDLE::getLength () {

		#ifndef HX_WINDOWS

		System::GCEnterBlocking ();
		int size = (int)SDL_GetIOSize ((SDL_IOStream*)handle);
		System::GCExitBlocking ();
		return size;

		#else

		return 0;

		#endif

	}


	bool FILE_HANDLE::isFile () {

		#ifndef HX_WINDOWS

		// Only streams backed by a real stdio FILE* can be handed to libraries
		// expecting one, anything else (Android assets, etc.) is read via SDL
		return getFile () != NULL;

		#else

		return true;

		#endif

	}


	int fclose (FILE_HANDLE *stream) {

		#ifndef HX_WINDOWS

		if (stream) {

			System::GCEnterBlocking ();
			int code = SDL_CloseIO ((SDL_IOStream*)stream->handle) ? 0 : EOF;
			delete stream;
			System::GCExitBlocking ();
			return code;

		}

		return 0;

		#else

		if (stream) {

			System::GCEnterBlocking ();
			int code = ::fclose ((FILE*)stream->handle);
			delete stream;
			System::GCExitBlocking ();
			return code;

		}

		return 0;

		#endif

	}


	FILE_HANDLE *fdopen (int fd, const char *mode) {

		#ifndef HX_WINDOWS

		System::GCEnterBlocking ();
		FILE* fp = ::fdopen (fd, mode);
		SDL_IOStream *result = IOFromFP (fp);
		System::GCExitBlocking ();

		if (result) {

			return new FILE_HANDLE (result);

		}

		return NULL;

		#else

		FILE* result;

		System::GCEnterBlocking ();
		result = ::fdopen (fd, mode);
		System::GCExitBlocking ();

		if (result) {

			return new FILE_HANDLE (result);

		}

		return NULL;

		#endif

	}


	FILE_HANDLE *fopen (const char *filename, const char *mode) {

		#ifndef HX_WINDOWS

		SDL_IOStream *result;

		System::GCEnterBlocking ();

		#ifdef HX_MACOS

		result = SDL_IOFromFile (filename, mode);

		if (!result && mode && mode[0] == 'r') {

			CFStringRef str = CFStringCreateWithCString (NULL, filename, kCFStringEncodingUTF8);
			CFURLRef path = CFBundleCopyResourceURL (CFBundleGetMainBundle (), str, NULL, NULL);
			CFRelease (str);

			if (path) {

				str = CFURLCopyPath (path);
				CFIndex maxSize = CFStringGetMaximumSizeForEncoding (CFStringGetLength (str), kCFStringEncodingUTF8);
				char *buffer = (char *)malloc (maxSize);

				if (CFStringGetCString (str, buffer, maxSize, kCFStringEncodingUTF8)) {

					result = IOFromFP (::fopen (buffer, "rb"));

				}

				free (buffer);
				CFRelease (str);
				CFRelease (path);

			}

		}
		#else
		result = SDL_IOFromFile (filename, mode);
		#endif

		System::GCExitBlocking ();

		if (result) {

			return new FILE_HANDLE (result);

		}

		return NULL;

		#else

		FILE* result;
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring* wfilename = new std::wstring (converter.from_bytes (filename));
		std::wstring* wmode = new std::wstring (converter.from_bytes (mode));

		System::GCEnterBlocking ();
		result = ::_wfopen (wfilename->c_str(), wmode->c_str());
		System::GCExitBlocking ();

		delete wfilename;
		delete wmode;

		if (result) {

			return new FILE_HANDLE (result);

		}

		return NULL;

		#endif

	}


	size_t fread (void *ptr, size_t size, size_t count, FILE_HANDLE *stream) {

		size_t nmem;
		System::GCEnterBlocking ();

		#ifndef HX_WINDOWS

		nmem = (stream && size > 0) ? SDL_ReadIO ((SDL_IOStream*)stream->handle, ptr, size * count) / size : 0;

		#else

		nmem = ::fread (ptr, size, count, (FILE*)stream->handle);

		#endif

		System::GCExitBlocking ();
		return nmem;

	}


	int fseek (FILE_HANDLE *stream, long int offset, int origin) {

		int success;
		System::GCEnterBlocking ();

		#ifndef HX_WINDOWS

		success = (stream && SDL_SeekIO ((SDL_IOStream*)stream->handle, offset, (SDL_IOWhence)origin) >= 0) ? 0 : -1;

		#else

		success = ::fseek ((FILE*)stream->handle, offset, origin);

		#endif

		System::GCExitBlocking ();
		return success;

	}


	long int ftell (FILE_HANDLE *stream) {

		long int pos;
		System::GCEnterBlocking ();

		#ifndef HX_WINDOWS

		pos = stream ? (long int)SDL_TellIO ((SDL_IOStream*)stream->handle) : -1;

		#else

		pos = ::ftell ((FILE*)stream->handle);

		#endif

		System::GCExitBlocking ();
		return pos;

	}


	size_t fwrite (const void *ptr, size_t size, size_t count, FILE_HANDLE *stream) {

		size_t nmem;
		System::GCEnterBlocking ();

		#ifndef HX_WINDOWS

		nmem = (stream && size > 0) ? SDL_WriteIO ((SDL_IOStream*)stream->handle, ptr, size * count) / size : 0;

		#else

		nmem = ::fwrite (ptr, size, count, (FILE*)stream->handle);

		#endif

		System::GCExitBlocking ();
		return nmem;

	}


}
