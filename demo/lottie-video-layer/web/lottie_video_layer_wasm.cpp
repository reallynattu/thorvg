#include <thorvg.h>
#include <thorvg_lottie.h>
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace tvg;
using namespace std;

static constexpr uint32_t CANVAS_W = 1280;
static constexpr uint32_t CANVAS_H = 720;
static constexpr uint32_t VIDEO_W = 800;
static constexpr uint32_t VIDEO_H = 448;
static constexpr float LOTTIE_FPS = 30.0f;
static constexpr float VIDEO_DURATION = 10.0f;

struct DemoVideoCtx
{
    vector<uint32_t> pixels = vector<uint32_t>(VIDEO_W * VIDEO_H);
    uint32_t opened = 0;
    uint32_t framed = 0;
    uint32_t realFramed = 0;
    uint32_t fallbackFramed = 0;
    uint32_t closed = 0;
};

struct DemoVariant
{
    const char* name;
    const char* assetId;
    const char* src;
    const char* strokeColor;
    const char* washColor;
    uint8_t fallbackR;
    uint8_t fallbackG;
    uint8_t fallbackB;
    uint8_t maskMode;
};

enum VariantMaskMode : uint8_t
{
    MaskNone = 0,
    MaskStatic = 1,
    MaskAnimatedCrop = 2
};

static constexpr uint32_t VARIANT_COUNT = 8;
static const DemoVariant VARIANTS[VARIANT_COUNT] = {
    {"MDN Flower", "video_flower", "assets/videos/mdn-flower.mp4", "0.18,0.88,1,1", "0.9,0.95,1,1", 214, 66, 146, MaskNone},
    {"MDN Friday", "video_friday", "assets/videos/mdn-friday.mp4", "1,0.76,0.22,1", "1,0.9,0.64,1", 238, 140, 46, MaskNone},
    {"Sample.Cat Seawater", "video_seawater", "assets/videos/samplecat-seawater.mp4", "0.32,0.93,0.66,1", "0.7,1,0.86,1", 43, 148, 222, MaskNone},
    {"TrueFileSize Sample", "video_sample", "assets/videos/truefilesize-sample.mp4", "0.72,0.62,1,1", "0.88,0.84,1,1", 139, 92, 246, MaskNone},
    {"MP4.to Pattern 720p", "video_pattern_720p", "assets/videos/mp4to-720p-pattern.mp4", "0.1,0.8,1,1", "0.68,0.93,1,1", 18, 168, 232, MaskNone},
    {"Sample.Cat Seawater 1080p", "video_seawater_1080p", "assets/videos/samplecat-seawater-1080p.mp4", "0.04,0.98,0.94,1", "0.62,1,0.96,1", 12, 214, 198, MaskNone},
    {"Masked Flower", "video_masked_flower", "assets/videos/mdn-flower.mp4", "0.36,1,0.68,1", "0.65,1,0.82,1", 142, 222, 96, MaskStatic},
    {"Animated Crop + Mask", "video_crop_mask", "assets/videos/samplecat-seawater-1080p.mp4", "1,0.68,0.3,1", "1,0.82,0.58,1", 250, 156, 68, MaskAnimatedCrop}
};

static DemoVideoCtx ctx;
static unique_ptr<LottieAnimation> animation;
static unique_ptr<SwCanvas> canvas;
static vector<uint32_t> buffer(CANVAS_W * CANVAS_H, 0);
static string lottieJson;
static uint32_t selectedVariant = 0;
static bool tvgInitialized = false;
static bool initialized = false;
static bool firstRender = true;
static int lastStatus = 0;

EM_JS(int, video_open_js, (const char* srcPtr, int width, int height), {
    const src = UTF8ToString(srcPtr);
    Module.__thorvgVideoAssets ||= {};
    let asset = Module.__thorvgVideoAssets[src];
    if (!asset) {
        const video = document.createElement("video");
        video.src = src;
        video.muted = true;
        video.loop = true;
        video.playsInline = true;
        video.preload = "auto";
        video.style.display = "none";

        const canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext("2d", { willReadFrequently: true });

        asset = { video, canvas, ctx, serial: 0, ready: false };
        const markReady = () => {
            asset.ready = true;
            video.play().catch(() => {});
        };
        video.addEventListener("loadeddata", markReady);
        video.addEventListener("canplay", markReady);
        document.body.appendChild(video);
        Module.__thorvgVideoAssets[src] = asset;
    }

    try {
        asset.video.currentTime = 0;
    } catch (e) {
    }
    asset.video.play().catch(() => {});
    return 1;
});

EM_JS(int, video_frame_js, (const char* srcPtr, double mediaTime, int width, int height, uintptr_t dstPtr), {
    const src = UTF8ToString(srcPtr);
    const asset = Module.__thorvgVideoAssets && Module.__thorvgVideoAssets[src];
    if (!asset || !asset.video || asset.video.readyState < 2) return 0;

    const video = asset.video;
    if (video.paused) video.play().catch(() => {});

    if (Number.isFinite(video.duration) && video.duration > 0.05) {
        const targetTime = ((mediaTime % video.duration) + video.duration) % video.duration;
        if (Math.abs(video.currentTime - targetTime) > 0.5 && !video.seeking) {
            try {
                video.currentTime = targetTime;
            } catch (e) {
            }
        }
    }

    try {
        asset.ctx.drawImage(video, 0, 0, width, height);
        const data = asset.ctx.getImageData(0, 0, width, height).data;
        Module.HEAPU8.set(data, dstPtr);
        asset.serial = (asset.serial + 1) >>> 0;
        return asset.serial || 1;
    } catch (e) {
        return 0;
    }
});

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

static const DemoVariant& variantForSrc(const char* src)
{
    if (src) {
        if (strcmp(src, VARIANTS[selectedVariant].src) == 0) return VARIANTS[selectedVariant];
        for (uint32_t i = 0; i < VARIANT_COUNT; ++i) {
            if (strcmp(src, VARIANTS[i].src) == 0) return VARIANTS[i];
        }
    }
    return VARIANTS[selectedVariant];
}

static Result videoOpen(const LottieVideoAssetInfo* asset, void*)
{
    ++ctx.opened;
    if (asset && asset->src) video_open_js(asset->src, VIDEO_W, VIDEO_H);
    return Result::Success;
}

static Result videoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void*)
{
    ++ctx.framed;

    const auto t = request->time;
    const auto frame = static_cast<uint32_t>(llround(t * LOTTIE_FPS));
    const auto* src = (request && request->asset) ? request->asset->src : nullptr;
    const auto realSerial = src ? video_frame_js(src, t, VIDEO_W, VIDEO_H, reinterpret_cast<uintptr_t>(ctx.pixels.data())) : 0;

    if (realSerial > 0) {
        ++ctx.realFramed;
        out->type = LottieVideoFrameType::Bitmap;
        out->data = ctx.pixels.data();
        out->width = VIDEO_W;
        out->height = VIDEO_H;
        out->colorSpace = ColorSpace::ABGR8888;
        out->timestamp = t;
        out->duration = 1.0 / LOTTIE_FPS;
        out->serial = (uint64_t(selectedVariant) << 32) | uint32_t(realSerial);
        return Result::Success;
    }

    ++ctx.fallbackFramed;
    const auto& variant = variantForSrc(src);
    const auto cx = 24.0 + fmod(t * 88.0, double(VIDEO_W - 48));
    const auto cy = VIDEO_H * 0.5 + sin(t * 5.0) * 24.0;

    for (uint32_t y = 0; y < VIDEO_H; ++y) {
        for (uint32_t x = 0; x < VIDEO_W; ++x) {
            auto wave = 0.5 + 0.5 * sin((double(x) * 0.11) + (t * 4.0));
            auto stripe = ((x + frame * 3) / 14) % 2;
            auto r = static_cast<uint8_t>(20 + variant.fallbackR * 0.45 * wave + (stripe ? 45 : 0));
            auto g = static_cast<uint8_t>(20 + variant.fallbackG * 0.35 + (double(y) / VIDEO_H) * 120);
            auto b = static_cast<uint8_t>(35 + variant.fallbackB * 0.48 * (1.0 - wave));

            auto dx = double(x) - cx;
            auto dy = double(y) - cy;
            if ((dx * dx + dy * dy) < 20.0 * 20.0) {
                r = 255;
                g = 236;
                b = 68;
            }
            if (abs(int(x) - int(cx)) < 3) {
                r = 45;
                g = 245;
                b = 255;
            }
            if (((x + y + frame * 2) % 37) < 2) {
                r = static_cast<uint8_t>(min(255, int(r) + 45));
                g = static_cast<uint8_t>(min(255, int(g) + 45));
                b = static_cast<uint8_t>(min(255, int(b) + 45));
            }
            ctx.pixels[y * VIDEO_W + x] = argb(r, g, b);
        }
    }

    out->type = LottieVideoFrameType::Bitmap;
    out->data = ctx.pixels.data();
    out->width = VIDEO_W;
    out->height = VIDEO_H;
    out->colorSpace = ColorSpace::ARGB8888;
    out->timestamp = t;
    out->duration = 1.0 / LOTTIE_FPS;
    out->serial = frame + 1;
    return Result::Success;
}

static void videoClose(const char*, void*)
{
    ++ctx.closed;
}

static const char* maskProperties(const DemoVariant& variant)
{
    switch (variant.maskMode) {
        case MaskStatic:
            return R"(
            "masksProperties":[{
                "nm":"Static diamond alpha mask","mode":"a","inv":false,
                "o":{"a":0,"k":100},"x":{"a":0,"k":0},
                "pt":{"a":0,"k":{
                    "i":[[0,0],[0,0],[0,0],[0,0]],
                    "o":[[0,0],[0,0],[0,0],[0,0]],
                    "v":[[100,4],[192,56],[100,108],[8,56]],
                    "c":true
                }}
            }],)";
        case MaskAnimatedCrop:
            return R"(
            "masksProperties":[{
                "nm":"Animated crop rectangle","mode":"a","inv":false,
                "o":{"a":0,"k":100},"x":{"a":1,"k":[
                    {"t":0,"s":[0],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":150,"s":[4],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":300,"s":[0]}
                ]},
                "pt":{"a":1,"k":[
                    {"t":0,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[22,10],[178,10],[178,102],[22,102]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":105,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[48,22],[158,22],[158,92],[48,92]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":210,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[10,18],[190,18],[190,96],[10,96]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":300,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[22,10],[178,10],[178,102],[22,102]],
                        "c":true
                    }]}
                ]}
            },{
                "nm":"Animated intersect mask","mode":"i","inv":false,
                "o":{"a":1,"k":[
                    {"t":0,"s":[100],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":150,"s":[78],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":300,"s":[100]}
                ]},"x":{"a":0,"k":0},
                "pt":{"a":1,"k":[
                    {"t":0,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[96,2],[190,50],[108,110],[18,62]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":120,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[118,8],[194,66],[82,108],[28,40]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":240,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[76,0],[178,42],[126,112],[14,72]],
                        "c":true
                    }],"o":{"x":[0.333],"y":[0]},"i":{"x":[0.667],"y":[1]}},
                    {"t":300,"s":[{
                        "i":[[0,0],[0,0],[0,0],[0,0]],
                        "o":[[0,0],[0,0],[0,0],[0,0]],
                        "v":[[96,2],[190,50],[108,110],[18,62]],
                        "c":true
                    }]}
                ]}
            }],)";
        default:
            return "";
    }
}

static string demoLottie(const DemoVariant& variant)
{
    char json[50000];
    snprintf(json, sizeof(json), R"({
        "v":"5.8.0","fr":30,"ip":0,"op":300,"w":320,"h":180,
        "assets":[{
            "id":"%s","w":200,"h":112,"u":"","p":"poster.png","e":0,
            "x-video":{"src":"%s","mime":"video/mp4","duration":10,"frameRate":30,"loop":true,"holdLastFrame":true,"muted":true}
        }],
        "layers":[{
            "ind":1,"ty":4,"sr":1,"ip":0,"op":300,"st":0,
            "shapes":[{
                "ty":"rc",
                "s":{"a":1,"k":[
                    {"t":0,"s":[204,116],"e":[216,124],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":120,"s":[216,124],"e":[198,112],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":225,"s":[198,112],"e":[204,116],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[204,116]}
                ]},
                "p":{"a":1,"k":[
                    {"t":0,"s":[168,96],"e":[164,92],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":120,"s":[164,92],"e":[171,99],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":225,"s":[171,99],"e":[168,96],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[168,96]}
                ]},
                "r":{"a":1,"k":[
                    {"t":0,"s":[5],"e":[12],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[12],"e":[5],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[5]}
                ]}
            },{
                "ty":"st","c":{"a":0,"k":[%s]},
                "o":{"a":1,"k":[
                    {"t":0,"s":[72],"e":[98],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[98],"e":[72],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[72]}
                ]},
                "w":{"a":1,"k":[
                    {"t":0,"s":[2.2],"e":[4.2],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[4.2],"e":[2.2],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[2.2]}
                ]}
            }]
        },{
            "ind":2,"ty":2,"refId":"%s","sr":1,"ip":0,"op":300,"st":0,
            %s
            "ks":{
                "o":{"a":0,"k":100},
                "r":{"a":1,"k":[
                    {"t":0,"s":[-18],"e":[18],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":90,"s":[18],"e":[-8],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":180,"s":[-8],"e":[22],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":255,"s":[22],"e":[-18],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[-18]}
                ]},
                "sk":{"a":1,"k":[
                    {"t":0,"s":[-12],"e":[16],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":75,"s":[16],"e":[-18],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":165,"s":[-18],"e":[10],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":240,"s":[10],"e":[-12],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[-12]}
                ]},
                "sa":{"a":0,"k":-22},
                "p":{"a":0,"k":[160,90,0]},
                "a":{"a":0,"k":[100,56,0]},
                "s":{"a":0,"k":[92,106,100]}
            }
        },{
            "ind":3,"ty":4,"sr":1,"ip":0,"op":300,"st":0,
            "shapes":[{
                "ty":"rc",
                "s":{"a":1,"k":[
                    {"t":0,"s":[214,126],"e":[232,138],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[232,138],"e":[208,122],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[214,126]}
                ]},
                "p":{"a":1,"k":[
                    {"t":0,"s":[167,97],"e":[162,94],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[162,94],"e":[171,100],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[167,97]}
                ]},
                "r":{"a":1,"k":[
                    {"t":0,"s":[7],"e":[14],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[14],"e":[7],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[7]}
                ]}
            },{
                "ty":"fl","c":{"a":0,"k":[%s]},
                "o":{"a":1,"k":[
                    {"t":0,"s":[8],"e":[26],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[26],"e":[12],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[8]}
                ]}
            }]
        },{
            "ind":4,"ty":4,"sr":1,"ip":0,"op":300,"st":0,
            "shapes":[{
                "ty":"rc","s":{"a":0,"k":[340,200]},
                "p":{"a":1,"k":[
                    {"t":0,"s":[160,90],"e":[154,92],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[154,92],"e":[166,88],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[160,90]}
                ]}
            },{
                "ty":"fl",
                "c":{"a":1,"k":[
                    {"t":0,"s":[0.025,0.035,0.055,1],"e":[0.02,0.05,0.085,1],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[0.02,0.05,0.085,1],"e":[0.035,0.028,0.06,1],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[0.025,0.035,0.055,1]}
                ]},
                "o":{"a":0,"k":100}
            },{
                "ty":"rc","s":{"a":0,"k":[84,260]},
                "p":{"a":1,"k":[
                    {"t":0,"s":[32,90],"e":[288,90],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[32,90]}
                ]},"r":{"a":0,"k":0}
            },{
                "ty":"fl","c":{"a":0,"k":[0.14,0.64,0.9,1]},
                "o":{"a":1,"k":[
                    {"t":0,"s":[0],"e":[10],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":150,"s":[10],"e":[0],"i":{"x":[0.667],"y":[1]},"o":{"x":[0.333],"y":[0]}},
                    {"t":300,"s":[0]}
                ]}
            }]
        }]
    })",
        variant.assetId,
        variant.src,
        variant.strokeColor,
        variant.assetId,
        maskProperties(variant),
        variant.washColor);

    return string(json);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
int demo_init()
{
    if (initialized) return 0;
    ctx = DemoVideoCtx{};
    firstRender = true;
    lastStatus = 0;

    if (!tvgInitialized) {
        if (Initializer::init() != Result::Success) {
            lastStatus = 1;
            return lastStatus;
        }
        tvgInitialized = true;
    }

    LottieVideoProvider provider = {videoOpen, videoFrame, videoClose, nullptr};
    animation.reset(LottieAnimation::gen());
    if (!animation || animation->videoProvider(&provider) != Result::Success) {
        lastStatus = 2;
        return lastStatus;
    }

    canvas.reset(SwCanvas::gen());
    if (!canvas || canvas->target(buffer.data(), CANVAS_W, CANVAS_W, CANVAS_H, ColorSpace::ABGR8888) != Result::Success) {
        lastStatus = 3;
        return lastStatus;
    }

    auto picture = animation->picture();
    lottieJson = demoLottie(VARIANTS[selectedVariant]);
    if (picture->load(lottieJson.c_str(), lottieJson.size(), "lot", ".", true) != Result::Success) {
        lastStatus = 4;
        return lastStatus;
    }
    if (picture->size(CANVAS_W, CANVAS_H) != Result::Success) {
        lastStatus = 11;
        return lastStatus;
    }
    if (canvas->add(picture) != Result::Success) {
        lastStatus = 5;
        return lastStatus;
    }

    initialized = true;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int demo_render(double elapsedSec)
{
    if (!initialized) {
        auto init = demo_init();
        if (init != 0) return init;
    }

    auto mediaSec = fmod(elapsedSec, double(VIDEO_DURATION));
    if (mediaSec < 0.0) mediaSec += VIDEO_DURATION;
    auto lottieFrame = float(mediaSec * LOTTIE_FPS);

    if (!firstRender) {
        if (animation->frame(lottieFrame) != Result::Success) {
            lastStatus = 6;
            return lastStatus;
        }
        if (canvas->update() != Result::Success) {
            lastStatus = 7;
            return lastStatus;
        }
    }

    if (canvas->draw(true) != Result::Success) {
        lastStatus = 8;
        return lastStatus;
    }
    if (canvas->sync() != Result::Success) {
        lastStatus = 9;
        return lastStatus;
    }

    firstRender = false;
    lastStatus = 0;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint32_t* demo_buffer()
{
    return buffer.data();
}

EMSCRIPTEN_KEEPALIVE
int demo_width()
{
    return CANVAS_W;
}

EMSCRIPTEN_KEEPALIVE
int demo_height()
{
    return CANVAS_H;
}

EMSCRIPTEN_KEEPALIVE
int demo_opened()
{
    return int(ctx.opened);
}

EMSCRIPTEN_KEEPALIVE
int demo_framed()
{
    return int(ctx.framed);
}

EMSCRIPTEN_KEEPALIVE
int demo_closed()
{
    return int(ctx.closed);
}

EMSCRIPTEN_KEEPALIVE
int demo_real_framed()
{
    return int(ctx.realFramed);
}

EMSCRIPTEN_KEEPALIVE
int demo_fallback_framed()
{
    return int(ctx.fallbackFramed);
}

EMSCRIPTEN_KEEPALIVE
int demo_variant_count()
{
    return int(VARIANT_COUNT);
}

EMSCRIPTEN_KEEPALIVE
int demo_variant()
{
    return int(selectedVariant);
}

EMSCRIPTEN_KEEPALIVE
const char* demo_variant_name(int index)
{
    if (index < 0 || uint32_t(index) >= VARIANT_COUNT) return "";
    return VARIANTS[index].name;
}

EMSCRIPTEN_KEEPALIVE
const char* demo_variant_src(int index)
{
    if (index < 0 || uint32_t(index) >= VARIANT_COUNT) return "";
    return VARIANTS[index].src;
}

EMSCRIPTEN_KEEPALIVE
int demo_lottie_size()
{
    return int(lottieJson.size());
}

EMSCRIPTEN_KEEPALIVE
int demo_select_variant(int index)
{
    if (index < 0 || uint32_t(index) >= VARIANT_COUNT) {
        lastStatus = 10;
        return lastStatus;
    }

    if (animation) animation->videoProvider(nullptr);
    canvas.reset();
    animation.reset();
    initialized = false;
    firstRender = true;
    lastStatus = 0;
    selectedVariant = uint32_t(index);
    return demo_init();
}

EMSCRIPTEN_KEEPALIVE
int demo_status()
{
    return lastStatus;
}

EMSCRIPTEN_KEEPALIVE
void demo_shutdown()
{
    if (animation) animation->videoProvider(nullptr);
    canvas.reset();
    animation.reset();
    initialized = false;
    if (tvgInitialized) {
        Initializer::term();
        tvgInitialized = false;
    }
}

}
