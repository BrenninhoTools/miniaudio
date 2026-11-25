package miniaudio;

class MiniAudioGroup
{
	public var id(default, null):Int = -1;
	public var valid(get, never):Bool;
	public var volume(get, set):Float;
	public var pitch(default, set):Float = 1;
	public var pan(default, set):Float = 0;
	public var effects(default, null):MiniAudioEffects;

	var fadeFrom:Float = 0;
	var fadeTo:Float = 0;
	var fadeStart:Float = -1;
	var fadeDuration:Float = 0;
	var fadeDone:Void->Void;

	function new(id:Int)
	{
		this.id = id;
		effects = new MiniAudioEffects(id);
	}

	public function fade(to:Float, milliseconds:Float, ?onComplete:Void->Void):MiniAudioGroup
	{
		if (!valid) return this;
		if (milliseconds <= 0)
		{
			volume = to;
			fadeStart = -1;
			if (onComplete != null) onComplete();
			return this;
		}
		fadeFrom = volume;
		fadeTo = to;
		fadeDuration = milliseconds;
		fadeStart = haxe.Timer.stamp() * 1000;
		fadeDone = onComplete;
		MiniAudio.track(this);
		return this;
	}

	public function duck(volume:Float = 0.3, milliseconds:Float = 150):MiniAudioGroup
	{
		return fade(volume, milliseconds);
	}

	public inline function unduck(milliseconds:Float = 300):MiniAudioGroup
	{
		return fade(1, milliseconds);
	}

	@:allow(miniaudio.MiniAudio)
	function updateFade(now:Float):Bool
	{
		if (fadeStart < 0 || !valid) return false;
		var t:Float = (now - fadeStart) / fadeDuration;
		if (t >= 1)
		{
			volume = fadeTo;
			fadeStart = -1;
			var done:Void->Void = fadeDone;
			fadeDone = null;
			if (done != null) done();
			return false;
		}
		volume = fadeFrom + (fadeTo - fadeFrom) * t;
		return true;
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
		fadeStart = -1;
		fadeDone = null;
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
