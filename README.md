## DRM-draw -- Video player

* Using linux's DRM to draw on screen.
* Using ALSA to playback sound.
* Using libav to decode media file.

## Build

* `git clone` this repo
* Navigate to `/`
* `mkdir build`
* `(cd build && cmake ../ && make && make install)`

## Run

* Switch to a non-graphic `tty`.
* To use the menu run it from a remote session.
* `./drmdraw` `"video_device"` `"alsa_device"` `"media_path"`

## Menu

* Pause/Unpause.
* Seek.
* Switch audio track.

## Issues

* No sync.
* Can't play multiple audio tracks if the params don't match.