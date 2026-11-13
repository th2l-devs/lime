package lime._internal.format;

import haxe.io.Bytes;
import lime._internal.backend.native.NativeCFFI;
import lime.graphics.Image;
import lime.graphics.ImageBuffer;
import lime.system.CFFI;
import lime.utils.UInt8Array;

#if !lime_debug
@:fileXml('tags="haxe,release"')
@:noDebug
#end
@:access(lime._internal.backend.native.NativeCFFI)
@:access(lime.graphics.ImageBuffer)
class QOI
{
	public static function decodeBytes(bytes:Bytes):Image
	{
		#if (lime_cffi && !macro && !cs)
		if (bytes == null || !isQOI(bytes)) return null;
		var buffer:ImageBuffer = NativeCFFI.lime_image_load_bytes(bytes, new ImageBuffer(new UInt8Array(Bytes.alloc(0))));
		if (buffer != null) return new Image(buffer);
		#end
		return null;
	}

	public static function decodeFile(path:String):Image
	{
		#if (lime_cffi && !macro && !cs)
		if (path == null) return null;
		var buffer:ImageBuffer = NativeCFFI.lime_image_load_file(path, new ImageBuffer(new UInt8Array(Bytes.alloc(0))));
		if (buffer != null) return new Image(buffer);
		#end
		return null;
	}

	public static function encode(image:Image):Bytes
	{
		#if (sys && lime_cffi && !macro && !cs)
		if (image == null || image.buffer == null) return null;
		if (image.premultiplied || image.format != RGBA32)
		{
			image = image.clone();
			image.premultiplied = false;
			image.format = RGBA32;
		}
		if (CFFI.enabled) return NativeCFFI.lime_image_encode(image.buffer, 2, 0, Bytes.alloc(0));
		#end
		return null;
	}

	public static function isQOI(bytes:Bytes):Bool
	{
		return bytes != null && bytes.length >= 22 && bytes.get(0) == "q".code && bytes.get(1) == "o".code && bytes.get(2) == "i".code
			&& bytes.get(3) == "f".code;
	}
}
