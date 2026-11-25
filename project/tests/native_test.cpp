#include "hxminiaudio.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static int failures = 0;

static void expect(bool condition, const char* label)
{
	if (!condition)
	{
		std::printf("FAIL: %s\n", label);
		failures++;
	}
}

static void put32(std::vector<unsigned char>& out, uint32_t value)
{
	for (int i = 0; i < 4; i++) out.push_back((value >> (i * 8)) & 0xFF);
}

static void put16(std::vector<unsigned char>& out, uint16_t value)
{
	out.push_back(value & 0xFF);
	out.push_back((value >> 8) & 0xFF);
}

static std::vector<unsigned char> makeWav(int rate, int frames)
{
	std::vector<unsigned char> wav;
	const char* riff = "RIFF";
	wav.insert(wav.end(), riff, riff + 4);
	put32(wav, 36 + frames * 4);
	const char* wave = "WAVEfmt ";
	wav.insert(wav.end(), wave, wave + 8);
	put32(wav, 16);
	put16(wav, 1);
	put16(wav, 2);
	put32(wav, rate);
	put32(wav, rate * 4);
	put16(wav, 4);
	put16(wav, 16);
	const char* data = "data";
	wav.insert(wav.end(), data, data + 4);
	put32(wav, frames * 4);
	for (int i = 0; i < frames; i++)
	{
		int16_t sample = (int16_t)(std::sin(i * 0.05) * 12000);
		put16(wav, (uint16_t)sample);
		put16(wav, (uint16_t)sample);
	}
	return wav;
}

static void wait(int milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int main()
{
	expect(hxminiaudio::engineInitEx(48000, 10, true), "engine starts");
	int rate = hxminiaudio::engineSampleRate();
	expect(rate > 0, "sample rate");

	std::vector<unsigned char> wav = makeWav(22050, 2205);
	int group = hxminiaudio::groupCreate(-1);
	expect(group >= 0, "group created");
	int child = hxminiaudio::groupCreate(group);
	expect(child >= 0, "child group created");

	int hit = hxminiaudio::soundLoadMemoryDecoded(wav.data(), (int)wav.size(), child);
	expect(hit >= 0, "decoded sound loaded");
	expect(std::fabs(hxminiaudio::soundGetLength(hit) - 100.0) < 2.0, "decoded length is 100 ms");

	hxminiaudio::soundSetMaxVoices(hit, 3);
	for (int i = 0; i < 6; i++) expect(hxminiaudio::soundPlayOneShot(hit, 0.8f, 1.0f + i * 0.1f, 0.0f, -1) >= 0, "one shot plays");
	expect(hxminiaudio::soundVoices(hit) == 3, "voice limit keeps 3 voices");

	expect(hxminiaudio::effectSetLowPass(group, 800, 4), "low pass on group");
	expect(hxminiaudio::effectSetLowPass(group, 1200, 4), "low pass retune");
	expect(hxminiaudio::effectSetHighPass(-1, 120, 2), "high pass on master");
	expect(hxminiaudio::effectSetEcho(child, 120, 0.4f, 0.5f), "echo on child group");
	hxminiaudio::effectSetGain(group, 0.9f);

	wait(400);
	hxminiaudio::engineUpdate();
	expect(hxminiaudio::soundVoices(hit) == 0, "finished one shots are released");

	int loop = hxminiaudio::soundLoadMemory(wav.data(), (int)wav.size());
	expect(loop >= 0, "streamed memory sound loaded");
	hxminiaudio::soundPlayOneShot(loop, 1.0f, 1.0f, 0.0f, group);
	expect(hxminiaudio::soundVoices(loop) == 1, "one shot from a streamed sound");
	hxminiaudio::soundRelease(loop);
	expect(hxminiaudio::soundCount() == 1, "releasing the source releases its voices");

	hxminiaudio::effectClear(group);
	hxminiaudio::groupRelease(group);
	expect(hxminiaudio::soundPlayOneShot(hit, 1.0f, 1.0f, 0.0f, -1) >= 0, "plays after parent group release");
	wait(50);
	expect(hxminiaudio::enginePeak() >= 0.0f, "master peak");
	expect(hxminiaudio::deviceCount() >= 1, "devices listed");
	expect(std::strlen(hxminiaudio::deviceNameAt(0)) > 0, "device name");

	hxminiaudio::soundRelease(hit);
	hxminiaudio::engineShutdown();

	if (failures == 0) std::printf("ok\n");
	return failures == 0 ? 0 : 1;
}
