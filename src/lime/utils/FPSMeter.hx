package lime.utils;

/**
	Smoothed, framerate-independent FPS counter.

	Unlike a windowed frame count (count frames in the last N ms, or blend this
	second's count with last second's), this tracks an exponential moving
	average of frame *time* and derives fps from that. It's a continuous
	function of `deltaTime` with no per-window rounding/boundary flicker.

	The smoothing factor is time-constant based (not a fixed per-frame ratio),
	so it converges at the same real-world speed regardless of frame rate.

	```haxe
	var meter = new FPSMeter();
	// once per frame:
	meter.update(deltaTime); // deltaTime in ms
	trace(meter.fps);
	```
**/
class FPSMeter
{
	/** Time constant in seconds. Lower reacts faster; higher is smoother. */
	public var smoothing:Float;

	/** A gap larger than this (seconds) snaps instead of blending - e.g. pause/alt-tab. */
	public var resetThreshold:Float;

	/** Smoothed frame time, in ms. 0 until the first sample. */
	public var frameTime(default, null):Float = 0;

	/** Smoothed frames-per-second. 0 until the first sample. */
	public var fps(get, never):Float;

	public function new(smoothing:Float = 0.2, resetThreshold:Float = 0.5)
	{
		this.smoothing = smoothing;
		this.resetThreshold = resetThreshold;
	}

	/** Feed one frame's delta time, in milliseconds. */
	public function update(deltaTime:Float):Void
	{
		if (deltaTime <= 0) return;

		var dt = deltaTime / 1000.0;

		if (frameTime <= 0 || dt > resetThreshold)
		{
			frameTime = deltaTime;
		}
		else
		{
			var alpha = 1.0 - Math.exp(-dt / smoothing);
			frameTime += (deltaTime - frameTime) * alpha;
		}
	}

	/** Resets to the unstarted state; the next `update()` snaps rather than blends. */
	public function reset():Void
	{
		frameTime = 0;
	}

	private function get_fps():Float
	{
		return frameTime > 0 ? 1000.0 / frameTime : 0;
	}
}
