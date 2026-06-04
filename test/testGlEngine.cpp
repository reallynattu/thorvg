/*
 * Copyright (c) 2026 ThorVG project. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>
#include "config.h"
#include <thorvg.h>
#ifdef THORVG_LOTTIE_LOADER_SUPPORT
#include <thorvg_lottie.h>
#endif
#include "catch.hpp"
#include "testGlEngine.h"

#if defined(THORVG_GL_TEST_SUPPORT)
#if defined(THORVG_GL_TARGET_GLES)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#endif

using namespace tvg;
using namespace std;

#if defined(THORVG_GL_TEST_SUPPORT)

#if defined(THORVG_LOTTIE_LOADER_SUPPORT)

struct GlLottieVideoTestCtx
{
    int opened = 0;
    int framed = 0;
    int closed = 0;
    int released = 0;
    int nativeReturned = 0;
    uint32_t lastFlags = 0;
    uint64_t nextSerial = 1;
    GLuint nativeTexture = 0;
    bool nativeFrame = false;
    bool nativeOnly = false;
    uint32_t pixels[4] = {};
};

static Result _glVideoOpen(const LottieVideoAssetInfo*, void* data)
{
    auto ctx = static_cast<GlLottieVideoTestCtx*>(data);
    ++ctx->opened;
    return Result::Success;
}

static Result _glVideoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<GlLottieVideoTestCtx*>(data);
    ++ctx->framed;
    ctx->lastFlags = request->flags;

    const auto color = request->time < 1.0 ? 0xffff0000 : 0xff00ff00;
    for (auto& pixel : ctx->pixels) pixel = color;

    auto nativeRequested = ctx->nativeFrame && (request->flags & static_cast<uint32_t>(LottieVideoFrameRequestFlag::GlTexture));
    if (ctx->nativeOnly && !nativeRequested) {
        out->type = LottieVideoFrameType::None;
        return Result::Success;
    }

    out->type = nativeRequested ? LottieVideoFrameType::GlTexture : LottieVideoFrameType::Bitmap;
    out->data = (ctx->nativeOnly && nativeRequested) ? nullptr : ctx->pixels;
    out->width = 2;
    out->height = 2;
    out->colorSpace = ColorSpace::ARGB8888;
    out->timestamp = request->time;
    out->duration = 1.0 / 30.0;
    out->serial = ctx->nextSerial++;
    if (nativeRequested) {
        ++ctx->nativeReturned;
        out->nativeId = ctx->nativeTexture;
        out->nativeTarget = GL_TEXTURE_2D;
        out->release = [](void* user) {
            auto ctx = static_cast<GlLottieVideoTestCtx*>(user);
            ++ctx->released;
            if (ctx->nativeTexture != 0) {
                glDeleteTextures(1, &ctx->nativeTexture);
                ctx->nativeTexture = 0;
            }
        };
        out->user = data;
    }
    return Result::Success;
}

static void _glVideoClose(const char*, void* data)
{
    auto ctx = static_cast<GlLottieVideoTestCtx*>(data);
    ++ctx->closed;
}

static void _glVideoLottie(char* out, size_t size)
{
    snprintf(out, size, R"({
        "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
        "assets":[{
            "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
            "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
        }],
        "layers":[{
            "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
            "ks":{
                "o":{"a":0,"k":100},
                "r":{"a":0,"k":0},
                "p":{"a":0,"k":[0,0,0]},
                "a":{"a":0,"k":[0,0,0]},
                "s":{"a":0,"k":[100,100,100]}
            }
        }]
    })");
}

static bool _glPixelChanged(const uint8_t* a, const uint8_t* b)
{
    return a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3];
}

static bool _glVideoRequestFlag(uint32_t flags, LottieVideoFrameRequestFlag flag)
{
    return (flags & static_cast<uint32_t>(flag)) != 0;
}

static GLuint _glCreateSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t pixels[2 * 2 * 4] = {
        r, g, b, a, r, g, b, a,
        r, g, b, a, r, g, b, a
    };
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texId;
}

TEST_CASE("GL Lottie Video Mutable Texture Refresh", "[tvgGlEngine]")
{
    TestGLEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        GlLottieVideoTestCtx ctx;
        LottieVideoProvider provider = {_glVideoOpen, _glVideoFrame, _glVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());

        auto picture = animation->picture();
        char lottie[2048];
        _glVideoLottie(lottie, sizeof(lottie));

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);

        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t first[4] = {};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, first);
        REQUIRE(ctx.framed == 2);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t second[4] = {};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, second);
        REQUIRE(ctx.framed == 3);
        REQUIRE(_glPixelChanged(first, second));
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Lottie Video Blend Requests Bitmap Fallback", "[tvgGlEngine]")
{
    TestGLEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        GlLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;

        LottieVideoProvider provider = {_glVideoOpen, _glVideoFrame, _glVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","bm":1,"sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));

        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed >= 2);
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(ctx.nativeReturned == 0);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Lottie Video Matte Source Requests Bitmap Fallback", "[tvgGlEngine]")
{
    TestGLEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        GlLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;

        LottieVideoProvider provider = {_glVideoOpen, _glVideoFrame, _glVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            },{
                "ind":2,"ty":4,"tt":1,"sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                },
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[2,2]},"p":{"a":0,"k":[1,1]}
                },{
                    "ty":"fl","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100}
                }]
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));

        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed >= 2);
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(ctx.nativeReturned == 0);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Lottie Video Native Texture Import", "[tvgGlEngine]")
{
    TestGLEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        GlLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        for (auto& pixel : ctx.pixels) pixel = 0xff000000;

        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());

        ctx.nativeTexture = _glCreateSolidTexture(255, 255, 255, 255);
        REQUIRE(ctx.nativeTexture != 0);

        LottieVideoProvider provider = {_glVideoOpen, _glVideoFrame, _glVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _glVideoLottie(lottie, sizeof(lottie));

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 0);
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(!_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_glVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));

        uint8_t sampled[4] = {};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sampled);
        REQUIRE(sampled[0] > 200);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] > 200);
        REQUIRE(sampled[3] > 200);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.released == 1);
        REQUIRE(ctx.nativeTexture == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

struct GlVideoBenchmarkCtx
{
    vector<uint32_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t serial = 1;
    uint32_t framed = 0;
    GLuint nativeTexture = 0;
};

struct GlVideoBenchmarkResult
{
    double avgMs = 0.0;
    uint32_t framed = 0;
    uint64_t bytes = 0;
};

static Result _glBenchmarkVideoOpen(const LottieVideoAssetInfo*, void*)
{
    return Result::Success;
}

static Result _glBenchmarkVideoFrame(const LottieVideoFrameRequest*, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<GlVideoBenchmarkCtx*>(data);
    ++ctx->framed;
    out->type = LottieVideoFrameType::Bitmap;
    out->data = ctx->pixels.data();
    out->width = ctx->width;
    out->height = ctx->height;
    out->colorSpace = ColorSpace::ARGB8888;
    out->timestamp = 0.0;
    out->duration = 1.0 / 30.0;
    out->serial = ctx->serial++;
    return Result::Success;
}

static Result _glBenchmarkNativeVideoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<GlVideoBenchmarkCtx*>(data);
    ++ctx->framed;
    if (!(request->flags & static_cast<uint32_t>(LottieVideoFrameRequestFlag::GlTexture)) || ctx->nativeTexture == 0) {
        out->type = LottieVideoFrameType::None;
        return Result::Success;
    }

    out->type = LottieVideoFrameType::GlTexture;
    out->width = 2;
    out->height = 2;
    out->colorSpace = ColorSpace::ARGB8888;
    out->timestamp = request->time;
    out->duration = 1.0 / 30.0;
    out->serial = ctx->serial++;
    out->nativeId = ctx->nativeTexture;
    out->nativeTarget = GL_TEXTURE_2D;
    return Result::Success;
}

static string _glBenchmarkVideoLottie(uint32_t width, uint32_t height)
{
    string ret = "{\"v\":\"5.8.0\",\"fr\":30,\"ip\":0,\"op\":120,\"w\":";
    ret += to_string(width);
    ret += ",\"h\":";
    ret += to_string(height);
    ret += ",\"assets\":[{\"id\":\"video_hero\",\"w\":";
    ret += to_string(width);
    ret += ",\"h\":";
    ret += to_string(height);
    ret += ",\"u\":\"\",\"p\":\"poster.raw\",\"e\":0,"
           "\"x-video\":{\"src\":\"video.mp4\",\"mime\":\"video/mp4\",\"duration\":4,\"frameRate\":30,\"loop\":true,\"holdLastFrame\":true,\"muted\":true}}],"
           "\"layers\":[{\"ind\":1,\"ty\":2,\"refId\":\"video_hero\",\"sr\":1,\"ip\":0,\"op\":120,\"st\":0,"
           "\"ks\":{\"o\":{\"a\":0,\"k\":100},\"r\":{\"a\":0,\"k\":0},"
           "\"p\":{\"a\":0,\"k\":[0,0,0]},\"a\":{\"a\":0,\"k\":[0,0,0]},"
           "\"s\":{\"a\":0,\"k\":[100,100,100]}}}]}";
    return ret;
}

static GlVideoBenchmarkResult _runGlBitmapBenchmark(uint32_t width, uint32_t height, uint32_t iterations)
{
    TestGLEngine engine(width, height);

    GlVideoBenchmarkCtx ctx;
    ctx.width = width;
    ctx.height = height;
    auto count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    ctx.pixels.resize(static_cast<size_t>(count), 0xffff0000);

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    REQUIRE(animation);
    LottieVideoProvider provider = {_glBenchmarkVideoOpen, _glBenchmarkVideoFrame, nullptr, &ctx};
    REQUIRE(animation->videoProvider(&provider) == Result::Success);

    auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
    REQUIRE(canvas);
    engine.target(canvas.get());

    auto picture = animation->picture();
    auto lottie = _glBenchmarkVideoLottie(width, height);
    REQUIRE(picture->load(lottie.c_str(), lottie.size(), "lot", TEST_DIR, true) == Result::Success);
    REQUIRE(canvas->add(picture) == Result::Success);
    REQUIRE(canvas->draw(true) == Result::Success);
    REQUIRE(canvas->sync() == Result::Success);

    auto begin = chrono::steady_clock::now();
    for (uint32_t i = 0; i < iterations; ++i) {
        REQUIRE(animation->frame(static_cast<float>(i + 1)) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    auto end = chrono::steady_clock::now();
    chrono::duration<double, milli> elapsed = end - begin;

    return {elapsed.count() / double(iterations), ctx.framed, count * sizeof(uint32_t)};
}

static GlVideoBenchmarkResult _runGlNativeBenchmark(uint32_t iterations)
{
    TestGLEngine engine(2, 2);

    auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
    REQUIRE(canvas);
    engine.target(canvas.get());

    GlVideoBenchmarkCtx ctx;
    ctx.nativeTexture = _glCreateSolidTexture(255, 255, 255, 255);
    REQUIRE(ctx.nativeTexture != 0);

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    REQUIRE(animation);
    LottieVideoProvider provider = {_glBenchmarkVideoOpen, _glBenchmarkNativeVideoFrame, nullptr, &ctx};
    REQUIRE(animation->videoProvider(&provider) == Result::Success);

    auto picture = animation->picture();
    char lottie[2048];
    _glVideoLottie(lottie, sizeof(lottie));
    REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
    REQUIRE(canvas->add(picture) == Result::Success);
    REQUIRE(canvas->draw(true) == Result::Success);
    REQUIRE(canvas->sync() == Result::Success);

    auto begin = chrono::steady_clock::now();
    for (uint32_t i = 0; i < iterations; ++i) {
        REQUIRE(animation->frame(static_cast<float>(i + 1)) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    auto end = chrono::steady_clock::now();
    chrono::duration<double, milli> elapsed = end - begin;

    REQUIRE(animation->videoProvider(nullptr) == Result::Success);
    glDeleteTextures(1, &ctx.nativeTexture);
    ctx.nativeTexture = 0;

    return {elapsed.count() / double(iterations), ctx.framed, 2 * 2 * sizeof(uint32_t)};
}

TEST_CASE("GL Performance Benchmarks For Lottie Video", "[.][benchmark][lottieVideo][tvgGlEngine]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto bitmap720 = _runGlBitmapBenchmark(1280, 720, 2);
        WARN("gl bitmap texture refresh 720p: " << bitmap720.avgMs << " ms/frame, provider frames="
                                                << bitmap720.framed << ", bytes=" << bitmap720.bytes);

        auto bitmap1080 = _runGlBitmapBenchmark(1920, 1080, 2);
        WARN("gl bitmap texture refresh 1080p: " << bitmap1080.avgMs << " ms/frame, provider frames="
                                                 << bitmap1080.framed << ", bytes=" << bitmap1080.bytes);

        auto native = _runGlNativeBenchmark(2);
        WARN("gl native texture import baseline: " << native.avgMs << " ms/frame, provider frames="
                                                   << native.framed << ", bytes=" << native.bytes);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

#endif

TEST_CASE("GL Basic draw", "[tvgGlEngine]")
{
    TestGLEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);

        engine.target(canvas.get());

        std::vector<MaskMethod> masks;
        masks.push_back(MaskMethod::None);
        masks.push_back(MaskMethod::Alpha);
        masks.push_back(MaskMethod::InvAlpha);
        masks.push_back(MaskMethod::Luma);
        masks.push_back(MaskMethod::InvLuma);
        masks.push_back(MaskMethod::Add);
        masks.push_back(MaskMethod::Subtract);
        masks.push_back(MaskMethod::Intersect);
        masks.push_back(MaskMethod::Difference);
        masks.push_back(MaskMethod::Lighten);
        masks.push_back(MaskMethod::Darken);

        std::vector<BlendMethod> methods;
        methods.push_back(BlendMethod::Normal);
        methods.push_back(BlendMethod::Multiply);
        methods.push_back(BlendMethod::Screen);
        methods.push_back(BlendMethod::Overlay);
        methods.push_back(BlendMethod::Darken);
        methods.push_back(BlendMethod::Lighten);
        methods.push_back(BlendMethod::ColorDodge);
        methods.push_back(BlendMethod::ColorBurn);
        methods.push_back(BlendMethod::HardLight);
        methods.push_back(BlendMethod::SoftLight);
        methods.push_back(BlendMethod::Difference);
        methods.push_back(BlendMethod::Hue);
        methods.push_back(BlendMethod::Saturation);
        methods.push_back(BlendMethod::Color);
        methods.push_back(BlendMethod::Luminosity);
        methods.push_back(BlendMethod::Add);
        methods.push_back(BlendMethod::Composition);

        auto mask = []() {
            auto mask = Shape::gen();
            mask->appendRect(0, 10, 20, 30, 5, 5);
            mask->opacity(127);
            mask->fill(255, 255, 255);
            return mask;
        };

        for (auto method : methods) {
            for (auto maskOp : masks) {
                // Arc Line
                auto shape1 = Shape::gen();
                REQUIRE(shape1->strokeFill(255, 255, 255, 255) == Result::Success);
                REQUIRE(shape1->strokeWidth(2) == Result::Success);
                REQUIRE(shape1->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape1->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape1) == Result::Success);

                // Cubic
                auto shape2 = Shape::gen();
                REQUIRE(shape2->moveTo(50, 25) == Result::Success);
                REQUIRE(shape2->cubicTo(62, 25, 75, 38, 75, 50) == Result::Success);
                REQUIRE(shape2->close() == Result::Success);
                REQUIRE(shape2->strokeFill(255, 0, 0, 125) == Result::Success);
                REQUIRE(shape2->strokeWidth(1) == Result::Success);
                REQUIRE(shape2->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape2->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape2) == Result::Success);

                // Fill
                auto shape3 = Shape::gen();
                REQUIRE(shape3->moveTo(0, 0) == Result::Success);
                REQUIRE(shape3->lineTo(20, 0) == Result::Success);
                REQUIRE(shape3->lineTo(20, 20) == Result::Success);
                REQUIRE(shape3->lineTo(0, 20) == Result::Success);
                REQUIRE(shape3->close() == Result::Success);
                REQUIRE(shape3->fill(255, 255, 255) == Result::Success);
                REQUIRE(shape3->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape3->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape3) == Result::Success);

                // Dashed Line shape
                auto shape4 = Shape::gen();
                float dashPattern[2] = {2.5f, 5.0f};
                REQUIRE(shape4->moveTo(0, 0) == Result::Success);
                REQUIRE(shape4->lineTo(25, 25) == Result::Success);
                REQUIRE(shape4->cubicTo(50, 50, 75, -75, 50, 100) == Result::Success);
                REQUIRE(shape4->close() == Result::Success);
                REQUIRE(shape4->fill(255, 255, 255) == Result::Success);
                REQUIRE(shape4->strokeFill(255, 0, 0, 255) == Result::Success);
                REQUIRE(shape4->strokeWidth(2) == Result::Success);
                REQUIRE(shape4->strokeDash(dashPattern, 2) == Result::Success);
                REQUIRE(shape4->strokeCap(StrokeCap::Round) == Result::Success);
                REQUIRE(shape4->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape4->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape4) == Result::Success);
            }
        }
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Image Draw", "[tvgGlEngine]")
{
    TestGLEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);

        engine.target(canvas.get());

        // raw image
        ifstream file(TEST_DIR "/rawimage_200x300.raw");
        if (!file.is_open()) return;
        auto data = (uint32_t*)malloc(sizeof(uint32_t) * (200 * 300));
        file.read(reinterpret_cast<char*>(data), sizeof(uint32_t) * 200 * 300);
        file.close();

        std::vector<MaskMethod> masks;
        masks.push_back(MaskMethod::None);
        masks.push_back(MaskMethod::Alpha);
        masks.push_back(MaskMethod::InvAlpha);
        masks.push_back(MaskMethod::Luma);
        masks.push_back(MaskMethod::InvLuma);
        masks.push_back(MaskMethod::Add);
        masks.push_back(MaskMethod::Subtract);
        masks.push_back(MaskMethod::Intersect);
        masks.push_back(MaskMethod::Difference);
        masks.push_back(MaskMethod::Lighten);
        masks.push_back(MaskMethod::Darken);

        std::vector<BlendMethod> methods;
        methods.push_back(BlendMethod::Normal);
        methods.push_back(BlendMethod::Multiply);
        methods.push_back(BlendMethod::Screen);
        methods.push_back(BlendMethod::Overlay);
        methods.push_back(BlendMethod::Darken);
        methods.push_back(BlendMethod::Lighten);
        methods.push_back(BlendMethod::ColorDodge);
        methods.push_back(BlendMethod::ColorBurn);
        methods.push_back(BlendMethod::HardLight);
        methods.push_back(BlendMethod::SoftLight);
        methods.push_back(BlendMethod::Difference);
        methods.push_back(BlendMethod::Hue);
        methods.push_back(BlendMethod::Saturation);
        methods.push_back(BlendMethod::Color);
        methods.push_back(BlendMethod::Luminosity);
        methods.push_back(BlendMethod::Add);
        methods.push_back(BlendMethod::Composition);

        auto mask = []() {
            auto mask = Shape::gen();
            mask->appendRect(0, 10, 20, 30, 5, 5);
            mask->fill(255, 255, 255);
            return mask;
        };

        for (auto method : methods) {
            for (auto maskOp : masks) {
                // Non-transformed images
                auto picture = Picture::gen();
                REQUIRE(picture->load(data, 200, 300, ColorSpace::ARGB8888, false) == Result::Success);
                REQUIRE(picture->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(picture->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(picture) == Result::Success);

                // Clipped images
                auto picture2 = picture->duplicate();
                REQUIRE(picture2->clip(mask()) == Result::Success);
                REQUIRE(canvas->add(picture2) == Result::Success);

                // Transformed images
                auto picture3 = picture->duplicate();
                REQUIRE(picture3->rotate(45) == Result::Success);
                REQUIRE(canvas->add(picture3) == Result::Success);

                // Up-scaled Image
                auto picture4 = picture->duplicate();
                REQUIRE(picture4->scale(2.0f) == Result::Success);
                REQUIRE(canvas->add(picture4) == Result::Success);

                // Down-scaled Image
                auto picture5 = picture->duplicate();
                REQUIRE(picture5->scale(0.25f) == Result::Success);
                REQUIRE(canvas->add(picture5) == Result::Success);

                // Direct Clipped image
                auto picture6 = Picture::gen();
                REQUIRE(picture6->load(data, 200, 300, ColorSpace::ARGB8888, false) == Result::Success);
                REQUIRE(picture6->clip(mask()) == Result::Success);
                REQUIRE(picture6->blend(method) == Result::Success);
                REQUIRE(canvas->add(picture6) == Result::Success);

                // Scaled Clipped image
                auto picture7 = picture6->duplicate();
                REQUIRE(picture7->scale(2.0f) == Result::Success);
                REQUIRE(canvas->add(picture7) == Result::Success);
            }
        }

        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        free(data);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Filling Draw", "[tvgGlEngine]")
{
    TestGLEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);

        engine.target(canvas.get());

        std::vector<MaskMethod> masks;
        masks.push_back(MaskMethod::None);
        masks.push_back(MaskMethod::Alpha);
        masks.push_back(MaskMethod::InvAlpha);
        masks.push_back(MaskMethod::Luma);
        masks.push_back(MaskMethod::InvLuma);
        masks.push_back(MaskMethod::Add);
        masks.push_back(MaskMethod::Subtract);
        masks.push_back(MaskMethod::Intersect);
        masks.push_back(MaskMethod::Difference);
        masks.push_back(MaskMethod::Lighten);
        masks.push_back(MaskMethod::Darken);

        std::vector<BlendMethod> methods;
        methods.push_back(BlendMethod::Normal);
        // Enabling the blend modes below on EGL/GLES currently triggers shader errors.
        // Keep them disabled until the shader path is fixed.
        // methods.push_back(BlendMethod::Multiply);
        // methods.push_back(BlendMethod::Screen);
        // methods.push_back(BlendMethod::Overlay);
        // methods.push_back(BlendMethod::Darken);
        // methods.push_back(BlendMethod::Lighten);
        // methods.push_back(BlendMethod::ColorDodge);
        // methods.push_back(BlendMethod::ColorBurn);
        // methods.push_back(BlendMethod::HardLight);
        // methods.push_back(BlendMethod::SoftLight);
        // methods.push_back(BlendMethod::Difference);
        // methods.push_back(BlendMethod::Hue);
        // methods.push_back(BlendMethod::Saturation);
        // methods.push_back(BlendMethod::Color);
        // methods.push_back(BlendMethod::Luminosity);
        // methods.push_back(BlendMethod::Add);
        // methods.push_back(BlendMethod::Composition);

        auto mask = []() {
            auto mask = Shape::gen();
            mask->appendRect(10, 10, 20, 30, 5, 5);
            mask->opacity(127);
            mask->fill(255, 255, 255);
            return mask;
        };

        Fill::ColorStop cs[4] = {
            {0.1f, 0, 0, 0, 0},
            {0.2f, 50, 25, 50, 25},
            {0.5f, 100, 100, 100, 125},
            {0.9f, 255, 255, 255, 255}};

        for (auto method : methods) {
            for (auto maskOp : masks) {
                // Linear Gradient
                auto linear = LinearGradient::gen();
                REQUIRE(linear->colorStops(cs, 4) == Result::Success);
                REQUIRE(linear->spread(FillSpread::Repeat) == Result::Success);
                REQUIRE(linear->linear(0.0f, 0.0f, 100.0f, 120.0f) == Result::Success);

                auto shape = Shape::gen();
                REQUIRE(shape->appendRect(0, 0, 50, 50, 5, 5) == Result::Success);
                REQUIRE(shape->fill(linear) == Result::Success);
                REQUIRE(shape->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape) == Result::Success);

                // Radial Gradient
                auto radial = RadialGradient::gen();
                REQUIRE(radial->colorStops(cs, 4) == Result::Success);
                REQUIRE(radial->spread(FillSpread::Pad) == Result::Success);
                REQUIRE(radial->radial(50.0f, 50.0f, 50.0f, 50.0f, 50.0f, 0.0f) == Result::Success);

                auto shape2 = Shape::gen();
                REQUIRE(shape2->appendRect(50, 0, 50, 50) == Result::Success);
                REQUIRE(shape2->fill(radial) == Result::Success);
                REQUIRE(shape2->blend(method) == Result::Success);
                if (maskOp != MaskMethod::None) REQUIRE(shape2->mask(mask(), maskOp) == Result::Success);
                REQUIRE(canvas->add(shape2) == Result::Success);
            }
        }

        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Image Rotation", "[tvgGlEngine]")
{
    TestGLEngine engine(960, 960);

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);

        engine.target(canvas.get());

        auto picture = Picture::gen();
        REQUIRE(picture);

        ifstream file(TEST_DIR "/rawimage_250x375.raw");
        if (!file.is_open()) return;
        auto data = (uint32_t*)malloc(sizeof(uint32_t) * (250 * 375));
        file.read(reinterpret_cast<char*>(data), sizeof(uint32_t) * 250 * 375);
        file.close();

        REQUIRE(picture->load(data, 250, 375, ColorSpace::ARGB8888, false) == Result::Success);

        REQUIRE(picture->size(240, 240) == Result::Success);
        REQUIRE(picture->transform({0.572866f, -4.431353f, 336.605835f, 5.198910f, -0.386219f, 30.710693f, 0.0f, 0.0f, 1.0f}) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);

        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        free(data);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Scene Effects", "[tvgGlEngine]")
{
    TestGLEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = std::unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());

        auto shape = Shape::gen();
        REQUIRE(shape);
        REQUIRE(shape->appendCircle(50, 50, 30, 30) == Result::Success);
        REQUIRE(shape->fill(0, 255, 0, 255) == Result::Success);

        auto scene = Scene::gen();
        REQUIRE(scene);
        REQUIRE(scene->add(shape) == Result::Success);

        auto picture = tvg::Picture::gen();
        picture->load(TEST_DIR "/tiger.svg");

        scene->add(picture);
        REQUIRE(canvas->add(scene) == Result::Success);

        // Gaussian Blur
        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::GaussianBlur, 1.5, 0, 0, 75) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::GaussianBlur, 1.5, 1, 0, 75) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::GaussianBlur, 1.5, 2, 0, 75) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        // Drop Shadow
        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::DropShadow, 128, 128, 128, 200, 45.0, 5.0, 2.0, 60) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::DropShadow, 128, 128, 128, 200, 45.0, 5.0, 0.0, 60) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        // Fill
        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::Fill, 255, 0, 0, 128) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        // Tint
        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::Tint, 0, 0, 0, 255, 255, 255, 50.0) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        // Tritone
        REQUIRE(scene->add(SceneEffect::Clear) == Result::Success);
        REQUIRE(scene->add(SceneEffect::Tritone, 0, 0, 0, 128, 128, 128, 255, 255, 255, 128) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        // Tritone + Gaussian Blur + Drop Shadow
        REQUIRE(scene->add(SceneEffect::GaussianBlur, 1.5, 0, 0, 75) == Result::Success);
        REQUIRE(scene->add(SceneEffect::DropShadow, 128, 128, 128, 200, 45.0, 5.0, 2.0, 60) == Result::Success);

        REQUIRE(canvas->add(scene->duplicate()) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("GL Solid Batch", "[tvgGlEngine]")
{
    TestGLEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = std::unique_ptr<GlCanvas>(GlCanvas::gen());
        REQUIRE(canvas);
        engine.target(canvas.get());

        for (uint32_t i = 0; i < 4; ++i) {
            auto shape = Shape::gen();
            REQUIRE(shape);
            REQUIRE(shape->appendRect(10, 10, 80, 80) == Result::Success);
            REQUIRE(shape->fill(255, 255, 255, 255) == Result::Success);
            REQUIRE(canvas->add(shape) == Result::Success);
        }

        REQUIRE(canvas->draw() == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

#endif
