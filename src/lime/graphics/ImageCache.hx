package lime.graphics;

#if (sys && lime_cffi && !macro && !hl && !cs)
import haxe.crypto.Md5;
import haxe.io.Bytes;
import lime._internal.backend.native.NativeCFFI;
import lime.system.AsyncIO;
import lime.system.DiskCache;
import lime.utils.UInt8Array;
import sys.FileSystem;
#if target.threaded
import sys.thread.Mutex;
#end

@:access(lime._internal.backend.native.NativeCFFI)
@:allow(lime.graphics.Image)
class ImageCache
{
	public static var enabled:Bool = true;
	public static var minBytes:Int = 32 * 1024;
	public static var cacheJPEG:Bool = false;
	public static var hits(default, null):Int = 0;
	public static var misses(default, null):Int = 0;

	static inline var VERSION:Int = 1;
	static inline var KIND:String = "images";
	static var storing:Map<String, Bool> = new Map();
	#if target.threaded
	static var lock:Mutex = new Mutex();
	#end

	public static function keyOfFile(path:String):Null<String>
	{
		if (!active() || path == null || path.indexOf("://") > -1 || !eligiblePath(path)) return null;
		try
		{
			final stat = FileSystem.stat(path);
			if (stat.size < minBytes) return null;
			return Md5.encode(VERSION + "|" + FileSystem.fullPath(path) + "|" + stat.size + "|" + stat.mtime.getTime()) + ".qoi";
		}
		catch (_:Dynamic)
		{
			return null;
		}
	}

	public static function keyOfBytes(bytes:Bytes):Null<String>
	{
		if (!active() || bytes == null || bytes.length < minBytes || !eligibleBytes(bytes)) return null;
		final hash:String = NativeCFFI.lime_bytes_hash(bytes, 0, bytes.length);
		return hash == null ? null : VERSION + "-" + hash + ".qoi";
	}

	public static function pathOf(key:String):Null<String>
	{
		if (key == null) return null;
		final path = DiskCache.pathFor(KIND, key);
		return path != null && FileSystem.exists(path) ? path : null;
	}

	public static function load(key:String):Null<ImageBuffer>
	{
		if (key == null) return null;
		final path = pathOf(key);
		if (path == null)
		{
			misses++;
			return null;
		}
		final buffer:ImageBuffer = NativeCFFI.lime_image_load_file(path, new ImageBuffer(new UInt8Array(Bytes.alloc(0))));
		if (buffer == null)
		{
			DiskCache.remove(KIND, key);
			misses++;
			return null;
		}
		hits++;
		return buffer;
	}

	public static function store(key:String, buffer:ImageBuffer):Void
	{
		if (key == null || buffer == null || buffer.bitsPerPixel != 32 || buffer.format != RGBA32 || buffer.premultiplied) return;
		#if target.threaded lock.acquire(); #end
		final busy = storing.exists(key);
		if (!busy) storing.set(key, true);
		#if target.threaded lock.release(); #end
		if (busy) return;
		final path = DiskCache.reserve(KIND, key);
		if (path == null)
		{
			release(key);
			return;
		}
		AsyncIO.writeQOI(path, buffer).onComplete(function(size)
		{
			release(key);
			DiskCache.commit(size);
		}).onError(function(_) release(key));
	}

	public static function remove(key:String):Void
	{
		if (key != null) DiskCache.remove(KIND, key);
	}

	public static function clear():Void
	{
		DiskCache.clear(KIND);
	}

	static function release(key:String):Void
	{
		#if target.threaded lock.acquire(); #end
		storing.remove(key);
		#if target.threaded lock.release(); #end
	}

	static inline function active():Bool
	{
		return enabled && DiskCache.enabled && AsyncIO.enabled && lime.system.CFFI.enabled;
	}

	static function eligiblePath(path:String):Bool
	{
		final dot = path.lastIndexOf(".");
		if (dot < 0) return false;
		return switch (path.substr(dot + 1).toLowerCase())
		{
			case "png": true;
			case "jpg", "jpeg": cacheJPEG;
			default: false;
		}
	}

	static function eligibleBytes(bytes:Bytes):Bool
	{
		if (bytes.length < 8) return false;
		if (bytes.get(0) == 0x89 && bytes.get(1) == 0x50 && bytes.get(2) == 0x4E && bytes.get(3) == 0x47) return true;
		return cacheJPEG && bytes.get(0) == 0xFF && bytes.get(1) == 0xD8;
	}
}
#end
