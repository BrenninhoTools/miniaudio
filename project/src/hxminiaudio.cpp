#define MINIAUDIO_IMPLEMENTATION
#define MA_API static
#define ma_atomic_global_lock hxminiaudio_ma_atomic_global_lock
#define MA_NO_GENERATION
#if defined(__APPLE__)
#define MA_NO_RUNTIME_LINKING
#endif
#include "miniaudio.h"
#include "hxminiaudio.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
	const int INDEX_BITS = 16;
	const int INDEX_MASK = (1 << INDEX_BITS) - 1;
	const int GENERATION_MASK = 0x7FFF;

	struct MeterNode
	{
		ma_node_base base;
		std::atomic<float> peak;
		std::atomic<float> rms;
	};

	void meterProcess(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
	{
		MeterNode* self = (MeterNode*)node;
		ma_uint32 channels = ma_node_get_output_channels(node, 0);
		ma_uint32 count = *frameCountIn < *frameCountOut ? *frameCountIn : *frameCountOut;
		const float* input = framesIn[0];
		float* output = framesOut[0];
		ma_uint32 samples = count * channels;
		float peak = 0.0f;
		double sum = 0.0;
		for (ma_uint32 i = 0; i < samples; i++)
		{
			float value = input[i];
			output[i] = value;
			float magnitude = value < 0 ? -value : value;
			if (magnitude > peak) peak = magnitude;
			sum += (double)value * value;
		}
		float previous = self->peak.load(std::memory_order_relaxed);
		float decayed = previous * 0.85f;
		self->peak.store(peak > decayed ? peak : decayed, std::memory_order_relaxed);
		self->rms.store(samples > 0 ? (float)std::sqrt(sum / samples) : 0.0f, std::memory_order_relaxed);
		*frameCountIn = count;
		*frameCountOut = count;
	}

	ma_node_vtable meterVTable = {meterProcess, NULL, 1, 1, 0};

	struct Group
	{
		ma_sound_group group;
		int generation;
	};

	struct Sound
	{
		ma_sound sound;
		ma_decoder decoder;
		MeterNode meter;
		unsigned char* memory;
		size_t memorySize;
		bool hasDecoder;
		bool hasMeter;
		int generation;
		int group;
	};

	ma_context context;
	ma_device device;
	ma_engine engine;
	MeterNode masterMeter;
	bool contextActive = false;
	bool deviceActive = false;
	bool engineActive = false;
	bool masterMeterActive = false;
	bool suspended = false;
	std::atomic<bool> restartRequested(false);
	std::vector<Sound*> sounds;
	std::vector<int> soundGenerations;
	std::vector<Group*> groups;
	std::vector<int> groupGenerations;
	std::recursive_mutex lock;

	int makeHandle(int index, int generation)
	{
		return ((generation & GENERATION_MASK) << INDEX_BITS) | (index & INDEX_MASK);
	}

	Sound* findSound(int id)
	{
		if (id < 0) return NULL;
		int index = id & INDEX_MASK;
		int generation = (id >> INDEX_BITS) & GENERATION_MASK;
		if (index >= (int)sounds.size()) return NULL;
		Sound* sound = sounds[index];
		return sound != NULL && sound->generation == generation ? sound : NULL;
	}

	Group* findGroup(int id)
	{
		if (id < 0) return NULL;
		int index = id & INDEX_MASK;
		int generation = (id >> INDEX_BITS) & GENERATION_MASK;
		if (index >= (int)groups.size()) return NULL;
		Group* group = groups[index];
		return group != NULL && group->generation == generation ? group : NULL;
	}

	int storeSound(Sound* sound)
	{
		size_t index = sounds.size();
		for (size_t i = 0; i < sounds.size(); i++)
		{
			if (sounds[i] == NULL)
			{
				index = i;
				break;
			}
		}
		if (index == sounds.size())
		{
			if ((int)index > INDEX_MASK) return -1;
			sounds.push_back(NULL);
			soundGenerations.push_back(0);
		}
		soundGenerations[index] = (soundGenerations[index] + 1) & GENERATION_MASK;
		sound->generation = soundGenerations[index];
		sounds[index] = sound;
		return makeHandle((int)index, sound->generation);
	}

	int storeGroup(Group* group)
	{
		size_t index = groups.size();
		for (size_t i = 0; i < groups.size(); i++)
		{
			if (groups[i] == NULL)
			{
				index = i;
				break;
			}
		}
		if (index == groups.size())
		{
			if ((int)index > INDEX_MASK) return -1;
			groups.push_back(NULL);
			groupGenerations.push_back(0);
		}
		groupGenerations[index] = (groupGenerations[index] + 1) & GENERATION_MASK;
		group->generation = groupGenerations[index];
		groups[index] = group;
		return makeHandle((int)index, group->generation);
	}

	ma_node* outputFor(int groupId)
	{
		Group* group = findGroup(groupId);
		if (group != NULL) return (ma_node*)&group->group;
		if (masterMeterActive) return (ma_node*)&masterMeter;
		return ma_engine_get_endpoint(&engine);
	}

	bool initMeter(MeterNode* meter, ma_node* output)
	{
		ma_uint32 channels = ma_engine_get_channels(&engine);
		ma_node_config config = ma_node_config_init();
		config.vtable = &meterVTable;
		config.pInputChannels = &channels;
		config.pOutputChannels = &channels;
		meter->peak.store(0.0f);
		meter->rms.store(0.0f);
		if (ma_node_init(ma_engine_get_node_graph(&engine), &config, NULL, meter) != MA_SUCCESS) return false;
		ma_node_attach_output_bus(meter, 0, output, 0);
		return true;
	}

	void attachSound(Sound* sound, int group)
	{
		ma_node* output = outputFor(group);
		sound->group = group;
		sound->hasMeter = initMeter(&sound->meter, output);
		ma_node_attach_output_bus(&sound->sound, 0, sound->hasMeter ? (ma_node*)&sound->meter : output, 0);
	}

	void destroySound(Sound* sound)
	{
		if (sound == NULL) return;
		ma_sound_uninit(&sound->sound);
		if (sound->hasMeter) ma_node_uninit(&sound->meter, NULL);
		if (sound->hasDecoder) ma_decoder_uninit(&sound->decoder);
		if (sound->memory != NULL) free(sound->memory);
		delete sound;
	}

	Sound* newSound()
	{
		Sound* sound = new Sound();
		sound->memory = NULL;
		sound->memorySize = 0;
		sound->hasDecoder = false;
		sound->hasMeter = false;
		sound->generation = 0;
		sound->group = -1;
		return sound;
	}

	void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
	{
		(void)pDevice;
		(void)pInput;
		ma_engine_read_pcm_frames(&engine, pOutput, frameCount, NULL);
	}

	void notificationCallback(const ma_device_notification* notification)
	{
		if (notification == NULL) return;
		if (notification->type == ma_device_notification_type_interruption_ended || notification->type == ma_device_notification_type_rerouted)
			restartRequested.store(true);
	}

	float sampleAt(const unsigned char* data, int bitsPerSample, int index)
	{
		switch (bitsPerSample)
		{
			case 8: return ((int)data[index] - 128) / 128.0f;
			case 16:
			{
				short value;
				memcpy(&value, data + index * 2, 2);
				return value / 32768.0f;
			}
			case 24:
			{
				const unsigned char* p = data + index * 3;
				int value = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
				return (value >> 8) / 8388608.0f;
			}
			case 32:
			{
				float value;
				memcpy(&value, data + index * 4, 4);
				return value;
			}
		}
		return 0.0f;
	}

	ma_uint32 soundRate(Sound* sound)
	{
		ma_uint32 rate = 0;
		if (ma_sound_get_data_format(&sound->sound, NULL, NULL, &rate, NULL, 0) != MA_SUCCESS || rate == 0) rate = ma_engine_get_sample_rate(&engine);
		return rate;
	}

	bool startEngine(int sampleRate, int periodMilliseconds, bool lowLatency)
	{
		if (ma_context_init(NULL, 0, NULL, &context) == MA_SUCCESS) contextActive = true;

		ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
		deviceConfig.playback.format = ma_format_f32;
		deviceConfig.playback.channels = 2;
		deviceConfig.sampleRate = sampleRate > 0 ? (ma_uint32)sampleRate : 0;
		deviceConfig.periodSizeInMilliseconds = periodMilliseconds > 0 ? (ma_uint32)periodMilliseconds : 0;
		deviceConfig.performanceProfile = lowLatency ? ma_performance_profile_low_latency : ma_performance_profile_conservative;
		deviceConfig.dataCallback = dataCallback;
		deviceConfig.notificationCallback = notificationCallback;
		deviceConfig.noPreSilencedOutputBuffer = MA_TRUE;
		deviceConfig.noClip = MA_FALSE;
		deviceConfig.wasapi.noAutoConvertSRC = MA_TRUE;
		deviceConfig.aaudio.usage = ma_aaudio_usage_game;
		deviceConfig.aaudio.contentType = ma_aaudio_content_type_music;
		deviceConfig.opensl.streamType = ma_opensl_stream_type_media;

		if (ma_device_init(contextActive ? &context : NULL, &deviceConfig, &device) == MA_SUCCESS)
		{
			deviceActive = true;
			ma_engine_config config = ma_engine_config_init();
			config.pDevice = &device;
			if (ma_engine_init(&config, &engine) == MA_SUCCESS)
			{
				engineActive = true;
				if (ma_device_start(&device) != MA_SUCCESS)
				{
					ma_engine_uninit(&engine);
					engineActive = false;
				}
			}
			if (!engineActive)
			{
				ma_device_uninit(&device);
				deviceActive = false;
			}
		}

		if (!engineActive)
		{
			ma_engine_config config = ma_engine_config_init();
			if (sampleRate > 0) config.sampleRate = (ma_uint32)sampleRate;
			if (periodMilliseconds > 0) config.periodSizeInMilliseconds = (ma_uint32)periodMilliseconds;
			engineActive = ma_engine_init(&config, &engine) == MA_SUCCESS;
		}

		if (!engineActive)
		{
			if (contextActive) ma_context_uninit(&context);
			contextActive = false;
			return false;
		}

		masterMeterActive = initMeter(&masterMeter, ma_engine_get_endpoint(&engine));
		suspended = false;
		restartRequested.store(false);
		return true;
	}
}

namespace hxminiaudio
{
	const char* version()
	{
		return ma_version_string();
	}

	const char* backendName()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return "none";
		ma_device* current = ma_engine_get_device(&engine);
		if (current == NULL || current->pContext == NULL) return "unknown";
		return ma_get_backend_name(current->pContext->backend);
	}

	const char* deviceName()
	{
		static char name[256];
		std::lock_guard<std::recursive_mutex> guard(lock);
		name[0] = '\0';
		if (!engineActive) return name;
		ma_device* current = ma_engine_get_device(&engine);
		if (current == NULL) return name;
		if (ma_device_get_name(current, ma_device_type_playback, name, sizeof(name), NULL) != MA_SUCCESS) name[0] = '\0';
		return name;
	}

	bool engineInit()
	{
		return engineInitEx(0, 0, true);
	}

	bool engineInitEx(int sampleRate, int periodMilliseconds, bool lowLatency)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (engineActive) return true;
		return startEngine(sampleRate, periodMilliseconds, lowLatency);
	}

	void engineShutdown()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return;
		if (deviceActive) ma_device_stop(&device);
		for (size_t i = 0; i < sounds.size(); i++)
		{
			destroySound(sounds[i]);
			sounds[i] = NULL;
		}
		for (size_t i = 0; i < groups.size(); i++)
		{
			if (groups[i] == NULL) continue;
			ma_sound_group_uninit(&groups[i]->group);
			delete groups[i];
			groups[i] = NULL;
		}
		if (masterMeterActive) ma_node_uninit(&masterMeter, NULL);
		masterMeterActive = false;
		ma_engine_uninit(&engine);
		engineActive = false;
		if (deviceActive) ma_device_uninit(&device);
		deviceActive = false;
		if (contextActive) ma_context_uninit(&context);
		contextActive = false;
		suspended = false;
	}

	bool engineReady()
	{
		return engineActive;
	}

	bool engineSuspend()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive || suspended) return engineActive;
		suspended = ma_engine_stop(&engine) == MA_SUCCESS;
		return suspended;
	}

	bool engineResume()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return false;
		if (!suspended) return true;
		suspended = ma_engine_start(&engine) != MA_SUCCESS;
		return !suspended;
	}

	bool engineSuspended()
	{
		return suspended;
	}

	void engineUpdate()
	{
		if (!restartRequested.exchange(false)) return;
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive || suspended) return;
		ma_device* current = ma_engine_get_device(&engine);
		if (current != NULL && ma_device_get_state(current) != ma_device_state_started) ma_device_start(current);
	}

	void setMasterVolume(float volume)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (engineActive) ma_engine_set_volume(&engine, volume < 0 ? 0 : volume);
	}

	float getMasterVolume()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		return engineActive ? ma_engine_get_volume(&engine) : 0.0f;
	}

	int engineSampleRate()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		return engineActive ? (int)ma_engine_get_sample_rate(&engine) : 0;
	}

	int engineChannels()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		return engineActive ? (int)ma_engine_get_channels(&engine) : 0;
	}

	double engineTime()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return 0;
		ma_uint32 rate = ma_engine_get_sample_rate(&engine);
		return rate > 0 ? (double)ma_engine_get_time_in_pcm_frames(&engine) * 1000.0 / rate : 0;
	}

	float engineLatency()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return 0;
		ma_device* current = ma_engine_get_device(&engine);
		if (current == NULL || current->sampleRate == 0) return 0;
		ma_uint32 frames = current->playback.internalPeriodSizeInFrames * (current->playback.internalPeriods > 0 ? current->playback.internalPeriods : 1);
		return frames * 1000.0f / current->sampleRate;
	}

	float enginePeak()
	{
		return masterMeterActive ? masterMeter.peak.load(std::memory_order_relaxed) : 0.0f;
	}

	int groupCreate(int parent)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return -1;
		Group* group = new Group();
		group->generation = 0;
		Group* parentGroup = findGroup(parent);
		if (ma_sound_group_init(&engine, 0, parentGroup != NULL ? &parentGroup->group : NULL, &group->group) != MA_SUCCESS)
		{
			delete group;
			return -1;
		}
		if (parentGroup == NULL && masterMeterActive) ma_node_attach_output_bus(&group->group, 0, &masterMeter, 0);
		int handle = storeGroup(group);
		if (handle < 0)
		{
			ma_sound_group_uninit(&group->group);
			delete group;
		}
		return handle;
	}

	void groupRelease(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		if (group == NULL) return;
		ma_node* fallback = masterMeterActive ? (ma_node*)&masterMeter : ma_engine_get_endpoint(&engine);
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* sound = sounds[i];
			if (sound == NULL || sound->group != id) continue;
			if (sound->hasMeter) ma_node_attach_output_bus(&sound->meter, 0, fallback, 0);
			else ma_node_attach_output_bus(&sound->sound, 0, fallback, 0);
			sound->group = -1;
		}
		ma_sound_group_uninit(&group->group);
		groups[id & INDEX_MASK] = NULL;
		delete group;
	}

	void groupSetVolume(int id, float volume)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		if (group != NULL) ma_sound_group_set_volume(&group->group, volume < 0 ? 0 : volume);
	}

	float groupGetVolume(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		return group != NULL ? ma_sound_group_get_volume(&group->group) : 0.0f;
	}

	void groupSetPitch(int id, float pitch)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		if (group != NULL) ma_sound_group_set_pitch(&group->group, pitch);
	}

	void groupSetPan(int id, float pan)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		if (group != NULL) ma_sound_group_set_pan(&group->group, pan);
	}

	bool groupStart(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		return group != NULL && ma_sound_group_start(&group->group) == MA_SUCCESS;
	}

	bool groupStop(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		return group != NULL && ma_sound_group_stop(&group->group) == MA_SUCCESS;
	}

	int soundLoadFile(const char* path, bool stream)
	{
		return soundLoadFileEx(path, stream, -1);
	}

	int soundLoadFileEx(const char* path, bool stream, int group)
	{
		if (path == NULL || !engineInit()) return -1;
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = newSound();
		ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		flags |= stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
		if (ma_sound_init_from_file(&engine, path, flags, NULL, NULL, &sound->sound) != MA_SUCCESS)
		{
			delete sound;
			return -1;
		}
		attachSound(sound, group);
		int handle = storeSound(sound);
		if (handle < 0) destroySound(sound);
		return handle;
	}

	int soundLoadMemory(const unsigned char* data, int size)
	{
		return soundLoadMemoryEx(data, size, -1);
	}

	int soundLoadMemoryEx(const unsigned char* data, int size, int group)
	{
		if (data == NULL || size <= 0 || !engineInit()) return -1;
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = newSound();
		sound->memory = (unsigned char*)malloc((size_t)size);
		if (sound->memory == NULL)
		{
			delete sound;
			return -1;
		}
		memcpy(sound->memory, data, (size_t)size);
		sound->memorySize = (size_t)size;
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
		if (ma_decoder_init_memory(sound->memory, (size_t)size, &decoderConfig, &sound->decoder) != MA_SUCCESS)
		{
			free(sound->memory);
			delete sound;
			return -1;
		}
		sound->hasDecoder = true;
		ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		if (ma_sound_init_from_data_source(&engine, &sound->decoder, flags, NULL, &sound->sound) != MA_SUCCESS)
		{
			ma_decoder_uninit(&sound->decoder);
			free(sound->memory);
			delete sound;
			return -1;
		}
		attachSound(sound, group);
		int handle = storeSound(sound);
		if (handle < 0) destroySound(sound);
		return handle;
	}

	int soundClone(int id, int group)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* source = findSound(id);
		if (source == NULL) return -1;
		if (source->memory != NULL)
		{
			size_t size = source->memorySize;
			return size > 0 ? soundLoadMemoryEx(source->memory, (int)size, group < 0 ? source->group : group) : -1;
		}
		Sound* sound = newSound();
		ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		if (ma_sound_init_copy(&engine, &source->sound, flags, NULL, &sound->sound) != MA_SUCCESS)
		{
			delete sound;
			return -1;
		}
		attachSound(sound, group < 0 ? source->group : group);
		int handle = storeSound(sound);
		if (handle < 0) destroySound(sound);
		return handle;
	}

	void soundRelease(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return;
		sounds[id & INDEX_MASK] = NULL;
		destroySound(sound);
	}

	int soundCount()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		int count = 0;
		for (size_t i = 0; i < sounds.size(); i++)
			if (sounds[i] != NULL) count++;
		return count;
	}

	bool soundPlay(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return false;
		ma_sound_reset_stop_time_and_fade(&sound->sound);
		ma_sound_set_start_time_in_pcm_frames(&sound->sound, 0);
		return ma_sound_start(&sound->sound) == MA_SUCCESS;
	}

	bool soundPlayDelayed(int id, double delayMilliseconds)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return false;
		ma_uint32 rate = ma_engine_get_sample_rate(&engine);
		ma_uint64 delay = (ma_uint64)(delayMilliseconds > 0 ? delayMilliseconds * rate / 1000.0 : 0);
		ma_sound_reset_stop_time_and_fade(&sound->sound);
		ma_sound_set_start_time_in_pcm_frames(&sound->sound, ma_engine_get_time_in_pcm_frames(&engine) + delay);
		return ma_sound_start(&sound->sound) == MA_SUCCESS;
	}

	bool soundStop(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL && ma_sound_stop(&sound->sound) == MA_SUCCESS;
	}

	bool soundStopFade(int id, double milliseconds)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return false;
		if (milliseconds <= 0) return ma_sound_stop(&sound->sound) == MA_SUCCESS;
		return ma_sound_stop_with_fade_in_milliseconds(&sound->sound, (ma_uint64)milliseconds) == MA_SUCCESS;
	}

	bool soundIsPlaying(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL && ma_sound_is_playing(&sound->sound);
	}

	void soundFade(int id, float from, float to, double milliseconds)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) ma_sound_set_fade_in_milliseconds(&sound->sound, from, to < 0 ? 0 : to, (ma_uint64)(milliseconds < 0 ? 0 : milliseconds));
	}

	void soundSetVolume(int id, float volume)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) ma_sound_set_volume(&sound->sound, volume < 0 ? 0 : volume);
	}

	float soundGetVolume(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL ? ma_sound_get_volume(&sound->sound) : 0.0f;
	}

	void soundSetPan(int id, float pan)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) ma_sound_set_pan(&sound->sound, pan < -1 ? -1 : (pan > 1 ? 1 : pan));
	}

	float soundGetPan(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL ? ma_sound_get_pan(&sound->sound) : 0.0f;
	}

	float soundGetPitch(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL ? ma_sound_get_pitch(&sound->sound) : 1.0f;
	}

	bool soundSetLoopPoints(int id, double startMilliseconds, double endMilliseconds)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return false;
		ma_data_source* source = ma_sound_get_data_source(&sound->sound);
		if (source == NULL) return false;
		ma_uint32 rate = 0;
		if (ma_sound_get_data_format(&sound->sound, NULL, NULL, &rate, NULL, 0) != MA_SUCCESS || rate == 0) return false;
		ma_uint64 start = startMilliseconds > 0 ? (ma_uint64)(startMilliseconds * rate / 1000.0) : 0;
		ma_uint64 end = endMilliseconds > 0 ? (ma_uint64)(endMilliseconds * rate / 1000.0) : ~((ma_uint64)0);
		if (end <= start) end = ~((ma_uint64)0);
		return ma_data_source_set_loop_point_in_pcm_frames(source, start, end) == MA_SUCCESS;
	}

	void soundSetPitch(int id, float pitch)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) ma_sound_set_pitch(&sound->sound, pitch > 0 ? pitch : 0.0001f);
	}

	void soundSetLooping(int id, bool looping)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) ma_sound_set_looping(&sound->sound, looping ? MA_TRUE : MA_FALSE);
	}

	bool soundIsLooping(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL && ma_sound_is_looping(&sound->sound);
	}

	bool soundAtEnd(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound == NULL || ma_sound_at_end(&sound->sound);
	}

	void soundSeek(int id, double milliseconds)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return;
		ma_uint64 frame = (ma_uint64)(milliseconds < 0 ? 0 : milliseconds * soundRate(sound) / 1000.0);
		ma_uint64 length = 0;
		if (ma_sound_get_length_in_pcm_frames(&sound->sound, &length) == MA_SUCCESS && length > 0 && frame > length) frame = length;
		ma_sound_seek_to_pcm_frame(&sound->sound, frame);
	}

	double soundGetTime(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return 0;
		ma_uint64 cursor = 0;
		if (ma_sound_get_cursor_in_pcm_frames(&sound->sound, &cursor) != MA_SUCCESS) return 0;
		return (double)cursor * 1000.0 / soundRate(sound);
	}

	double soundGetLength(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return 0;
		ma_uint64 length = 0;
		if (ma_sound_get_length_in_pcm_frames(&sound->sound, &length) != MA_SUCCESS) return 0;
		return (double)length * 1000.0 / soundRate(sound);
	}

	float soundGetPeak(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL || !sound->hasMeter || !ma_sound_is_playing(&sound->sound)) return 0.0f;
		return sound->meter.peak.load(std::memory_order_relaxed);
	}

	float soundGetRms(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL || !sound->hasMeter || !ma_sound_is_playing(&sound->sound)) return 0.0f;
		return sound->meter.rms.load(std::memory_order_relaxed);
	}

	int soundSampleRate(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		return sound != NULL ? (int)soundRate(sound) : 0;
	}

	int soundChannels(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound == NULL) return 0;
		ma_uint32 channels = 0;
		ma_sound_get_data_format(&sound->sound, NULL, &channels, NULL, NULL, 0);
		return (int)channels;
	}

	float bufferPeak(const unsigned char* data, int byteLength, int bitsPerSample, int channels, int startFrame, int frameCount, int channel)
	{
		if (data == NULL || channels <= 0 || frameCount <= 0) return 0.0f;
		int bytesPerSample = bitsPerSample / 8;
		if (bytesPerSample <= 0) return 0.0f;
		int totalFrames = byteLength / (bytesPerSample * channels);
		if (startFrame < 0) startFrame = 0;
		int endFrame = startFrame + frameCount;
		if (endFrame > totalFrames) endFrame = totalFrames;
		float peak = 0.0f;
		for (int frame = startFrame; frame < endFrame; frame++)
		{
			const int base = frame * channels;
			for (int c = 0; c < channels; c++)
			{
				if (channel >= 0 && c != channel) continue;
				float value = sampleAt(data, bitsPerSample, base + c);
				if (value < 0) value = -value;
				if (value > peak) peak = value;
			}
		}
		return peak > 1.0f ? 1.0f : peak;
	}
}
