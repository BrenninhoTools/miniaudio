package miniaudio;

import haxe.io.Bytes;

class MiniAudio
{
	public static var available(get, never):Bool;
	public static var ready(get, never):Bool;
	public static var version(get, never):String;
	public static var backend(get, never):String;
	public static var device(get, never):String;
	public static var masterVolume(get, set):Float;
	public static var suspended(get, never):Bool;
	public static var time(get, never):Float;
	public static var latency(get, never):Float;
	public static var peak(get, never):Float;
	public static var soundCount(get, never):Int;
	public static var autoSuspend:Bool = true;
	public static var autoUpdate:Bool = true;

	static var hooked:Bool = false;
	static var focusSuspended:Bool = false;

	public static function init(sampleRate:Int = 0, periodMilliseconds:Int = 0, lowLatency:Bool = true):Bool
	{
		#if cpp
		var result:Bool = MiniAudioNative.engineInitEx(sampleRate, periodMilliseconds, lowLatency);
		if (result) hook();
		return result;
		#else
		return false;
		#end
	}

	public static function shutdown():Void
	{
		#if cpp
		MiniAudioNative.engineShutdown();
		#end
	}

	public static function suspend():Bool
	{
		#if cpp
		return MiniAudioNative.engineSuspend();
		#else
		return false;
		#end
	}

	public static function resume():Bool
	{
		#if cpp
		return MiniAudioNative.engineResume();
		#else
		return false;
		#end
	}

	public static function update():Void
	{
		#if cpp
		if (MiniAudioNative.engineReady()) MiniAudioNative.engineUpdate();
		#end
	}

	public static function sampleRate():Int
	{
		#if cpp
		return MiniAudioNative.engineSampleRate();
		#else
		return 0;
		#end
	}

	public static function channels():Int
	{
		#if cpp
		return MiniAudioNative.engineChannels();
		#else
		return 0;
		#end
	}

	public static function bufferPeak(bytes:Bytes, offset:Int, length:Int, bitsPerSample:Int, channels:Int, startFrame:Int, frameCount:Int, channel:Int = -1):Float
	{
		if (bytes == null || length <= 0 || channels <= 0 || frameCount <= 0) return 0;
		if (offset < 0 || offset + length > bytes.length) return 0;
		#if cpp
		var pointer = cpp.Pointer.arrayElem(bytes.getData(), offset).constRaw;
		return MiniAudioNative.bufferPeak(pointer, length, bitsPerSample, channels, startFrame, frameCount, channel);
		#else
		return softwarePeak(bytes, offset, length, bitsPerSample, channels, startFrame, frameCount, channel);
		#end
	}

	public static function softwarePeak(bytes:Bytes, offset:Int, length:Int, bitsPerSample:Int, channels:Int, startFrame:Int, frameCount:Int, channel:Int = -1):Float
	{
		var bytesPerSample:Int = Std.int(bitsPerSample / 8);
		if (bytesPerSample <= 0) return 0;
		var totalFrames:Int = Std.int(length / (bytesPerSample * channels));
		var start:Int = startFrame < 0 ? 0 : startFrame;
		var end:Int = start + frameCount > totalFrames ? totalFrames : start + frameCount;
		var step:Int = Std.int(Math.max(1, (end - start) / 256));
		var peak:Float = 0;
		var frame:Int = start;
		while (frame < end)
		{
			for (c in 0...channels)
			{
				if (channel >= 0 && c != channel) continue;
				var position:Int = offset + (frame * channels + c) * bytesPerSample;
				var value:Float = switch (bytesPerSample)
				{
					case 1: (bytes.get(position) - 128) / 128;
					case 2:
						var raw:Int = bytes.getUInt16(position);
						(raw > 32767 ? raw - 65536 : raw) / 32768;
					case 3:
						var raw:Int = bytes.get(position) | (bytes.get(position + 1) << 8) | (bytes.get(position + 2) << 16);
						(raw > 8388607 ? raw - 16777216 : raw) / 8388608;
					case 4: bytes.getFloat(position);
					default: 0;
				}
				if (value < 0) value = -value;
				if (value > peak) peak = value;
			}
			frame += step;
		}
		return peak > 1 ? 1 : peak;
	}

	public static function focusLost():Void
	{
		if (!autoSuspend || suspended) return;
		focusSuspended = suspend();
	}

	public static function focusGained():Void
	{
		if (!focusSuspended) return;
		focusSuspended = false;
		resume();
	}

	static function hook():Void
	{
		if (hooked) return;
		hooked = true;
		#if lime
		var app:lime.app.Application = lime.app.Application.current;
		if (app == null) return;
		app.onUpdate.add(function(_)
		{
			if (autoUpdate) update();
		});
		var window:lime.ui.Window = app.window;
		if (window != null)
		{
			window.onFocusOut.add(focusLost);
			window.onFocusIn.add(focusGained);
		}
		#end
	}

	static function get_available():Bool
	{
		#if cpp
		return true;
		#else
		return false;
		#end
	}

	static function get_ready():Bool
	{
		#if cpp
		return MiniAudioNative.engineReady();
		#else
		return false;
		#end
	}

	static function get_version():String
	{
		#if cpp
		return untyped __cpp__('::String::create({0})', MiniAudioNative.version());
		#else
		return 'unavailable';
		#end
	}

	static function get_backend():String
	{
		#if cpp
		return untyped __cpp__('::String::create({0})', MiniAudioNative.backendName());
		#else
		return 'none';
		#end
	}

	static function get_device():String
	{
		#if cpp
		return untyped __cpp__('::String::create({0})', MiniAudioNative.deviceName());
		#else
		return '';
		#end
	}

	static function get_masterVolume():Float
	{
		#if cpp
		return MiniAudioNative.getMasterVolume();
		#else
		return 0;
		#end
	}

	static function set_masterVolume(value:Float):Float
	{
		#if cpp
		MiniAudioNative.setMasterVolume(value);
		#end
		return value;
	}

	static function get_suspended():Bool
	{
		#if cpp
		return MiniAudioNative.engineSuspended();
		#else
		return false;
		#end
	}

	static function get_time():Float
	{
		#if cpp
		return MiniAudioNative.engineTime();
		#else
		return 0;
		#end
	}

	static function get_latency():Float
	{
		#if cpp
		return MiniAudioNative.engineLatency();
		#else
		return 0;
		#end
	}

	static function get_peak():Float
	{
		#if cpp
		return MiniAudioNative.enginePeak();
		#else
		return 0;
		#end
	}

	static function get_soundCount():Int
	{
		#if cpp
		return MiniAudioNative.soundCount();
		#else
		return 0;
		#end
	}
}
