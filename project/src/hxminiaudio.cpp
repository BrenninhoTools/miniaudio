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

	struct EffectNode
	{
		ma_node_base base;
		std::atomic_flag busy;
		ma_uint32 channels;
		ma_uint32 sampleRate;
		ma_lpf lpf;
		ma_hpf hpf;
		bool lpfReady;
		bool hpfReady;
		bool lpfOn;
		bool hpfOn;
		float* echo;
		ma_uint32 echoFrames;
		ma_uint32 echoCursor;
		float echoFeedback;
		float echoWet;
		float gain;
		bool active;
	};

	void effectProcess(ma_node* node, const float** framesIn, ma_uint32* frameCountIn, float** framesOut, ma_uint32* frameCountOut)
	{
		EffectNode* self = (EffectNode*)node;
		ma_uint32 count = *frameCountIn < *frameCountOut ? *frameCountIn : *frameCountOut;
		ma_uint32 channels = self->channels;
		ma_uint32 samples = count * channels;
		float* output = framesOut[0];
		memcpy(output, framesIn[0], samples * sizeof(float));
		*frameCountIn = count;
		*frameCountOut = count;

		if (self->busy.test_and_set(std::memory_order_acquire)) return;

		if (self->hpfOn && self->hpfReady) ma_hpf_process_pcm_frames(&self->hpf, output, output, count);
		if (self->lpfOn && self->lpfReady) ma_lpf_process_pcm_frames(&self->lpf, output, output, count);

		if (self->echo != NULL && self->echoFrames > 0)
		{
			float feedback = self->echoFeedback;
			float wet = self->echoWet;
			for (ma_uint32 frame = 0; frame < count; frame++)
			{
				float* line = self->echo + (size_t)self->echoCursor * channels;
				float* sample = output + (size_t)frame * channels;
				for (ma_uint32 c = 0; c < channels; c++)
				{
					float delayed = line[c];
					float input = sample[c];
					line[c] = input + delayed * feedback;
					sample[c] = input + delayed * wet;
				}
				self->echoCursor++;
				if (self->echoCursor >= self->echoFrames) self->echoCursor = 0;
			}
		}

		if (self->gain != 1.0f)
			for (ma_uint32 i = 0; i < samples; i++)
				output[i] *= self->gain;

		self->busy.clear(std::memory_order_release);
	}

	ma_node_vtable effectVTable = {effectProcess, NULL, 1, 1, 0};

	struct Group
	{
		ma_sound_group group;
		EffectNode effects;
		bool hasEffects;
		int generation;
	};

	struct Sound
	{
		ma_sound sound;
		ma_decoder decoder;
		ma_audio_buffer_ref bufferRef;
		MeterNode meter;
		unsigned char* memory;
		size_t memorySize;
		float* pcm;
		ma_uint64 pcmFrames;
		ma_uint32 pcmChannels;
		bool hasDecoder;
		bool hasBufferRef;
		bool hasMeter;
		bool autoRelease;
		int parent;
		int maxVoices;
		unsigned long long order;
		int generation;
		int group;
	};

	unsigned long long playOrder = 0;

	ma_context context;
	ma_device device;
	ma_engine engine;
	MeterNode masterMeter;
	EffectNode masterEffects;
	bool masterEffectsActive = false;
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

	ma_node* rootOutput()
	{
		if (masterEffectsActive) return (ma_node*)&masterEffects;
		if (masterMeterActive) return (ma_node*)&masterMeter;
		return ma_engine_get_endpoint(&engine);
	}

	ma_node* outputFor(int groupId)
	{
		Group* group = findGroup(groupId);
		if (group != NULL) return (ma_node*)&group->group;
		return rootOutput();
	}

	bool initEffects(EffectNode* effects, ma_node* output)
	{
		ma_uint32 channels = ma_engine_get_channels(&engine);
		ma_node_config config = ma_node_config_init();
		config.vtable = &effectVTable;
		config.pInputChannels = &channels;
		config.pOutputChannels = &channels;
		effects->busy.clear();
		effects->channels = channels;
		effects->sampleRate = ma_engine_get_sample_rate(&engine);
		effects->lpfReady = false;
		effects->hpfReady = false;
		effects->lpfOn = false;
		effects->hpfOn = false;
		effects->echo = NULL;
		effects->echoFrames = 0;
		effects->echoCursor = 0;
		effects->echoFeedback = 0.0f;
		effects->echoWet = 0.0f;
		effects->gain = 1.0f;
		effects->active = false;
		if (ma_node_init(ma_engine_get_node_graph(&engine), &config, NULL, effects) != MA_SUCCESS) return false;
		ma_node_attach_output_bus(effects, 0, output, 0);
		effects->active = true;
		return true;
	}

	void lockEffects(EffectNode* effects)
	{
		while (effects->busy.test_and_set(std::memory_order_acquire)) {}
	}

	void unlockEffects(EffectNode* effects)
	{
		effects->busy.clear(std::memory_order_release);
	}

	void uninitEffects(EffectNode* effects)
	{
		if (!effects->active) return;
		ma_node_uninit(effects, NULL);
		if (effects->lpfReady) ma_lpf_uninit(&effects->lpf, NULL);
		if (effects->hpfReady) ma_hpf_uninit(&effects->hpf, NULL);
		if (effects->echo != NULL) free(effects->echo);
		effects->echo = NULL;
		effects->lpfReady = false;
		effects->hpfReady = false;
		effects->active = false;
	}

	EffectNode* findEffects(int groupId)
	{
		if (groupId < 0) return masterEffectsActive ? &masterEffects : NULL;
		Group* group = findGroup(groupId);
		return group != NULL && group->hasEffects ? &group->effects : NULL;
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

	void attachSound(Sound* sound, int group, bool meter = true)
	{
		ma_node* output = outputFor(group);
		sound->group = group;
		sound->hasMeter = meter && initMeter(&sound->meter, output);
		ma_node_attach_output_bus(&sound->sound, 0, sound->hasMeter ? (ma_node*)&sound->meter : output, 0);
	}

	void destroySound(Sound* sound)
	{
		if (sound == NULL) return;
		ma_sound_uninit(&sound->sound);
		if (sound->hasMeter) ma_node_uninit(&sound->meter, NULL);
		if (sound->hasDecoder) ma_decoder_uninit(&sound->decoder);
		if (sound->hasBufferRef) ma_audio_buffer_ref_uninit(&sound->bufferRef);
		if (sound->memory != NULL) free(sound->memory);
		if (sound->pcm != NULL && sound->parent < 0) ma_free(sound->pcm, NULL);
		delete sound;
	}

	Sound* newSound()
	{
		Sound* sound = new Sound();
		sound->memory = NULL;
		sound->memorySize = 0;
		sound->pcm = NULL;
		sound->pcmFrames = 0;
		sound->pcmChannels = 0;
		sound->hasDecoder = false;
		sound->hasBufferRef = false;
		sound->autoRelease = false;
		sound->parent = -1;
		sound->maxVoices = 8;
		sound->order = 0;
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
		masterEffectsActive = initEffects(&masterEffects, masterMeterActive ? (ma_node*)&masterMeter : ma_engine_get_endpoint(&engine));
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
			if (groups[i]->hasEffects) uninitEffects(&groups[i]->effects);
			delete groups[i];
			groups[i] = NULL;
		}
		if (masterEffectsActive) uninitEffects(&masterEffects);
		masterEffectsActive = false;
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
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!engineActive) return;
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* sound = sounds[i];
			if (sound == NULL || !sound->autoRelease) continue;
			if (ma_sound_is_playing(&sound->sound) && !ma_sound_at_end(&sound->sound)) continue;
			sounds[i] = NULL;
			destroySound(sound);
		}
		if (!restartRequested.exchange(false) || suspended) return;
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
		group->hasEffects = false;
		Group* parentGroup = findGroup(parent);
		if (ma_sound_group_init(&engine, 0, parentGroup != NULL ? &parentGroup->group : NULL, &group->group) != MA_SUCCESS)
		{
			delete group;
			return -1;
		}
		ma_node* target = parentGroup != NULL ? (ma_node*)&parentGroup->group : rootOutput();
		group->hasEffects = initEffects(&group->effects, target);
		ma_node_attach_output_bus(&group->group, 0, group->hasEffects ? (ma_node*)&group->effects : target, 0);
		int handle = storeGroup(group);
		if (handle < 0)
		{
			ma_sound_group_uninit(&group->group);
			if (group->hasEffects) uninitEffects(&group->effects);
			delete group;
		}
		return handle;
	}

	void groupRelease(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Group* group = findGroup(id);
		if (group == NULL) return;
		ma_node* fallback = rootOutput();
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* sound = sounds[i];
			if (sound == NULL || sound->group != id) continue;
			if (sound->hasMeter) ma_node_attach_output_bus(&sound->meter, 0, fallback, 0);
			else ma_node_attach_output_bus(&sound->sound, 0, fallback, 0);
			sound->group = -1;
		}
		for (size_t i = 0; i < groups.size(); i++)
		{
			Group* child = groups[i];
			if (child == NULL || child == group) continue;
			ma_node_base* childOutput = child->hasEffects ? (ma_node_base*)&child->effects : (ma_node_base*)&child->group;
			if (childOutput->pOutputBuses[0].pInputNode == (ma_node*)&group->group) ma_node_attach_output_bus(childOutput, 0, fallback, 0);
		}
		ma_sound_group_uninit(&group->group);
		if (group->hasEffects) uninitEffects(&group->effects);
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
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* child = sounds[i];
			if (child == NULL || child->parent != id) continue;
			sounds[i] = NULL;
			destroySound(child);
		}
		sounds[id & INDEX_MASK] = NULL;
		destroySound(sound);
	}

	int soundLoadMemoryDecoded(const unsigned char* data, int size, int group)
	{
		if (data == NULL || size <= 0 || !engineInit()) return -1;
		std::lock_guard<std::recursive_mutex> guard(lock);
		ma_uint32 rate = ma_engine_get_sample_rate(&engine);
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, rate);
		ma_uint64 frames = 0;
		void* pcm = NULL;
		if (ma_decode_memory(data, (size_t)size, &decoderConfig, &frames, &pcm) != MA_SUCCESS || pcm == NULL || frames == 0)
		{
			if (pcm != NULL) ma_free(pcm, NULL);
			return -1;
		}
		Sound* sound = newSound();
		sound->pcm = (float*)pcm;
		sound->pcmFrames = frames;
		sound->pcmChannels = decoderConfig.channels;
		if (sound->pcmChannels == 0)
		{
			ma_decoder probe;
			ma_decoder_config probeConfig = ma_decoder_config_init(ma_format_f32, 0, rate);
			if (ma_decoder_init_memory(data, (size_t)size, &probeConfig, &probe) == MA_SUCCESS)
			{
				sound->pcmChannels = probe.outputChannels;
				ma_decoder_uninit(&probe);
			}
		}
		if (sound->pcmChannels == 0 || ma_audio_buffer_ref_init(ma_format_f32, sound->pcmChannels, sound->pcm, frames, &sound->bufferRef) != MA_SUCCESS)
		{
			ma_free(pcm, NULL);
			delete sound;
			return -1;
		}
		sound->bufferRef.sampleRate = rate;
		sound->hasBufferRef = true;
		ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		if (ma_sound_init_from_data_source(&engine, &sound->bufferRef, flags, NULL, &sound->sound) != MA_SUCCESS)
		{
			ma_audio_buffer_ref_uninit(&sound->bufferRef);
			ma_free(pcm, NULL);
			delete sound;
			return -1;
		}
		attachSound(sound, group);
		int handle = storeSound(sound);
		if (handle < 0) destroySound(sound);
		return handle;
	}

	void soundSetMaxVoices(int id, int voices)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* sound = findSound(id);
		if (sound != NULL) sound->maxVoices = voices < 1 ? 1 : voices;
	}

	int soundVoices(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		int count = 0;
		for (size_t i = 0; i < sounds.size(); i++)
			if (sounds[i] != NULL && sounds[i]->parent == id) count++;
		return count;
	}

	int soundPlayOneShot(int id, float volume, float pitch, float pan, int group)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		Sound* source = findSound(id);
		if (source == NULL) return -1;

		int voices = 0;
		size_t oldestIndex = 0;
		unsigned long long oldestOrder = ~0ULL;
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* child = sounds[i];
			if (child == NULL || child->parent != id) continue;
			voices++;
			if (child->order < oldestOrder)
			{
				oldestOrder = child->order;
				oldestIndex = i;
			}
		}
		if (voices >= source->maxVoices && oldestOrder != ~0ULL)
		{
			Sound* oldest = sounds[oldestIndex];
			sounds[oldestIndex] = NULL;
			destroySound(oldest);
		}

		int targetGroup = group >= 0 ? group : source->group;
		Sound* sound = newSound();
		sound->parent = id;
		sound->autoRelease = true;
		ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;

		if (source->pcm != NULL)
		{
			sound->pcm = source->pcm;
			sound->pcmFrames = source->pcmFrames;
			sound->pcmChannels = source->pcmChannels;
			if (ma_audio_buffer_ref_init(ma_format_f32, source->pcmChannels, source->pcm, source->pcmFrames, &sound->bufferRef) != MA_SUCCESS)
			{
				delete sound;
				return -1;
			}
			sound->bufferRef.sampleRate = source->bufferRef.sampleRate;
			sound->hasBufferRef = true;
			if (ma_sound_init_from_data_source(&engine, &sound->bufferRef, flags, NULL, &sound->sound) != MA_SUCCESS)
			{
				ma_audio_buffer_ref_uninit(&sound->bufferRef);
				delete sound;
				return -1;
			}
		}
		else if (source->memory != NULL)
		{
			ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
			if (ma_decoder_init_memory(source->memory, source->memorySize, &decoderConfig, &sound->decoder) != MA_SUCCESS)
			{
				delete sound;
				return -1;
			}
			sound->hasDecoder = true;
			if (ma_sound_init_from_data_source(&engine, &sound->decoder, flags, NULL, &sound->sound) != MA_SUCCESS)
			{
				ma_decoder_uninit(&sound->decoder);
				delete sound;
				return -1;
			}
		}
		else if (ma_sound_init_copy(&engine, &source->sound, flags, NULL, &sound->sound) != MA_SUCCESS)
		{
			delete sound;
			return -1;
		}

		attachSound(sound, targetGroup, false);
		ma_sound_set_volume(&sound->sound, volume < 0 ? 0 : volume);
		ma_sound_set_pitch(&sound->sound, pitch > 0 ? pitch : 1.0f);
		ma_sound_set_pan(&sound->sound, pan);
		sound->order = ++playOrder;
		int handle = storeSound(sound);
		if (handle < 0)
		{
			destroySound(sound);
			return -1;
		}
		if (ma_sound_start(&sound->sound) != MA_SUCCESS)
		{
			sounds[handle & INDEX_MASK] = NULL;
			destroySound(sound);
			return -1;
		}
		return handle;
	}

	void soundStopVoices(int id)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		for (size_t i = 0; i < sounds.size(); i++)
		{
			Sound* child = sounds[i];
			if (child == NULL || child->parent != id) continue;
			sounds[i] = NULL;
			destroySound(child);
		}
	}

	bool effectSetLowPass(int groupId, double cutoff, int order)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		EffectNode* effects = findEffects(groupId);
		if (effects == NULL) return false;
		bool ok = true;
		lockEffects(effects);
		if (cutoff <= 0)
		{
			effects->lpfOn = false;
		}
		else
		{
			double nyquist = effects->sampleRate * 0.5 - 1.0;
			ma_lpf_config config = ma_lpf_config_init(ma_format_f32, effects->channels, effects->sampleRate, cutoff < nyquist ? cutoff : nyquist, (ma_uint32)(order < 1 ? 2 : (order > 8 ? 8 : order)));
			if (effects->lpfReady && effects->lpf.lpf1Count + effects->lpf.lpf2Count * 2 == config.order) ok = ma_lpf_reinit(&config, &effects->lpf) == MA_SUCCESS;
			else
			{
				if (effects->lpfReady) ma_lpf_uninit(&effects->lpf, NULL);
				effects->lpfReady = ma_lpf_init(&config, NULL, &effects->lpf) == MA_SUCCESS;
				ok = effects->lpfReady;
			}
			effects->lpfOn = ok;
		}
		unlockEffects(effects);
		return ok;
	}

	bool effectSetHighPass(int groupId, double cutoff, int order)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		EffectNode* effects = findEffects(groupId);
		if (effects == NULL) return false;
		bool ok = true;
		lockEffects(effects);
		if (cutoff <= 0)
		{
			effects->hpfOn = false;
		}
		else
		{
			double nyquist = effects->sampleRate * 0.5 - 1.0;
			ma_hpf_config config = ma_hpf_config_init(ma_format_f32, effects->channels, effects->sampleRate, cutoff < nyquist ? cutoff : nyquist, (ma_uint32)(order < 1 ? 2 : (order > 8 ? 8 : order)));
			if (effects->hpfReady && effects->hpf.hpf1Count + effects->hpf.hpf2Count * 2 == config.order) ok = ma_hpf_reinit(&config, &effects->hpf) == MA_SUCCESS;
			else
			{
				if (effects->hpfReady) ma_hpf_uninit(&effects->hpf, NULL);
				effects->hpfReady = ma_hpf_init(&config, NULL, &effects->hpf) == MA_SUCCESS;
				ok = effects->hpfReady;
			}
			effects->hpfOn = ok;
		}
		unlockEffects(effects);
		return ok;
	}

	bool effectSetEcho(int groupId, double delayMilliseconds, float feedback, float wet)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		EffectNode* effects = findEffects(groupId);
		if (effects == NULL) return false;
		ma_uint32 frames = delayMilliseconds > 0 ? (ma_uint32)(delayMilliseconds * effects->sampleRate / 1000.0) : 0;
		if (frames > effects->sampleRate * 4) frames = effects->sampleRate * 4;
		float* buffer = NULL;
		if (frames > 0 && frames != effects->echoFrames)
		{
			buffer = (float*)calloc((size_t)frames * effects->channels, sizeof(float));
			if (buffer == NULL) return false;
		}
		lockEffects(effects);
		float* old = NULL;
		if (frames == 0)
		{
			old = effects->echo;
			effects->echo = NULL;
			effects->echoFrames = 0;
		}
		else if (buffer != NULL)
		{
			old = effects->echo;
			effects->echo = buffer;
			effects->echoFrames = frames;
			effects->echoCursor = 0;
		}
		effects->echoFeedback = feedback < 0 ? 0 : (feedback > 0.95f ? 0.95f : feedback);
		effects->echoWet = wet < 0 ? 0 : wet;
		unlockEffects(effects);
		if (old != NULL) free(old);
		return true;
	}

	void effectSetGain(int groupId, float gain)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		EffectNode* effects = findEffects(groupId);
		if (effects == NULL) return;
		lockEffects(effects);
		effects->gain = gain < 0 ? 0 : gain;
		unlockEffects(effects);
	}

	void effectClear(int groupId)
	{
		effectSetLowPass(groupId, 0, 0);
		effectSetHighPass(groupId, 0, 0);
		effectSetEcho(groupId, 0, 0, 0);
		effectSetGain(groupId, 1.0f);
	}

	int deviceCount()
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!contextActive) return 0;
		ma_device_info* infos = NULL;
		ma_uint32 count = 0;
		if (ma_context_get_devices(&context, &infos, &count, NULL, NULL) != MA_SUCCESS) return 0;
		return (int)count;
	}

	const char* deviceNameAt(int index)
	{
		static char name[256];
		std::lock_guard<std::recursive_mutex> guard(lock);
		name[0] = '\0';
		if (!contextActive) return name;
		ma_device_info* infos = NULL;
		ma_uint32 count = 0;
		if (ma_context_get_devices(&context, &infos, &count, NULL, NULL) != MA_SUCCESS || index < 0 || (ma_uint32)index >= count) return name;
		strncpy(name, infos[index].name, sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		return name;
	}

	bool deviceIsDefault(int index)
	{
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (!contextActive) return false;
		ma_device_info* infos = NULL;
		ma_uint32 count = 0;
		if (ma_context_get_devices(&context, &infos, &count, NULL, NULL) != MA_SUCCESS || index < 0 || (ma_uint32)index >= count) return false;
		return infos[index].isDefault != 0;
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
