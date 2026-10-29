package lime.system;

#if macro
import haxe.macro.Context;

class LogConsoleIcons
{
	public static function embed(?dir:String = "art"):Void
	{
		for (size in [16, 32, 64])
		{
			final path = haxe.io.Path.join([dir, "shell-" + size + ".png"]);
			if (sys.FileSystem.exists(path)) Context.addResource("lime-shell-" + size, sys.io.File.getBytes(path));
		}
	}
}
#end
