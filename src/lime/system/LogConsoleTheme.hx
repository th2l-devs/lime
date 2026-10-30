package lime.system;

class LogConsoleTheme
{
	public var background:String = "#000000";
	public var text:String = "#E5E7EB";
	public var dim:String = "#6B7280";
	public var faint:String = "#3F3F46";
	public var accent:String = "#8B5CF6";
	public var accentText:String = "#FFFFFF";
	public var accentSoft:String = "#C4B5FD";
	public var error:String = "#F87171";
	public var errorText:String = "#000000";
	public var errorBg:String = "#EF4444";
	public var warn:String = "#FBBF24";
	public var warnText:String = "#000000";
	public var warnBg:String = "#F59E0B";
	public var info:String = "#60A5FA";
	public var infoText:String = "#FFFFFF";
	public var infoBg:String = "#2563EB";
	public var logText:String = "#D4D4D8";
	public var logBg:String = "#3F3F46";
	public var logMessage:String = "#A1A1AA";
	public var renderText:String = "#FFFFFF";
	public var renderBg:String = "#0F766E";
	public var garbageText:String = "#FFFFFF";
	public var garbageBg:String = "#A16207";
	public var chip:String = "#DDD6FE";
	public var chipBg:String = "#2E1065";
	public var labelError:String = "FAILED";
	public var labelWarn:String = "WARNING";
	public var labelInfo:String = "INFO";
	public var tags:Array<String> = ["#A78BFA", "#34D399", "#F472B6", "#38BDF8", "#FB923C", "#FACC15", "#2DD4BF", "#F87171"];
	public var labelLog:String = "TRACE";
	public var labelRender:String = "RENDER";
	public var labelGarbage:String = "GARBAGE";
	public var labelSession:String = "SESSION";
	public var font:String = "Consolas";
	public var fontSize:Int = 16;
	public var columns:Int = 150;
	public var rows:Int = 42;
	public var history:Int = 5000;
	public var view:String = "normal";
	public var pinHeader:Bool = true;
	public var showSession:Bool = true;

	public function new() {}

	public function toPowerShell():String
	{
		final parts:Array<String> = [];
		for (field in Reflect.fields(this))
		{
			final value:Dynamic = Reflect.field(this, field);
			if (Reflect.isFunction(value)) continue;
			if ((value is Array))
			{
				final list:Array<String> = [for (item in (value : Array<Dynamic>)) "'" + toRgb(Std.string(item)) + "'"];
				parts.push(field + " = @(" + (list.length > 0 ? list.join(", ") : "'255;255;255'") + ")");
			}
			else if ((value is Bool)) parts.push(field + " = $" + (value ? "true" : "false"));
			else if ((value is Int)) parts.push(field + " = " + value);
			else
			{
				final raw = Std.string(value);
				final rgb = StringTools.startsWith(field, "label") || field == "font" || field == "view" ? null : toRgb(raw);
				parts.push(field + " = '" + StringTools.replace(rgb != null ? rgb : raw, "'", "''") + "'");
			}
		}
		return "$T = @{ " + parts.join("; ") + " }";
	}

	static function toRgb(value:String):String
	{
		var v = StringTools.trim(value);
		if (v.indexOf(",") >= 0)
		{
			final p = v.split(",");
			if (p.length >= 3) return Std.parseInt(StringTools.trim(p[0])) + ";" + Std.parseInt(StringTools.trim(p[1])) + ";" + Std.parseInt(StringTools.trim(p[2]));
		}
		if (StringTools.startsWith(v, "#")) v = v.substr(1);
		if (StringTools.startsWith(v, "0x")) v = v.substr(2);
		if (v.length == 8) v = v.substr(2);
		if (v.length == 3) v = v.charAt(0) + v.charAt(0) + v.charAt(1) + v.charAt(1) + v.charAt(2) + v.charAt(2);
		final n = Std.parseInt("0x" + v);
		if (v.length != 6 || n == null) return "255;255;255";
		return ((n >> 16) & 0xFF) + ";" + ((n >> 8) & 0xFF) + ";" + (n & 0xFF);
	}
}
