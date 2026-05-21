package lime.system;

import lime.app.Application;
import lime.app.Event;
#if sys
#if haxe4
import sys.thread.Deque;
import sys.thread.Thread;
#elseif cpp
import cpp.vm.Deque;
import cpp.vm.Thread;
#elseif neko
import neko.vm.Deque;
import neko.vm.Thread;
#end
#end
#if !lime_debug
@:fileXml('tags="haxe,release"')
@:noDebug
#end
class BackgroundWorker
{
	private static var MESSAGE_COMPLETE = "__COMPLETE__";
	private static var MESSAGE_ERROR = "__ERROR__";

	public var canceled(default, null):Bool;
	public var completed(default, null):Bool;
	public var doWork = new Event<Dynamic->Void>();
	public var onComplete = new Event<Dynamic->Void>();
	public var onError = new Event<Dynamic->Void>();
	public var onProgress = new Event<Dynamic->Void>();

	/**
		When `true`, `run()` executes the work on the calling (main) thread instead of
		spawning a background thread, dispatching results immediately. Needed on Windows
		for native modal dialogs, which deadlock if pumped from a background thread.
	**/
	public var synchronous:Bool;

	@:noCompletion private var __runMessage:Dynamic;
	#if (cpp || neko)
	@:noCompletion private var __messageQueue:Deque<Dynamic>;
	@:noCompletion private var __workerThread:Thread;
	#end

	public function new(synchronous:Bool = false)
	{
		this.synchronous = synchronous;
	}

	public function cancel():Void
	{
		canceled = true;

		#if (cpp || neko)
		__workerThread = null;
		#end
	}

	public function run(message:Dynamic = null):Void
	{
		canceled = false;
		completed = false;
		__runMessage = message;

		#if (cpp || neko)
		if (synchronous)
		{
			__doWork();
		}
		else
		{
			__messageQueue = new Deque<Dynamic>();
			__workerThread = Thread.create(__doWork);

			// TODO: Better way to do this

			if (Application.current != null)
			{
				Application.current.onUpdate.add(__update);
			}
		}
		#else
		__doWork();
		#end
	}

	public function sendComplete(message:Dynamic = null):Void
	{
		completed = true;

		#if (cpp || neko)
		if (!synchronous)
		{
			__messageQueue.add(MESSAGE_COMPLETE);
			__messageQueue.add(message);
			return;
		}
		#end

		if (!canceled)
		{
			canceled = true;
			onComplete.dispatch(message);
		}
	}

	public function sendError(message:Dynamic = null):Void
	{
		#if (cpp || neko)
		if (!synchronous)
		{
			__messageQueue.add(MESSAGE_ERROR);
			__messageQueue.add(message);
			return;
		}
		#end

		if (!canceled)
		{
			canceled = true;
			onError.dispatch(message);
		}
	}

	public function sendProgress(message:Dynamic = null):Void
	{
		#if (cpp || neko)
		if (!synchronous)
		{
			__messageQueue.add(message);
			return;
		}
		#end

		if (!canceled)
		{
			onProgress.dispatch(message);
		}
	}

	@:noCompletion private function __doWork():Void
	{
		// A throwing job would otherwise kill the worker thread silently, so the
		// completion message is never queued and the caller waits forever. Route
		// the exception to onError instead.
		try
		{
			doWork.dispatch(__runMessage);
		}
		catch (e:Dynamic)
		{
			sendError(e);
		}

		// #if (cpp || neko)
		//
		// __messageQueue.add (MESSAGE_COMPLETE);
		//
		// #else
		//
		// if (!canceled) {
		//
		// canceled = true;
		// onComplete.dispatch (null);
		//
		// }
		//
		// #end
	}

	@:noCompletion private function __update(deltaTime:Float):Void
	{
		#if (cpp || neko)
		var message = __messageQueue.pop(false);

		if (message != null)
		{
			if (message == MESSAGE_ERROR)
			{
				if (Application.current != null) Application.current.onUpdate.remove(__update);

				if (!canceled)
				{
					canceled = true;
					onError.dispatch(__messageQueue.pop(false));
				}
			}
			else if (message == MESSAGE_COMPLETE)
			{
				if (Application.current != null) Application.current.onUpdate.remove(__update);

				if (!canceled)
				{
					canceled = true;
					onComplete.dispatch(__messageQueue.pop(false));
				}
			}
			else
			{
				if (!canceled)
				{
					onProgress.dispatch(message);
				}
			}
		}
		#end
	}
}
