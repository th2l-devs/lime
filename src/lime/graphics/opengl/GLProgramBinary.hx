package lime.graphics.opengl;

import haxe.io.Bytes;
#if (lime_cffi && !macro)
import lime._internal.backend.native.NativeCFFI;
#end

@:access(lime._internal.backend.native.NativeCFFI)
@:access(lime.graphics.opengl.GLObject)
class GLProgramBinary
{
	public static function ready():Bool
	{
		#if (lime_cffi && (lime_opengl || lime_opengles) && !macro)
		return NativeCFFI.lime_gl_program_binary_ready(0);
		#else
		return false;
		#end
	}

	public static function allowRead(program:GLProgram):Void
	{
		#if (lime_cffi && (lime_opengl || lime_opengles) && !macro)
		NativeCFFI.lime_gl_program_binary_allow_read(program.id);
		#end
	}

	public static function read(program:GLProgram, into:Bytes):Int
	{
		#if (lime_cffi && (lime_opengl || lime_opengles) && !macro)
		return NativeCFFI.lime_gl_program_binary_read(program.id, into);
		#else
		return 0;
		#end
	}

	public static function write(program:GLProgram, format:Int, bytes:Bytes):Bool
	{
		#if (lime_cffi && (lime_opengl || lime_opengles) && !macro)
		return NativeCFFI.lime_gl_program_binary_write(program.id, format, bytes);
		#else
		return false;
		#end
	}
}
