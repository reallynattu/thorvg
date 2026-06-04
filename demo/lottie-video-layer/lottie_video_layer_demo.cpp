/*
 * ThorVG Lottie video-layer demo.
 *
 * Builds against the local ThorVG tree and renders a Lottie image layer with an
 * x-video extension. The provider below is intentionally a tiny synthetic video
 * source: ThorVG requests frames by timeline time, and the provider returns a
 * time-varying ARGB bitmap.
 */

#include <thorvg.h>
#include <thorvg_lottie.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace tvg;
using namespace std;

static constexpr uint32_t CANVAS_W = 320;
static constexpr uint32_t CANVAS_H = 180;
static constexpr uint32_t VIDEO_W = 200;
static constexpr uint32_t VIDEO_H = 112;
static constexpr uint32_t OUTPUT_FRAMES = 300;
static constexpr float OUTPUT_FPS = 30.0f;
static constexpr float LOTTIE_FPS = 30.0f;

struct DemoVideoCtx
{
    vector<uint32_t> pixels = vector<uint32_t>(VIDEO_W * VIDEO_H);
    uint32_t opened = 0;
    uint32_t framed = 0;
    uint32_t closed = 0;
};

static uint32_t argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

static Result videoOpen(const LottieVideoAssetInfo* asset, void* data)
{
    auto ctx = static_cast<DemoVideoCtx*>(data);
    ++ctx->opened;
    cout << "opened video asset id=" << (asset->assetId ? asset->assetId : "")
         << " src=" << (asset->src ? asset->src : "")
         << " duration=" << asset->duration << "s\n";
    return Result::Success;
}

static Result videoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<DemoVideoCtx*>(data);
    ++ctx->framed;

    const auto t = request->time;
    const auto frame = static_cast<uint32_t>(llround(t * LOTTIE_FPS));
    const auto cx = 24.0 + fmod(t * 88.0, double(VIDEO_W - 48));
    const auto cy = VIDEO_H * 0.5 + sin(t * 5.0) * 24.0;

    for (uint32_t y = 0; y < VIDEO_H; ++y) {
        for (uint32_t x = 0; x < VIDEO_W; ++x) {
            auto wave = 0.5 + 0.5 * sin((double(x) * 0.11) + (t * 4.0));
            auto stripe = ((x + frame * 3) / 14) % 2;
            auto r = static_cast<uint8_t>(32 + 96 * wave + (stripe ? 70 : 0));
            auto g = static_cast<uint8_t>(34 + (double(y) / VIDEO_H) * 150);
            auto b = static_cast<uint8_t>(90 + 105 * (1.0 - wave));

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
            ctx->pixels[y * VIDEO_W + x] = argb(r, g, b);
        }
    }

    out->type = LottieVideoFrameType::Bitmap;
    out->data = ctx->pixels.data();
    out->width = VIDEO_W;
    out->height = VIDEO_H;
    out->colorSpace = ColorSpace::ARGB8888;
    out->timestamp = t;
    out->duration = 1.0 / LOTTIE_FPS;
    out->serial = frame + 1;
    return Result::Success;
}

static void videoClose(const char* assetId, void* data)
{
    auto ctx = static_cast<DemoVideoCtx*>(data);
    ++ctx->closed;
    cout << "closed video asset id=" << (assetId ? assetId : "") << "\n";
}

static bool ensureDir(const string& path)
{
    if (mkdir(path.c_str(), 0755) == 0) return true;
    return errno == EEXIST;
}

static bool writePpm(const string& path, const vector<uint32_t>& pixels, uint32_t w, uint32_t h)
{
    ofstream out(path, ios::binary);
    if (!out) return false;
    out << "P6\n" << w << " " << h << "\n255\n";
    for (auto pixel : pixels) {
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((pixel >> 16) & 0xff),
            static_cast<unsigned char>((pixel >> 8) & 0xff),
            static_cast<unsigned char>(pixel & 0xff)
        };
        out.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
    }
    return out.good();
}

static string framePath(const string& outDir, uint32_t idx)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/frame_%03u.ppm", outDir.c_str(), idx);
    return buf;
}

static const char* demoLottie()
{
    return R"({
        "v":"5.8.0","fr":30,"ip":0,"op":300,"w":320,"h":180,
        "assets":[{
            "id":"video_demo","w":200,"h":112,"u":"","p":"poster.png","e":0,
            "x-video":{"src":"synthetic://moving-gradient","mime":"video/x-synthetic","duration":10,"frameRate":30,"loop":true,"holdLastFrame":true,"muted":true}
        }],
        "layers":[{
            "ind":1,"ty":4,"sr":1,"ip":0,"op":300,"st":0,
            "shapes":[{
                "ty":"rc","s":{"a":0,"k":[204,116]},"p":{"a":0,"k":[168,96]},"r":{"a":0,"k":5}
            },{
                "ty":"st","c":{"a":0,"k":[0.18,0.88,1,1]},"o":{"a":0,"k":92},"w":{"a":0,"k":3}
            }]
        },{
            "ind":2,"ty":2,"refId":"video_demo","sr":1,"ip":0,"op":300,"st":0,
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
                "ty":"rc","s":{"a":0,"k":[214,126]},"p":{"a":0,"k":[167,97]},"r":{"a":0,"k":7}
            },{
                "ty":"fl","c":{"a":0,"k":[0.9,0.95,1,1]},"o":{"a":0,"k":18}
            }]
        },{
            "ind":4,"ty":4,"sr":1,"ip":0,"op":300,"st":0,
            "shapes":[{
                "ty":"rc","s":{"a":0,"k":[320,180]},"p":{"a":0,"k":[160,90]}
            },{
                "ty":"fl","c":{"a":0,"k":[0.025,0.035,0.055,1]},"o":{"a":0,"k":100}
            }]
        }]
    })";
}

int main(int argc, char** argv)
{
    const string outDir = argc > 1 ? argv[1] : "demo/lottie-video-layer/out";
    if (!ensureDir(outDir)) {
        cerr << "failed to create output directory: " << outDir << "\n";
        return 1;
    }

    if (Initializer::init() != Result::Success) {
        cerr << "ThorVG initialization failed\n";
        return 1;
    }

    DemoVideoCtx ctx;
    LottieVideoProvider provider = {videoOpen, videoFrame, videoClose, &ctx};

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    if (!animation || animation->videoProvider(&provider) != Result::Success) {
        cerr << "failed to configure Lottie video provider\n";
        Initializer::term();
        return 1;
    }

    auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
    vector<uint32_t> buffer(CANVAS_W * CANVAS_H, 0);
    if (!canvas || canvas->target(buffer.data(), CANVAS_W, CANVAS_W, CANVAS_H, ColorSpace::ARGB8888) != Result::Success) {
        cerr << "failed to create software canvas\n";
        Initializer::term();
        return 1;
    }

    auto picture = animation->picture();
    const auto* lottie = demoLottie();
    {
        ofstream json(outDir + "/demo.lottie.json");
        json << lottie << "\n";
    }
    if (picture->load(lottie, strlen(lottie), "lot", ".", true) != Result::Success) {
        cerr << "failed to load demo Lottie\n";
        Initializer::term();
        return 1;
    }

    if (canvas->add(picture) != Result::Success) {
        cerr << "failed to add Lottie picture to canvas\n";
        Initializer::term();
        return 1;
    }

    for (uint32_t i = 0; i < OUTPUT_FRAMES; ++i) {
        const auto lottieFrame = (float(i) / OUTPUT_FPS) * LOTTIE_FPS;
        if (i > 0 && animation->frame(lottieFrame) != Result::Success) {
            cerr << "animation frame update failed at output frame " << i << "\n";
            Initializer::term();
            return 1;
        }
        if (i > 0 && canvas->update() != Result::Success) {
            cerr << "canvas update failed at output frame " << i << "\n";
            Initializer::term();
            return 1;
        }
        if (canvas->draw(true) != Result::Success) {
            cerr << "canvas draw failed at output frame " << i << "\n";
            Initializer::term();
            return 1;
        }
        if (canvas->sync() != Result::Success) {
            cerr << "canvas sync failed at output frame " << i << "\n";
            Initializer::term();
            return 1;
        }
        if (!writePpm(framePath(outDir, i), buffer, CANVAS_W, CANVAS_H)) {
            cerr << "failed to write frame " << i << "\n";
            Initializer::term();
            return 1;
        }
    }

    animation->videoProvider(nullptr);
    cout << "rendered " << OUTPUT_FRAMES << " frames to " << outDir << "\n"
         << "provider calls: opened=" << ctx.opened
         << " framed=" << ctx.framed
         << " closed=" << ctx.closed << "\n";

    canvas.reset();
    animation.reset();

    auto term = Initializer::term();
    return term == Result::Success ? 0 : 1;
}
