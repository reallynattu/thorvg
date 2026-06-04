# Lottie Video Layer Demo

This demo renders a Lottie image layer with an `x-video` asset extension through
ThorVG's `LottieVideoProvider` API. The native provider is synthetic; the WASM
demo uses browser-decoded MP4 frames and falls back to generated pixels while a
clip is still loading.

The embedded Lottie also applies a 10-second, 30 fps keyframed rotation/skew
animation plus non-uniform scale to exercise transformed video compositing.
The supporting Lottie layers are animated too: the stroke frame changes size,
position, corner radius, opacity, and width; the translucent wash changes size,
position, radius, and opacity; and the background runs a subtle color drift plus
an ambient sweep.
The WASM page renders ThorVG at `1280x720` and decodes provider frames at
`800x448` so the browser does not upscale a tiny software buffer.
The browser page also reports the selected generated Lottie JSON size, current
browser FPS, browser frame count, and provider frame counters beside an approach
summary column that explains the `x-video` asset, ThorVG provider callbacks,
browser MP4 decode path, and final software-buffer blit.

The WASM demo includes eight Lottie variants. The first six share the same
transform animation but point at different `x-video` sources:

- `MDN Flower` from `interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4`
- `MDN Friday` from `interactive-examples.mdn.mozilla.net/media/cc0-videos/friday.mp4`
- `Sample.Cat Seawater` from `disk.sample.cat/samples/mp4/1416529-sd_640_360_30fps.mp4`
- `TrueFileSize Sample` from `cdn.truefilesize.com/mp4/sample-1mb.mp4`
- `MP4.to Pattern 720p` from `mp4.to/static/samples/mp4/sample-1280x720.mp4`
- `Sample.Cat Seawater 1080p` from `disk.sample.cat/samples/mp4/1416529-hd_1920_1080_30fps.mp4`

Two additional variants reuse those sources to exercise image-layer masking:

- `Masked Flower` reuses the MDN flower clip with a static alpha mask on the
  transformed image layer.
- `Animated Crop + Mask` reuses the 1080p seawater clip with an animated
  rectangular crop mask and a second animated intersect mask. ThorVG's Lottie
  parser does not implement an AE Crop effect, so this example models cropping
  through the supported `masksProperties` path.

Build from the repository root after `build-verify-ndebug` exists:

```sh
mkdir -p demo/lottie-video-layer/bin demo/lottie-video-layer/out
c++ -std=c++14 demo/lottie-video-layer/lottie_video_layer_demo.cpp \
  -Iinc -Isrc/loaders/lottie \
  build-verify-ndebug/src/libthorvg-1.1.dylib \
  -Wl,-rpath,$PWD/build-verify-ndebug/src \
  -o demo/lottie-video-layer/bin/lottie-video-layer-demo
```

Run:

```sh
demo/lottie-video-layer/bin/lottie-video-layer-demo demo/lottie-video-layer/out
```

Optional visual artifacts:

```sh
magick -delay 8 -loop 0 demo/lottie-video-layer/out/frame_*.ppm \
  demo/lottie-video-layer/out/lottie-video-layer-demo.gif

magick montage \
  demo/lottie-video-layer/out/frame_000.ppm \
  demo/lottie-video-layer/out/frame_008.ppm \
  demo/lottie-video-layer/out/frame_016.ppm \
  demo/lottie-video-layer/out/frame_024.ppm \
  demo/lottie-video-layer/out/frame_032.ppm \
  demo/lottie-video-layer/out/frame_040.ppm \
  -tile 3x2 -geometry +8+8 -background '#111827' \
  demo/lottie-video-layer/out/lottie-video-layer-contact-sheet.png

ffmpeg -y -framerate 12 -i demo/lottie-video-layer/out/frame_%03d.ppm \
  -pix_fmt yuv420p demo/lottie-video-layer/out/lottie-video-layer-demo.mp4
```
