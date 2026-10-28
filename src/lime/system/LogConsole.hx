package lime.system;

import lime._internal.backend.native.NativeShell;

class LogConsole
{
	static var pid:Int = 0;

	public static var theme:LogConsoleTheme = new LogConsoleTheme();

	public static function resetTheme():Void
	{
		theme = new LogConsoleTheme();
	}

	public static var available(get, never):Bool;

	public static function open(file:String, ?title:String):Bool
	{
		if (file == null || file == "" || !available) return false;
		if (isOpen()) return true;
		#if sys
		file = sys.FileSystem.fullPath(file);
		#end
		if (title == null || title == "") title = "Logs";
		final owner = NativeShell.currentPid();
		final script = scriptFile(owner);
		if (script == null) return false;
		#if sys
		try
		{
			sys.io.File.saveContent(script, "\uFEFF" + viewerScript(file, title, owner));
		}
		catch (e:Dynamic)
		{
			return false;
		}
		#end
		hookExit();
		pid = NativeShell.spawn("powershell.exe", "-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"");
		return pid > 0;
	}

	static var exitHooked:Bool = false;

	static function hookExit():Void
	{
		if (exitHooked || lime.app.Application.current == null) return;
		exitHooked = true;
		lime.app.Application.current.onExit.add(function(_) close());
	}

	public static function isOpen():Bool
	{
		if (pid > 0 && !NativeShell.alive(pid)) pid = 0;
		return pid > 0;
	}

	public static function close():Void
	{
		if (pid > 0) NativeShell.kill(pid);
		pid = 0;
	}

	public static function toggle(file:String, ?title:String):Bool
	{
		if (isOpen())
		{
			close();
			return false;
		}
		return open(file, title);
	}

	static function get_available():Bool
	{
		#if (cpp && windows)
		return true;
		#else
		return false;
		#end
	}

	static function viewerScript(file:String, title:String, owner:Int):String
	{
		final script = [
			"$ErrorActionPreference = 'SilentlyContinue'",
			"[Console]::OutputEncoding = [Text.Encoding]::UTF8",
			"$f = '__FILE__'",
			"$title = '__TITLE__'",
			"$owner = __OWNER__",
			"__THEME__",
			"$ui = $host.UI.RawUI",
			"$ui.WindowTitle = $title",
			"Add-Type -Namespace LimeLog -Name Con -UsingNamespace System.Diagnostics, System.Threading -MemberDefinition @'",
			"[StructLayout(LayoutKind.Explicit, Size = 20)]",
			"public struct InputRecord { [FieldOffset(0)] public short Type; [FieldOffset(4)] public int A; [FieldOffset(8)] public int B; [FieldOffset(12)] public int C; [FieldOffset(16)] public int Flags; }",
			"[DllImport(\"kernel32.dll\")] public static extern bool GetNumberOfConsoleInputEvents(IntPtr h, out int n);",
			"[DllImport(\"kernel32.dll\", CharSet = CharSet.Unicode)] public static extern bool ReadConsoleInput(IntPtr h, [Out] InputRecord[] b, int len, out int read);",
			"public static int Poll(int page) {",
			"  IntPtr h = GetStdHandle(-10); int n;",
			"  if (!GetNumberOfConsoleInputEvents(h, out n) || n <= 0) return 0;",
			"  InputRecord[] b = new InputRecord[n]; int r;",
			"  if (!ReadConsoleInput(h, b, n, out r)) return 0;",
			"  int d = 0;",
			"  for (int i = 0; i < r; i++) {",
			"    if (b[i].Type == 1 && b[i].A != 0) {",
			"      int vk = (b[i].B >> 16) & 0xFFFF; int rep = Math.Max(1, b[i].B & 0xFFFF);",
			"      if (vk == 0x21) d += page * rep; else if (vk == 0x22) d -= page * rep;",
			"      else if (vk == 0x26) d += rep; else if (vk == 0x28) d -= rep;",
			"      else if (vk == 0x24) return 1000000; else if (vk == 0x23) return -1000000;",
			"      else if (vk == 0x56) return 2000000;",
			"    } else if (b[i].Type == 2 && (b[i].Flags & 0x4) != 0) {",
			"      d += ((short)((b[i].B >> 16) & 0xFFFF)) > 0 ? 3 : -3;",
			"    }",
			"  }",
			"  return d;",
			"}",
			"[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]",
			"public struct FontInfo { public int cbSize; public int nFont; public short dwFontSizeX; public short dwFontSizeY; public int FontFamily; public int FontWeight; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string FaceName; }",
			"[DllImport(\"kernel32.dll\", SetLastError = true)] public static extern IntPtr GetStdHandle(int h);",
			"[DllImport(\"kernel32.dll\", SetLastError = true, CharSet = CharSet.Unicode)] public static extern bool SetCurrentConsoleFontEx(IntPtr h, bool max, ref FontInfo f);",
			"[DllImport(\"kernel32.dll\")] public static extern bool GetConsoleMode(IntPtr h, out int m);",
			"[DllImport(\"kernel32.dll\")] public static extern bool SetConsoleMode(IntPtr h, int m);",
			"public static void Font(string face, short size) { FontInfo f = new FontInfo(); f.cbSize = Marshal.SizeOf(f); f.dwFontSizeY = size; f.FontFamily = 54; f.FontWeight = 400; f.FaceName = face; SetCurrentConsoleFontEx(GetStdHandle(-11), false, ref f); }",
			"public static void Watch(int pid) { Thread t = new Thread(() => { try { Process.GetProcessById(pid).WaitForExit(); } catch { } Environment.Exit(0); }); t.IsBackground = true; t.Start(); }",
			"public static void Vt() { IntPtr h = GetStdHandle(-11); int m; GetConsoleMode(h, out m); SetConsoleMode(h, m | 0x0004); IntPtr i = GetStdHandle(-10); int n; GetConsoleMode(i, out n); SetConsoleMode(i, (n & ~0x0040 & ~0x0200 & ~0x0001 & ~0x0002 & ~0x0004) | 0x0080 | 0x0010); }",
			"'@",
			"[LimeLog.Con]::Font($T.font, $T.fontSize)",
			"[LimeLog.Con]::Vt()",
			"[LimeLog.Con]::Watch($owner)",
			"$cols = [Math]::Min($T.columns, $ui.MaxPhysicalWindowSize.Width)",
			"$rows = [Math]::Min($T.rows, $ui.MaxPhysicalWindowSize.Height)",
			"$ui.WindowSize = New-Object Management.Automation.Host.Size([Math]::Min($cols, $ui.WindowSize.Width), [Math]::Min($rows, $ui.WindowSize.Height))",
			"$ui.BufferSize = New-Object Management.Automation.Host.Size($cols, $rows)",
			"$ui.WindowSize = New-Object Management.Automation.Host.Size($cols, $rows)",
			"",
			"$E = [char]27",
			"function Fg([string]$rgb) { \"$E[38;2;${rgb}m\" }",
			"function Bg([string]$rgb) { \"$E[48;2;${rgb}m\" }",
			"$BASE = \"$E[0m\" + (Bg $T.background) + (Fg $T.text)",
			"$R = $BASE; $BOLD = \"$E[1m\"",
			"$labelW = 2 + (@($T.labelError, $T.labelWarn, $T.labelInfo, $T.labelLog, $T.labelRender) | ForEach-Object { $_.Length } | Measure-Object -Maximum).Maximum",
			"$INDENT = 8 + 1 + $labelW + 1",
			"$MODES = @('normal', 'verbose', 'compact', 'warnings', 'errors')",
			"$FOLD = 4",
			"$DUP = 9998",
			"$script:lastKey = ''",
			"$script:mode = [Math]::Max(0, [Array]::IndexOf($MODES, ([string]$T.view).ToLower()))",
			"[Console]::Write($BASE + \"$E[?25l$E[2J$E[1;1H\")",
			"",
			"$script:sb = $null",
			"function Emit([string]$s) { if ($null -ne $script:sb) { [void]$script:sb.Append($s) } else { [Console]::Write($s) } }",
			"function Width { [Math]::Max(40, [Math]::Min($ui.WindowSize.Width, $ui.BufferSize.Width) - 2) }",
			"function Height { [Math]::Max(10, $ui.WindowSize.Height) }",
			"",
			"function Badge([string]$label, [string]$fg, [string]$bg) {",
			"  $text = (' ' + $label).PadRight($labelW)",
			"  return (Bg $bg) + (Fg $fg) + $text + $R",
			"}",
			"",
			"function DrawHeader {",
			"  $w = Width",
			"  $name = ' ' + $title + ' '",
			"  Emit (\"$E\" + '7' + \"$E[1;1H\" + $BASE + \"$E[2K\")",
			"  Emit (' ' + (Bg $T.accent) + (Fg $T.accentText) + $BOLD + $name + $R)",
			"  if ($T.showSession -and $script:session -ne '') { Emit ('  ' + (Fg $T.accentSoft) + $T.labelSession.ToLower() + ' ' + $R + (Fg $T.text) + $script:session + $R) }",
			"  Emit ('   ' + (Fg $T.dim) + 'view ' + $R + (Fg $T.accentSoft) + $BOLD + $MODES[$script:mode] + $R + (Fg $T.faint) + ' V' + $R)",
			"  if ($script:offset -gt 0) { Emit ('   ' + (Bg $T.accent) + (Fg $T.accentText) + ' ' + [char]0x2191 + ' ' + $script:offset + ' ' + $R + ' ' + (Fg $T.accentSoft) + 'End' + (Fg $T.dim) + ' to follow' + $R) }",
			"  Emit (\"$E[2;1H$E[2K\" + ' ' + (Fg $T.dim) + $f + (Fg $T.faint) + '   pid ' + $owner + $R)",
			"  Emit (\"$E[3;1H$E[2K\" + (Fg $T.accent) + ([string][char]0x2500 * $w) + $R)",
			"  Emit (\"$E\" + '8')",
			"}",
			"",
			"function Fit {",
			"  $w = [Math]::Max(20, $ui.WindowSize.Width); $h = [Math]::Max(8, $ui.WindowSize.Height)",
			"  if ($ui.BufferSize.Width -ne $w -or $ui.BufferSize.Height -ne $h) {",
			"    try { $ui.BufferSize = New-Object Management.Automation.Host.Size($w, $h) } catch {",
			"      try { $ui.WindowSize = New-Object Management.Automation.Host.Size([Math]::Min($w, $ui.BufferSize.Width), [Math]::Min($h, $ui.BufferSize.Height)); $ui.BufferSize = New-Object Management.Automation.Host.Size($w, $h) } catch { }",
			"    }",
			"  }",
			"  try { if ($ui.WindowPosition.X -ne 0 -or $ui.WindowPosition.Y -ne 0) { $ui.WindowPosition = New-Object Management.Automation.Host.Coordinates(0, 0) } } catch { }",
			"}",
			"",
			"function Region([bool]$top = $false) {",
			"  if ($T.pinHeader) { Emit (\"$E[4;$(Height)r\") }",
			"  if ($top) { Emit \"$E[4;1H\" } else { Emit (\"$E[$(Height);1H\") }",
			"}",
			"",
			"function Wrap([string]$text, [int]$room) {",
			"  $out = New-Object Collections.Generic.List[string]",
			"  if ($room -lt 20) { $room = 20 }",
			"  while ($text.Length -gt $room) {",
			"    $cut = $text.LastIndexOf(' ', $room)",
			"    if ($cut -lt $room / 2) { $cut = $room }",
			"    $out.Add($text.Substring(0, $cut))",
			"    $text = $text.Substring($cut).TrimStart()",
			"  }",
			"  $out.Add($text)",
			"  return ,$out",
			"}",
			"",
			"function TagColor([string]$name) {",
			"  $h = 0",
			"  foreach ($c in $name.ToLower().ToCharArray()) { $h = ($h * 31 + [int]$c) % 1000003 }",
			"  return $T.tags[$h % $T.tags.Count]",
			"}",
			"",
			"function Entry([string]$time, [string]$lvl, [string]$msg, [bool]$compact) {",
			"  $kind = if ($lvl -match 'render') { 'render' } elseif ($lvl -match 'error|crash|fatal') { 'error' } elseif ($lvl -match 'warn') { 'warn' } elseif ($lvl -match 'info') { 'info' } else { 'log' }",
			"  $badge = switch ($kind) {",
			"    'error' { Badge $T.labelError $T.errorText $T.errorBg }",
			"    'warn' { Badge $T.labelWarn $T.warnText $T.warnBg }",
			"    'info' { Badge $T.labelInfo $T.infoText $T.infoBg }",
			"    'render' { Badge $T.labelRender $T.renderText $T.renderBg }",
			"    default { Badge $T.labelLog $T.logText $T.logBg }",
			"  }",
			"  $color = switch ($kind) { 'error' { Fg $T.error } 'log' { Fg $T.logMessage } default { Fg $T.text } }",
			"  $lead = ''; $leadLen = 0; $loc = ''; $chip = ''; $marks = ''; $markLen = 0",
			"  while ($msg -match '^\\[([A-Za-z][^\\]:]{0,31})\\]\\s?(.*)$') {",
			"    $marks += (Fg $T.dim) + '[' + $BOLD + (Fg (TagColor $matches[1])) + $matches[1] + $R + (Fg $T.dim) + ']' + $R + ' '",
			"    $markLen += $matches[1].Length + 3; $msg = $matches[2]",
			"  }",
			"  if ($msg -match '^\\[([^\\]\\s]+\\.lua):(\\d+)\\]\\s?(.*)$') {",
			"    $chip = $matches[1] + ':' + $matches[2]; $msg = $matches[3]",
			"    if ($msg -match '^(\\w+:\\w+)\\s+-\\s+(.*)$') { $lead = $BOLD + (Fg $T.text) + $matches[1] + $R + (Fg $T.dim) + ' - ' + $R; $leadLen = $matches[1].Length + 3; $msg = $matches[2] }",
			"  }",
			"  elseif ($msg -match '^\\[([^\\]\\s]+:\\d+)\\]\\s?(.*)$') { $chip = $matches[1]; $msg = $matches[2] }",
			"  elseif ($msg -match '^(\\S+\\.hx:\\d+):\\s?(.*)$') { $loc = $matches[1]; $msg = $matches[2] }",
			"  elseif ($msg -match '^runtime error in (\\S+):\\s?(.*)$') { $chip = $matches[1]; $msg = $matches[2] }",
			"  elseif ($msg -match '^error\\[(\\w+)\\]\\s+(\\w+):\\s?(.*)$') { $lead = $BOLD + (Fg $T.error) + $matches[2] + $R + (Fg $T.dim) + ' ' + $matches[1] + ' - ' + $R; $leadLen = $matches[2].Length + $matches[1].Length + 4; $msg = $matches[3] }",
			"  Emit ((Fg $T.dim) + $time + $R + ' ' + $badge + ' ')",
			"  $chipLen = 0",
			"  if ($marks -ne '') { Emit $marks; $chipLen += $markLen }",
			"  if ($chip -ne '') { Emit ((Bg $T.chipBg) + (Fg $T.chip) + '[' + $chip + ']' + $R + ' '); $chipLen += $chip.Length + 3 }",
			"  $room = (Width) - $INDENT - 1 - $chipLen - $leadLen",
			"  if ($compact) {",
			"    if ($room -lt 10) { $room = 10 }",
			"    if ($msg.Length -gt $room) { $msg = $msg.Substring(0, $room - 1) + [char]0x2026 }",
			"    $lines = New-Object Collections.Generic.List[string]; $lines.Add($msg); $loc = ''",
			"  } else { $lines = Wrap $msg $room }",
			"  for ($i = 0; $i -lt $lines.Count; $i++) {",
			"    if ($i -gt 0) { Emit (\"`n\" + (' ' * ($INDENT + $chipLen + $leadLen))) } else { Emit $lead }",
			"    Emit ($color + $lines[$i] + $R)",
			"  }",
			"  if ($loc -ne '') {",
			"    $used = $chipLen + $leadLen + $lines[$lines.Count - 1].Length",
			"    if ($used + $loc.Length + 2 -le (Width) - $INDENT - 1) { Emit ('  ' + (Fg $T.faint) + $loc + $R) } else { Emit (\"`n\" + (' ' * $INDENT) + (Fg $T.faint) + $loc + $R) }",
			"  }",
			"  Emit \"`n\"",
			"  return $kind",
			"}",
			"",
			"function Visible([int]$meta) {",
			"  $kind = [Math]::Floor($meta / 10000); $cont = $meta % 10000",
			"  if ($kind -eq 9) { return $false }",
			"  if ($cont -eq $DUP) { return $MODES[$script:mode] -eq 'verbose' }",
			"  if ($kind -eq 4 -and $MODES[$script:mode] -ne 'verbose') { return $false }",
			"  switch ($MODES[$script:mode]) {",
			"    'verbose' { return $true }",
			"    'compact' { return $cont -eq 0 }",
			"    'warnings' { return ($kind -eq 2 -or $kind -eq 3) -and ($cont -le $FOLD) }",
			"    'errors' { return $kind -eq 3 -and $cont -le $FOLD }",
			"  }",
			"  return $cont -le $FOLD",
			"}",
			"",
			"function Redraw([bool]$full) {",
			"  $script:sb = New-Object Text.StringBuilder 65536",
			"  if ($full) { Emit ($BASE + \"$E[r$E[2J$E[1;1H\"); DrawHeader; Region $true } else { DrawHeader; Emit ($BASE + \"$E[4;1H$E[J\") }",
			"  $room = (Height) - 4",
			"  $vis = New-Object Collections.Generic.List[int]",
			"  $need = $script:offset + $room",
			"  for ($j = $recent.Count - 1; $j -ge 0 -and $vis.Count -lt $need; $j--) { if (Visible $meta[$j]) { $vis.Add($j) } }",
			"  if ($vis.Count -lt $need) { $script:offset = [Math]::Max(0, $vis.Count - $room); DrawHeader }",
			"  $last = [Math]::Min($vis.Count, $script:offset + $room) - 1",
			"  for ($k = $last; $k -ge $script:offset; $k--) { Emit (Cached $vis[$k]) }",
			"  $out = $script:sb.ToString(); $script:sb = $null",
			"  [Console]::Write($out)",
			"}",
			"",
			"function Cached([int]$i) {",
			"  $c = $cache[$i]",
			"  if ($null -eq $c) {",
			"    $outer = $script:sb",
			"    $script:sb = New-Object Text.StringBuilder 512",
			"    Render $recent[$i] $meta[$i]",
			"    $c = $script:sb.ToString()",
			"    $script:sb = $outer",
			"    $cache[$i] = $c",
			"  }",
			"  return $c",
			"}",
			"",
			"function DropCache { for ($i = 0; $i -lt $cache.Count; $i++) { $cache[$i] = $null } }",
			"",
			"function Alive { $null -ne (Get-Process -Id $owner -ErrorAction SilentlyContinue) }",
			"",
			"Fit",
			"$lastW = $ui.WindowSize.Width; $lastH = $ui.WindowSize.Height",
			"DrawHeader",
			"Region $true",
			"",
			"$fs = $null",
			"while ($null -eq $fs) {",
			"  try { $fs = [IO.File]::Open($f, 'Open', 'Read', 'ReadWrite, Delete') } catch { if (-not (Alive)) { exit }; Start-Sleep -Milliseconds 300 }",
			"}",
			"$start = 0",
			"$probe = [Math]::Min($fs.Length, 262144)",
			"if ($probe -gt 0) {",
			"  [void]$fs.Seek(-$probe, 'End')",
			"  $buf = New-Object byte[] $probe",
			"  [void]$fs.Read($buf, 0, $probe)",
			"  $txt = [Text.Encoding]::UTF8.GetString($buf)",
			"  $at = $txt.LastIndexOf(\"`n=====\")",
			"  if ($at -ge 0) { $start = $fs.Length - $probe + [Text.Encoding]::UTF8.GetByteCount($txt.Substring(0, $at + 1)) } else { $start = $fs.Length - $probe }",
			"}",
			"[void]$fs.Seek($start, 'Begin')",
			"$sr = New-Object IO.StreamReader($fs)",
			"$recent = New-Object Collections.Generic.List[string]",
			"$meta = New-Object Collections.Generic.List[int]",
			"$cache = New-Object Collections.Generic.List[object]",
			"$script:resizeAt = $null",
			"$script:broken = $false",
			"$script:session = ''",
			"$script:offset = 0",
			"$script:lastKind = 0",
			"$script:cont = 0",
			"$idle = 0",
			"",
			"function Classify([string]$l) {",
			"  if ($l.Trim() -eq '') { return 90000 }",
			"  if ($l -match '^\\s') { $script:cont++; return $script:lastKind * 10000 + [Math]::Min($script:cont, 9990) }",
			"  if ($l -match '^=+\\s*(.*?)\\s*=+$') {",
			"    $script:session = ($matches[1] -replace '\\s{2,}', '  ')",
			"    DrawHeader",
			"    return 90000",
			"  }",
			"  $script:cont = 0",
			"  if ($l -match '^\\d\\d:\\d\\d:\\d\\d \\[(\\w+)\\s*\\]') {",
			"    $lvl = $matches[1]",
			"    $script:lastKind = if ($lvl -match 'render') { 4 } elseif ($lvl -match 'error|crash|fatal') { 3 } elseif ($lvl -match 'warn') { 2 } elseif ($lvl -match 'info') { 1 } else { 0 }",
			"    $key = $l.Substring(0, 8) + '|' + (($l -replace '^\\d\\d:\\d\\d:\\d\\d \\[\\w+\\s*\\]\\s?', '') -replace '^\\S+\\.hx:\\d+:\\s*', '').Trim()",
			"    $isRepeat = $key -eq $script:lastKey",
			"    $script:lastKey = $key",
			"    if ($isRepeat) { return $script:lastKind * 10000 + $DUP }",
			"  }",
			"  return $script:lastKind * 10000",
			"}",
			"",
			"function Render([string]$l, [int]$m) {",
			"  $kind = [Math]::Floor($m / 10000); $cont = $m % 10000",
			"  $gut = switch ($kind) { 3 { Fg $T.errorBg } 2 { Fg $T.warnBg } default { Fg $T.faint } }",
			"  if ($cont -gt 0 -and $cont -ne $DUP) {",
			"    if ($cont -eq $FOLD -and $MODES[$script:mode] -ne 'verbose') { Emit ((' ' * $INDENT) + $gut + [char]0x2502 + ' ' + (Fg $T.dim) + [char]0x2026 + ' more in verbose (V)' + $R + \"`n\"); return }",
			"    foreach ($part in (Wrap $l.Trim() ((Width) - $INDENT - 3))) { Emit ((' ' * $INDENT) + $gut + [char]0x2502 + ' ' + $part + $R + \"`n\") }",
			"    return",
			"  }",
			"  if ($l -match '^(\\d\\d:\\d\\d:\\d\\d) \\[(\\w+)\\s*\\] ?(.*)$') { [void](Entry $matches[1] $matches[2] $matches[3] ($MODES[$script:mode] -eq 'compact')) }",
			"  else { foreach ($part in (Wrap $l ((Width) - $INDENT))) { Emit ((' ' * $INDENT) + (Fg $T.logMessage) + $part + $R + \"`n\") } }",
			"}",
			"",
			"function Step {",
			"  $ww = $ui.WindowSize.Width; $wh = $ui.WindowSize.Height",
			"  if ($ww -ne $lastW -or $wh -ne $lastH) {",
			"    $script:lastW = $ww; $script:lastH = $wh",
			"    $script:resizeAt = [DateTime]::UtcNow",
			"  } elseif ($null -eq $script:resizeAt -and ($ui.BufferSize.Height -ne $wh -or $ui.BufferSize.Width -ne $ww)) {",
			"    $script:resizeAt = [DateTime]::UtcNow.AddMilliseconds(-150)",
			"  }",
			"  if ($null -ne $script:resizeAt -and ([DateTime]::UtcNow - $script:resizeAt).TotalMilliseconds -ge 150) {",
			"    $script:resizeAt = $null",
			"    Fit",
			"    DropCache",
			"    Redraw $true",
			"    $script:broken = $false",
			"  }",
			"  if ($script:broken) { $script:broken = $false; Fit; DropCache; Redraw $true }",
			"  $d = [LimeLog.Con]::Poll([Math]::Max(1, (Height) - 5))",
			"  if ($d -eq 2000000) {",
			"    $script:mode = ($script:mode + 1) % $MODES.Count",
			"    $script:offset = 0",
			"    DropCache",
			"    Redraw $false",
			"  } elseif ($d -ne 0 -and $null -eq $script:resizeAt) {",
			"    $next = [Math]::Max(0, $script:offset + $d)",
			"    if ($next -ne $script:offset) { $script:offset = $next; Redraw $false }",
			"  }",
			"  $budget = 64",
			"  while ($budget -gt 0) {",
			"    $l = $sr.ReadLine()",
			"    if ($null -eq $l) { return $false }",
			"    $budget--",
			"    $m = Classify $l",
			"    $recent.Add($l); $meta.Add($m); $cache.Add($null)",
			"    if ($recent.Count -gt $T.history) { $recent.RemoveAt(0); $meta.RemoveAt(0); $cache.RemoveAt(0) }",
			"    if (-not (Visible $m)) { continue }",
			"    if ($script:offset -gt 0) { $script:offset++; DrawHeader; continue }",
			"    if ($null -ne $script:resizeAt) { continue }",
			"    Emit (Cached ($recent.Count - 1))",
			"  }",
			"  return $true",
			"}",
			"",
			"while ($true) {",
			"  $busy = $false",
			"  try { $busy = Step } catch { $script:broken = $true; $script:resizeAt = [DateTime]::UtcNow }",
			"  if (-not $busy) {",
			"    Start-Sleep -Milliseconds 12",
			"    $idle++",
			"    if ($idle % 80 -eq 0 -and -not (Alive)) { break }",
			"  }",
			"}"
		].join("\n");
		final quotedTitle = StringTools.replace(title, "'", "''");
		final quotedFile = StringTools.replace(file, "'", "''");
		return StringTools.replace(StringTools.replace(StringTools.replace(StringTools.replace(script, "__THEME__", theme.toPowerShell()), "__OWNER__", Std.string(owner)), "__TITLE__", quotedTitle), "__FILE__", quotedFile);
	}

	static function scriptFile(owner:Int):String
	{
		#if sys
		var dir = Sys.getEnv("TEMP");
		if (dir == null || dir == "") dir = Sys.getEnv("TMP");
		if (dir == null || dir == "") return null;
		return haxe.io.Path.join([dir, "lime-log-console-" + owner + ".ps1"]);
		#else
		return null;
		#end
	}
}
