package lime.system;

#if (lime_telemetry && !macro)
import lime.graphics.opengl.GL;
import lime.graphics.opengl.GLQuery;
import lime.utils.Log;
#if sys
import sys.io.File;
import sys.io.FileOutput;
#end
#end

/**
	Per-frame performance recorder and benchmark.

	Compile with `-Dlime_telemetry` to enable; without it every call compiles away
	to nothing.

	Each frame records the wall-clock frame time, the CPU cost split into update /
	render / buffer-swap, GPU time from a GL timer query, and heap usage. Rows are
	streamed to CSV while the app runs, so the data survives a freeze that has to
	be killed from Task Manager.

	Label phases to get them shaded in the chart and broken out in the summary:

	```haxe
	Telemetry.section("menu");
	Telemetry.section("gameplay");
	```

	On exit a benchmark summary (percentiles, 1% lows, stalls, per-section
	breakdown) is logged and written next to the CSV.
**/
class Telemetry
{
	/** Frames longer than this (ms) count as a stall and are logged live. */
	public static var freezeThreshold:Float = 100;

	/** Expected frame budget in ms, used for the "over budget" stat. 0 = infer from data. */
	public static var targetFrameMs:Float = 0;

	/** Maximum samples retained in memory; older samples are overwritten once full. */
	public static var capacity(default, null):Int = 36000;

	/** Stream the CSV to disk while running. */
	public static var autoSave:Bool = true;

	/**
		Folder (relative to the app directory) that the CSV, summary, chart and plot
		script are written into. Created on demand. Set to "" to write beside the exe.
	**/
	public static var outputDirectory:String = "benchmark";

	/** Filename for the streamed CSV, inside `outputDirectory`. */
	public static var autoSavePath:String = "telemetry.csv";

	/** Seconds between incremental writes. */
	public static var flushInterval:Float = 5;

	/** Frames held back before writing, so async GPU results have time to land. */
	public static var flushLag:Int = 8;

	/** Frames between heap-usage samples; the last value is carried forward. */
	public static var memoryInterval:Int = 30;

	/** On exit, render the chart with Python and open it. */
	public static var openChartOnExit:Bool = true;

	/** Allow `pip install matplotlib` if it is missing. Set false to never touch pip. */
	public static var installChartDeps:Bool = true;

	/** Chart image written next to the CSV. */
	public static var chartPath:String = "chart.png";

	/** Target frame period passed to the chart, in ms. 0 = let the script infer it. */
	public static var chartTargetMs:Float = 0;

	#if (lime_telemetry && !macro)
	private static inline var TIME_ELAPSED:Int = 0x88BF;
	private static inline var QUERY_POOL:Int = 4;

	private static var times:Array<Float>;
	private static var frameTimes:Array<Float>;
	private static var updateTimes:Array<Float>;
	private static var renderTimes:Array<Float>;
	private static var swapTimes:Array<Float>;
	private static var gpuTimes:Array<Float>;
	private static var memory:Array<Float>;
	private static var sections:Array<String>;

	private static var head:Int = 0;
	private static var totalPushed:Int = 0;

	private static var startStamp:Float = -1;
	private static var frameStart:Float = 0;
	private static var lastFrameStart:Float = -1;
	private static var nativeDelta:Float = -1;

	private static var updateMs:Float = 0;
	private static var renderStart:Float = 0;
	private static var renderMs:Float = 0;
	private static var swapStart:Float = 0;
	private static var swapMs:Float = 0;

	private static var currentSection:String = "";
	private static var lastMemory:Float = 0;

	private static var gpuSupported:Bool = true;
	private static var gpuFailures:Int = 0;
	private static var gpuResults:Int = 0;
	private static var gpuWarned:Bool = false;
	private static var queries:Array<GLQuery>;
	private static var queryTarget:Array<Int>;
	private static var queryBusy:Array<Bool>;
	private static var queryIndex:Int = 0;
	private static var queryOpen:Bool = false;

	private static var gpuName:String = null;
	private static var glVersion:String = null;

	private static var stallCount:Int = 0;
	private static var stallTotalMs:Float = 0;

	#if sys
	private static var output:FileOutput;
	private static var resolvedPath:String;
	private static var writeCursor:Int = 0;
	private static var lastFlush:Float = -1;
	private static var streamFailed:Bool = false;
	private static var droppedWarned:Bool = false;
	#end

	private static function init():Void
	{
		times = [];
		frameTimes = [];
		updateTimes = [];
		renderTimes = [];
		swapTimes = [];
		gpuTimes = [];
		memory = [];
		sections = [];
		startStamp = haxe.Timer.stamp();
		#if sys
		lastFlush = startStamp;
		#end
	}

	private static inline function now():Float
	{
		return haxe.Timer.stamp();
	}

	private static function sampleMemory():Float
	{
		#if cpp
		try
		{
			return cpp.vm.Gc.memUsage() / 1048576.0;
		}
		catch (e:Dynamic) {}
		#end
		return lastMemory;
	}

	private static function captureGpuInfo():Void
	{
		if (gpuName != null) return;

		try
		{
			var r = GL.getParameter(GL.RENDERER);
			var v = GL.getParameter(GL.VERSION);
			gpuName = (r == null) ? "unknown" : Std.string(r);
			glVersion = (v == null) ? "unknown" : Std.string(v);
		}
		catch (e:Dynamic)
		{
			gpuName = "unknown";
			glVersion = "unknown";
		}
	}
	#end

	/** Labels the frames that follow. Shows as a shaded band in the chart. */
	public static function section(name:String):Void
	{
		#if (lime_telemetry && !macro)
		if (name == null) name = "";
		// commas and newlines would break the CSV row
		name = StringTools.replace(StringTools.replace(name, ",", ";"), "\n", " ");
		currentSection = name;
		#end
	}

	/** The section label currently in effect. */
	public static function getSection():String
	{
		#if (lime_telemetry && !macro)
		return currentSection;
		#else
		return "";
		#end
	}

	/** Called at the top of the update dispatch. `deltaTime` is lime's high-resolution frame delta in ms. */
	public static function beginFrame(deltaTime:Float = -1):Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) init();

		frameStart = now();
		nativeDelta = deltaTime;
		updateMs = 0;
		renderMs = 0;
		swapMs = 0;
		#end
	}

	/** Called after the update dispatch returns. */
	public static function endUpdate():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;
		updateMs = (now() - frameStart) * 1000;
		#end
	}

	/** Called before the render dispatch. */
	public static function beginRender():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;

		renderStart = now();
		captureGpuInfo();
		// polled here, not in beginFrame: the GL context is only guaranteed current
		// during the render phase
		pollQueries();
		beginQuery();
		#end
	}

	/** Called after the render dispatch, before the buffer swap. */
	public static function endRender():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;

		renderMs = (now() - renderStart) * 1000;
		endQuery();
		#end
	}

	/** Called around the buffer swap, which is where vsync waiting shows up. */
	public static function beginSwap():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;
		swapStart = now();
		#end
	}

	/** Called after the buffer swap. */
	public static function endSwap():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;
		swapMs = (now() - swapStart) * 1000;
		#end
	}

	/** Commits the frame's sample. Called once per frame, after the swap. */
	public static function endFrame():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null) return;
		commit(now());
		#end
	}

	#if (lime_telemetry && !macro)
	private static function commit(end:Float):Void
	{
		var frameMs = (nativeDelta >= 0) ? nativeDelta : ((lastFrameStart < 0) ? 0.0 : (frameStart - lastFrameStart) * 1000);
		lastFrameStart = frameStart;

		if (totalPushed % memoryInterval == 0) lastMemory = sampleMemory();

		var index = push(frameStart - startStamp, frameMs);

		if (queryOpen && index >= 0)
		{
			queryTarget[queryIndex] = index;
			queryOpen = false;
		}

		if (frameMs > freezeThreshold)
		{
			stallCount++;
			stallTotalMs += frameMs;

			Log.warn("[telemetry] stall: " + Std.int(frameMs) + "ms frame at t=" + Std.string(Math.round((frameStart - startStamp) * 100) / 100)
				+ "s (update " + fmt(updateMs) + " render " + fmt(renderMs) + " swap " + fmt(swapMs)
				+ (currentSection == "" ? "" : ", section " + currentSection) + ")");
		}

		#if sys
		if (autoSave && !streamFailed && (end - lastFlush) >= flushInterval)
		{
			lastFlush = end;
			flush();
		}
		#end
	}

	private static inline function fmt(v:Float):String
	{
		return Std.string(Math.round(v * 100) / 100);
	}

	private static function push(time:Float, frame:Float):Int
	{
		var index = head;

		times[index] = time;
		frameTimes[index] = frame;
		updateTimes[index] = updateMs;
		renderTimes[index] = renderMs;
		swapTimes[index] = swapMs;
		gpuTimes[index] = -1;
		memory[index] = lastMemory;
		sections[index] = currentSection;

		head++;
		if (head >= capacity) head = 0;

		totalPushed++;

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
						queries = null;
						Log.warn("[telemetry] GL.createQuery() returned null - GPU timing unavailable "
							+ "(lime is built without LIME_GLES3_API for this target, so glGenQueries is a no-op)");
						return;
					}
					queries.push(q);
					queryTarget.push(-1);
					queryBusy.push(false);
				}
			}

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
		// warn even when queries were never created, so a silent failure still surfaces
		if (!gpuWarned && gpuResults == 0 && totalPushed > 300)
		{
			gpuWarned = true;
			Log.warn("[telemetry] no GPU timer results after 300 frames - gpu_ms will stay empty");
		}

		if (!gpuSupported || queries == null) return;

		try
		{
			for (i in 0...QUERY_POOL)
			{
				if (!queryBusy[i]) continue;
				if (GL.getQueryObjectui(queries[i], GL.QUERY_RESULT_AVAILABLE) == 0) continue;

				var ns = GL.getQueryObjectui(queries[i], GL.QUERY_RESULT);
				var target = queryTarget[i];

				if (target >= 0 && target < gpuTimes.length) gpuTimes[target] = ns / 1000000.0;

				gpuResults++;
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

	#if sys
	private static function resolvePath(path:String):String
	{
		if (path == null || path == "") path = "telemetry.csv";

		if (path.indexOf(":") == 1 || StringTools.startsWith(path, "/") || StringTools.startsWith(path, "\\")) return path;

		var dir = System.applicationDirectory;
		if (dir == null || dir == "") dir = "./";
		if (!StringTools.endsWith(dir, "/") && !StringTools.endsWith(dir, "\\")) dir += "/";

		if (outputDirectory != null && outputDirectory != "")
		{
			dir += outputDirectory + "/";
			ensureDirectory(dir);
		}

		return dir + path;
	}

	private static function ensureDirectory(dir:String):Void
	{
		try
		{
			if (!sys.FileSystem.exists(dir)) sys.FileSystem.createDirectory(dir);
		}
		catch (e:Dynamic)
		{
			Log.warn("[telemetry] could not create " + dir + ": " + Std.string(e));
		}
	}

	private static function row(idx:Int):String
	{
		return Std.string(times[idx]) + "," + Std.string(frameTimes[idx]) + "," + Std.string(updateTimes[idx] + renderTimes[idx]) + ","
			+ Std.string(updateTimes[idx]) + "," + Std.string(renderTimes[idx]) + "," + Std.string(swapTimes[idx]) + "," + Std.string(gpuTimes[idx]) + ","
			+ Std.string(memory[idx]) + "," + sections[idx] + "\n";
	}

	private static function header():String
	{
		var buf = new StringBuf();
		buf.add("# lime telemetry\n");
		buf.add("# gpu=" + Std.string(gpuName) + "\n");
		buf.add("# gl=" + Std.string(glVersion) + "\n");
		buf.add("# platform=" + Std.string(System.platformLabel) + "\n");
		buf.add("time_s,frame_ms,cpu_ms,update_ms,render_ms,swap_ms,gpu_ms,mem_mb,section\n");
		return buf.toString();
	}
	#end
	#end

	/** Appends everything recorded since the last flush. */
	public static function flush():Bool
	{
		#if (lime_telemetry && sys && !macro)
		if (times == null || streamFailed) return false;

		var limit = totalPushed - flushLag;
		if (limit <= writeCursor) return false;

		try
		{
			if (output == null)
			{
				resolvedPath = resolvePath(autoSavePath);
				output = File.write(resolvedPath, false);
				output.writeString(header());
				Log.info("[telemetry] recording to " + resolvedPath);
			}

			if (totalPushed - writeCursor > capacity)
			{
				if (!droppedWarned)
				{
					droppedWarned = true;
					Log.warn("[telemetry] buffer wrapped before flush, some samples were dropped");
				}
				writeCursor = totalPushed - capacity;
			}

			var buf = new StringBuf();

			while (writeCursor < limit)
			{
				buf.add(row(writeCursor % capacity));
				writeCursor++;
			}

			output.writeString(buf.toString());
			output.flush();
			return true;
		}
		catch (e:Dynamic)
		{
			streamFailed = true;
			Log.warn("[telemetry] could not write " + Std.string(resolvedPath) + ": " + Std.string(e));
			return false;
		}
		#else
		return false;
		#end
	}

	/**
		Benchmark summary over everything still in the buffer: percentiles, 1% and
		0.1% lows, stalls, and a per-section breakdown.
	**/
	public static function summary():String
	{
		#if (lime_telemetry && !macro)
		if (times == null || totalPushed == 0) return "[telemetry] no samples recorded";

		var held = (totalPushed < capacity) ? totalPushed : capacity;
		var first = totalPushed - held;

		var frames = [];
		var cpuSum = 0.0, updSum = 0.0, rndSum = 0.0, swpSum = 0.0;
		var gpuSum = 0.0, gpuCount = 0;
		var memMax = 0.0;

		for (i in 0...held)
		{
			var idx = (first + i) % capacity;
			frames.push(frameTimes[idx]);
			updSum += updateTimes[idx];
			rndSum += renderTimes[idx];
			swpSum += swapTimes[idx];
			cpuSum += updateTimes[idx] + renderTimes[idx];
			if (gpuTimes[idx] >= 0)
			{
				gpuSum += gpuTimes[idx];
				gpuCount++;
			}
			if (memory[idx] > memMax) memMax = memory[idx];
		}

		var sorted = frames.copy();
		sorted.sort(function(a, b) return a < b ? -1 : (a > b ? 1 : 0));

		var duration = times[(first + held - 1) % capacity] - times[first % capacity];
		var avgFrame = 0.0;
		for (f in frames) avgFrame += f;
		avgFrame /= held;

		var budget = (targetFrameMs > 0) ? targetFrameMs : pct(sorted, 50);
		var over = 0;
		for (f in frames) if (f > budget * 1.5) over++;

		var buf = new StringBuf();
		buf.add("\n===== lime telemetry summary =====\n");
		buf.add("gpu           : " + Std.string(gpuName) + "\n");
		buf.add("gl            : " + Std.string(glVersion) + "\n");
		buf.add("duration      : " + fmt(duration) + "s over " + held + " frames\n");
		buf.add("average       : " + fmt(avgFrame) + " ms  (" + fmt(1000 / avgFrame) + " fps)\n");
		buf.add("frame p50     : " + fmt(pct(sorted, 50)) + " ms  (" + fmt(1000 / pct(sorted, 50)) + " fps)\n");
		buf.add("frame p95     : " + fmt(pct(sorted, 95)) + " ms\n");
		buf.add("frame p99     : " + fmt(pct(sorted, 99)) + " ms\n");
		buf.add("frame max     : " + fmt(sorted[sorted.length - 1]) + " ms\n");
		buf.add("1% low        : " + fmt(1000 / lowAvg(sorted, 0.01)) + " fps\n");
		buf.add("0.1% low      : " + fmt(1000 / lowAvg(sorted, 0.001)) + " fps\n");
		buf.add("cpu avg       : " + fmt(cpuSum / held) + " ms  (update " + fmt(updSum / held) + " render " + fmt(rndSum / held) + ")\n");
		buf.add("swap avg      : " + fmt(swpSum / held) + " ms\n");
		buf.add("gpu avg       : " + (gpuCount > 0 ? fmt(gpuSum / gpuCount) + " ms" : "n/a") + "\n");
		buf.add("heap peak     : " + fmt(memMax) + " MB\n");
		buf.add("over budget   : " + over + " frames > " + fmt(budget * 1.5) + " ms\n");
		buf.add("stalls        : " + stallCount + " over " + fmt(freezeThreshold) + " ms, " + fmt(stallTotalMs) + " ms total\n");

		var names = [];
		var secFrames = new Map<String, Array<Float>>();

		for (i in 0...held)
		{
			var idx = (first + i) % capacity;
			var s = sections[idx];
			if (s == null || s == "") s = "(unlabelled)";
			if (!secFrames.exists(s))
			{
				secFrames.set(s, []);
				names.push(s);
			}
			secFrames.get(s).push(frameTimes[idx]);
		}

		if (names.length > 1 || names[0] != "(unlabelled)")
		{
			buf.add("--- sections ---\n");

			for (name in names)
			{
				var fs = secFrames.get(name);
				var sum = 0.0;
				for (f in fs) sum += f;
				var avg = sum / fs.length;
				var fsSorted = fs.copy();
				fsSorted.sort(function(a, b) return a < b ? -1 : (a > b ? 1 : 0));

				buf.add("  " + name + ": " + fs.length + " frames, avg " + fmt(1000 / avg) + " fps, 1% low " + fmt(1000 / lowAvg(fsSorted, 0.01))
					+ " fps\n");
			}
		}

		buf.add("==================================\n");
		return buf.toString();
		#else
		return "[telemetry] disabled";
		#end
	}

	#if (lime_telemetry && !macro)
	private static function pct(sorted:Array<Float>, p:Float):Float
	{
		if (sorted.length == 0) return 0;
		var i = Std.int(sorted.length * p / 100);
		if (i >= sorted.length) i = sorted.length - 1;
		return sorted[i];
	}

	/** Mean of the slowest `frac` of frames - the standard "1% low" metric. */
	private static function lowAvg(sorted:Array<Float>, frac:Float):Float
	{
		if (sorted.length == 0) return 0;
		var n = Std.int(sorted.length * frac);
		if (n < 1) n = 1;
		var sum = 0.0;
		for (i in 0...n) sum += sorted[sorted.length - 1 - i];
		return sum / n;
	}
	#end

	/**
		Pulls `tools/telemetry/plot_telemetry.py` out of the lime checkout at compile
		time, so the chart can be rendered at runtime without knowing where lime lives.
	**/
	private static macro function embeddedPlotScript():haxe.macro.Expr
	{
		var content = "";

		try
		{
			var self = haxe.macro.Context.resolvePath("lime/system/Telemetry.hx");
			var script = haxe.io.Path.normalize(haxe.io.Path.directory(self) + "/../../../tools/telemetry/plot_telemetry.py");

			if (sys.FileSystem.exists(script)) content = sys.io.File.getContent(script);
		}
		catch (e:Dynamic) {}

		return macro $v{content};
	}

	#if (lime_telemetry && sys && !macro)
	/** First python on PATH that can actually run, or null. */
	private static function findPython():String
	{
		// "py" first: on Windows a bare "python" can be the Microsoft Store stub
		for (exe in ["py", "python", "python3"])
		{
			try
			{
				if (Sys.command(exe, ["-c", "pass"]) == 0) return exe;
			}
			catch (e:Dynamic) {}
		}

		return null;
	}

	private static function openFile(path:String):Void
	{
		try
		{
			#if windows
			Sys.command("cmd", ["/c", "start", "", path]);
			#elseif mac
			Sys.command("open", [path]);
			#else
			Sys.command("xdg-open", [path]);
			#end
		}
		catch (e:Dynamic) {}
	}

	private static function renderChart():Void
	{
		if (resolvedPath == null) return;

		var script = embeddedPlotScript();

		if (script == null || script == "")
		{
			Log.warn("[telemetry] plot script was not embedded at compile time, skipping chart");
			return;
		}

		var python = findPython();

		if (python == null)
		{
			Log.warn("[telemetry] python not found on PATH, skipping chart (CSV and summary were still written)");
			return;
		}

		var dir = haxe.io.Path.directory(resolvedPath);
		if (dir == null || dir == "") dir = ".";

		var scriptPath = dir + "/plot_telemetry.py";
		var chart = (chartPath.indexOf(":") == 1 || StringTools.startsWith(chartPath, "/")) ? chartPath : dir + "/" + chartPath;

		try
		{
			File.saveContent(scriptPath, script);
		}
		catch (e:Dynamic)
		{
			Log.warn("[telemetry] could not write " + scriptPath + ": " + Std.string(e));
			return;
		}

		if (installChartDeps)
		{
			var hasMatplotlib = false;

			try
			{
				hasMatplotlib = Sys.command(python, ["-c", "import matplotlib"]) == 0;
			}
			catch (e:Dynamic) {}

			if (!hasMatplotlib)
			{
				Log.info("[telemetry] installing matplotlib...");

				try
				{
					Sys.command(python, ["-m", "pip", "install", "--quiet", "--disable-pip-version-check", "matplotlib"]);
				}
				catch (e:Dynamic) {}
			}
		}

		var args = [scriptPath, resolvedPath, "--out", chart];
		if (chartTargetMs > 0) args = args.concat(["--target", Std.string(chartTargetMs)]);

		try
		{
			if (Sys.command(python, args) == 0)
			{
				Log.info("[telemetry] chart written to " + chart);
				openFile(chart);
			}
			else
			{
				Log.warn("[telemetry] chart generation failed; run it yourself: " + python + " " + scriptPath + " " + resolvedPath);
			}
		}
		catch (e:Dynamic)
		{
			Log.warn("[telemetry] chart generation failed: " + Std.string(e));
		}
	}
	#end

	#if (lime_telemetry && !macro)
	private static var closed:Bool = false;
	#end

	/**
		Flushes remaining samples, writes the summary and chart, and closes the stream.
		Safe to call more than once - only the first call does the work, because it is
		hooked from both the window-close event and `System.exit`.
	**/
	public static function close():Void
	{
		#if (lime_telemetry && !macro)
		if (times == null || closed) return;
		closed = true;

		var report = summary();
		Log.info(report);

		#if sys
		flushLag = 0;
		flush();

		if (output != null)
		{
			try
			{
				output.close();
				Log.info("[telemetry] wrote " + writeCursor + " samples to " + resolvedPath);
			}
			catch (e:Dynamic) {}

			output = null;
		}

		try
		{
			if (resolvedPath != null)
			{
				var txt = StringTools.endsWith(resolvedPath, ".csv") ? resolvedPath.substr(0, resolvedPath.length - 4) + "-summary.txt" : resolvedPath
					+ "-summary.txt";
				File.saveContent(txt, report);
			}
		}
		catch (e:Dynamic) {}

		// last, so a failure here can never cost us the CSV or the summary
		if (openChartOnExit) renderChart();
		#end
		#end
	}

	/** Number of samples recorded this session. */
	public static function getSampleCount():Int
	{
		#if (lime_telemetry && !macro)
		return totalPushed;
		#else
		return 0;
		#end
	}

	/** Writes a full snapshot of the in-memory buffer to an arbitrary path. */
	public static function save(path:String = "telemetry-snapshot.csv"):Bool
	{
		#if (lime_telemetry && sys && !macro)
		if (times == null || totalPushed == 0) return false;

		var held = (totalPushed < capacity) ? totalPushed : capacity;
		var first = totalPushed - held;

		var buf = new StringBuf();
		buf.add(header());

		for (i in 0...held)
		{
			buf.add(row((first + i) % capacity));
		}

		var target = resolvePath(path);

		try
		{
			File.saveContent(target, buf.toString());
			Log.info("[telemetry] wrote " + held + " samples to " + target);
			return true;
		}
		catch (e:Dynamic)
		{
			Log.warn("[telemetry] could not write " + target + ": " + Std.string(e));
			return false;
		}
		#else
		return false;
		#end
	}
}
