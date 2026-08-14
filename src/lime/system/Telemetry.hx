package lime.system;

#if (lime_telemetry && !macro)
import lime.graphics.opengl.GL;
import lime.graphics.opengl.GLQuery;
import lime.utils.Log;
#if sys
import sys.io.File;
#end
#end

/**
	Per-frame CPU / GPU timing recorder.

	Compile with `-Dlime_telemetry` to enable; without it every call compiles away
	to nothing. Samples are kept in a ring buffer and written as CSV by `save()`.

	```haxe
	Telemetry.save("telemetry.csv");
	```

	CPU time is the time spent inside the update + render dispatch (the work the
	app actually does). Frame time is wall-clock between frame starts, so a stall
	that happens outside the dispatch — vsync waits, the frame limiter, driver
	hitches, the Windows modal loop during a window drag — still shows up as a
	frame-time spike even though CPU time stays flat. GPU time comes from an
	asynchronous GL timer query and is filled in a few frames late.
**/
class Telemetry
{
	/** Frames longer than this (ms) are logged as a stall while recording. */
	public static var freezeThreshold:Float = 100;

	/** Maximum samples retained; older samples are overwritten once full. */
	public static var capacity(default, null):Int = 36000;

	/** Write the CSV automatically when the application exits. */
	public static var autoSave:Bool = true;

	/** Destination used by the automatic save on exit. */
	public static var autoSavePath:String = "telemetry.csv";

	#if (lime_telemetry && !macro)
	private static inline var TIME_ELAPSED:Int = 0x88BF;
	private static inline var QUERY_POOL:Int = 4;

	private static var times:Array<Float>;
	private static var cpuTimes:Array<Float>;
	private static var gpuTimes:Array<Float>;
	private static var frameTimes:Array<Float>;

	private static var count:Int = 0;
	private static var head:Int = 0;
	private static var wrapped:Bool = false;

	private static var startStamp:Float = -1;
	private static var frameStart:Float = 0;
	private static var lastFrameStart:Float = -1;
	private static var cpuAccum:Float = 0;
	private static var renderStart:Float = 0;

	private static var gpuSupported:Bool = true;
	private static var gpuFailures:Int = 0;
	private static var queries:Array<GLQuery>;
	private static var queryTarget:Array<Int>;
	private static var queryBusy:Array<Bool>;
	private static var queryIndex:Int = 0;
	private static var queryOpen:Bool = false;

	private static function init():Void
	{
		times = [];
		cpuTimes = [];
		gpuTimes = [];
		frameTimes = [];
		startStamp = haxe.Timer.stamp();
	}

	private static inline function now():Float
	{
		return haxe.Timer.stamp();
	}
	#end

	/** Called at the top of the update dispatch. */
	public static function beginFrame():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) init();

		frameStart = now();
		cpuAccum = 0;

		pollQueries();
		#end
	}

	/** Called after the update dispatch returns. */
	public static function endUpdate():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;
		cpuAccum += now() - frameStart;
		#end
	}

	/** Called before the render dispatch. */
	public static function beginRender():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;

		renderStart = now();
		beginQuery();
		#end
	}

	/**
		Called after the render dispatch. Commits one sample.
	**/
	public static function endRender():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;

		var end = now();
		cpuAccum += end - renderStart;

		endQuery();

		var frameMs = (lastFrameStart < 0) ? 0.0 : (frameStart - lastFrameStart) * 1000;
		lastFrameStart = frameStart;

		var index = push(frameStart - startStamp, cpuAccum * 1000, frameMs);

		if (queryOpen && index >= 0)
		{
			queryTarget[queryIndex] = index;
			queryOpen = false;
		}

		if (frameMs > freezeThreshold)
		{
			Log.warn("[telemetry] stall: " + Std.int(frameMs) + "ms frame at t=" + Std.string(Math.round((frameStart - startStamp) * 100) / 100)
				+ "s (cpu " + Std.int(cpuAccum * 1000) + "ms)");
		}
		#end
	}

	#if (lime_telemetry && !macro)
	private static function push(time:Float, cpu:Float, frame:Float):Int
	{
		var index = head;

		times[index] = time;
		cpuTimes[index] = cpu;
		frameTimes[index] = frame;
		gpuTimes[index] = -1;

		head++;

		if (head >= capacity)
		{
			head = 0;
			wrapped = true;
		}

		if (!wrapped) count = head;
		else count = capacity;

		return index;
	}

	private static function beginQuery():Void
	{
		if (!gpuSupported) return;

		try
		{
			if (queries == null)
			{
				queries = [];
				queryTarget = [];
				queryBusy = [];

				for (i in 0...QUERY_POOL)
				{
					var q = GL.createQuery();
					if (q == null)
					{
						gpuSupported = false;
						return;
					}
					queries.push(q);
					queryTarget.push(-1);
					queryBusy.push(false);
				}
			}

			// find a free slot; if none, skip GPU timing this frame
			var slot = -1;

			for (i in 0...QUERY_POOL)
			{
				var idx = (queryIndex + 1 + i) % QUERY_POOL;
				if (!queryBusy[idx])
				{
					slot = idx;
					break;
				}
			}

			if (slot < 0) return;

			queryIndex = slot;
			GL.beginQuery(TIME_ELAPSED, queries[slot]);
			queryOpen = true;
		}
		catch (e:Dynamic)
		{
			disableGpu(e);
		}
	}

	private static function endQuery():Void
	{
		if (!gpuSupported || !queryOpen) return;

		try
		{
			GL.endQuery(TIME_ELAPSED);
			queryBusy[queryIndex] = true;
		}
		catch (e:Dynamic)
		{
			queryOpen = false;
			disableGpu(e);
		}
	}

	private static function pollQueries():Void
	{
		if (!gpuSupported || queries == null) return;

		try
		{
			for (i in 0...QUERY_POOL)
			{
				if (!queryBusy[i]) continue;

				if (GL.getQueryObjectui(queries[i], GL.QUERY_RESULT_AVAILABLE) == 0) continue;

				var ns = GL.getQueryObjectui(queries[i], GL.QUERY_RESULT);
				var target = queryTarget[i];

				if (target >= 0 && target < gpuTimes.length)
				{
					gpuTimes[target] = ns / 1000000.0;
				}

				queryBusy[i] = false;
				queryTarget[i] = -1;
			}
		}
		catch (e:Dynamic)
		{
			disableGpu(e);
		}
	}

	private static function disableGpu(e:Dynamic):Void
	{
		gpuFailures++;

		if (gpuFailures >= 3)
		{
			gpuSupported = false;
			Log.warn("[telemetry] GPU timer queries unavailable, recording CPU only (" + Std.string(e) + ")");
		}
	}
	#end

	/** Number of samples currently held. */
	public static function getSampleCount():Int
	{
		#if (lime_telemetry && !macro)
		return count;
		#else
		return 0;
		#end
	}

	/** Drops all recorded samples and restarts the clock. */
	public static function reset():Void
	{
		#if (lime_telemetry && !macro)
		init();
		count = 0;
		head = 0;
		wrapped = false;
		lastFrameStart = -1;
		#end
	}

	/**
		Writes the recorded samples as CSV: `time_s,cpu_ms,gpu_ms,frame_ms`.
		A `gpu_ms` of -1 means the query result never arrived for that frame.
		Returns false if telemetry is disabled or nothing was recorded.
	**/
	public static function save(path:String = "telemetry.csv"):Bool
	{
		#if (lime_telemetry && sys && !macro)
		if (times == null || count == 0) return false;

		var buf = new StringBuf();
		buf.add("time_s,cpu_ms,gpu_ms,frame_ms\n");

		var start = wrapped ? head : 0;

		for (i in 0...count)
		{
			var idx = (start + i) % capacity;

			buf.add(Std.string(times[idx]));
			buf.add(",");
			buf.add(Std.string(cpuTimes[idx]));
			buf.add(",");
			buf.add(Std.string(gpuTimes[idx]));
			buf.add(",");
			buf.add(Std.string(frameTimes[idx]));
			buf.add("\n");
		}

		try
		{
			File.saveContent(path, buf.toString());
			Log.info("[telemetry] wrote " + count + " samples to " + path);
			return true;
		}
		catch (e:Dynamic)
		{
			Log.warn("[telemetry] could not write " + path + ": " + Std.string(e));
			return false;
		}
		#else
		return false;
		#end
	}
}
