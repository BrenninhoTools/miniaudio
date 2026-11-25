package;

import miniaudio.MiniAudio;
import miniaudio.MiniAudioGroup;
import miniaudio.MiniAudioSound;

class Main
{
	static function main():Void
	{
		if (!MiniAudio.init())
		{
			Sys.println('MiniAudio: failed to start');
			return;
		}

		Sys.println('MiniAudio ${MiniAudio.version} on ${MiniAudio.backend} (${MiniAudio.device})');
		Sys.println('Rate ${MiniAudio.sampleRate()} Hz, ${MiniAudio.channels()} channels, latency ${MiniAudio.latency} ms');

		var args:Array<String> = Sys.args();

		if (args.length > 0)
		{
			var music:MiniAudioGroup = MiniAudioGroup.create();
			var sound:MiniAudioSound = MiniAudioSound.fromFile(args[0], true, music);

			if (sound != null)
			{
				sound.play();

				while (sound.playing)
				{
					MiniAudio.update();
					Sys.sleep(0.05);
				}

				sound.destroy();
			}

			if (music != null) music.destroy();
		}

		MiniAudio.shutdown();
	}
}
