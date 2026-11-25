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
- Playback device and backend names.

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

With Lime, `MiniAudio.update()` runs every frame and the device follows the window focus. Set `MiniAudio.autoUpdate` or `MiniAudio.autoSuspend` to `false` to drive them yourself. Without Lime, call `MiniAudio.update()` from your loop.

Check out the [sample](sample/) folder for a command line player.

## License

The bindings are MIT. miniaudio is public domain or MIT-0, see [project/include/LICENSE-miniaudio](project/include/LICENSE-miniaudio).
