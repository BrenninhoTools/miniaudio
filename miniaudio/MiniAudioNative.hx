package miniaudio;

#if cpp
@:buildXml('<include name="${haxelib:miniaudio}/project/Build.xml"/>')
@:include('hxminiaudio.h')
extern class MiniAudioNative
{
	@:native('::hxminiaudio::version') static function version():cpp.ConstCharStar;
	@:native('::hxminiaudio::backendName') static function backendName():cpp.ConstCharStar;
	@:native('::hxminiaudio::deviceName') static function deviceName():cpp.ConstCharStar;
	@:native('::hxminiaudio::engineInit') static function engineInit():Bool;
	@:native('::hxminiaudio::engineInitEx') static function engineInitEx(sampleRate:Int, periodMilliseconds:Int, lowLatency:Bool):Bool;
	@:native('::hxminiaudio::engineShutdown') static function engineShutdown():Void;
	@:native('::hxminiaudio::engineReady') static function engineReady():Bool;
	@:native('::hxminiaudio::engineSuspend') static function engineSuspend():Bool;
	@:native('::hxminiaudio::engineResume') static function engineResume():Bool;
	@:native('::hxminiaudio::engineSuspended') static function engineSuspended():Bool;
	@:native('::hxminiaudio::engineUpdate') static function engineUpdate():Void;
	@:native('::hxminiaudio::setMasterVolume') static function setMasterVolume(volume:cpp.Float32):Void;
	@:native('::hxminiaudio::getMasterVolume') static function getMasterVolume():cpp.Float32;
	@:native('::hxminiaudio::engineSampleRate') static function engineSampleRate():Int;
	@:native('::hxminiaudio::engineChannels') static function engineChannels():Int;
	@:native('::hxminiaudio::engineTime') static function engineTime():Float;
	@:native('::hxminiaudio::engineLatency') static function engineLatency():cpp.Float32;
	@:native('::hxminiaudio::enginePeak') static function enginePeak():cpp.Float32;
	@:native('::hxminiaudio::groupCreate') static function groupCreate(parent:Int):Int;
	@:native('::hxminiaudio::groupRelease') static function groupRelease(id:Int):Void;
	@:native('::hxminiaudio::groupSetVolume') static function groupSetVolume(id:Int, volume:cpp.Float32):Void;
	@:native('::hxminiaudio::groupGetVolume') static function groupGetVolume(id:Int):cpp.Float32;
	@:native('::hxminiaudio::groupSetPitch') static function groupSetPitch(id:Int, pitch:cpp.Float32):Void;
	@:native('::hxminiaudio::groupSetPan') static function groupSetPan(id:Int, pan:cpp.Float32):Void;
	@:native('::hxminiaudio::groupStart') static function groupStart(id:Int):Bool;
	@:native('::hxminiaudio::groupStop') static function groupStop(id:Int):Bool;
	@:native('::hxminiaudio::soundLoadFileEx') static function soundLoadFileEx(path:cpp.ConstCharStar, stream:Bool, group:Int):Int;
	@:native('::hxminiaudio::soundLoadMemoryEx') static function soundLoadMemoryEx(data:cpp.RawConstPointer<cpp.UInt8>, size:Int, group:Int):Int;
	@:native('::hxminiaudio::soundClone') static function soundClone(id:Int, group:Int):Int;
	@:native('::hxminiaudio::soundRelease') static function soundRelease(id:Int):Void;
	@:native('::hxminiaudio::soundCount') static function soundCount():Int;
	@:native('::hxminiaudio::soundPlay') static function soundPlay(id:Int):Bool;
	@:native('::hxminiaudio::soundPlayDelayed') static function soundPlayDelayed(id:Int, delayMilliseconds:Float):Bool;
	@:native('::hxminiaudio::soundStop') static function soundStop(id:Int):Bool;
	@:native('::hxminiaudio::soundStopFade') static function soundStopFade(id:Int, milliseconds:Float):Bool;
	@:native('::hxminiaudio::soundIsPlaying') static function soundIsPlaying(id:Int):Bool;
	@:native('::hxminiaudio::soundFade') static function soundFade(id:Int, from:cpp.Float32, to:cpp.Float32, milliseconds:Float):Void;
	@:native('::hxminiaudio::soundSetVolume') static function soundSetVolume(id:Int, volume:cpp.Float32):Void;
	@:native('::hxminiaudio::soundGetVolume') static function soundGetVolume(id:Int):cpp.Float32;
	@:native('::hxminiaudio::soundSetPan') static function soundSetPan(id:Int, pan:cpp.Float32):Void;
	@:native('::hxminiaudio::soundSetPitch') static function soundSetPitch(id:Int, pitch:cpp.Float32):Void;
	@:native('::hxminiaudio::soundGetPan') static function soundGetPan(id:Int):cpp.Float32;
	@:native('::hxminiaudio::soundGetPitch') static function soundGetPitch(id:Int):cpp.Float32;
	@:native('::hxminiaudio::soundSetLoopPoints') static function soundSetLoopPoints(id:Int, startMilliseconds:Float, endMilliseconds:Float):Bool;
	@:native('::hxminiaudio::soundSetLooping') static function soundSetLooping(id:Int, looping:Bool):Void;
	@:native('::hxminiaudio::soundIsLooping') static function soundIsLooping(id:Int):Bool;
	@:native('::hxminiaudio::soundAtEnd') static function soundAtEnd(id:Int):Bool;
	@:native('::hxminiaudio::soundSeek') static function soundSeek(id:Int, milliseconds:Float):Void;
	@:native('::hxminiaudio::soundGetTime') static function soundGetTime(id:Int):Float;
	@:native('::hxminiaudio::soundGetLength') static function soundGetLength(id:Int):Float;
	@:native('::hxminiaudio::soundGetPeak') static function soundGetPeak(id:Int):cpp.Float32;
	@:native('::hxminiaudio::soundGetRms') static function soundGetRms(id:Int):cpp.Float32;
	@:native('::hxminiaudio::soundSampleRate') static function soundSampleRate(id:Int):Int;
	@:native('::hxminiaudio::soundChannels') static function soundChannels(id:Int):Int;
	@:native('::hxminiaudio::soundLoadMemoryDecoded') static function soundLoadMemoryDecoded(data:cpp.RawConstPointer<cpp.UInt8>, size:Int, group:Int):Int;
	@:native('::hxminiaudio::soundPlayOneShot') static function soundPlayOneShot(id:Int, volume:cpp.Float32, pitch:cpp.Float32, pan:cpp.Float32, group:Int):Int;
	@:native('::hxminiaudio::soundSetMaxVoices') static function soundSetMaxVoices(id:Int, voices:Int):Void;
	@:native('::hxminiaudio::soundVoices') static function soundVoices(id:Int):Int;
	@:native('::hxminiaudio::soundStopVoices') static function soundStopVoices(id:Int):Void;
	@:native('::hxminiaudio::effectSetLowPass') static function effectSetLowPass(group:Int, cutoff:Float, order:Int):Bool;
	@:native('::hxminiaudio::effectSetHighPass') static function effectSetHighPass(group:Int, cutoff:Float, order:Int):Bool;
	@:native('::hxminiaudio::effectSetEcho') static function effectSetEcho(group:Int, delayMilliseconds:Float, feedback:cpp.Float32, wet:cpp.Float32):Bool;
	@:native('::hxminiaudio::effectSetGain') static function effectSetGain(group:Int, gain:cpp.Float32):Void;
	@:native('::hxminiaudio::effectClear') static function effectClear(group:Int):Void;
	@:native('::hxminiaudio::deviceCount') static function deviceCount():Int;
	@:native('::hxminiaudio::deviceNameAt') static function deviceNameAt(index:Int):cpp.ConstCharStar;
	@:native('::hxminiaudio::deviceIsDefault') static function deviceIsDefault(index:Int):Bool;
	@:native('::hxminiaudio::bufferPeak') static function bufferPeak(data:cpp.RawConstPointer<cpp.UInt8>, byteLength:Int, bitsPerSample:Int, channels:Int, startFrame:Int, frameCount:Int, channel:Int):cpp.Float32;
}
#end
