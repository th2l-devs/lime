package lime.system;

#if (sys && lime_cffi && !macro && !hl && !cs)
import haxe.io.Bytes;
import lime._internal.backend.native.NativeCFFI;
import lime.app.Application;
import lime.app.Future;
import lime.app.Promise;
import lime.graphics.ImageBuffer;
#if target.threaded
import sys.thread.Mutex;
#end

@:access(lime._internal.backend.native.NativeCFFI)
class AsyncIO
{
	public static var enabled:Bool = true;
	public static var pending(get, never):Int;

	static var nextId:Int = 1;
	static var reads:Map<Int, Promise<Bytes>> = new Map();
	static var writes:Map<Int, Promise<Int>> = new Map();
	static var listening:Bool = false;
	#if target.threaded
	static var lock:Mutex = new Mutex();
	#end

	public static function readFile(path:String):Future<Bytes>
	{
		if (path == null) return cast Future.withError("[AsyncIO] null path");
		final promise = new Promise<Bytes>();
		final id = reserve();
		#if target.threaded lock.acquire(); #end
		reads.set(id, promise);
		#if target.threaded lock.release(); #end
		if (!NativeCFFI.lime_async_io_read_file(path, id))
		{
			drop(id);
			return cast Future.withError("[AsyncIO] could not read \"" + path + "\"");
		}
		listen();
		return promise.future;
	}

	public static function writeFile(path:String, bytes:Bytes):Future<Int>
	{
		if (path == null || bytes == null) return cast Future.withError("[AsyncIO] null argument");
		final promise = new Promise<Int>();
		final id = reserve();
		#if target.threaded lock.acquire(); #end
		writes.set(id, promise);
		#if target.threaded lock.release(); #end
		if (!NativeCFFI.lime_async_io_write_file(path, bytes, id))
		{
			drop(id);
			return cast Future.withError("[AsyncIO] could not write \"" + path + "\"");
		}
		listen();
		return promise.future;
	}

	public static function writeQOI(path:String, buffer:ImageBuffer):Future<Int>
	{
		if (path == null || buffer == null) return cast Future.withError("[AsyncIO] null argument");
		final promise = new Promise<Int>();
		final id = reserve();
		#if target.threaded lock.acquire(); #end
		writes.set(id, promise);
		#if target.threaded lock.release(); #end
		if (!NativeCFFI.lime_async_io_write_qoi(path, buffer, id))
		{
			drop(id);
			return cast Future.withError("[AsyncIO] could not encode \"" + path + "\"");
		}
		listen();
		return promise.future;
	}

	public static function update(waitMS:Int = 0):Void
	{
		listen();
		final results:Array<Dynamic> = NativeCFFI.lime_async_io_poll(waitMS);
		if (results == null) return;
		for (result in results)
		{
			final id:Int = result.id;
			final ok:Bool = result.ok;
			final size:Int = result.size;
			#if target.threaded lock.acquire(); #end
			final read = reads.get(id);
			final write = writes.get(id);
			reads.remove(id);
			writes.remove(id);
			#if target.threaded lock.release(); #end
			if (read != null)
			{
				if (!ok)
				{
					read.error("[AsyncIO] read failed");
					continue;
				}
				final bytes = Bytes.alloc(size);
				if (NativeCFFI.lime_async_io_take(id, bytes)) read.complete(bytes);
				else read.error("[AsyncIO] read buffer lost");
			}
			else if (write != null)
			{
				if (ok) write.complete(size);
				else write.error("[AsyncIO] write failed");
			}
			else if (ok && size >= 0)
			{
				NativeCFFI.lime_async_io_take(id, null);
			}
		}
	}

	public static function flush(timeoutMS:Int = 5000):Void
	{
		final end = haxe.Timer.stamp() + timeoutMS / 1000;
		while (count() > 0 && haxe.Timer.stamp() < end)
			update(16);
	}

	static function reserve():Int
	{
		#if target.threaded lock.acquire(); #end
		final id = nextId++;
		#if target.threaded lock.release(); #end
		return id;
	}

	static function drop(id:Int):Void
	{
		#if target.threaded lock.acquire(); #end
		reads.remove(id);
		writes.remove(id);
		#if target.threaded lock.release(); #end
	}

	static function count():Int
	{
		var n = 0;
		#if target.threaded lock.acquire(); #end
		for (_ in reads) n++;
		for (_ in writes) n++;
		#if target.threaded lock.release(); #end
		return n;
	}

	static function listen():Void
	{
		if (listening || Application.current == null) return;
		if (!isMainThread()) return;
		listening = true;
		Application.current.onUpdate.add(onUpdate);
	}

	static function onUpdate(_:Float):Void
	{
		if (count() > 0) update(0);
	}

	static inline function isMainThread():Bool
	{
		#if target.threaded
		return sys.thread.Thread.current() == mainThread;
		#else
		return true;
		#end
	}

	#if target.threaded
	static var mainThread:sys.thread.Thread = sys.thread.Thread.current();
	#end

	static function get_pending():Int
	{
		return NativeCFFI.lime_async_io_pending();
	}
}
#end
