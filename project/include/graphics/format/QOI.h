#ifndef LIME_GRAPHICS_FORMAT_QOI_H
#define LIME_GRAPHICS_FORMAT_QOI_H


#include <graphics/ImageBuffer.h>
#include <utils/Bytes.h>
#include <utils/Resource.h>
#include <stddef.h>


namespace lime {


	class QOI {


		public:

			static bool Decode (Resource *resource, ImageBuffer *imageBuffer, bool decodeData = true);
			static bool Encode (ImageBuffer *imageBuffer, Bytes *bytes);
			static unsigned char* EncodeRaw (const unsigned char* rgba, int width, int height, int* outLength);
			static void FreeRaw (void* data);
			static bool IsQOI (const unsigned char* data, size_t length);


	};


}


#endif
