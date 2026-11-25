package miniaudio;

class MiniAudioEffects
{
	public var target(default, null):Int;
	public var lowPass(default, set):Float = 0;
	public var highPass(default, set):Float = 0;
	public var filterOrder(default, set):Int = 2;
	public var gain(default, set):Float = 1;
	public var echoDelay(default, null):Float = 0;
	public var echoFeedback(default, null):Float = 0;
	public var echoWet(default, null):Float = 0;

	@:allow(miniaudio.MiniAudio)
	@:allow(miniaudio.MiniAudioGroup)
	function new(target:Int)
	{
		this.target = target;
	}

	public function setEcho(delayMilliseconds:Float, feedback:Float = 0.35, wet:Float = 0.4):Bool
	{
		echoDelay = delayMilliseconds;
		echoFeedback = feedback;
		echoWet = wet;
		#if cpp
		return MiniAudioNative.effectSetEcho(target, delayMilliseconds, feedback, wet);
		#else
		return false;
		#end
	}

	public inline function clearEcho():Bool
	{
		return setEcho(0, 0, 0);
	}

	public function muffle(amount:Float):Void
	{
		var clamped:Float = amount < 0 ? 0 : (amount > 1 ? 1 : amount);
		lowPass = clamped <= 0 ? 0 : 20000 * Math.pow(0.0075, clamped);
	}

	public function clear():Void
	{
		@:bypassAccessor lowPass = 0;
		@:bypassAccessor highPass = 0;
		@:bypassAccessor gain = 1;
		echoDelay = 0;
		echoFeedback = 0;
		echoWet = 0;
		#if cpp
		MiniAudioNative.effectClear(target);
		#end
	}

	function set_lowPass(value:Float):Float
	{
		lowPass = value;
		#if cpp
		MiniAudioNative.effectSetLowPass(target, value, filterOrder);
		#end
		return value;
	}

	function set_highPass(value:Float):Float
	{
		highPass = value;
		#if cpp
		MiniAudioNative.effectSetHighPass(target, value, filterOrder);
		#end
		return value;
	}

	function set_filterOrder(value:Int):Int
	{
		filterOrder = value < 1 ? 1 : (value > 8 ? 8 : value);
		if (lowPass > 0) set_lowPass(lowPass);
		if (highPass > 0) set_highPass(highPass);
		return filterOrder;
	}

	function set_gain(value:Float):Float
	{
		gain = value < 0 ? 0 : value;
		#if cpp
		MiniAudioNative.effectSetGain(target, gain);
		#end
		return gain;
	}
}
