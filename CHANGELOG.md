Changelog
=========

1.1.0
-----

- Added decoded sounds (`MiniAudioSound.decoded` and `decodedAsset`).
- Added one shots with voice limits (`playOneShot`, `maxVoices`, `voices`, `stopVoices`).
- Added effects on groups and the master output (`effects.lowPass`, `highPass`, `filterOrder`, `setEcho`, `gain`, `muffle`, `clear`).
- Added group fades, `duck` and `unduck`, and `onComplete` on sounds.
- Added `MiniAudio.devices()`.
- Releasing a group keeps its child groups playing.
- Added a native test that runs on the null backend.

1.0.0
-----

- First release as a haxelib, moved out of Funkin' Psych.
- Added loop points, `pan` and `pitch` getters on the native side and the playback device name.
- Lime projects get automatic updates and focus based suspend without any framework dependency.
