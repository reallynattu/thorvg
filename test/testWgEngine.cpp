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
#include <vector>
#include "config.h"
#include <thorvg.h>
#ifdef THORVG_LOTTIE_LOADER_SUPPORT
#include <thorvg_lottie.h>
#endif
#include <webgpu/wgpu.h>
#include "catch.hpp"
#include "testWgEngine.h"

using namespace tvg;
using namespace std;

#if defined(THORVG_WG_TEST_SUPPORT)

#if defined(THORVG_LOTTIE_LOADER_SUPPORT)

struct WgLottieVideoTestCtx;

struct WgLottieVideoFrameRef
{
    WgLottieVideoTestCtx* ctx = nullptr;
    WGPUTexture texture = nullptr;
};

struct WgLottieVideoTestCtx
{
    int opened = 0;
    int framed = 0;
    int closed = 0;
    int released = 0;
    int nativeReturned = 0;
    uint32_t lastFlags = 0;
    uint64_t nextSerial = 1;
    WGPUTexture nativeTexture = nullptr;
    WGPUTexture nativeTextures[2] = {};
    uint32_t nativeTextureCount = 0;
    uint32_t nativeTextureCursor = 0;
    WgLottieVideoFrameRef nativeRefs[16] = {};
    uint32_t nativeRefCursor = 0;
    bool fixedSerial = false;
    uint64_t fixedSerialValue = 1;
    bool nativeFrame = false;
    bool nativeOnly = false;
    bool destroyOnRelease = false;
    bool omitRelease = false;
    uint32_t bitmapColor = 0xffffffff;
    uint32_t pixels[4] = {};
};

static void _wgDestroyTexture(WGPUTexture& texture)
{
    if (!texture) return;
    wgpuTextureDestroy(texture);
    wgpuTextureRelease(texture);
    texture = nullptr;
}

static Result _wgVideoOpen(const LottieVideoAssetInfo*, void* data)
{
    auto ctx = static_cast<WgLottieVideoTestCtx*>(data);
    ++ctx->opened;
    return Result::Success;
}

static Result _wgVideoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<WgLottieVideoTestCtx*>(data);
    ++ctx->framed;
    ctx->lastFlags = request->flags;

    for (auto& pixel : ctx->pixels) pixel = ctx->bitmapColor;

    auto nativeRequested = ctx->nativeFrame && (request->flags & static_cast<uint32_t>(LottieVideoFrameRequestFlag::WgTexture));
    if (ctx->nativeOnly && !nativeRequested) {
        out->type = LottieVideoFrameType::None;
        return Result::Success;
    }

    out->type = nativeRequested ? LottieVideoFrameType::WgTexture : LottieVideoFrameType::Bitmap;
    out->data = (ctx->nativeOnly && nativeRequested) ? nullptr : ctx->pixels;
    out->width = 2;
    out->height = 2;
    out->colorSpace = ColorSpace::ABGR8888;
    out->timestamp = request->time;
    out->duration = 1.0 / 30.0;
    out->serial = ctx->fixedSerial ? ctx->fixedSerialValue : ctx->nextSerial++;
    if (nativeRequested) {
        ++ctx->nativeReturned;
        auto nativeTexture = ctx->nativeTexture;
        if (ctx->nativeTextureCount > 0) {
            auto idx = ctx->nativeTextureCursor;
            if (idx >= ctx->nativeTextureCount) idx = ctx->nativeTextureCount - 1;
            nativeTexture = ctx->nativeTextures[idx];
            if (ctx->nativeTextureCursor < ctx->nativeTextureCount) ++ctx->nativeTextureCursor;
        }
        out->nativeHandle = nativeTexture;
        if (!ctx->omitRelease) {
            out->release = [](void* user) {
                auto ref = static_cast<WgLottieVideoFrameRef*>(user);
                auto ctx = ref->ctx;
                if (!ctx) return;
                ++ctx->released;
                if (ctx->destroyOnRelease && ref->texture) {
                    auto texture = ref->texture;
                    if (ctx->nativeTexture == texture) ctx->nativeTexture = nullptr;
                    for (auto& nativeTexture : ctx->nativeTextures) {
                        if (nativeTexture == texture) nativeTexture = nullptr;
                    }
                    wgpuTextureDestroy(texture);
                    wgpuTextureRelease(texture);
                    ref->texture = nullptr;
                }
                ref->ctx = nullptr;
            };
            auto ref = &ctx->nativeRefs[ctx->nativeRefCursor++ % 16];
            ref->ctx = ctx;
            ref->texture = nativeTexture;
            out->user = ref;
        }
    }
    return Result::Success;
}

static void _wgVideoClose(const char*, void* data)
{
    auto ctx = static_cast<WgLottieVideoTestCtx*>(data);
    ++ctx->closed;
}

static void _wgVideoLottie(char* out, size_t size)
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

static void _wgSharedVideoLottie(char* out, size_t size)
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
        },{
            "ind":2,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
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

static bool _wgVideoRequestFlag(uint32_t flags, LottieVideoFrameRequestFlag flag)
{
    return (flags & static_cast<uint32_t>(flag)) != 0;
}

static WGPUTexture _wgCreateSolidTexture(WGPUDevice device, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t pixels[2 * 2 * 4] = {
        r, g, b, a, r, g, b, a,
        r, g, b, a, r, g, b, a
    };

    WGPUTextureDescriptor textureDesc = {};
    textureDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.size.width = 2;
    textureDesc.size.height = 2;
    textureDesc.size.depthOrArrayLayers = 1;
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;

    auto texture = wgpuDeviceCreateTexture(device, &textureDesc);
    if (!texture) return nullptr;

    auto queue = wgpuDeviceGetQueue(device);
    const WGPUTexelCopyTextureInfo copyTextureInfo{ .texture = texture };
    const WGPUTexelCopyBufferLayout copyBufferLayout{ .bytesPerRow = 2 * 4, .rowsPerImage = 2 };
    const WGPUExtent3D writeSize{ .width = 2, .height = 2, .depthOrArrayLayers = 1 };
    wgpuQueueWriteTexture(queue, &copyTextureInfo, pixels, sizeof(pixels), &copyBufferLayout, &writeSize);
    wgpuQueueRelease(queue);

    return texture;
}

static bool _wgReadFirstPixel(TestWgEngine& engine, uint8_t pixel[4])
{
    constexpr uint32_t BytesPerRow = 256;
    constexpr uint64_t BufferSize = BytesPerRow;

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.size = BufferSize;
    auto buffer = wgpuDeviceCreateBuffer(engine.device, &bufferDesc);
    if (!buffer) return false;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    auto encoder = wgpuDeviceCreateCommandEncoder(engine.device, &encoderDesc);
    if (!encoder) {
        wgpuBufferRelease(buffer);
        return false;
    }

    WGPUTexelCopyTextureInfo source = {};
    source.texture = engine.texture;
    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = buffer;
    destination.layout.bytesPerRow = BytesPerRow;
    destination.layout.rowsPerImage = 1;
    WGPUExtent3D copySize = {};
    copySize.width = 1;
    copySize.height = 1;
    copySize.depthOrArrayLayers = 1;
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

    WGPUCommandBufferDescriptor commandDesc = {};
    auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
    wgpuCommandEncoderRelease(encoder);
    if (!command) {
        wgpuBufferRelease(buffer);
        return false;
    }

    auto queue = wgpuDeviceGetQueue(engine.device);
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);
    wgpuQueueRelease(queue);

    struct MapState
    {
        bool done = false;
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
    } state;

    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
        auto state = static_cast<MapState*>(userdata1);
        state->status = status;
        state->done = true;
    };
    callbackInfo.userdata1 = &state;

    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, BufferSize, callbackInfo);

    for (uint32_t i = 0; i < 1000 && !state.done; ++i) {
        wgpuDevicePoll(engine.device, true, nullptr);
    }

    auto success = state.done && state.status == WGPUMapAsyncStatus_Success;
    if (success) {
        auto data = static_cast<const uint8_t*>(wgpuBufferGetMappedRange(buffer, 0, 4));
        if (data) memcpy(pixel, data, 4);
        else success = false;
    }

    if (success) wgpuBufferUnmap(buffer);
    wgpuBufferRelease(buffer);
    return success;
}

TEST_CASE("WG Lottie Video Native Texture Import", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        ctx.nativeTexture = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        REQUIRE(ctx.nativeTexture);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _wgVideoLottie(lottie, sizeof(lottie));

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 0);
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed >= 2);
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(ctx.nativeReturned >= 1);

        uint8_t sampled[4] = {};
        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] > 200);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] > 200);
        REQUIRE(sampled[3] > 200);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == ctx.nativeReturned);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTexture);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Native Texture Replacement Release", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        ctx.destroyOnRelease = true;
        ctx.nativeTextureCount = 2;
        ctx.nativeTextures[0] = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        ctx.nativeTextures[1] = _wgCreateSolidTexture(engine.device, 0, 255, 0, 255);
        REQUIRE(ctx.nativeTextures[0]);
        REQUIRE(ctx.nativeTextures[1]);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _wgVideoLottie(lottie, sizeof(lottie));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t sampled[4] = {};
        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] > 200);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] > 200);
        REQUIRE(sampled[3] > 200);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.released >= 1);
        REQUIRE(ctx.nativeTextures[0] == nullptr);

        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] < 80);
        REQUIRE(sampled[1] > 180);
        REQUIRE(sampled[2] < 80);
        REQUIRE(sampled[3] > 200);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == ctx.nativeReturned);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTextures[0]);
        _wgDestroyTexture(ctx.nativeTextures[1]);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Blend Requests Bitmap Fallback", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.bitmapColor = 0xffff0000;
        ctx.nativeTexture = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        REQUIRE(ctx.nativeTexture);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

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
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed >= 2);
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(ctx.nativeReturned == 0);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTexture);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Matte Source Requests Bitmap Fallback", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.bitmapColor = 0xffffffff;
        ctx.nativeTexture = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        REQUIRE(ctx.nativeTexture);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

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
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed >= 2);
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_wgVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(ctx.nativeReturned == 0);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTexture);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Native Texture Clear Release", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        ctx.destroyOnRelease = true;
        ctx.nativeTextureCount = 1;
        ctx.nativeTextures[0] = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        REQUIRE(ctx.nativeTextures[0]);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _wgVideoLottie(lottie, sizeof(lottie));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t sampled[4] = {};
        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] > 200);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] > 200);
        REQUIRE(sampled[3] > 200);
        REQUIRE(ctx.nativeReturned == 1);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(ctx.nativeTextures[0] == nullptr);

        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTextures[0]);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Native Texture Without Release Callback Clear", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        ctx.omitRelease = true;
        ctx.nativeTexture = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        REQUIRE(ctx.nativeTexture);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _wgVideoLottie(lottie, sizeof(lottie));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t sampled[4] = {};
        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] > 200);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] > 200);
        REQUIRE(sampled[3] > 200);
        REQUIRE(ctx.nativeReturned == 1);
        REQUIRE(ctx.released == 0);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == 0);

        _wgDestroyTexture(ctx.nativeTexture);

        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTexture);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("WG Lottie Video Shared Native Texture Clear Release", "[tvgWgEngine]")
{
    TestWgEngine engine(2, 2);

    REQUIRE(Initializer::init() == Result::Success);
    {
        WgLottieVideoTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeOnly = true;
        ctx.destroyOnRelease = true;
        ctx.fixedSerial = true;
        ctx.nativeTextureCount = 2;
        ctx.nativeTextures[0] = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
        ctx.nativeTextures[1] = _wgCreateSolidTexture(engine.device, 0, 255, 0, 255);
        REQUIRE(ctx.nativeTextures[0]);
        REQUIRE(ctx.nativeTextures[1]);

        LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[4096];
        _wgSharedVideoLottie(lottie, sizeof(lottie));

        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);
        REQUIRE(engine.target(canvas.get()) == Result::Success);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        uint8_t sampled[4] = {};
        REQUIRE(_wgReadFirstPixel(engine, sampled));
        REQUIRE(sampled[0] < 80);
        REQUIRE(sampled[1] > 200);
        REQUIRE(sampled[2] < 80);
        REQUIRE(sampled[3] > 200);
        REQUIRE(ctx.nativeReturned >= 2);
        REQUIRE(ctx.released == 0);
        REQUIRE(ctx.nativeTextures[0] != nullptr);
        REQUIRE(ctx.nativeTextures[1] != nullptr);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == ctx.nativeReturned);
        REQUIRE(ctx.nativeTextures[0] == nullptr);
        REQUIRE(ctx.nativeTextures[1] == nullptr);

        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        canvas.reset();
        animation.reset();

        _wgDestroyTexture(ctx.nativeTextures[0]);
        _wgDestroyTexture(ctx.nativeTextures[1]);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

struct WgVideoBenchmarkCtx
{
    vector<uint32_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t serial = 1;
    uint32_t framed = 0;
};

struct WgVideoBenchmarkResult
{
    double avgMs = 0.0;
    uint32_t framed = 0;
    uint64_t bytes = 0;
};

static Result _wgBenchmarkVideoOpen(const LottieVideoAssetInfo*, void*)
{
    return Result::Success;
}

static Result _wgBenchmarkVideoFrame(const LottieVideoFrameRequest*, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<WgVideoBenchmarkCtx*>(data);
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

static string _wgBenchmarkVideoLottie(uint32_t width, uint32_t height)
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

static WgVideoBenchmarkResult _runWgBitmapBenchmark(uint32_t width, uint32_t height, uint32_t iterations)
{
    TestWgEngine engine(width, height);

    WgVideoBenchmarkCtx ctx;
    ctx.width = width;
    ctx.height = height;
    auto count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    ctx.pixels.resize(static_cast<size_t>(count), 0xffff0000);

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    REQUIRE(animation);
    LottieVideoProvider provider = {_wgBenchmarkVideoOpen, _wgBenchmarkVideoFrame, nullptr, &ctx};
    REQUIRE(animation->videoProvider(&provider) == Result::Success);

    auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
    REQUIRE(canvas);
    REQUIRE(engine.target(canvas.get()) == Result::Success);

    auto picture = animation->picture();
    auto lottie = _wgBenchmarkVideoLottie(width, height);
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

static WgVideoBenchmarkResult _runWgNativeBenchmark(uint32_t iterations)
{
    TestWgEngine engine(2, 2);

    WgLottieVideoTestCtx ctx;
    ctx.nativeFrame = true;
    ctx.nativeOnly = true;
    ctx.omitRelease = true;
    ctx.nativeTexture = _wgCreateSolidTexture(engine.device, 255, 255, 255, 255);
    REQUIRE(ctx.nativeTexture);

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    REQUIRE(animation);
    LottieVideoProvider provider = {_wgVideoOpen, _wgVideoFrame, _wgVideoClose, &ctx};
    REQUIRE(animation->videoProvider(&provider) == Result::Success);

    auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
    REQUIRE(canvas);
    REQUIRE(engine.target(canvas.get()) == Result::Success);

    auto picture = animation->picture();
    char lottie[2048];
    _wgVideoLottie(lottie, sizeof(lottie));
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
    _wgDestroyTexture(ctx.nativeTexture);

    return {elapsed.count() / double(iterations), static_cast<uint32_t>(ctx.framed), 2 * 2 * sizeof(uint32_t)};
}

TEST_CASE("WG Performance Benchmarks For Lottie Video", "[.][benchmark][lottieVideo][tvgWgEngine]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto bitmap720 = _runWgBitmapBenchmark(1280, 720, 2);
        WARN("wg bitmap texture refresh 720p: " << bitmap720.avgMs << " ms/frame, provider frames="
                                                << bitmap720.framed << ", bytes=" << bitmap720.bytes);

        auto bitmap1080 = _runWgBitmapBenchmark(1920, 1080, 2);
        WARN("wg bitmap texture refresh 1080p: " << bitmap1080.avgMs << " ms/frame, provider frames="
                                                 << bitmap1080.framed << ", bytes=" << bitmap1080.bytes);

        auto native = _runWgNativeBenchmark(2);
        WARN("wg native texture import baseline: " << native.avgMs << " ms/frame, provider frames="
                                                   << native.framed << ", bytes=" << native.bytes);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

#endif

TEST_CASE("WG Basic draw", "[tvgWgEngine]")
{
    TestWgEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
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

TEST_CASE("WG Image Draw", "[tvgWgEngine]")
{
    TestWgEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);

        REQUIRE(engine.target(canvas.get()) == Result::Success);

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

TEST_CASE("WG Filling Draw", "[tvgWgEngine]")
{
    TestWgEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(canvas);

        REQUIRE(engine.target(canvas.get()) == Result::Success);

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

TEST_CASE("WG Image Rotation", "[tvgWgEngine]")
{
    TestWgEngine engine(960, 960);

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());

        REQUIRE(canvas);

        REQUIRE(engine.target(canvas.get()) == Result::Success);

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

TEST_CASE("WG Scene Effects", "[tvgWgEngine]")
{
    TestWgEngine engine;

    REQUIRE(Initializer::init() == Result::Success);
    {
        auto canvas = unique_ptr<WgCanvas>(WgCanvas::gen());
        REQUIRE(engine.target(canvas.get()) == Result::Success);

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

#endif
