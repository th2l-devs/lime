package lime.utils;

/**
	Frame-time based FPS meter using a robust windowed statistic (median over a sliding
	time window), not an exponential moving average.

	Why not an EMA: any IIR blend gives every incoming sample a persistent vote that only
	decays over time. A single anomalous frame (GC pause, asset-load hitch - common during
	real gameplay, well under any reasonable "the app was actually paused" threshold) either
	swings the reading hard by itself, or - if its weight is capped - keeps throttling the
	*next several* readings, because recovery is governed by each good frame's own small
	blend weight. Either way a one-off stall shows up as a dip that visibly crawls back.

	A windowed median has no such lingering state: a stall is one sample among many and is
	gone the instant it ages out of the window - recovery time is exactly `window`, not an
	indirect function of a decay constant. A genuine sustained rate change still shows up
	within a fraction of `window`, as soon as it's the majority of the trailing samples.

	```haxe
	var meter = new FPSMeter();
	meter.update(deltaTime); // once per frame, ms
	trace(meter.fps);        // median fps over the trailing window
	trace(meter.lowFps);     // 1% low - average of the slowest ~1% of frames in the window
	```
**/
class FPSMeter
{
	/** Time window, in seconds, that `fps`/`lowFps` are computed over. */
	public var windowSeconds:Float;

	/** A gap larger than this (seconds) clears the window - e.g. pause/alt-tab. */
	public var resetThreshold:Float;

	/** Median frame rate over the trailing window. 0 until the first sample. */
	public var fps(get, never):Float;

	/** Average of the slowest ~1% of frames in the trailing window - the stutter number. */
	public var lowFps(get, never):Float;

	private var times:Array<Float>;
	private var samples:Array<Float>;
	private var capacity:Int;
	private var head:Int = 0;
	private var count:Int = 0;
	private var clock:Float = 0;

	private var dirty:Bool = true;
	private var cachedFps:Float = 0;
	private var cachedLowFps:Float = 0;

	public function new(windowSeconds:Float = 0.35, resetThreshold:Float = 0.5, capacity:Int = 4096)
	{
		this.windowSeconds = windowSeconds;
		this.resetThreshold = resetThreshold;
		this.capacity = capacity;

		times = [];
		samples = [];
		for (i in 0...capacity)
		{
			times.push(0);
			samples.push(0);
		}
	}

	/** Feed one frame's delta time, in milliseconds. */
	public function update(deltaTime:Float):Void
	{
		if (deltaTime <= 0) return;

		var dt = deltaTime / 1000.0;

		if (dt > resetThreshold) reset();

		clock += dt;

		times[head] = clock;
		samples[head] = deltaTime;
		head = (head + 1) % capacity;
		if (count < capacity) count++;

		dirty = true;
	}

	/** Clears all recorded samples; the next `update()` starts a fresh window. */
	public function reset():Void
	{
		count = 0;
		head = 0;
		dirty = true;
		cachedFps = 0;
		cachedLowFps = 0;
	}

	private function get_fps():Float
	{
		refresh();
		return cachedFps;
	}

	private function get_lowFps():Float
	{
		refresh();
		return cachedLowFps;
	}

	private function refresh():Void
	{
		if (!dirty) return;
		dirty = false;

		if (count == 0)
		{
			cachedFps = 0;
			cachedLowFps = 0;
			return;
		}

		var cutoff = clock - windowSeconds;
		var window:Array<Float> = [];

		var i = (head - 1 + capacity) % capacity;
		var n = 0;

		while (n < count)
		{
			if (times[i] < cutoff) break;
			window.push(samples[i]);
			i = (i - 1 + capacity) % capacity;
			n++;
		}

		if (window.length == 0)
		{
			// window shorter than one sample's own dt (e.g. right after a reset)
			var last = samples[(head - 1 + capacity) % capacity];
			cachedFps = last > 0 ? 1000 / last : 0;
			cachedLowFps = cachedFps;
			return;
		}

		window.sort(function(a, b) return a < b ? -1 : (a > b ? 1 : 0));

		var medianMs = window[Std.int(window.length / 2)];
		cachedFps = medianMs > 0 ? 1000 / medianMs : 0;

		var lowCount = Std.int(Math.max(1, window.length * 0.01));
		var sum = 0.0;
		for (k in 0...lowCount) sum += window[window.length - 1 - k];
		var lowAvgMs = sum / lowCount;
		cachedLowFps = lowAvgMs > 0 ? 1000 / lowAvgMs : 0;
	}
}
