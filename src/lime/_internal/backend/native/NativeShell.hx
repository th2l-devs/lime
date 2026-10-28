package lime._internal.backend.native;

#if (cpp && windows)
@:buildXml('
<target id="haxe">
	<lib name="shell32.lib" />
</target>
')
@:cppFileCode('
#include <windows.h>
#include <shellapi.h>
')
#end
class NativeShell
{
	public static function open(target:String):Bool
	{
		if (target == null || target == "") return false;
		#if (cpp && windows)
		var result:Int = untyped __cpp__('(int)(intptr_t)ShellExecuteW(NULL, L"open", {0}.wchar_str(), NULL, NULL, SW_SHOWNORMAL)', target);
		return result > 32;
		#else
		return false;
		#end
	}

	public static function spawn(file:String, params:String):Int
	{
		#if (cpp && windows)
		return untyped __cpp__('[&]() -> int {
			SHELLEXECUTEINFOW info;
			ZeroMemory(&info, sizeof(info));
			info.cbSize = sizeof(info);
			info.fMask = SEE_MASK_NOCLOSEPROCESS;
			info.lpVerb = L"open";
			info.lpFile = {0}.wchar_str();
			info.lpParameters = {1}.wchar_str();
			info.nShow = SW_SHOWNORMAL;
			if (!ShellExecuteExW(&info) || info.hProcess == NULL) return 0;
			int pid = (int)GetProcessId(info.hProcess);
			CloseHandle(info.hProcess);
			return pid;
		}()', file, params);
		#else
		return 0;
		#end
	}

	public static function alive(pid:Int):Bool
	{
		if (pid <= 0) return false;
		#if (cpp && windows)
		return untyped __cpp__('[&]() -> bool {
			HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD){0});
			if (h == NULL) return false;
			bool running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
			CloseHandle(h);
			return running;
		}()', pid);
		#else
		return false;
		#end
	}

	public static function kill(pid:Int):Void
	{
		if (pid <= 0) return;
		#if (cpp && windows)
		untyped __cpp__('{
			HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD){0});
			if (h != NULL) { TerminateProcess(h, 0); CloseHandle(h); }
		}', pid);
		#end
	}

	public static function currentPid():Int
	{
		#if (cpp && windows)
		return untyped __cpp__('(int)GetCurrentProcessId()');
		#else
		return 0;
		#end
	}
}
