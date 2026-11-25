#pragma once

namespace hxminiaudio
{
	const char* version();
	const char* backendName();
	const char* deviceName();

	bool engineInit();
	bool engineInitEx(int sampleRate, int periodMilliseconds, bool lowLatency);
	void engineShutdown();
	bool engineReady();
	bool engineSuspend();
	bool engineResume();
	bool engineSuspended();
	void engineUpdate();
	void setMasterVolume(float volume);
	float getMasterVolume();
	int engineSampleRate();
	int engineChannels();
	double engineTime();
	float engineLatency();
	float enginePeak();

	int groupCreate(int parent);
	void groupRelease(int id);
	void groupSetVolume(int id, float volume);
	float groupGetVolume(int id);
	void groupSetPitch(int id, float pitch);
	void groupSetPan(int id, float pan);
	bool groupStart(int id);
	bool groupStop(int id);

	int soundLoadFile(const char* path, bool stream);
	int soundLoadFileEx(const char* path, bool stream, int group);
	int soundLoadMemory(const unsigned char* data, int size);
	int soundLoadMemoryEx(const unsigned char* data, int size, int group);
	int soundClone(int id, int group);
	void soundRelease(int id);
	int soundCount();
	bool soundPlay(int id);
	bool soundPlayDelayed(int id, double delayMilliseconds);
	bool soundStop(int id);
	bool soundStopFade(int id, double milliseconds);
	bool soundIsPlaying(int id);
	void soundFade(int id, float from, float to, double milliseconds);
	void soundSetVolume(int id, float volume);
	float soundGetVolume(int id);
	void soundSetPan(int id, float pan);
	void soundSetPitch(int id, float pitch);
	float soundGetPan(int id);
	float soundGetPitch(int id);
	bool soundSetLoopPoints(int id, double startMilliseconds, double endMilliseconds);
	void soundSetLooping(int id, bool looping);
	bool soundIsLooping(int id);
	bool soundAtEnd(int id);
	void soundSeek(int id, double milliseconds);
	double soundGetTime(int id);
	double soundGetLength(int id);
	float soundGetPeak(int id);
	float soundGetRms(int id);
	int soundSampleRate(int id);
	int soundChannels(int id);

	int soundLoadMemoryDecoded(const unsigned char* data, int size, int group);
	int soundPlayOneShot(int id, float volume, float pitch, float pan, int group);
	void soundSetMaxVoices(int id, int voices);
	int soundVoices(int id);
	void soundStopVoices(int id);

	bool effectSetLowPass(int groupId, double cutoff, int order);
	bool effectSetHighPass(int groupId, double cutoff, int order);
	bool effectSetEcho(int groupId, double delayMilliseconds, float feedback, float wet);
	void effectSetGain(int groupId, float gain);
	void effectClear(int groupId);

	int deviceCount();
	const char* deviceNameAt(int index);
	bool deviceIsDefault(int index);

	float bufferPeak(const unsigned char* data, int byteLength, int bitsPerSample, int channels, int startFrame, int frameCount, int channel);
}
