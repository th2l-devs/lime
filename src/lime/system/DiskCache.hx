package lime.system;

#if sys
import haxe.io.Bytes;
import sys.FileSystem;
import sys.io.File;

class DiskCache
{
	public static var enabled:Bool = true;
	public static var limit:Float = 256 * 1024 * 1024;

	static var root:String = null;
	static var total:Float = -1;

	public static function get(kind:String, key:String):Null<Bytes>
	{
		if (!enabled) return null;
		final path = pathOf(kind, key);
		if (path == null) return null;
		try
		{
			return FileSystem.exists(path) ? File.getBytes(path) : null;
		}
		catch (_:Dynamic)
		{
			return null;
		}
	}

	public static function put(kind:String, key:String, bytes:Bytes):Bool
	{
		if (!enabled || bytes == null) return false;
		final path = pathOf(kind, key);
		if (path == null) return false;
		try
		{
			if (total < 0) total = size();
			if (FileSystem.exists(path))
			{
				total -= FileSystem.stat(path).size;
				FileSystem.deleteFile(path);
			}
			File.saveBytes(path + ".tmp", bytes);
			FileSystem.rename(path + ".tmp", path);
			total += bytes.length;
			if (total > limit) trim();
			return true;
		}
		catch (_:Dynamic)
		{
			return false;
		}
	}

	public static function remove(kind:String, key:String):Void
	{
		final path = pathOf(kind, key);
		if (path == null) return;
		try
		{
			if (FileSystem.exists(path))
			{
				if (total >= 0) total -= FileSystem.stat(path).size;
				FileSystem.deleteFile(path);
			}
		}
		catch (_:Dynamic) {}
	}

	public static function clear(?kind:String):Void
	{
		if (!ensureRoot()) return;
		for (name in kinds())
		{
			if (kind != null && kind != name) continue;
			deleteFolder(root + name + "/");
		}
		total = -1;
	}

	public static function size(?kind:String):Float
	{
		if (!ensureRoot()) return 0;
		var sum = 0.0;
		for (name in kinds())
		{
			if (kind != null && kind != name) continue;
			final dir = root + name + "/";
			try
			{
				for (file in FileSystem.readDirectory(dir))
					sum += FileSystem.stat(dir + file).size;
			}
			catch (_:Dynamic) {}
		}
		return sum;
	}

	public static function removeLegacy(folder:String):Void
	{
		try
		{
			deleteFolder(System.applicationStorageDirectory + folder + "/");
		}
		catch (_:Dynamic) {}
	}

	static function deleteFolder(dir:String):Void
	{
		if (!FileSystem.exists(dir) || !FileSystem.isDirectory(dir)) return;
		for (file in FileSystem.readDirectory(dir))
			FileSystem.deleteFile(dir + file);
		FileSystem.deleteDirectory(dir);
	}

	static function kinds():Array<String>
	{
		try
		{
			return [for (name in FileSystem.readDirectory(root)) if (FileSystem.isDirectory(root + name)) name];
		}
		catch (_:Dynamic)
		{
			return [];
		}
	}

	static function trim():Void
	{
		final files:Array<{path:String, size:Float, time:Float}> = [];
		for (name in kinds())
		{
			final dir = root + name + "/";
			for (file in FileSystem.readDirectory(dir))
			{
				final stat = FileSystem.stat(dir + file);
				files.push({path: dir + file, size: stat.size, time: stat.mtime.getTime()});
			}
		}
		files.sort((a, b) -> a.time < b.time ? -1 : (a.time > b.time ? 1 : 0));
		final target = limit * 0.8;
		for (file in files)
		{
			if (total <= target) break;
			try
			{
				FileSystem.deleteFile(file.path);
				total -= file.size;
			}
			catch (_:Dynamic) {}
		}
	}

	static function ensureRoot():Bool
	{
		if (root != null) return true;
		try
		{
			final dir = System.applicationStorageDirectory + "cache/";
			if (!FileSystem.exists(dir)) FileSystem.createDirectory(dir);
			root = dir;
			return true;
		}
		catch (_:Dynamic)
		{
			return false;
		}
	}

	static function pathOf(kind:String, key:String):Null<String>
	{
		if (!ensureRoot()) return null;
		final dir = root + kind + "/";
		try
		{
			if (!FileSystem.exists(dir)) FileSystem.createDirectory(dir);
		}
		catch (_:Dynamic)
		{
			return null;
		}
		return dir + key;
	}
}
#end
