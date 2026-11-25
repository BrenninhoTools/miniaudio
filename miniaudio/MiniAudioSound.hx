package miniaudio;

import haxe.io.Bytes;

class MiniAudioSound
{
	public var id(default, null):Int = -1;
	public var valid(get, never):Bool;
	public var playing(get, never):Bool;
	public var volume(get, set):Float;
	public var pan(default, set):Float = 0;
	public var pitch(default, set):Float = 1;
	public var looping(get, set):Bool;
	public var time(get, set):Float;
	public var length(get, never):Float;
	public var peak(get, never):Float;
	public var finished(get, never):Bool;
	public var rms(get, never):Float;
	public var sampleRate(get, never):Int;
	public var channels(get, never):Int;
	public var group(default, null):MiniAudioGroup;

	function new(id:Int)
	{
		this.id = id;
	}

	public static function fromFile(path:String, stream:Bool = false, ?group:MiniAudioGroup):MiniAudioSound
	{
		#if cpp
		if (path == null || !MiniAudio.init()) return null;
		var id:Int = MiniAudioNative.soundLoadFileEx(untyped __cpp__('{0}.utf8_str()', path), stream, group != null ? group.id : -1);
		return wrap(id, group);
		#else
		return null;
		#end
	}

	public static function fromBytes(bytes:Bytes, ?group:MiniAudioGroup):MiniAudioSound
	{
		#if cpp
		if (bytes == null || bytes.length < 1 || !MiniAudio.init()) return null;
		var id:Int = MiniAudioNative.soundLoadMemoryEx(cpp.Pointer.arrayElem(bytes.getData(), 0).constRaw, bytes.length, group != null ? group.id : -1);
		return wrap(id, group);
		#else
		return null;
		#end
	}

	static function wrap(id:Int, group:MiniAudioGroup):MiniAudioSound
	{
		if (id < 0) return null;
		var sound:MiniAudioSound = new MiniAudioSound(id);
		sound.group = group;
		return sound;
	}

	public function clone(?group:MiniAudioGroup):MiniAudioSound
	{
		#if cpp
		if (!valid) return null;
		var target:MiniAudioGroup = group != null ? group : this.group;
		return wrap(MiniAudioNative.soundClone(id, target != null ? target.id : -1), target);
		#else
		return null;
		#end
	}

	public static function fromAsset(key:String, stream:Bool = false, ?group:MiniAudioGroup):MiniAudioSound
	{
		#if sys
		if (sys.FileSystem.exists(key)) return stream ? fromFile(key, true, group) : fromBytes(sys.io.File.getBytes(key), group);
		#end
		#if openfl
		if (openfl.utils.Assets.exists(key)) return fromBytes(openfl.utils.Assets.getBytes(key), group);
		#elseif lime
		if (lime.utils.Assets.exists(key)) return fromBytes(lime.utils.Assets.getBytes(key), group);
		#end
		return null;
	}

	public function setLoopPoints(start:Float, end:Float = -1):Bool
	{
		#if cpp
		return valid && MiniAudioNative.soundSetLoopPoints(id, start, end);
		#else
		return false;
		#end
	}

	public function play():MiniAudioSound
	{
		#if cpp
		if (valid) MiniAudioNative.soundPlay(id);
		#end
		return this;
	}

	public function playDelayed(milliseconds:Float):MiniAudioSound
	{
		#if cpp
		if (valid) MiniAudioNative.soundPlayDelayed(id, milliseconds);
		#end
		return this;
	}

	public function fade(from:Float, to:Float, milliseconds:Float):MiniAudioSound
	{
		#if cpp
		if (valid) MiniAudioNative.soundFade(id, from, to, milliseconds);
		#end
		return this;
	}

	public function fadeIn(milliseconds:Float, to:Float = 1):MiniAudioSound
	{
		play();
		return fade(0, to, milliseconds);
	}

	public function fadeOut(milliseconds:Float):MiniAudioSound
	{
		#if cpp
		if (valid) MiniAudioNative.soundStopFade(id, milliseconds);
		#end
		return this;
	}

	public function stop():MiniAudioSound
	{
		#if cpp
		if (valid) MiniAudioNative.soundStop(id);
		#end
		return this;
	}

	public function destroy():Void
	{
		#if cpp
		if (valid) MiniAudioNative.soundRelease(id);
		#end
		id = -1;
		group = null;
	}

	inline function get_valid():Bool
		return id > -1;

	function get_playing():Bool
	{
		#if cpp
		return valid && MiniAudioNative.soundIsPlaying(id);
		#else
		return false;
		#end
	}

	function get_volume():Float
	{
		#if cpp
		return valid ? MiniAudioNative.soundGetVolume(id) : 0;
		#else
		return 0;
		#end
	}

	function set_volume(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.soundSetVolume(id, value);
		#end
		return value;
	}

	function set_pan(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.soundSetPan(id, value);
		#end
		return pan = value;
	}

	function set_pitch(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.soundSetPitch(id, value);
		#end
		return pitch = value;
	}

	function get_looping():Bool
	{
		#if cpp
		return valid && MiniAudioNative.soundIsLooping(id);
		#else
		return false;
		#end
	}

	function set_looping(value:Bool):Bool
	{
		#if cpp
		if (valid) MiniAudioNative.soundSetLooping(id, value);
		#end
		return value;
	}

	function get_time():Float
	{
		#if cpp
		return valid ? MiniAudioNative.soundGetTime(id) : 0;
		#else
		return 0;
		#end
	}

	function set_time(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.soundSeek(id, value);
		#end
		return value;
	}

	function get_length():Float
	{
		#if cpp
		return valid ? MiniAudioNative.soundGetLength(id) : 0;
		#else
		return 0;
		#end
	}

	function get_peak():Float
	{
		#if cpp
		return valid ? MiniAudioNative.soundGetPeak(id) : 0;
		#else
		return 0;
		#end
	}

	function get_rms():Float
	{
		#if cpp
		return valid ? MiniAudioNative.soundGetRms(id) : 0;
		#else
		return 0;
		#end
	}

	function get_sampleRate():Int
	{
		#if cpp
		return valid ? MiniAudioNative.soundSampleRate(id) : 0;
		#else
		return 0;
		#end
	}

	function get_channels():Int
	{
		#if cpp
		return valid ? MiniAudioNative.soundChannels(id) : 0;
		#else
		return 0;
		#end
	}

	function get_finished():Bool
	{
		#if cpp
		return !valid || MiniAudioNative.soundAtEnd(id);
		#else
		return true;
		#end
	}
}
