package lime.media;

import lime.system.CFFI;
import lime.app.Application;
import haxe.Timer;
import lime._internal.backend.native.NativeCFFI;
import lime.media.openal.AL;
import lime.media.openal.ALC;
import lime.media.openal.ALContext;
import lime.media.openal.ALDevice;
#if (js && html5)
import js.Browser;
#end

#if !lime_debug
@:fileXml('tags="haxe,release"')
@:noDebug
#end
@:access(lime._internal.backend.native.NativeCFFI)
class AudioManager
{
	public static var context:AudioContext;

	public static function init(context:AudioContext = null)
	{
		if (AudioManager.context == null)
		{
			if (context == null)
			{
				AudioManager.context = new AudioContext();
				context = AudioManager.context;

				#if !lime_doc_gen
				if (context.type == OPENAL)
				{
					var alc = context.openal;

					var device = alc.openDevice();

					var ctx = device != null ? alc.createContext(device) : null;
					alc.makeContextCurrent(ctx);
					alc.processContext(ctx);

					var version:String = ctx != null ? alc.getString(AL.VERSION) : null;
					var alSoft:Bool = version != null && StringTools.contains(version, "ALSOFT");

					if (alSoft)
					{
						alc.disable(AL.STOP_SOURCES_ON_DISCONNECT_SOFT);

						Application.current.onUpdate.add((_) -> {
							AudioManager.update();
						});

						alc.eventControlSOFT(3, [
							ALC.EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT,
							ALC.EVENT_TYPE_DEVICE_ADDED_SOFT,
							ALC.EVENT_TYPE_DEVICE_REMOVED_SOFT
						], true);
						alc.eventCallbackSOFT(device, __deviceEventCallback);
					}
				}
				#end
			}

			AudioManager.context = context;

			#if (lime_cffi && !macro && lime_openal && (ios || tvos || mac))
			var timer = new Timer(100);
			timer.run = function()
			{
				NativeCFFI.lime_al_cleanup();
			};
			#end
		}
	}

	public static function update():Void
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			if (__audioDeviceChanged)
			{
				var alc = context.openal;
				var context = alc.getCurrentContext();
				if (context != null)
				{
					var device = alc.getContextsDevice(context);
					var reopened = alc.reopenDeviceSOFT(device, null, null);
					if (reopened)
					{
						__audioDeviceChanged = false;
					}
				}
			}
		}
		#end
	}

	/**
		The name of the currently active output device, or `null` if audio isn't initialized.
	**/
	public static function getCurrentDevice():String
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			var alc = context.openal;
			var currentContext = alc.getCurrentContext();

			if (currentContext != null)
			{
				var device = alc.getContextsDevice(currentContext);
				return alc.getString(ALC.DEVICE_SPECIFIER, device);
			}
		}
		#end
		return null;
	}

	/**
		Lists the available audio output devices, suitable for passing to `setDevice()`.
	**/
	public static function getDeviceList():Array<String>
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			return context.openal.getDeviceList();
		}
		#end
		return [];
	}

	/**
		Switches audio output to a different device without interrupting playback,
		using `ALC_SOFT_reopen_device` - the same mechanism `update()` already uses
		to recover from a disconnected device. Requires OpenAL-Soft; does nothing
		(and returns `false`) otherwise.
		@param	deviceName	A name from `getDeviceList()`, or `null` for the system default.
	**/
	public static function setDevice(deviceName:String):Bool
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			var alc = context.openal;
			var currentContext = alc.getCurrentContext();

			if (currentContext != null)
			{
				var device = alc.getContextsDevice(currentContext);
				return alc.reopenDeviceSOFT(device, deviceName, null);
			}
		}
		#end
		return false;
	}

	public static function resume():Void
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			var alc = context.openal;
			var currentContext = alc.getCurrentContext();

			if (currentContext != null)
			{
				var device = alc.getContextsDevice(currentContext);
				alc.resumeDevice(device);
				alc.processContext(currentContext);
			}
		}
		#end
	}

	public static function shutdown():Void
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			var alc = context.openal;
			var currentContext = alc.getCurrentContext();

			if (currentContext != null)
			{
				var device = alc.getContextsDevice(currentContext);
				alc.makeContextCurrent(null);
				alc.destroyContext(currentContext);

				if (device != null)
				{
					alc.closeDevice(device);
				}
			}
		}
		#end

		context = null;
	}

	public static function suspend():Void
	{
		#if !lime_doc_gen
		if (context != null && context.type == OPENAL)
		{
			var alc = context.openal;
			var currentContext = alc.getCurrentContext();

			if (currentContext != null)
			{
				alc.suspendContext(currentContext);
				var device = alc.getContextsDevice(currentContext);

				if (device != null)
				{
					alc.pauseDevice(device);
				}
			}
		}
		#end
	}

	@:noCompletion static var __audioDeviceChanged:Bool = false;
	@:noCompletion static function __deviceEventCallback(eventType:Int, deviceType:Int, device:Dynamic,#if hl message:hl.Bytes #else message:String #end, userParam:Dynamic):Void
	{
		#if !lime_doc_gen
		#if hl
		var message = CFFI.stringValue(message);
		#end

		if (eventType == ALC.EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT && deviceType == ALC.PLAYBACK_DEVICE_SOFT)
		{
			// We can't make any calls to OpenAL here.
			// Let's set a flag and then reopen the device in the update() function that gets
			// called on the main thread.
			__audioDeviceChanged = true;
		}
		#end
	}
}
