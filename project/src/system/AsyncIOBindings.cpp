#include <SDL3/SDL.h>
#include <graphics/ImageBuffer.h>
#include <graphics/format/QOI.h>
#include <system/CFFI.h>
#include <system/System.h>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdlib.h>
#include <string.h>


namespace lime {


	enum AsyncTaskKind {

		ASYNC_READ = 0,
		ASYNC_WRITE = 1

	};


	struct AsyncTask {

		int id;
		int kind;
		int stage;
		bool ok;
		SDL_AsyncIO* io;
		unsigned char* data;
		size_t size;
		unsigned char* pixels;
		int width;
		int height;
		std::string path;
		std::string tmp;

	};


	struct AsyncResult {

		int id;
		bool ok;
		size_t size;

	};


	static SDL_SpinLock asyncInitLock = 0;
	static bool asyncReady = false;
	static bool asyncTried = false;
	static SDL_AsyncIOQueue* asyncQueue = NULL;
	static SDL_Mutex* asyncMutex = NULL;
	static SDL_Condition* encodeSignal = NULL;
	static SDL_Condition* doneSignal = NULL;
	static SDL_Thread* encodeThread = NULL;
	static SDL_Thread* completeThread = NULL;
	static std::deque<AsyncTask*> encodeJobs;
	static std::vector<AsyncResult> doneResults;
	static std::unordered_map<int, std::pair<void*, size_t> > readBuffers;
	static SDL_AtomicInt asyncPending;


	static int SDLCALL encodeWorker (void* unused);
	static int SDLCALL completeWorker (void* unused);


	static bool asyncInit () {

		if (asyncReady) return true;

		SDL_LockSpinlock (&asyncInitLock);

		if (!asyncTried) {

			asyncTried = true;
			asyncMutex = SDL_CreateMutex ();
			encodeSignal = SDL_CreateCondition ();
			doneSignal = SDL_CreateCondition ();
			asyncQueue = SDL_CreateAsyncIOQueue ();

			if (asyncQueue && asyncMutex && encodeSignal && doneSignal) {

				completeThread = SDL_CreateThread (completeWorker, "lime-asyncio", NULL);
				encodeThread = SDL_CreateThread (encodeWorker, "lime-qoi-encode", NULL);

			}

			if (!completeThread || !encodeThread) {

				SDL_Log ("[AsyncIO] init failed: %s", SDL_GetError ());

			} else {

				SDL_DetachThread (completeThread);
				SDL_DetachThread (encodeThread);
				SDL_MemoryBarrierRelease ();
				asyncReady = true;

			}

		}

		SDL_UnlockSpinlock (&asyncInitLock);
		return asyncReady;

	}


	static std::string stringOf (HxString value) {

		const char* utf8 = hxs_utf8 (value, nullptr);
		return utf8 ? utf8 : "";

	}


	static unsigned char* bytesOf (value bytes, int* length) {

		static int id_b = val_id ("b");
		static int id_length = val_id ("length");

		*length = 0;
		if (val_is_null (bytes)) return NULL;

		*length = val_int (val_field (bytes, id_length));
		if (*length <= 0) return NULL;

		value b = val_field (bytes, id_b);
		return val_is_string (b) ? (unsigned char*)val_string (b) : (unsigned char*)buffer_data (val_to_buffer (b));

	}


	static void finishTask (AsyncTask* task, void* readBuffer) {

		AsyncResult result;
		result.id = task->id;
		result.ok = task->ok;
		result.size = task->size;

		SDL_LockMutex (asyncMutex);
		if (readBuffer) readBuffers[task->id] = std::make_pair (readBuffer, task->size);
		doneResults.push_back (result);
		SDL_SignalCondition (doneSignal);
		SDL_UnlockMutex (asyncMutex);

		if (task->data) free (task->data);
		if (task->pixels) free (task->pixels);
		delete task;

	}


	static void failTask (AsyncTask* task) {

		task->ok = false;
		task->size = 0;
		finishTask (task, NULL);

	}


	static void submitWrite (AsyncTask* task) {

		task->stage = 0;
		task->io = SDL_AsyncIOFromFile (task->tmp.c_str (), "w");

		if (!task->io) {

			SDL_Log ("[AsyncIO] open %s failed: %s", task->tmp.c_str (), SDL_GetError ());
			failTask (task);
			return;

		}

		if (!SDL_WriteAsyncIO (task->io, task->data, 0, task->size, asyncQueue, task)) {

			SDL_Log ("[AsyncIO] write %s failed: %s", task->tmp.c_str (), SDL_GetError ());
			task->ok = false;
			task->stage = 1;

			if (!SDL_CloseAsyncIO (task->io, false, asyncQueue, task)) {

				SDL_RemovePath (task->tmp.c_str ());
				failTask (task);

			}

		}

	}


	static void handleOutcome (SDL_AsyncIOOutcome& outcome) {

		AsyncTask* task = (AsyncTask*)outcome.userdata;
		if (!task) return;

		bool complete = outcome.result == SDL_ASYNCIO_COMPLETE;

		if (task->kind == ASYNC_READ) {

			if (complete && outcome.buffer) {

				task->size = (size_t)outcome.bytes_transferred;
				finishTask (task, outcome.buffer);

			} else {

				if (outcome.buffer) SDL_free (outcome.buffer);
				SDL_Log ("[AsyncIO] read %s failed: %s", task->path.c_str (), SDL_GetError ());
				failTask (task);

			}

			return;

		}

		if (task->stage == 0) {

			task->ok = complete && outcome.bytes_transferred == task->size;
			free (task->data);
			task->data = NULL;
			task->stage = 1;

			if (!SDL_CloseAsyncIO (task->io, false, asyncQueue, task)) {

				SDL_RemovePath (task->tmp.c_str ());
				failTask (task);

			}

			return;

		}

		if (task->ok && complete && SDL_RenamePath (task->tmp.c_str (), task->path.c_str ())) {

			finishTask (task, NULL);
			return;

		}

		SDL_Log ("[AsyncIO] write %s failed: %s", task->path.c_str (), SDL_GetError ());
		SDL_RemovePath (task->tmp.c_str ());
		failTask (task);

	}


	static int SDLCALL completeWorker (void* unused) {

		SDL_AsyncIOOutcome outcome;

		while (true) {

			if (SDL_WaitAsyncIOResult (asyncQueue, &outcome, -1)) {

				handleOutcome (outcome);

			}

		}

		return 0;

	}


	static int SDLCALL encodeWorker (void* unused) {

		while (true) {

			SDL_LockMutex (asyncMutex);

			while (encodeJobs.empty ()) {

				SDL_WaitCondition (encodeSignal, asyncMutex);

			}

			AsyncTask* task = encodeJobs.front ();
			encodeJobs.pop_front ();
			SDL_UnlockMutex (asyncMutex);

			int length = 0;
			task->data = QOI::EncodeRaw (task->pixels, task->width, task->height, &length);
			free (task->pixels);
			task->pixels = NULL;

			if (!task->data || length <= 0) {

				SDL_Log ("[AsyncIO] qoi encode %dx%d failed for %s", task->width, task->height, task->path.c_str ());
				failTask (task);
				continue;

			}

			task->size = (size_t)length;
			submitWrite (task);

		}

		return 0;

	}


	static AsyncTask* createTask (int id, int kind, const std::string& path) {

		AsyncTask* task = new AsyncTask ();
		task->id = id;
		task->kind = kind;
		task->stage = 0;
		task->ok = true;
		task->io = NULL;
		task->data = NULL;
		task->size = 0;
		task->pixels = NULL;
		task->width = 0;
		task->height = 0;
		task->path = path;
		task->tmp = path + ".tmp";
		SDL_AddAtomicInt (&asyncPending, 1);
		return task;

	}


	bool lime_async_io_read_file (HxString path, int id) {

		if (!asyncInit ()) return false;

		std::string file = stringOf (path);
		AsyncTask* task = createTask (id, ASYNC_READ, file);

		if (!SDL_LoadFileAsync (file.c_str (), asyncQueue, task)) {

			SDL_Log ("[AsyncIO] read %s failed: %s", file.c_str (), SDL_GetError ());
			SDL_AddAtomicInt (&asyncPending, -1);
			delete task;
			return false;

		}

		return true;

	}


	bool lime_async_io_write_file (HxString path, value bytes, int id) {

		if (!asyncInit ()) return false;

		int length = 0;
		unsigned char* source = bytesOf (bytes, &length);
		if (!source) return false;

		unsigned char* copy = (unsigned char*)malloc ((size_t)length);
		if (!copy) return false;
		memcpy (copy, source, (size_t)length);

		AsyncTask* task = createTask (id, ASYNC_WRITE, stringOf (path));
		task->data = copy;
		task->size = (size_t)length;
		submitWrite (task);
		return true;

	}


	bool lime_async_io_write_qoi (HxString path, value buffer, int id) {

		if (!asyncInit ()) return false;

		ImageBuffer imageBuffer = ImageBuffer (buffer);

		if (!imageBuffer.data || !imageBuffer.data->buffer || !imageBuffer.data->buffer->b || imageBuffer.bitsPerPixel != 32 || imageBuffer.width <= 0 || imageBuffer.height <= 0) return false;

		size_t size = (size_t)imageBuffer.width * imageBuffer.height * 4;
		if ((size_t)imageBuffer.data->buffer->length < size) return false;

		unsigned char* pixels = (unsigned char*)malloc (size);
		if (!pixels) return false;
		memcpy (pixels, imageBuffer.data->buffer->b, size);

		AsyncTask* task = createTask (id, ASYNC_WRITE, stringOf (path));
		task->pixels = pixels;
		task->width = imageBuffer.width;
		task->height = imageBuffer.height;

		SDL_LockMutex (asyncMutex);
		encodeJobs.push_back (task);
		SDL_SignalCondition (encodeSignal);
		SDL_UnlockMutex (asyncMutex);
		return true;

	}


	value lime_async_io_poll (int waitMS) {

		value array = alloc_array (0);
		if (!asyncReady) return array;

		std::vector<AsyncResult> results;

		SDL_LockMutex (asyncMutex);

		if (doneResults.empty () && waitMS > 0 && SDL_GetAtomicInt (&asyncPending) > 0) {

			System::GCEnterBlocking ();
			SDL_WaitConditionTimeout (doneSignal, asyncMutex, waitMS);
			System::GCExitBlocking ();

		}

		results.swap (doneResults);
		SDL_UnlockMutex (asyncMutex);

		static int id_id = val_id ("id");
		static int id_ok = val_id ("ok");
		static int id_size = val_id ("size");

		for (size_t i = 0; i < results.size (); i++) {

			SDL_AddAtomicInt (&asyncPending, -1);

			value item = alloc_empty_object ();
			alloc_field (item, id_id, alloc_int (results[i].id));
			alloc_field (item, id_ok, alloc_bool (results[i].ok));
			alloc_field (item, id_size, alloc_int ((int)results[i].size));
			val_array_push (array, item);

		}

		return array;

	}


	bool lime_async_io_take (int id, value bytes) {

		if (!asyncReady) return false;

		SDL_LockMutex (asyncMutex);
		std::unordered_map<int, std::pair<void*, size_t> >::iterator it = readBuffers.find (id);

		if (it == readBuffers.end ()) {

			SDL_UnlockMutex (asyncMutex);
			return false;

		}

		void* data = it->second.first;
		size_t size = it->second.second;
		readBuffers.erase (it);
		SDL_UnlockMutex (asyncMutex);

		bool ok = false;

		if (!val_is_null (bytes)) {

			int length = 0;
			unsigned char* target = bytesOf (bytes, &length);

			if (size == 0) {

				ok = true;

			} else if (target && (size_t)length >= size) {

				memcpy (target, data, size);
				ok = true;

			}

		}

		SDL_free (data);
		return ok;

	}


	int lime_async_io_pending () {

		return SDL_GetAtomicInt (&asyncPending);

	}


	struct ContentHash {

		Uint64 h1;
		Uint64 h2;
		unsigned char tail[8];
		int tailLength;

		ContentHash (Uint64 total) : h1 (0x9E3779B97F4A7C15ULL ^ total), h2 (0xC2B2AE3D27D4EB4FULL + total), tailLength (0) {}

		void Word (Uint64 w) {

			h1 = (h1 ^ w) * 0x100000001B3ULL;
			h1 ^= h1 >> 29;
			h2 = (h2 + w) * 0xFF51AFD7ED558CCDULL;
			h2 ^= h2 >> 31;

		}

		void Update (const unsigned char* data, size_t length) {

			while (length > 0 && tailLength > 0) {

				tail[tailLength++] = *data++;
				length--;

				if (tailLength == 8) {

					Uint64 w;
					memcpy (&w, tail, 8);
					Word (w);
					tailLength = 0;

				}

			}

			while (length >= 8) {

				Uint64 w;
				memcpy (&w, data, 8);
				Word (w);
				data += 8;
				length -= 8;

			}

			while (length > 0) {

				tail[tailLength++] = *data++;
				length--;

			}

		}

		value Finish () {

			for (int i = 0; i < tailLength; i++) {

				h1 = (h1 ^ tail[i]) * 0x100000001B3ULL;
				h2 = (h2 + tail[i]) * 0xFF51AFD7ED558CCDULL;

			}

			h1 ^= h2 >> 17;
			h2 ^= h1 >> 23;
			h1 *= 0xBF58476D1CE4E5B9ULL;
			h2 *= 0x94D049BB133111EBULL;
			h1 ^= h1 >> 31;
			h2 ^= h2 >> 29;

			char hex[33];
			SDL_snprintf (hex, sizeof (hex), "%016llx%016llx", (unsigned long long)h1, (unsigned long long)h2);
			return alloc_string (hex);

		}

	};


	value lime_bytes_hash (value bytes, int offset, int length) {

		int total = 0;
		const unsigned char* data = bytesOf (bytes, &total);

		if (offset < 0) offset = 0;
		if (offset > total) offset = total;
		if (length < 0 || length > total - offset) length = total - offset;
		if (data) data += offset;

		ContentHash hash ((Uint64)length);
		if (data && length > 0) hash.Update (data, (size_t)length);
		return hash.Finish ();

	}


	value lime_file_hash (HxString path, int minBytes) {

		std::string file = stringOf (path);
		FILE_HANDLE* handle = lime::fopen (file.c_str (), "rb");
		if (!handle) return alloc_null ();

		lime::fseek (handle, 0, SEEK_END);
		long size = lime::ftell (handle);
		lime::fseek (handle, 0, SEEK_SET);

		if (size <= 0 || size < minBytes) {

			lime::fclose (handle);
			return alloc_null ();

		}

		const size_t chunk = 1 << 20;
		unsigned char* buffer = (unsigned char*)malloc (chunk);

		if (!buffer) {

			lime::fclose (handle);
			return alloc_null ();

		}

		ContentHash hash ((Uint64)size);
		size_t remaining = (size_t)size;

		System::GCEnterBlocking ();

		while (remaining > 0) {

			size_t want = remaining < chunk ? remaining : chunk;
			size_t got = lime::fread (buffer, 1, want, handle);
			if (got == 0) break;
			hash.Update (buffer, got);
			remaining -= got;

		}

		System::GCExitBlocking ();

		free (buffer);
		lime::fclose (handle);

		if (remaining > 0) return alloc_null ();
		return hash.Finish ();

	}


	DEFINE_PRIME2 (lime_async_io_read_file);
	DEFINE_PRIME3 (lime_async_io_write_file);
	DEFINE_PRIME3 (lime_async_io_write_qoi);
	DEFINE_PRIME1 (lime_async_io_poll);
	DEFINE_PRIME2 (lime_async_io_take);
	DEFINE_PRIME0 (lime_async_io_pending);
	DEFINE_PRIME3 (lime_bytes_hash);
	DEFINE_PRIME2 (lime_file_hash);


}


extern "C" int lime_async_io_register_prims () {

	return 0;

}
