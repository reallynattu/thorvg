# ThorVG Lottie Video Layer Design and Feasibility Plan

Research date: 2026-06-04

Scope: design only. This document does not propose implementing video decoding in this pass.

## 1. Executive summary

The best V1 architecture is to keep video codecs outside ThorVG and add a Lottie video-frame provider interface. ThorVG should parse a video-capable Lottie asset/layer, ask an application or platform adapter for the decoded frame matching the current Lottie timeline frame, then render that frame through the same composition path used by image layers.

This preserves ThorVG's strengths: small modular builds, portability, deterministic render backends, and minimal renderer disruption. It also avoids making ThorVG responsible for demuxing containers, selecting codecs, hardware decoder policy, DRM, audio, buffering, network streaming, and platform-specific lifecycle rules.

Recommended V1:

- Represent the video layer as an image-compatible Lottie layer (`ty: 2`) that references an asset with a vendor video extension and a poster image fallback.
- Add a `LottieVideoFrameProvider` style interface that receives asset metadata plus a timeline-derived timestamp and returns either a CPU bitmap frame or a GPU-native frame handle.
- Implement the CPU bitmap path first, using existing `Picture`/`RenderSurface` semantics where possible.
- Add renderer fast paths later for stable mutable image sources and imported GL/WebGPU/platform textures.
- Treat ThorVG as a compositor and timeline mapper, not a decoder or media player.

Main answer: ThorVG should not own video decoding in V1. It should render decoded frames provided by platform adapters.

## 2. ThorVG architecture findings

ThorVG is a compact vector graphics and animation renderer with modular loaders and selectable render backends. Its README describes supported primitives including images, composition, effects, and Lottie animation, and calls out the lightweight, modular build style and broad portability goals. Build options show selectable engines (`cpu`, `gl`, `wg`) and loaders (`svg`, `png`, `jpg`, `lottie`, `ttf`, `otf`, `webp`).

Important source findings:

- Lottie is an `AnimLoader` behind `Animation::frame()`. The public `Animation` API sets a frame, exposes the owned `Picture`, reports duration/total frame, and supports segments.
- `Animation::frame()` delegates to the picture's loader, then marks the picture with `RenderUpdateFlag::All`.
- `LottieLoader` owns the parsed `LottieComposition`, a `LottieBuilder`, frame count/rate, and segment boundaries. Calling `frame()` shortens the frame value, clears the composition scene, and schedules a task. `sync()` forces the task to finish.
- `LottieBuilder::update()` clamps the requested frame to the composition range, updates expressions with composition time, then updates child layers in draw order.
- Each Lottie layer becomes a ThorVG `Scene` with transform, opacity, blend method, masks, effects, and content. This is exactly where a video layer should enter.
- All render backends implement a common `RenderMethod` contract with `prepare(RenderSurface*)`, `renderImage()`, compositing, bounds, and intersection methods.

Current renderer surfaces are CPU pixel surfaces:

```cpp
struct RenderSurface {
    pixel_t* data;
    uint32_t stride;
    uint32_t w, h;
    ColorSpace cs;
    bool premultiplied;
};
```

That is enough for a portable V1 fallback. It is not enough for low-copy video across all GPUs unless ThorVG adds a frame-source abstraction or backend-specific import hooks.

Key architecture implication: the Lottie loader/builder is the right integration point. The canvas and renderer should only see a normal image-like paint or a renderer image source.

## 3. Current Lottie image-layer flow

ThorVG's current image layer flow is:

1. `LottieParser::parseLayer()` parses common layer fields:
   - `ty` to `LottieLayer::Type`
   - `ks` to `LottieTransform`
   - `ip`/`op` to layer visibility frames
   - `st` to `startFrame`
   - `sr` to `timeStretch`
   - `bm` to blend method
   - `parent`, `tt`, `tp`, masks, effects, and `refId`
2. `LottieParser::parseAsset()` parses image assets:
   - `id`
   - `u` directory/path
   - `p` filename, URL, or data URI
   - `w`/`h`
   - `e` embedded flag
   - `sid` for slots
3. `LottieParser::parseImage()` handles embedded `data:image/...;base64,...`, HTTP(S) URLs, or a path relative to the Lottie file directory.
4. `LottieImage::prepare()` creates a ThorVG `Picture`, loads embedded or external data, sizes the picture to asset width/height, and stores it in `bitmap.picture`.
5. `LottieBuilder::_buildReference()` attaches the referenced image asset to an image layer's `children`.
6. `LottieBuilder::updateLayer()` creates a `Scene`, applies transform/opacity/blend/matte/effects, then calls `updateImage()` for image layers.
7. `LottieBuilder::updateImage()` resolves an unresolved asset through `Picture::resolver()`, sizes it, and adds either the shared picture or a duplicate to the layer scene.
8. `PictureImpl::update()` loads the picture, computes the image scaling, and calls `renderer->prepare(RenderSurface*, ...)` for bitmap images.
9. The backend renders the image through `renderImage()`.

This is a good template for video. The video layer can reuse the same layer parsing, transform, visibility, parent hierarchy, scene opacity, blend, mask, matte, and backend image rendering contracts.

The current asset resolver is not enough for video playback because it resolves an external image once. A video provider must be called per requested timeline frame or must expose a frame cache that ThorVG can query during `updateVideo()`.

## 4. Proposed video layer model

Model the V1 video layer as "image layer plus dynamic source":

- The Lottie layer remains `ty: 2` image layer for maximum compatibility with current Lottie semantics.
- The referenced asset carries normal image fields for fallback poster rendering plus a vendor video extension.
- The parsed model gets a new internal asset object, for example `LottieVideo`, alongside `LottieImage`.
- The builder adds an `updateVideo()` path parallel to `updateImage()`.
- `updateVideo()` computes the media timestamp from the Lottie frame and asks the registered provider for a decoded frame.
- The returned frame becomes the content of a stable ThorVG image-like paint in the layer scene.

Internal model sketch:

```cpp
struct LottieVideo : LottieObject {
    LottieBitmap poster;
    LottieVideoAsset asset;
    VideoFrameSource* source = nullptr;
    bool opened = false;
};

struct LottieVideoAsset {
    char* src;
    char* mime;
    float width;
    float height;
    float durationSec;
    float frameRate;
    bool loop;
    bool muted;
};
```

Frame provider sketch:

```cpp
struct LottieVideoFrameRequest {
    const char* assetId;
    const char* src;
    double timeSec;
    uint64_t frameSerialHint;
    bool looping;
    bool preferGpu;
};

struct LottieVideoFrame {
    enum class Type { None, Bitmap, GlTexture, WgpuTexture, PlatformHandle };
    Type type;
    uint32_t width;
    uint32_t height;
    ColorSpace colorSpace;
    bool premultiplied;
    double timestampSec;
    double durationSec;
    uint64_t serial;
    RenderSurface surface;       // for Type::Bitmap
    void* nativeHandle;          // for backend-specific paths
    void (*release)(void* user);
    void* user;
};

class LottieVideoFrameProvider {
public:
    virtual Result open(const LottieVideoAssetInfo& asset) = 0;
    virtual Result frame(const LottieVideoFrameRequest& request,
                         LottieVideoFrame* out) = 0;
    virtual void close(const char* assetId) = 0;
};
```

The exact public API can use C callbacks instead of C++ virtuals if ThorVG wants ABI stability. The architectural requirement is the same: ThorVG asks for decoded frames by timeline time; the adapter owns decoding.

## 5. Decoder ownership options

### Option A: ThorVG owns codecs and demuxing

ThorVG would parse video containers, pick codecs, decode frames, handle frame queues, seek, loop, and upload to the renderer.

Pros:

- One API for all apps.
- Easier deterministic tests if a fixed software decoder is bundled.
- Less adapter work for simple desktop use.

Cons:

- Pulls ThorVG into a large media stack with codec/container licensing, security, maintenance, and platform acceleration complexity.
- Conflicts with ThorVG's small binary and modular loader goals.
- Hardware decode is platform-specific anyway: WebCodecs on web, AVFoundation/CoreVideo on Apple platforms, MediaCodec on Android, FFmpeg/GStreamer or app engines on desktop.
- DRM and secure decode surfaces cannot generally be sampled into arbitrary vector compositing.
- Audio and A/V sync would become a separate product area.

This option is not recommended for V1.

### Option B: ThorVG renders app-provided decoded frames

ThorVG exposes a provider interface. Platform integrations decode frames and pass bitmap or texture handles into ThorVG.

Pros:

- Minimal renderer and loader disruption.
- Keeps codecs, demuxers, DRM policy, hardware acceleration, and platform threading outside ThorVG.
- Supports web, mobile, desktop, and embedded targets with the best native decode path per platform.
- Enables software fallback with the current raw bitmap image path.
- Allows optional GPU imports later without blocking V1.

Cons:

- Apps or ThorVG platform bindings must implement adapters.
- Requires careful frame lifetime, thread-safety, and color-space contracts.
- Behavior may vary by platform decoder.

This is recommended.

### Option C: Optional external decoder plugin maintained beside ThorVG

ThorVG keeps the provider API, and separate optional packages implement FFmpeg/GStreamer/WebCodecs/etc.

Pros:

- Convenient for users who want batteries included.
- Keeps ThorVG core clean.

Cons:

- Still requires packaging and support policy.
- Must not become an implicit dependency of the core renderer.

This is a good phase after the provider API exists.

## 6. Rendering/backend strategy

### V1 portable path: decoded bitmap frames

The first implementation should support provider-returned bitmap frames in ThorVG-supported color spaces:

- `ColorSpace::ABGR8888`
- `ColorSpace::ARGB8888`
- `ColorSpace::ABGR8888S`
- `ColorSpace::ARGB8888S`

The frame should enter as a stable mutable image source, not as a newly-created `Picture` and `RawLoader` every frame. Creating a new picture/loader per frame would defeat texture caching and add allocator churn.

The current code needs one of these small abstractions:

- Add a mutable `RenderSurface` revision/serial and teach backends to refresh texture data when the serial changes.
- Or add a `RenderImageSource` wrapper with dimensions, color space, serial, CPU surface, and optional native handles.

The serial matters because the GL backend currently caches texture IDs by `RenderSurface*` and does not re-upload when the same source pointer changes. WebGPU already has an explicit refresh path when `RenderUpdateFlag::Image` is set, but GL needs equivalent mutable-surface handling.

### GPU-friendly path

Native decoded frames are often GPU-backed:

- Web: `VideoFrame`, `HTMLVideoElement`, `ImageBitmap`, WebGL/WebGPU external copy paths.
- iOS/macOS: `CVPixelBuffer`, `IOSurface`, Metal texture cache, OpenGL texture cache.
- Android: `Surface`, `ImageReader`, `AHardwareBuffer`, `SurfaceTexture`, `MediaCodec`.
- Desktop: OpenGL textures, Vulkan/Metal/D3D interop, DMA-BUF on Linux, or FFmpeg software `AVFrame`.

V1 should not require zero-copy GPU import. It should define frame types so native paths can be added without changing Lottie parsing again.

Backend policy:

- Software renderer: consume CPU bitmap frames.
- GL/WebGL: initially upload bitmap frames; later import/apply native texture handles where supported.
- WebGPU: initially upload bitmap frames; later support `WGPUTexture` or browser-supported external texture/copy paths.
- If a frame type is unsupported by the active backend, fall back to bitmap if the provider can supply one.
- If no usable frame is available, render the poster or nothing according to asset policy.

## 7. Timeline/playback sync

ThorVG should not run an independent video clock. The application already controls Lottie playback by calling `Animation::frame(no)`, and `LottieLoader` maps that frame into the parsed composition.

For a video layer, derive the media time from the same frame update:

```text
compositionFrame = frame passed to LottieBuilder after clamp
layerActive = layer.inFrame <= compositionFrame < layer.outFrame
localFrame = layer.timeRemap ? comp.frameAtTime(timeRemap(compositionFrame))
                             : (compositionFrame - layer.startFrame) / layer.timeStretch
mediaTimeSec = localFrame / comp.frameRate
```

Then apply media behavior:

- If `loop == true`, wrap `mediaTimeSec` modulo `asset.durationSec`.
- If `loop == false` and time exceeds duration, V1 should hold the last frame by default or render the poster if `holdLastFrame == false`.
- If `mediaTimeSec < 0`, render nothing or poster until the layer time reaches zero.

Playback controls:

- Play: the app advances `Animation::frame()` over time.
- Pause: the app stops advancing frames; ThorVG asks for the same frame and should reuse the previous decoded frame if serial/time did not change.
- Seek: the app sets a different frame; the provider should return the nearest frame for that media time or report pending/unavailable.
- Looping: layer/asset policy, not an internal ThorVG decoder loop.

The provider should be allowed to return:

- exact frame
- nearest frame within a tolerance
- pending frame, so ThorVG can keep the previous frame or poster
- unsupported/unavailable, so ThorVG can fallback cleanly

## 8. Transform/composition behavior

Video should behave like image wherever possible:

Supported in V1 through existing layer machinery:

- position
- scale
- rotation
- anchor
- opacity
- parent transforms
- in/out points
- start frame and time stretch
- composition ordering with vector and image layers
- clipping to the composition viewport
- normal image sizing from asset `w`/`h`

Composition behavior:

- The video frame is inserted into the layer scene as an image-like paint.
- Layer opacity is applied at the scene level, matching current image behavior.
- Blend method is set on the layer scene as it is today.
- Masks, mattes, and effects should route through the same scene composition path if the active backend can sample the video frame as a normal image.

V1 caveat:

- CPU bitmap frames can support the same masks/mattes/blend/effect path as images.
- Native GPU texture fast paths may need fallback for masks, mattes, non-normal blend modes, and effects. If a backend cannot use a native video handle in an offscreen composition pass, it must request/fallback to a bitmap frame or render the poster.

This keeps semantics correct before optimizing every backend-specific path.

## 9. Lottie/dotLottie asset schema proposal

The current Lottie spec has image layers (`ty: 2`) and static image assets. Image assets use `id`, `p`, `u`, `e`, `w`, `h`, and optional `sid`. The image asset docs also mention `t: "seq"` for image sequences. There is no standard video layer in the spec surface researched.

Recommended V1 schema: image-compatible layer with video extension and poster fallback.

```json
{
  "assets": [
    {
      "id": "video_hero",
      "w": 1280,
      "h": 720,
      "u": "i/",
      "p": "video_hero_poster.png",
      "e": 0,
      "x-video": {
        "src": "v/video_hero.mp4",
        "mime": "video/mp4",
        "duration": 2.4,
        "frameRate": 30,
        "loop": true,
        "holdLastFrame": true,
        "muted": true
      }
    }
  ],
  "layers": [
    {
      "ty": 2,
      "refId": "video_hero",
      "ip": 0,
      "op": 120,
      "st": 0,
      "sr": 1,
      "ks": {
        "a": { "k": [640, 360] },
        "p": { "k": [640, 360] },
        "s": { "k": [100, 100] },
        "r": { "k": 0 },
        "o": { "k": 100 }
      }
    }
  ]
}
```

Why this shape:

- Existing players can render the poster image or ignore the unsupported extension.
- ThorVG can preserve image-layer transform/composition semantics.
- Video-specific metadata is isolated under a vendor extension instead of overloading standard image sequence fields.

dotLottie constraints:

- dotLottie v1 uses `manifest.json`, `animations/`, and optional `images/`.
- dotLottie v2 uses `manifest.json`, `a/`, optional `i/`, optional `t/`, optional `s/`, and optional `f/`.
- Current dotLottie asset docs cover images and fonts; they do not define a video asset directory.
- Therefore, embedding video inside dotLottie is not portable unless the dotLottie spec accepts a video asset extension.

Recommended dotLottie V1 policy:

- Store poster images in the standard image directory (`images/` or `i/`).
- Put video files in a vendor extension directory such as `v/` only for players that explicitly support this extension.
- Or use an external/app-resolved `x-video.src` and keep dotLottie packages spec-compatible.
- Add a manifest extension only when needed:

```json
{
  "version": "2",
  "animations": [{ "id": "main" }],
  "x-videoAssets": [
    {
      "id": "video_hero",
      "path": "v/video_hero.mp4",
      "mime": "video/mp4",
      "poster": "i/video_hero_poster.png"
    }
  ]
}
```

The fallback poster is mandatory for forward compatibility.

## 10. ThorVG API proposal

Minimal C++ shape:

```cpp
class LottieAnimation final : public Animation {
public:
    Result videoProvider(LottieVideoFrameProvider* provider, void* data) noexcept;
};
```

Provider callback shape:

```cpp
using LottieVideoOpenCb = Result (*)(const LottieVideoAssetInfo*, void* user);
using LottieVideoFrameCb = Result (*)(const LottieVideoFrameRequest*,
                                      LottieVideoFrame*,
                                      void* user);
using LottieVideoCloseCb = void (*)(const char* assetId, void* user);

struct LottieVideoProvider {
    LottieVideoOpenCb open;
    LottieVideoFrameCb frame;
    LottieVideoCloseCb close;
    void* user;
};
```

Recommended API rules:

- Provider must be set before `Picture::load()` for initial asset open. This matches current resolver behavior.
- Provider owns decoding, frame queues, seek, and frame lifetime.
- ThorVG owns only the render reference while a frame is active.
- Provider calls must be allowed from the Lottie update task thread, so adapters must be thread-safe or ThorVG must document single-threaded callback mode.
- `frame()` should be nonblocking where possible. If decoding is pending, return a pending status and let ThorVG reuse the last frame/poster.
- Public C API should mirror the callback struct if this is shipped as public ThorVG API.

Alternative API:

- Extend `Picture::resolver()` to support time-dependent video. This is not recommended because the current resolver is an image/font asset loader, called once for unresolved image data, and has no timestamp or frame lifecycle contract.

## 11. Performance and memory strategy

Goals:

- Avoid per-frame `Picture`/loader allocation.
- Avoid CPU copies when native decoded frames can be sampled by the active backend.
- Avoid texture reallocation when dimensions and format are stable.
- Avoid blocking the render/update path on slow decode.

V1 CPU strategy:

- Provider owns a small ring buffer of decoded frames.
- ThorVG keeps one stable image source per video layer.
- Each provider frame has a serial/revision. If serial changes, ThorVG marks image content dirty.
- Software renderer reads the current CPU frame.
- GL/WG upload only when serial changes.
- If the requested frame is unavailable, reuse the last frame within layer visibility or render poster.

GPU strategy:

- Add optional frame types for backend-native texture handles.
- Do not require readback for normal blending.
- Fall back to CPU bitmap for masks, mattes, effects, or complex blends until native texture composition is proven per backend.
- Keep frame lifetime explicit: ThorVG must call provider release when backend no longer needs a frame.

Color and format:

- V1 should require RGBA/BGRA 8-bit CPU frames in ThorVG color spaces.
- YUV/NV12 should be handled by platform adapters or future shader paths, not the initial core path.
- Providers must declare premultiplied vs straight alpha. Most decoded video is opaque; alpha video can be supported only if the adapter supplies a compatible RGBA frame.

Threading:

- Decode should happen outside ThorVG update/render whenever possible.
- Provider `frame()` should be bounded and fast.
- The provider can pre-decode based on frame requests or app playback time.

Memory:

- Use 2 to 4 decoded frames per video layer by default.
- Cap decoded frame resolution to asset dimensions unless the adapter explicitly supports higher native resolution.
- Release frames on layer destruction, animation unload, or provider change.

## 12. Platform notes: web, iOS, Android, desktop

### Web

Use WebCodecs where available. WebCodecs exposes `VideoDecoder`, `EncodedVideoChunk`, and `VideoFrame`; `VideoFrame` has timestamp/duration metadata, pixel format, visible rect, display size, and can be copied to CPU memory if needed. It does not demux containers by itself, so adapters need a demuxer or a video element path.

Recommended web adapters:

- WebCodecs + demuxer for frame-accurate control.
- HTMLVideoElement/canvas fallback for simpler playback but weaker frame-accurate seeking.
- WebGPU/WebGL texture import/copy in future phases.

### iOS/macOS

Use AVFoundation/CoreVideo. `AVPlayerItemVideoOutput` can pull pixel buffers for item time. CoreVideo provides `CVPixelBuffer` and texture cache APIs for Metal/OpenGL interop.

Recommended Apple adapters:

- AVFoundation for decode and time mapping.
- `CVPixelBuffer` CPU fallback in BGRA.
- Metal/OpenGL texture cache fast path later.

### Android

Use MediaCodec. Android's docs note that raw frame access differs between Surface mode, ImageReader, Image, and ByteBuffer paths. `ImageReader` provides direct access to image data rendered to a `Surface`, but producers can stall if images are not acquired/released fast enough.

Recommended Android adapters:

- MediaCodec to `ImageReader` or `Image` for CPU fallback.
- MediaCodec to `SurfaceTexture`/`AHardwareBuffer` for future GPU path.
- Avoid secure/DRM surfaces for V1 because arbitrary compositing usually requires sampled pixels.

### Desktop

Use app-provided frames, FFmpeg, or GStreamer outside ThorVG.

Recommended desktop adapters:

- FFmpeg/libavcodec for broad software decode and deterministic tests.
- GStreamer `appsink` for pipeline-based integration and hardware decode where apps already use GStreamer.
- App-provided frames for engines that already have video pipelines.

Desktop V1 should not make FFmpeg or GStreamer a ThorVG dependency. Provide sample adapters separately.

## 13. Unsupported V1 cases

V1 should explicitly define these as unsupported or fallback-only:

- ThorVG-owned codec/container decoding.
- Audio playback or A/V sync.
- DRM or secure video surfaces.
- Network streaming inside ThorVG.
- Adaptive streaming formats such as HLS/DASH inside ThorVG.
- Reverse playback as a continuous real-time mode.
- Arbitrary-rate trick play beyond timeline seeking.
- Unsupported codecs, containers, and color formats.
- YUV shader sampling in the core path.
- Alpha video unless the provider supplies RGBA/BGRA frames with a clear premultiplication contract.
- Native GPU texture use with masks, mattes, effects, and non-normal blend modes unless the backend proves support; fallback to bitmap/poster.
- Video layers as matte sources in native-zero-copy mode unless the backend can sample them in the composition pass.
- Image sequence `t: "seq"` compatibility beyond not conflicting with it.

Reverse seeking should be allowed as a request, but providers may satisfy it by seeking to a keyframe and decoding forward, or may return pending/unavailable.

## 14. Implementation phases

Phase 0: spec and parser prototype

- Agree on the Lottie/dotLottie extension shape.
- Add parser recognition for `x-video` on image assets.
- Preserve poster fallback.
- Add model structs without changing renderer behavior.

Phase 1: provider API and CPU bitmap rendering

- Add provider registration API.
- Add `LottieVideo` internal model.
- Add `LottieBuilder::updateVideo()` parallel to `updateImage()`.
- Add a stable mutable image source or `RenderSurface` serial.
- Make GL refresh mutable surfaces on image dirty serial.
- Use provider-returned CPU frames only.

Phase 2: tests and deterministic sample provider

- Add fake provider that returns solid-color or checkerboard frames by timestamp.
- Verify timing, transforms, opacity, loop, hold, seek, and fallback.
- Add image comparison tests on software renderer first.

Phase 3: GPU-friendly handles

- Extend frame type for GL/WebGPU/native texture handles.
- Add backend import or copy paths.
- Add fallback when composition features require CPU or renderable texture support.

Phase 4: platform adapters outside core

- WebCodecs adapter.
- AVFoundation/CoreVideo adapter.
- Android MediaCodec adapter.
- FFmpeg/GStreamer sample desktop adapters.

Phase 5: dotLottie extension alignment

- Coordinate with dotLottie/Lottie spec maintainers if embedded video assets should become portable.
- Keep the poster fallback mandatory until a portable spec exists.

## 15. Test and benchmark plan

Unit tests:

- Parse `x-video` asset with poster.
- Parse image asset without `x-video` unchanged.
- Unknown video extension falls back to poster.
- Layer timing maps `ip`, `op`, `st`, `sr`, `tm`, and composition `fr` correctly.
- Loop and non-loop end behavior.
- Provider pending/unavailable behavior.

Rendering tests:

- Fake provider returns color by timestamp; verify expected frame color at key Lottie frames.
- Transform tests for position, scale, rotation, anchor.
- Opacity tests against expected compositing.
- Parent transform tests.
- Layer ordering with vector/image/video layers.
- Masks and mattes on software path.
- Blend mode smoke tests.

Backend tests:

- Software renderer with bitmap frames.
- GL renderer texture refresh when the frame serial changes.
- WebGPU renderer texture refresh when the frame serial changes.
- Fallback from unsupported native texture to bitmap/poster.

Performance benchmarks:

- CPU upload cost for 720p, 1080p, and 4K frames.
- Texture refresh cost on GL and WebGPU.
- End-to-end animation frame time with 1, 2, and 4 video layers.
- Memory under seek and loop stress.
- Provider pending rate under decode pressure.
- Comparison of poster fallback, CPU bitmap, and native texture paths.

Correctness metrics:

- Frame timestamp error vs requested Lottie time.
- Dropped/stale frame count.
- Render frame time p50/p95/p99.
- Peak decoded frame memory.
- Texture allocation count per second.

## 16. Risks and open questions

Risks:

- Nonstandard schema may not travel across Lottie players without fallback.
- dotLottie currently has no portable video asset directory.
- Backend-native texture import can become platform-specific quickly.
- GL currently needs a mutable image refresh mechanism for same-pointer frame updates.
- Time remap and reverse seeks can be expensive for inter-frame codecs.
- Color management and HDR are outside the simple RGBA V1 path.
- Provider callbacks from ThorVG task threads can surprise platform APIs that require a main thread or specific decoder queue.
- Full image-layer composition semantics may force CPU fallback for native video handles.

Open questions:

- Should the public API live on `LottieAnimation`, `Picture`, or a new asset-provider object?
- Should pending frame reuse the last video frame or poster by default?
- Should non-looping video hold the last frame or disappear after duration?
- How should alpha video be represented in metadata?
- Should ThorVG expose a generic mutable image-source API useful beyond Lottie video?
- Should the dotLottie extension use `v/`, `assets/video/`, or only external references until standardized?
- Should the provider be synchronous with a pending status, or split into async prefetch plus sync acquire?
- What is the minimum viable GPU import path for WebGL/WebGPU without overfitting?

## 17. Recommended V1 path

Recommended path:

1. Keep codecs outside ThorVG.
2. Add video metadata parsing as an image-layer extension with poster fallback.
3. Add a provider API that maps Lottie timeline time to decoded frames.
4. Implement bitmap frame rendering first through the existing image/composition path.
5. Add a stable mutable image-source serial so backends refresh frame content without reallocating paint/loader objects.
6. Defer native texture import until the CPU path is correct and measured.
7. Keep platform decoders as adapters outside core ThorVG.

Why this is the best V1:

- It matches current ThorVG loader and builder architecture.
- It gives the requested image-layer behavior with minimal renderer changes.
- It respects ThorVG's portability and lightweight build goals.
- It allows high performance where possible without making performance depend on every backend supporting every native video handle immediately.
- It fails gracefully through poster fallback or no-frame rendering.

The feasibility is good if the first milestone is scoped to provider-returned RGBA/BGRA bitmap frames. The main technical change needed for performance is not decoding; it is adding a stable mutable image source and backend refresh semantics so a video layer does not recreate textures and pictures every frame.

## Source map

Local ThorVG source inspected:

- `README.md`: supported primitives, modular design, portability, threading, smart rendering, render backends, Lottie and dotLottie context.
- `meson_options.txt`: selectable engines/loaders and optional extras.
- `inc/thorvg.h`: `ColorSpace`, `Picture::load(raw)`, asset resolver, `SwCanvas`, `GlCanvas`, `WgCanvas`, and `Animation` public APIs.
- `src/loaders/lottie/thorvg_lottie.h`: `LottieAnimation` marker, tween, slot, and quality APIs.
- `src/loaders/lottie/tvgLottieLoader.*`: parse/update lifecycle, task scheduling, frame/segment/duration handling.
- `src/loaders/lottie/tvgLottieParser.cpp`: image asset parsing and layer field parsing.
- `src/loaders/lottie/tvgLottieModel.*`: composition, layer, transform, image, and timing model.
- `src/loaders/lottie/tvgLottieBuilder.*`: scene build/update, image layer update, matte/effect handling, layer transform/composition.
- `src/renderer/tvgPicture.h`: bitmap/vector picture update/render path and image asset resolver behavior.
- `src/renderer/tvgRender.h`: `RenderSurface` and `RenderMethod` image backend contract.
- `src/loaders/raw/tvgRawLoader.cpp`: raw bitmap loader and copy/no-copy behavior.
- `src/renderer/cpu_engine/tvgSwRenderer.cpp`: software image prepare path.
- `src/renderer/gpu_engine/gl/tvgGlRenderer.cpp` and `tvgGlTextureMgr.cpp`: GL image rendering and texture caching by `RenderSurface*`.
- `src/renderer/gpu_engine/wg/tvgWgRenderer.cpp`, `tvgWgTextureMgr.cpp`, and `tvgWgRenderData.h`: WebGPU image rendering, texture upload, and image render data.

External references:

- ThorVG GitHub: https://github.com/thorvg/thorvg
- Lottie layer spec: https://lottie.github.io/lottie-spec/latest/specs/layers/
- Lottie helper/transform spec: https://lottie.github.io/lottie-spec/latest/specs/helpers/
- Lottie asset docs: https://lottiefiles.github.io/lottie-docs/assets/
- dotLottie spec overview: https://dotlottie.io/spec/
- dotLottie v1.0 spec: https://dotlottie.io/spec/1.0/
- dotLottie v2.0 spec: https://dotlottie.io/spec/2.0/
- MDN WebCodecs API: https://developer.mozilla.org/en-US/docs/Web/API/WebCodecs_API
- MDN VideoFrame: https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame
- Apple AVPlayerItemVideoOutput: https://developer.apple.com/documentation/avfoundation/avplayeritemvideooutput
- Apple CoreVideo: https://developer.apple.com/documentation/CoreVideo
- Android MediaCodec: https://developer.android.com/reference/android/media/MediaCodec
- Android ImageReader: https://developer.android.com/reference/android/media/ImageReader
- FFmpeg decoding API docs: https://ffmpeg.org/doxygen/4.2/group__lavc__decoding.html
- GStreamer appsink docs: https://gstreamer.freedesktop.org/documentation/app/appsink.html
