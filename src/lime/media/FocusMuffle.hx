package lime.media;

#if (lime_cffi && lime_openal && !macro)
import haxe.Timer;
import lime.app.Application;
import lime.media.openal.AL;
import lime.media.openal.ALFilter;
import lime.media.openal.ALSource;

class FocusMuffle
{
	public static var enabled(default, set):Bool = false;
	public static var volume(default, set):Float = 0.35;
	public static var strength(default, set):Float = 0.85;
	public static var fadeTime:Float = 0.35;

	public static var amount(default, null):Float = 0;

	@:noCompletion public static var __busy:Bool = false;

	static var target:Float = 0;
	static var focused:Bool = true;
	static var hooked:Bool = false;
	static var filter:ALFilter = null;
	static var filterFailed:Bool = false;
	static var sources:Array<ALSource> = [];
	static var custom:Array<ALSource> = [];
	static var timer:Timer = null;
	static var lastStamp:Float = 0;

	static function set_enabled(value:Bool):Bool
	{
		enabled = value;
		hook();
		retarget();
		return value;
	}

	static function set_volume(value:Float):Float
	{
		volume = Math.max(0, Math.min(1, value));
		if (amount > 0) applyGain();
		return volume;
	}

	static function set_strength(value:Float):Float
	{
		strength = Math.max(0, Math.min(1, value));
		if (amount > 0) applyFilter();
		return strength;
	}

	static function hook():Void
	{
		if (hooked) return;
		var window = Application.current != null ? Application.current.window : null;
		if (window == null) return;
		hooked = true;
		window.onFocusOut.add(function()
		{
			focused = false;
			retarget();
		});
		window.onFocusIn.add(function()
		{
			focused = true;
			retarget();
		});
	}

	static function retarget():Void
	{
		target = enabled && !focused ? 1 : 0;
		if (target == amount) return;
		if (timer == null)
		{
			lastStamp = Timer.stamp();
			timer = new Timer(16);
			timer.run = step;
		}
	}

	static function step():Void
	{
		var now = Timer.stamp();
		var dt = now - lastStamp;
		lastStamp = now;
		var delta = fadeTime > 0 ? dt / fadeTime : 1;
		amount = amount < target ? Math.min(target, amount + delta) : Math.max(target, amount - delta);

		applyGain();
		applyFilter();

		if (amount == target && timer != null)
		{
			timer.stop();
			timer = null;
		}
	}

	static function applyGain():Void
	{
		AL.listenerf(AL.GAIN, 1 - (1 - volume) * amount);
	}

	static function ensureFilter():Bool
	{
		if (filter != null) return true;
		if (filterFailed) return false;
		filter = AL.createFilter();
		if (filter == null)
		{
			filterFailed = true;
			return false;
		}
		AL.filteri(filter, AL.FILTER_TYPE, AL.FILTER_LOWPASS);
		return true;
	}

	static function applyFilter():Void
	{
		if (amount <= 0)
		{
			__busy = true;
			for (source in sources)
				if (custom.indexOf(source) < 0) AL.removeDirectFilter(source);
			__busy = false;
			return;
		}
		if (!ensureFilter()) return;
		AL.filterf(filter, AL.LOWPASS_GAIN, 1);
		AL.filterf(filter, AL.LOWPASS_GAINHF, 1 - strength * amount);
		for (source in sources) attach(source);
	}

	static function attach(source:ALSource):Void
	{
		if (custom.indexOf(source) >= 0) return;
		__busy = true;
		AL.sourcei(source, AL.DIRECT_FILTER, filter);
		__busy = false;
	}

	@:noCompletion public static function __created(source:ALSource):Void
	{
		if (source == null) return;
		sources.push(source);
		if (amount > 0 && ensureFilter()) attach(source);
	}

	@:noCompletion public static function __deleted(source:ALSource):Void
	{
		sources.remove(source);
		custom.remove(source);
	}

	@:noCompletion public static function __directFilter(source:ALSource, value:Dynamic):Void
	{
		if (value == null || value == 0)
		{
			__removed(source);
			return;
		}
		if (custom.indexOf(source) < 0) custom.push(source);
	}

	@:noCompletion public static function __removed(source:ALSource):Void
	{
		custom.remove(source);
		if (amount > 0 && sources.indexOf(source) >= 0 && ensureFilter()) attach(source);
	}
}
#end
