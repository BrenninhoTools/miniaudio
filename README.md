# miniaudio

Haxe/C++ bindings for [miniaudio](https://miniaud.io) focused on low latency playback for games on desktop and mobile.

## Features

- Thread safe sound and group handles with generation ids, so stale handles never touch freed memory.
- Low latency device setup with a custom sample rate and period size.
- Sound groups with volume, pitch and pan, nested groups.
- Fades, fade out on stop, delayed playback, accurate seek and time.
- Loop points in milliseconds.
- Peak and RMS meters for every sound and the master output, plus a fast peak scan over raw PCM buffers.
- Suspends the device when the window loses focus and resumes it when focus comes back (Lime).
- Playback device and backend names, plus a list of output devices.
- Decoded sounds: short effects are decoded once into memory, so nothing is decoded on the audio thread while playing.
- One shots with voice limits: fire and forget copies that share the decoded audio and release themselves when done; the oldest voice is cut when the limit is reached.
- Effects on every group and on the master output: low pass, high pass, echo and gain, changed live without clicks or locks on the audio thread.
- Group fades, ducking and `onComplete` callbacks for sounds.

## Platforms

Windows, macOS, Linux, Android and iOS on hxcpp. Other targets compile, with every call returning a neutral value.

## Installation

```
haxelib git miniaudio https://github.com/BrenninhoTools/miniaudio
```

Add `-lib miniaudio` to your hxml, or `<haxelib name="miniaudio"/>` to your Lime project.

## Usage

```haxe
import miniaudio.MiniAudio;
import miniaudio.MiniAudioGroup;
import miniaudio.MiniAudioSound;

MiniAudio.init();

var music:MiniAudioGroup = MiniAudioGroup.create();
var song:MiniAudioSound = MiniAudioSound.fromFile('assets/music/song.ogg', true, music);
song.looping = true;
song.setLoopPoints(4000);
song.play();

var hit:MiniAudioSound = MiniAudioSound.fromAsset('assets/sounds/hit.ogg');
hit.playDelayed(120);

music.volume = 0.8;
song.fade(1, 0, 2000);
```

Hit sounds and other short effects:

```haxe
var hit:MiniAudioSound = MiniAudioSound.decodedAsset('assets/sounds/hitsound.ogg');
hit.maxVoices = 6;
hit.playOneShot(0.8, FlxG.random.float(0.95, 1.05));
```

Effects:

```haxe
music.effects.lowPass = 900;
music.effects.muffle(0.6);
music.effects.setEcho(180, 0.35, 0.4);
music.effects.clear();
MiniAudio.masterEffects.highPass = 60;
music.duck(0.3, 150);
music.unduck(400);
song.onComplete = function() endSong();
```

With Lime, `MiniAudio.update()` runs every frame and the device follows the window focus. Set `MiniAudio.autoUpdate` or `MiniAudio.autoSuspend` to `false` to drive them yourself. Without Lime, call `MiniAudio.update()` from your loop.

Check out the [sample](sample/) folder for a command line player.

## License

The bindings are MIT. miniaudio is public domain or MIT-0, see [project/include/LICENSE-miniaudio](project/include/LICENSE-miniaudio).
