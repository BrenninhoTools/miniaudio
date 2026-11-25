package miniaudio;

class MiniAudioGroup
{
	public var id(default, null):Int = -1;
	public var valid(get, never):Bool;
	public var volume(get, set):Float;
	public var pitch(default, set):Float = 1;
	public var pan(default, set):Float = 0;

	function new(id:Int)
	{
		this.id = id;
	}

	public static function create(?parent:MiniAudioGroup):MiniAudioGroup
	{
		#if cpp
		if (!MiniAudio.ready && !MiniAudio.init()) return null;
		var handle:Int = MiniAudioNative.groupCreate(parent != null ? parent.id : -1);
		return handle < 0 ? null : new MiniAudioGroup(handle);
		#else
		return null;
		#end
	}

	public function start():Bool
	{
		#if cpp
		return valid && MiniAudioNative.groupStart(id);
		#else
		return false;
		#end
	}

	public function stop():Bool
	{
		#if cpp
		return valid && MiniAudioNative.groupStop(id);
		#else
		return false;
		#end
	}

	public function destroy():Void
	{
		#if cpp
		if (valid) MiniAudioNative.groupRelease(id);
		#end
		id = -1;
	}

	inline function get_valid():Bool
		return id > -1;

	function get_volume():Float
	{
		#if cpp
		return valid ? MiniAudioNative.groupGetVolume(id) : 0;
		#else
		return 0;
		#end
	}

	function set_volume(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.groupSetVolume(id, value);
		#end
		return value;
	}

	function set_pitch(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.groupSetPitch(id, value);
		#end
		return pitch = value;
	}

	function set_pan(value:Float):Float
	{
		#if cpp
		if (valid) MiniAudioNative.groupSetPan(id, value);
		#end
		return pan = value;
	}
}
