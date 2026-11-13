#include <graphics/format/QOI.h>
#include <system/System.h>
#include <stdlib.h>
#include <string.h>


namespace lime {

	static thread_local ImageBuffer* qoiTarget = NULL;
	static thread_local size_t qoiTargetSize = 0;

	static void* qoiAlloc (size_t size) {

		if (qoiTarget && size == qoiTargetSize) {

			ImageBuffer* target = qoiTarget;
			qoiTarget = NULL;
			return target->data->buffer->b;

		}

		return malloc (size);

	}

	static void qoiFree (void* p) {

		free (p);

	}

}


#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#define QOI_MALLOC(sz) lime::qoiAlloc (sz)
#define QOI_FREE(p) lime::qoiFree (p)
#include "../../../lib/qoi/qoi.h"


namespace lime {


	static unsigned int readBE32 (const unsigned char* p) {

		return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];

	}


	bool QOI::IsQOI (const unsigned char* data, size_t length) {

		return data && length >= QOI_HEADER_SIZE + sizeof (qoi_padding) && data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f';

	}


	static bool decodeMemory (const unsigned char* data, size_t length, ImageBuffer* imageBuffer, bool decodeData) {

		if (!QOI::IsQOI (data, length) || length > 0x7fffffff) return false;

		unsigned int width = readBE32 (data + 4);
		unsigned int height = readBE32 (data + 8);

		if (width == 0 || height == 0 || height >= QOI_PIXELS_MAX / width) return false;

		if (!decodeData) {

			imageBuffer->width = width;
			imageBuffer->height = height;
			return true;

		}

		imageBuffer->Resize (width, height, 32);

		if (!imageBuffer->data || !imageBuffer->data->buffer || !imageBuffer->data->buffer->b) return false;

		qoiTarget = imageBuffer;
		qoiTargetSize = (size_t)width * height * 4;

		qoi_desc desc;
		void* pixels = qoi_decode (data, (int)length, &desc, 4);

		bool inPlace = qoiTarget == NULL;
		qoiTarget = NULL;

		if (!pixels) return false;

		if (!inPlace) {

			memcpy (imageBuffer->data->buffer->b, pixels, qoiTargetSize);
			free (pixels);

		}

		imageBuffer->format = RGBA32;
		imageBuffer->premultiplied = false;
		imageBuffer->transparent = desc.channels == 4;
		return true;

	}


	bool QOI::Decode (Resource *resource, ImageBuffer *imageBuffer, bool decodeData) {

		if (!resource->path) {

			if (!resource->data) return false;
			return decodeMemory (resource->data->b, resource->data->length, imageBuffer, decodeData);

		}

		FILE_HANDLE* file = lime::fopen (resource->path, "rb");
		if (!file) return false;

		unsigned char header[QOI_HEADER_SIZE];

		if (lime::fread (header, 1, QOI_HEADER_SIZE, file) != QOI_HEADER_SIZE || header[0] != 'q' || header[1] != 'o' || header[2] != 'i' || header[3] != 'f') {

			lime::fclose (file);
			return false;

		}

		lime::fseek (file, 0, SEEK_END);
		long size = lime::ftell (file);

		if (size < (long)(QOI_HEADER_SIZE + sizeof (qoi_padding))) {

			lime::fclose (file);
			return false;

		}

		if (!decodeData) {

			lime::fclose (file);
			imageBuffer->width = readBE32 (header + 4);
			imageBuffer->height = readBE32 (header + 8);
			return imageBuffer->width > 0 && imageBuffer->height > 0;

		}

		unsigned char* data = (unsigned char*)malloc ((size_t)size);

		if (!data) {

			lime::fclose (file);
			return false;

		}

		lime::fseek (file, 0, SEEK_SET);
		size_t read = lime::fread (data, 1, (size_t)size, file);
		lime::fclose (file);

		bool result = read == (size_t)size && decodeMemory (data, (size_t)size, imageBuffer, true);
		free (data);
		return result;

	}


	unsigned char* QOI::EncodeRaw (const unsigned char* rgba, int width, int height, int* outLength) {

		if (!rgba || width <= 0 || height <= 0) return NULL;

		qoi_desc desc;
		desc.width = width;
		desc.height = height;
		desc.channels = 4;
		desc.colorspace = QOI_SRGB;

		qoiTarget = NULL;
		return (unsigned char*)qoi_encode (rgba, &desc, outLength);

	}


	void QOI::FreeRaw (void* data) {

		free (data);

	}


	bool QOI::Encode (ImageBuffer *imageBuffer, Bytes *bytes) {

		if (!imageBuffer || !imageBuffer->data || !imageBuffer->data->buffer || imageBuffer->bitsPerPixel != 32) return false;

		int length = 0;
		unsigned char* encoded = EncodeRaw (imageBuffer->data->buffer->b, imageBuffer->width, imageBuffer->height, &length);
		if (!encoded) return false;

		if (!bytes->TryResize (length)) {

			free (encoded);
			return false;

		}

		memcpy (bytes->b, encoded, length);
		free (encoded);
		return true;

	}


}
