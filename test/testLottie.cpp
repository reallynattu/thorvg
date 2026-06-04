/*
 * Copyright (c) 2024 - 2026 ThorVG project. All rights reserved.

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

#include "config.h"
#include <thorvg.h>
#ifdef THORVG_LOTTIE_LOADER_SUPPORT
#include <thorvg_lottie.h>
#endif
#include <chrono>
#include <fstream>
#include <cstring>
#include <vector>
#include "catch.hpp"

using namespace tvg;
using namespace std;

#ifdef THORVG_LOTTIE_LOADER_SUPPORT

struct LottieVideoProviderTestCtx
{
    int opened = 0;
    int framed = 0;
    int closed = 0;
    int released = 0;
    double lastTime = -1.0;
    uint64_t lastSerialHint = 0;
    uint32_t lastFlags = 0;
    uint64_t nextSerial = 1;
    float assetWidth = 0.0f;
    float assetHeight = 0.0f;
    float assetDuration = 0.0f;
    float assetFrameRate = 0.0f;
    const char* assetId = nullptr;
    const char* src = nullptr;
    const char* mime = nullptr;
    bool assetLoop = false;
    bool assetHoldLastFrame = false;
    bool assetMuted = false;
    bool pending = false;
    bool nativeFrame = false;
    bool wgNativeFrame = false;
    bool platformNativeFrame = false;
    bool nativeBitmapFallback = false;
    bool invalidNativeTarget = false;
    bool invalidBitmap = false;
    bool colorByTime = false;
    bool sizeByTime = false;
    bool fixedSerial = false;
    Result openResult = Result::Success;
    Result frameResult = Result::Success;
    uint64_t fixedSerialValue = 1;
    uint32_t frame0Width = 2;
    uint32_t frame0Height = 2;
    uint32_t frame1Width = 2;
    uint32_t frame1Height = 2;
    uint32_t frame0Color = 0xffff0000;
    uint32_t frame1Color = 0xff00ff00;
    ColorSpace colorSpace = ColorSpace::ARGB8888;
    uint32_t pixels[4] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
};

static Result _testVideoOpen(const LottieVideoAssetInfo* asset, void* data)
{
    auto ctx = static_cast<LottieVideoProviderTestCtx*>(data);
    ++ctx->opened;
    ctx->assetId = asset->assetId;
    ctx->src = asset->src;
    ctx->mime = asset->mime;
    ctx->assetWidth = asset->width;
    ctx->assetHeight = asset->height;
    ctx->assetDuration = asset->duration;
    ctx->assetFrameRate = asset->frameRate;
    ctx->assetLoop = asset->loop;
    ctx->assetHoldLastFrame = asset->holdLastFrame;
    ctx->assetMuted = asset->muted;
    return ctx->openResult;
}

static uint64_t _testVideoSerial(LottieVideoProviderTestCtx* ctx)
{
    return ctx->fixedSerial ? ctx->fixedSerialValue : ctx->nextSerial++;
}

static Result _testVideoFrame(const LottieVideoFrameRequest* request, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<LottieVideoProviderTestCtx*>(data);
    ++ctx->framed;
    ctx->lastTime = request->time;
    ctx->lastSerialHint = request->serialHint;
    ctx->lastFlags = request->flags;
    if (ctx->frameResult != Result::Success) return ctx->frameResult;
    if (ctx->pending) {
        out->type = LottieVideoFrameType::None;
        return Result::Success;
    }
    if (ctx->nativeFrame) {
        out->type = ctx->platformNativeFrame ? LottieVideoFrameType::NativeHandle :
                                               (ctx->wgNativeFrame ? LottieVideoFrameType::WgTexture : LottieVideoFrameType::GlTexture);
        out->width = 2;
        out->height = 2;
        out->serial = _testVideoSerial(ctx);
        if (ctx->platformNativeFrame) {
            out->nativeHandle = ctx;
        } else if (ctx->wgNativeFrame) {
            out->nativeHandle = ctx;
        } else {
            out->nativeId = 7;
            out->nativeTarget = ctx->invalidNativeTarget ? 0 : 0x0DE1; // GL_TEXTURE_2D
        }
        if (ctx->nativeBitmapFallback) {
            out->data = ctx->pixels;
            out->colorSpace = ctx->colorSpace;
        }
        out->release = [](void* user) {
            auto ctx = static_cast<LottieVideoProviderTestCtx*>(user);
            ++ctx->released;
        };
        out->user = data;
        return Result::Success;
    }
    if (ctx->colorByTime) {
        auto color = request->time < 1.0 ? ctx->frame0Color : ctx->frame1Color;
        for (auto i = 0; i < 4; ++i) ctx->pixels[i] = color;
    }
    out->type = LottieVideoFrameType::Bitmap;
    if (ctx->invalidBitmap) {
        out->serial = _testVideoSerial(ctx);
        out->release = [](void* user) {
            auto ctx = static_cast<LottieVideoProviderTestCtx*>(user);
            ++ctx->released;
        };
        out->user = data;
        return Result::Success;
    }
    out->data = ctx->pixels;
    out->width = ctx->sizeByTime && request->time >= 1.0 ? ctx->frame1Width : ctx->frame0Width;
    out->height = ctx->sizeByTime && request->time >= 1.0 ? ctx->frame1Height : ctx->frame0Height;
    out->colorSpace = ctx->colorSpace;
    out->timestamp = request->time;
    out->duration = 1.0 / 30.0;
    out->serial = _testVideoSerial(ctx);
    out->release = [](void* user) {
        auto ctx = static_cast<LottieVideoProviderTestCtx*>(user);
        ++ctx->released;
    };
    out->user = data;
    return Result::Success;
}

static void _testVideoClose(const char*, void* data)
{
    auto ctx = static_cast<LottieVideoProviderTestCtx*>(data);
    ++ctx->closed;
}


static void _testVideoLottie(char* out, size_t size, bool loop, bool holdLastFrame)
{
    snprintf(out, size, R"({
        "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
        "assets":[{
            "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
            "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":%s,"holdLastFrame":%s,"muted":true}
        }],
        "layers":[{
            "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":0,
            "ks":{
                "o":{"a":0,"k":100},
                "r":{"a":0,"k":0},
                "p":{"a":0,"k":[0,0,0]},
                "a":{"a":0,"k":[0,0,0]},
                "s":{"a":0,"k":[100,100,100]}
            }
        }]
    })", loop ? "true" : "false", holdLastFrame ? "true" : "false");
}


static uint32_t _testCountPixels(const uint32_t* buffer, uint32_t count, uint32_t color)
{
    uint32_t ret = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (buffer[i] == color) ++ret;
    }
    return ret;
}


static void _testFillPixels(uint32_t* buffer, uint32_t count, uint32_t color)
{
    for (uint32_t i = 0; i < count; ++i) buffer[i] = color;
}


static bool _testHasRedAndBlue(uint32_t color)
{
    auto red = (color >> 16) & 0xff;
    auto blue = color & 0xff;
    return red > 0 && blue > 0;
}


static bool _testVideoRequestFlag(uint32_t flags, LottieVideoFrameRequestFlag flag)
{
    return (flags & static_cast<uint32_t>(flag)) != 0;
}

static bool _testVideoNativeHandleRequestFlagClear(uint32_t flags)
{
    return !_testVideoRequestFlag(flags, LottieVideoFrameRequestFlag::NativeHandle);
}

TEST_CASE("Lottie Coverages", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        #define TEST_CNT 10

        const char* names[TEST_CNT] = {
            "test3.lot",
            "test4.lot",
            "test5.lot",
            "test6.lot",
            "test7.lot",
            "test8.lot",
            "test9.lot",
            "test10.lot",
            "test11.lot",
            "test12.lot"
        };

        auto animation = unique_ptr<Animation>(Animation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        for (int i = 0; i < TEST_CNT; ++i) {
            char buf[100];
            snprintf(buf, sizeof(buf), TEST_DIR"/%s", names[i]);
            REQUIRE(picture->load(buf) == Result::Success);
            REQUIRE(animation->frame(0.0f) == Result::InsufficientCondition);
            REQUIRE(animation->frame(animation->totalFrame() * 0.5f) == Result::Success);
            REQUIRE(animation->frame(animation->totalFrame()) == Result::Success);
        }
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Slot", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        //Slot Test 1
        const char* slotJson = R"({"gradient_fill":{"p":{"p":2,"k":{"a":0,"k":[0,0.1,0.1,0.2,1,1,0.1,0.2,0.1,1]}}}})";

        //Negative: slot generation before loaded
        REQUIRE(animation->gen(slotJson) == 0);

        REQUIRE(picture->load(TEST_DIR"/slot.lot") == Result::Success);

        auto id = animation->gen(slotJson);
        REQUIRE(id > 0);

        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->apply(id) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->apply(id) == Result::Success);
        REQUIRE(animation->gen("") == 0);
        REQUIRE(animation->del(id) == Result::Success);

        //Slot Test 2
        const char* slotJson2 = R"({"lottie-icon-outline":{"p":{"a":0,"k":[1,1,0]}},"lottie-icon-solid":{"p":{"a":0,"k":[0,0,1]}}})";

        auto id2 = animation->gen(slotJson2);
        REQUIRE(id2 > 0);

        REQUIRE(animation->apply(id2) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->apply(id2) == Result::Success);
        REQUIRE(animation->del(id2) == Result::Success);

        //Slot Test 3 (Transform)
        const char* positionSlot = R"({"transform_id":{"p":{"a":1,"k":[{"i":{"x":0.833,"y":0.833},"o":{"x":0.167,"y":0.167},"s":[100,100],"t":0},{"s":[200,300],"t":100}]}}})";
        auto id3 = animation->gen(positionSlot);
        REQUIRE(id3 > 0);
        REQUIRE(animation->apply(id3) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id3) == Result::Success);

        const char* scaleSlot = R"({"transform_id":{"p":{"a":1,"k":[{"i":{"x":0.833,"y":0.833},"o":{"x":0.167,"y":0.167},"s":[0,0],"t":0},{"s":[100,100],"t":100}]}}})";
        auto id4 = animation->gen(scaleSlot);
        REQUIRE(id4 > 0);
        REQUIRE(animation->apply(id4) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id4) == Result::Success);

        const char* rotationSlot = R"({"transform_id":{"p":{"a":1,"k":[{"i":{"x":0.833,"y":0.833},"o":{"x":0.167,"y":0.167},"s":[0],"t":0},{"s":[180],"t":100}]}}})";
        auto id5 = animation->gen(rotationSlot);
        REQUIRE(id5 > 0);
        REQUIRE(animation->apply(id5) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id5) == Result::Success);

        const char* opacitySlot = R"({"transform_id":{"p":{"a":1,"k":[{"i":{"x":0.833,"y":0.833},"o":{"x":0.167,"y":0.167},"s":[0],"t":0},{"s":[100],"t":100}]}}})";
        auto id6 = animation->gen(opacitySlot);
        REQUIRE(id6 > 0);
        REQUIRE(animation->apply(id6) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id6) == Result::Success);

        //Slot Test 4: Expression
        const char* expressionSlot = R"({"rect_rotation":{"p":{"x":"var $bm_rt = time * 360;"}},"rect_scale":{"p":{"x":"var $bm_rt = [];$bm_rt[0] = value[0] + Math.cos(2 * Math.PI * time) * 100;$bm_rt[1] = value[1];"}},"rect_position":{"p":{"x":"var $bm_rt = [];$bm_rt[0] = value[0] + Math.cos(2 * Math.PI * time) * 100;$bm_rt[1] = value[1];"}}})";
        auto id7 = animation->gen(expressionSlot);
        REQUIRE(id7 > 0);
        REQUIRE(animation->apply(id7) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id7) == Result::Success);

        //Slot Test 5: Text
        const char* textSlot = R"({"text_doc":{"p":{"k":[{"s":{"f":"Ubuntu Light Italic","t":"ThorVG!","j":0,"s":48,"fc":[1,1,1]},"t":0}]}}})";
        auto id8 = animation->gen(textSlot);
        REQUIRE(id8 > 0);
        REQUIRE(animation->apply(id8) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id8) == Result::Success);

        //Slot Test 6: Image
        const char* imageSlot = R"({"path_img":{"p":{"id":"image_0","w":200,"h":300,"u":"images/","p":"logo.png","e":0}}})";
        auto id9 = animation->gen(imageSlot);
        REQUIRE(id9 > 0);
        REQUIRE(animation->apply(id9) == Result::Success);
        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->del(id9) == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Provider", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"test.png","e":0,
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
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(ctx.lastSerialHint == 0);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(!strcmp(ctx.assetId, "video_hero"));
        REQUIRE(strstr(ctx.src, "video.mp4") != nullptr);
        REQUIRE(!strcmp(ctx.mime, "video/mp4"));
        REQUIRE(ctx.assetWidth == 2.0f);
        REQUIRE(ctx.assetHeight == 2.0f);
        REQUIRE(ctx.assetDuration == 2.0f);
        REQUIRE(ctx.assetFrameRate == 30.0f);
        REQUIRE(ctx.assetLoop == false);
        REQUIRE(ctx.assetHoldLastFrame == true);
        REQUIRE(ctx.assetMuted == true);
        REQUIRE(ctx.lastTime == Approx(0.0));

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.released == 2);
        REQUIRE(ctx.lastSerialHint == 1);
        REQUIRE(ctx.lastTime == Approx(1.0));

        ctx.pending = true;
        REQUIRE(animation->frame(31.0f) == Result::Success);
        REQUIRE(ctx.framed == 3);
        REQUIRE(ctx.released == 2);
        REQUIRE(ctx.lastSerialHint == 2);

        animation.reset();
        REQUIRE(ctx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Source URI Preservation", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto verify = [](const char* src, const char* expected) {
            LottieVideoProviderTestCtx ctx;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto picture = animation->picture();
            char lottie[2048];
            snprintf(lottie, sizeof(lottie), R"({
                "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
                "assets":[{
                    "id":"video_hero","w":2,"h":2,"u":"","p":"test.png","e":0,
                    "x-video":{"src":"%s","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
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
            })", src);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.opened == 1);
            REQUIRE(ctx.src);
            REQUIRE(!strcmp(ctx.src, expected));
        };

        auto relative = string(TEST_DIR) + "/v/video.mp4";
        verify("v/video.mp4", relative.c_str());
        verify("asset://video_hero", "asset://video_hero");
        verify("file:///tmp/video.mp4", "file:///tmp/video.mp4");
        verify("data:video/mp4;base64,AAAA", "data:video/mp4;base64,AAAA");
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Provider Change Closes Open Assets", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx firstCtx;
        LottieVideoProvider firstProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &firstCtx};

        LottieVideoProviderTestCtx secondCtx;
        LottieVideoProvider secondProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &secondCtx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&firstProvider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(firstCtx.opened == 1);
        REQUIRE(firstCtx.framed == 1);
        REQUIRE(firstCtx.closed == 0);
        REQUIRE(secondCtx.opened == 0);
        REQUIRE(secondCtx.framed == 0);

        REQUIRE(animation->videoProvider(&secondProvider) == Result::Success);
        REQUIRE(firstCtx.closed == 1);
        REQUIRE(secondCtx.opened == 0);
        REQUIRE(secondCtx.framed == 0);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(secondCtx.opened == 1);
        REQUIRE(secondCtx.framed == 1);
        REQUIRE(secondCtx.lastTime == Approx(1.0));

        animation.reset();
        REQUIRE(firstCtx.closed == 1);
        REQUIRE(secondCtx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Provider Change Clears Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx firstCtx;
        firstCtx.colorByTime = true;
        firstCtx.frame0Color = 0xffff0000;
        LottieVideoProvider firstProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &firstCtx};

        LottieVideoProviderTestCtx secondCtx;
        secondCtx.pending = true;
        LottieVideoProvider secondProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &secondCtx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&firstProvider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };
        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(firstCtx.framed == 1);
        REQUIRE(_testCountPixels(buffer, 4, firstCtx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        REQUIRE(animation->videoProvider(&secondProvider) == Result::Success);
        REQUIRE(firstCtx.closed == 1);
        REQUIRE(secondCtx.opened == 0);
        REQUIRE(secondCtx.framed == 0);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(secondCtx.opened == 1);
        REQUIRE(secondCtx.framed == 1);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, firstCtx.frame0Color) == 0);

        canvas.reset();
        animation.reset();
        REQUIRE(firstCtx.closed == 1);
        REQUIRE(secondCtx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Invalid Provider Change Keeps Active Provider", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};
        LottieVideoProvider invalidProvider = {_testVideoOpen, nullptr, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 0);

        REQUIRE(animation->videoProvider(&invalidProvider) == Result::InvalidArguments);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 0);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.closed == 0);
        REQUIRE(ctx.lastTime == Approx(1.0));

        animation.reset();
        REQUIRE(ctx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Provider Change Resets Open State Without Close Callback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx firstCtx;
        LottieVideoProvider firstProvider = {_testVideoOpen, _testVideoFrame, nullptr, &firstCtx};

        LottieVideoProviderTestCtx secondCtx;
        LottieVideoProvider secondProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &secondCtx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&firstProvider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(firstCtx.opened == 1);
        REQUIRE(firstCtx.framed == 1);
        REQUIRE(firstCtx.closed == 0);

        REQUIRE(animation->videoProvider(&secondProvider) == Result::Success);
        REQUIRE(firstCtx.closed == 0);
        REQUIRE(secondCtx.opened == 0);
        REQUIRE(secondCtx.framed == 0);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(firstCtx.opened == 1);
        REQUIRE(firstCtx.framed == 1);
        REQUIRE(secondCtx.opened == 1);
        REQUIRE(secondCtx.framed == 1);
        REQUIRE(secondCtx.lastTime == Approx(1.0));

        animation.reset();
        REQUIRE(secondCtx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Provider Clear Closes Open Assets", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.pixels[0] = ctx.pixels[1] = ctx.pixels[2] = ctx.pixels[3] = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 0);
        REQUIRE(ctx.released == 1);

        REQUIRE(animation->videoProvider(nullptr) == Result::Success);
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == 1);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);

        animation.reset();
        REQUIRE(ctx.closed == 1);
        REQUIRE(ctx.released == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Image Slot Override Disables Provider", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","sid":"video_slot","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":0,
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
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 0);

        const char* slotJson = R"({"video_slot":{"p":{"id":"slot_image","w":2,"h":2,"u":"","p":"poster.raw","e":0}}})";
        auto slot = animation->gen(slotJson);
        REQUIRE(slot > 0);

        REQUIRE(animation->apply(slot) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 1);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 1);

        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->frame(31.0f) == Result::Success);
        REQUIRE(ctx.opened == 2);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.closed == 1);

        animation.reset();
        REQUIRE(ctx.closed == 2);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Slot Extension Override Disables Provider", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","sid":"video_slot","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":0,
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
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 0);

        const char* slotJson = R"({
            "video_slot":{"p":{
                "id":"slot_video","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"slot-video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }}
        })";
        auto slot = animation->gen(slotJson);
        REQUIRE(slot > 0);

        REQUIRE(animation->apply(slot) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 1);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.closed == 1);

        REQUIRE(animation->apply(0) == Result::Success);
        REQUIRE(animation->frame(31.0f) == Result::Success);
        REQUIRE(ctx.opened == 2);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.closed == 1);

        animation.reset();
        REQUIRE(ctx.closed == 2);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Open Failure Falls Back Without Retrying", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx failingCtx;
        failingCtx.openResult = Result::InsufficientCondition;
        LottieVideoProvider failingProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &failingCtx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&failingProvider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(failingCtx.opened == 1);
        REQUIRE(failingCtx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(failingCtx.opened == 1);
        REQUIRE(failingCtx.framed == 0);
        REQUIRE(failingCtx.closed == 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);

        LottieVideoProviderTestCtx recoveryCtx;
        recoveryCtx.colorByTime = true;
        LottieVideoProvider recoveryProvider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &recoveryCtx};
        REQUIRE(animation->videoProvider(&recoveryProvider) == Result::Success);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(31.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(recoveryCtx.opened == 1);
        REQUIRE(recoveryCtx.framed == 1);
        REQUIRE(recoveryCtx.lastTime == Approx(31.0 / 30.0));
        REQUIRE(_testCountPixels(buffer, 4, recoveryCtx.frame1Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        canvas.reset();
        animation.reset();
        REQUIRE(recoveryCtx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Timeline Policies", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto verify = [](bool loop, bool holdLastFrame, float frameNo, bool expectFrame, double expectedTime) {
            LottieVideoProviderTestCtx ctx;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto picture = animation->picture();
            char lottie[2048];
            _testVideoLottie(lottie, sizeof(lottie), loop, holdLastFrame);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.framed == 1);
            REQUIRE(ctx.lastTime == Approx(0.0));

            REQUIRE(animation->frame(frameNo) == Result::Success);
            if (expectFrame) {
                REQUIRE(ctx.framed == 2);
                REQUIRE(ctx.lastTime == Approx(expectedTime));
            } else {
                REQUIRE(ctx.framed == 1);
            }
        };

        verify(true, true, 75.0f, true, 0.5);
        verify(false, true, 90.0f, true, 2.0);
        verify(false, false, 90.0f, false, 0.0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Layer Visibility", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":90,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":3,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":30,"op":60,"st":0,
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
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(animation->frame(29.0f) == Result::Success);
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.lastTime == Approx(1.0));

        REQUIRE(animation->frame(60.0f) == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);

        animation.reset();
        REQUIRE(ctx.closed == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Layer Time Mapping", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto verify = [](const char* timing, float frameNo, int initialFrames, double expectedTime) {
            LottieVideoProviderTestCtx ctx;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto picture = animation->picture();
            char lottie[2048];
            snprintf(lottie, sizeof(lottie), R"({
                "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
                "assets":[{
                    "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                    "x-video":{"src":"video.mp4","mime":"video/mp4","duration":10,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
                }],
                "layers":[{
                    "ind":1,"ty":2,"refId":"video_hero","ip":0,"op":120,%s
                    "ks":{
                        "o":{"a":0,"k":100},
                        "r":{"a":0,"k":0},
                        "p":{"a":0,"k":[0,0,0]},
                        "a":{"a":0,"k":[0,0,0]},
                        "s":{"a":0,"k":[100,100,100]}
                    }
                }]
            })", timing);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.opened == (initialFrames > 0 ? 1 : 0));
            REQUIRE(ctx.framed == initialFrames);

            REQUIRE(animation->frame(frameNo) == Result::Success);
            REQUIRE(ctx.opened == 1);
            REQUIRE(ctx.framed == initialFrames + 1);
            REQUIRE(ctx.lastTime == Approx(expectedTime));
        };

        verify(R"("sr":2,"st":30,)", 90.0f, 0, 1.0);
        verify(R"("sr":2,"st":30,"tm":{"a":0,"k":1},)", 90.0f, 1, 1.0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Composition Frame Rate Mapping", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":60,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":10,"frameRate":24,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":0,
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
        REQUIRE(ctx.lastTime == Approx(0.0));

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.lastTime == Approx(0.5));

        REQUIRE(animation->frame(90.0f) == Result::Success);
        REQUIRE(ctx.framed == 3);
        REQUIRE(ctx.lastTime == Approx(1.5));
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Shared Asset Independent Layer Timing", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Width = 1;
        ctx.frame0Height = 1;
        ctx.frame1Width = 1;
        ctx.frame1Height = 1;
        ctx.frame0Color = 0xffff0000;
        ctx.frame1Color = 0xff00ff00;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":1,"h":1,"u":"","p":"poster.raw","e":0,
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
                "ind":2,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":30,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[1,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed >= 2);
        REQUIRE(buffer[0] == ctx.frame1Color);
        REQUIRE(buffer[1] == ctx.frame0Color);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Pre-Roll Poster", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":30,
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
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) == 0);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.lastTime == Approx(0.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Pre-Roll Clears Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":30,
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
        REQUIRE(canvas->add(picture) == Result::Success);

        REQUIRE(animation->frame(60.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.lastTime == Approx(1.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        REQUIRE(animation->frame(0.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);

        ctx.pending = true;
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.lastTime == Approx(0.0));
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) == 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Post-Roll Poster Does Not Open Provider", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":false,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":-90,
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
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Post-Roll Clears Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":120,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":false,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":120,"st":0,
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
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.lastTime == Approx(0.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        REQUIRE(animation->frame(90.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) == 0);

        ctx.pending = true;
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.lastTime == Approx(1.0));
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) == 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Reverse Seek Requests Timeline Time", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.lastTime == Approx(0.0));

        REQUIRE(animation->frame(45.0f) == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.lastTime == Approx(1.5));
        REQUIRE(ctx.lastSerialHint == 1);

        REQUIRE(animation->frame(15.0f) == Result::Success);
        REQUIRE(ctx.framed == 3);
        REQUIRE(ctx.lastTime == Approx(0.5));
        REQUIRE(ctx.lastSerialHint == 2);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Asset Sizing And Viewport Clip", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[5 * 5] = {};
        REQUIRE(canvas->target(buffer, 5, 5, 5, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":3,"h":3,
            "assets":[{
                "id":"video_hero","w":4,"h":4,"u":"","p":"poster.raw","e":0,
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
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(buffer[0] == ctx.frame0Color);
        REQUIRE(buffer[2 + 2 * 5] == ctx.frame0Color);
        REQUIRE(buffer[3] == 0);
        REQUIRE(buffer[3 + 2 * 5] == 0);
        REQUIRE(buffer[2 + 3 * 5] == 0);
        REQUIRE(buffer[4 + 4 * 5] == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video No Asset Size Tracks Frame Dimensions", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.sizeByTime = true;
        ctx.frame0Width = 1;
        ctx.frame0Height = 1;
        ctx.frame1Width = 2;
        ctx.frame1Height = 2;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[3 * 3] = {};
        REQUIRE(canvas->target(buffer, 3, 3, 3, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":3,"h":3,
            "assets":[{
                "id":"video_hero","u":"","p":"poster.raw","e":0,
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
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(buffer[0] == ctx.frame0Color);
        REQUIRE(buffer[1] == 0);
        REQUIRE(buffer[3] == 0);

        _testFillPixels(buffer, 9, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 2);
        REQUIRE(buffer[0] == ctx.frame1Color);
        REQUIRE(buffer[1] == ctx.frame1Color);
        REQUIRE(buffer[3] == ctx.frame1Color);
        REQUIRE(buffer[4] == ctx.frame1Color);
        REQUIRE(buffer[2] == 0);
        REQUIRE(buffer[6] == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Rendering", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);

        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Bitmap Color Space Validation", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto verifyAccepted = [](ColorSpace cs) {
            LottieVideoProviderTestCtx ctx;
            ctx.colorSpace = cs;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto picture = animation->picture();
            char lottie[2048];
            _testVideoLottie(lottie, sizeof(lottie), false, true);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.framed == 1);
            REQUIRE(ctx.released == 1);
            REQUIRE(ctx.lastSerialHint == 0);

            REQUIRE(animation->frame(30.0f) == Result::Success);
            REQUIRE(ctx.framed == 2);
            REQUIRE(ctx.released == 2);
            REQUIRE(ctx.lastSerialHint == 1);
        };

        auto verifyRejected = [](ColorSpace cs) {
            LottieVideoProviderTestCtx ctx;
            ctx.colorSpace = cs;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto picture = animation->picture();
            char lottie[2048];
            _testVideoLottie(lottie, sizeof(lottie), false, true);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.framed == 1);
            REQUIRE(ctx.released == 1);
            REQUIRE(ctx.lastSerialHint == 0);

            REQUIRE(animation->frame(30.0f) == Result::Success);
            REQUIRE(ctx.framed == 2);
            REQUIRE(ctx.released == 2);
            REQUIRE(ctx.lastSerialHint == 0);
        };

        verifyAccepted(ColorSpace::ABGR8888);
        verifyAccepted(ColorSpace::ARGB8888);
        verifyAccepted(ColorSpace::ABGR8888S);
        verifyAccepted(ColorSpace::ARGB8888S);

        verifyRejected(ColorSpace::Grayscale8);
        verifyRejected(ColorSpace::Unknown);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Straight Alpha Software Composition", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        constexpr uint32_t straightHalfRed = 0x80ff0000;
        constexpr uint32_t blue = 0xff0000ff;

        LottieVideoProviderTestCtx ctx;
        ctx.colorSpace = ColorSpace::ARGB8888S;
        ctx.pixels[0] = straightHalfRed;
        ctx.pixels[1] = straightHalfRed;
        ctx.pixels[2] = straightHalfRed;
        ctx.pixels[3] = straightHalfRed;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

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
                "ind":2,"ty":4,"sr":1,"ip":0,"op":60,"st":0,
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[2,2]},"p":{"a":0,"k":[1,1]}
                },{
                    "ty":"fl","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100}
                }]
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        for (uint32_t i = 0; i < 4; ++i) {
            auto color = buffer[i];
            auto alpha = (color >> 24) & 0xff;
            auto red = (color >> 16) & 0xff;
            auto green = (color >> 8) & 0xff;
            auto blueChannel = color & 0xff;

            REQUIRE(alpha == 0xff);
            REQUIRE(red > 100);
            REQUIRE(red < 200);
            REQUIRE(green < 10);
            REQUIRE(blueChannel > 80);
            REQUIRE(blueChannel < 180);
            REQUIRE(color != straightHalfRed);
            REQUIRE(color != blue);
        }
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Transform", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[6 * 6] = {};
        REQUIRE(canvas->target(buffer, 6, 6, 6, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":6,"h":6,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":90},
                    "p":{"a":0,"k":[3,3,0]},
                    "a":{"a":0,"k":[1,1,0]},
                    "s":{"a":0,"k":[200,100,100]}
                }
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 36, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(buffer[2] == ctx.frame0Color);
        REQUIRE(buffer[2 + 2 * 6] == ctx.frame0Color);
        REQUIRE(buffer[3 + 3 * 6] == ctx.frame0Color);
        REQUIRE(buffer[0] == 0);
        REQUIRE(buffer[5 + 5 * 6] == 0);
        REQUIRE(buffer[1 + 2 * 6] == 0);
        REQUIRE(buffer[4 + 2 * 6] == 0);
        REQUIRE(_testCountPixels(buffer, 36, ctx.frame0Color) == 8);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Parent Transform", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[4 * 4] = {};
        REQUIRE(canvas->target(buffer, 4, 4, 4, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":4,"h":4,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","parent":2,"sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            },{
                "ind":2,"ty":3,"sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[2,1,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 16, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(buffer[2 + 1 * 4] == ctx.frame0Color);
        REQUIRE(buffer[3 + 2 * 4] == ctx.frame0Color);
        REQUIRE(buffer[1 + 1 * 4] == 0);
        REQUIRE(buffer[2 + 0 * 4] == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Mask", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
                "hasMask":true,
                "masksProperties":[{
                    "inv":false,"mode":"a",
                    "pt":{"a":0,"k":{"i":[[0,0],[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0],[0,0]],"v":[[0,0],[1,0],[1,2],[0,2]],"c":true}},
                    "o":{"a":0,"k":100},
                    "x":{"a":0,"k":0}
                }],
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
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(buffer[0] == ctx.frame0Color);
        REQUIRE(buffer[2] == ctx.frame0Color);
        REQUIRE(buffer[1] == 0);
        REQUIRE(buffer[3] == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Matte", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":4,"sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                },
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[1,2]},"p":{"a":0,"k":[0.5,1]}
                },{
                    "ty":"fl","c":{"a":0,"k":[1,1,1,1]},"o":{"a":0,"k":100}
                }]
            },{
                "ind":2,"ty":2,"refId":"video_hero","tt":1,"sr":1,"ip":0,"op":60,"st":0,
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
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(buffer[0] == ctx.frame0Color);
        REQUIRE(buffer[2] == ctx.frame0Color);
        REQUIRE(buffer[1] == 0);
        REQUIRE(buffer[3] == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Matte Source Requests Bitmap Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.frame0Color = 0xffff0000;
        for (auto& pixel : ctx.pixels) pixel = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

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
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(ctx.released == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Blend", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        constexpr uint32_t blue = 0xff0000ff;
        constexpr uint32_t multiplyRedOverBlue = 0xff000000;

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.frame0Color = 0xffff0000;
        for (auto& pixel : ctx.pixels) pixel = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

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
            },{
                "ind":2,"ty":4,"sr":1,"ip":0,"op":60,"st":0,
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[2,2]},"p":{"a":0,"k":[1,1]}
                },{
                    "ty":"fl","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100}
                }]
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
        REQUIRE(ctx.released == 1);
        REQUIRE(buffer[0] == multiplyRedOverBlue);
        REQUIRE(buffer[1] == multiplyRedOverBlue);
        REQUIRE(buffer[2] == multiplyRedOverBlue);
        REQUIRE(buffer[3] == multiplyRedOverBlue);
        REQUIRE(buffer[0] != blue);
        REQUIRE(buffer[0] != ctx.frame0Color);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Request Effect Flag", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

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
                "ef":[{"ty":21,"en":1,"ef":[]}],
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
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Effect));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Mask));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Matte));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Blend));
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Fill Effect", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        constexpr uint32_t red = 0xffff0000;
        constexpr uint32_t green = 0xff00ff00;

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = red;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
                "ef":[{
                    "ty":21,"en":1,
                    "ef":[
                        {"v":{"k":0}},
                        {"v":{"k":0}},
                        {"v":{"k":[0,1,0,1]}},
                        {"v":{"k":0}},
                        {"v":{"k":0}},
                        {"v":{"k":0}},
                        {"v":{"k":1}}
                    ]
                }],
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
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 4, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::Effect));
        REQUIRE(_testCountPixels(buffer, 4, green) == 4);
        REQUIRE(_testCountPixels(buffer, 4, red) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Poster Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.pending = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Pending Reuses Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        ctx.pending = true;
        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.lastTime == Approx(1.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Same Serial Reuses Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.fixedSerial = true;
        ctx.fixedSerialValue = 7;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(ctx.lastSerialHint == 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.released == 2);
        REQUIRE(ctx.lastSerialHint == 7);
        REQUIRE(ctx.lastTime == Approx(1.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Unavailable Frame Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        {
            LottieVideoProviderTestCtx ctx;
            ctx.frameResult = Result::InsufficientCondition;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
            REQUIRE(canvas);

            uint32_t buffer[2 * 2] = {};
            REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

            auto picture = animation->picture();
            REQUIRE(picture->resolver(resolver, poster) == Result::Success);

            char lottie[2048];
            _testVideoLottie(lottie, sizeof(lottie), false, true);
            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(canvas->add(picture) == Result::Success);
            REQUIRE(canvas->draw(true) == Result::Success);
            REQUIRE(canvas->sync() == Result::Success);

            REQUIRE(ctx.opened == 1);
            REQUIRE(ctx.framed == 1);
            REQUIRE(ctx.released == 0);
            REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        }

        {
            LottieVideoProviderTestCtx ctx;
            ctx.colorByTime = true;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
            REQUIRE(canvas);

            uint32_t buffer[2 * 2] = {};
            REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

            auto picture = animation->picture();
            REQUIRE(picture->resolver(resolver, poster) == Result::Success);

            char lottie[2048];
            _testVideoLottie(lottie, sizeof(lottie), false, true);
            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(canvas->add(picture) == Result::Success);
            REQUIRE(canvas->draw(true) == Result::Success);
            REQUIRE(canvas->sync() == Result::Success);
            REQUIRE(ctx.framed == 1);
            REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
            REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

            ctx.frameResult = Result::InsufficientCondition;
            _testFillPixels(buffer, 4, 0);
            REQUIRE(animation->frame(30.0f) == Result::Success);
            REQUIRE(canvas->update() == Result::Success);
            REQUIRE(canvas->draw(true) == Result::Success);
            REQUIRE(canvas->sync() == Result::Success);

            REQUIRE(ctx.framed == 2);
            REQUIRE(ctx.released == 1);
            REQUIRE(ctx.lastTime == Approx(1.0));
            REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
            REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
            REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
        }
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Invalid Bitmap Reuses Cached Frame", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        ctx.invalidBitmap = true;
        _testFillPixels(buffer, 4, 0);
        REQUIRE(animation->frame(30.0f) == Result::Success);
        REQUIRE(canvas->update() == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.released == 2);
        REQUIRE(ctx.lastTime == Approx(1.0));
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame1Color) == 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Unsupported Native Frame Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Native Frame Bitmap Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.pixels[0] = ctx.pixels[1] = ctx.pixels[2] = ctx.pixels[3] = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        canvas.reset();
        animation.reset();
        REQUIRE(ctx.released == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video WebGPU Native Frame Bitmap Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.wgNativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.pixels[0] = ctx.pixels[1] = ctx.pixels[2] = ctx.pixels[3] = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        canvas.reset();
        animation.reset();
        REQUIRE(ctx.released == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Platform Native Handle Uses Bitmap Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.platformNativeFrame = true;
        ctx.nativeBitmapFallback = true;
        for (auto& pixel : ctx.pixels) pixel = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::BitmapRequired));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::GlTexture));
        REQUIRE(!_testVideoRequestFlag(ctx.lastFlags, LottieVideoFrameRequestFlag::WgTexture));
        REQUIRE(_testVideoNativeHandleRequestFlagClear(ctx.lastFlags));

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);

        canvas.reset();
        animation.reset();
        REQUIRE(ctx.released == 1);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Invalid Native Frame Uses Bitmap Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        ctx.nativeFrame = true;
        ctx.nativeBitmapFallback = true;
        ctx.invalidNativeTarget = true;
        for (auto& pixel : ctx.pixels) pixel = ctx.frame0Color;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        char lottie[2048];
        _testVideoLottie(lottie, sizeof(lottie), false, true);
        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, ctx.frame0Color) > 0);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Static Image Compatibility", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{"id":"image_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0}],
            "layers":[{
                "ind":1,"ty":2,"refId":"image_hero","sr":1,"ip":0,"op":60,"st":0,
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
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Image Sequence Flag Compatibility", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{"id":"image_seq","w":2,"h":2,"u":"","p":"poster.raw","e":0,"t":"seq"}],
            "layers":[{
                "ind":1,"ty":2,"refId":"image_seq","sr":1,"ip":0,"op":60,"st":0,
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
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Invalid Extension Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        LottieVideoProviderTestCtx ctx;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[2 * 2] = {};
        REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto poster = static_cast<const uint32_t*>(data);
            return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, poster) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"mime":"video/mp4","duration":2,"frameRate":30,"loop":true}
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
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(ctx.opened == 0);
        REQUIRE(ctx.framed == 0);

        REQUIRE(canvas->add(picture) == Result::Success);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);
        REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Malformed Extension Fallback", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        const uint32_t posterColor = 0xff0000ff;
        uint32_t poster[4] = {posterColor, posterColor, posterColor, posterColor};

        auto verify = [&](const char* extension) {
            LottieVideoProviderTestCtx ctx;
            LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

            auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
            REQUIRE(animation);
            REQUIRE(animation->videoProvider(&provider) == Result::Success);

            auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
            REQUIRE(canvas);

            uint32_t buffer[2 * 2] = {};
            REQUIRE(canvas->target(buffer, 2, 2, 2, ColorSpace::ARGB8888) == Result::Success);

            auto picture = animation->picture();
            auto resolver = [](Paint* p, const char*, void* data) -> bool {
                if (p->type() != Type::Picture) return false;
                auto poster = static_cast<const uint32_t*>(data);
                return static_cast<Picture*>(p)->load(poster, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
            };

            REQUIRE(picture->resolver(resolver, poster) == Result::Success);

            char lottie[2048];
            snprintf(lottie, sizeof(lottie), R"({
                "v":"5.8.0","fr":30,"ip":0,"op":60,"w":2,"h":2,
                "assets":[{
                    "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                    "x-video":%s
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
            })", extension);

            REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
            REQUIRE(ctx.opened == 0);
            REQUIRE(ctx.framed == 0);

            REQUIRE(canvas->add(picture) == Result::Success);
            REQUIRE(canvas->draw(true) == Result::Success);
            REQUIRE(canvas->sync() == Result::Success);
            REQUIRE(_testCountPixels(buffer, 4, posterColor) > 0);
        };

        verify(R"("invalid")");
        verify(R"({"src":"","mime":"video/mp4","duration":2,"frameRate":30,"loop":true})");
        verify(R"({"src":7,"mime":false,"duration":"2","frameRate":null,"loop":[],"holdLastFrame":{},"muted":"yes"})");
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Software Composition", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        constexpr uint32_t blue = 0xff0000ff;

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = 0xffff0000;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[4 * 4] = {};
        REQUIRE(canvas->target(buffer, 4, 4, 4, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":4,"h":4,
            "assets":[{
                "id":"video_hero","w":2,"h":2,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            }],
            "layers":[{
                "ind":1,"ty":2,"refId":"video_hero","sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":50},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            },{
                "ind":2,"ty":4,"sr":1,"ip":0,"op":60,"st":0,
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[4,4]},"p":{"a":0,"k":[2,2]}
                },{
                    "ty":"fl","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100}
                }]
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 16, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(buffer[3 + 3 * 4] == blue);
        REQUIRE(buffer[0] != blue);
        REQUIRE(buffer[0] != ctx.frame0Color);
        REQUIRE(_testHasRedAndBlue(buffer[0]));
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Marker", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        //Set marker name before loaded
        REQUIRE(animation->segment("sectionC") == Result::InsufficientCondition);

        // Get marker info before loaded
        float markerBegin, markerEnd;
        REQUIRE(animation->marker(0, &markerBegin, &markerEnd) == nullptr);

        //Animation load
        REQUIRE(picture->load(TEST_DIR"/segment.lot") == Result::Success);

        //Set marker
        REQUIRE(animation->segment("sectionA") == Result::Success);

        //Set marker by invalid name
        REQUIRE(animation->segment("") == Result::InvalidArguments);

        //Get marker count
        REQUIRE(animation->markersCnt() == 3);

        //Get marker name by index
        REQUIRE(!strcmp(animation->marker(1), "sectionB"));

        // Get marker name and segment by index
        REQUIRE(!strcmp(animation->marker(0, &markerBegin, &markerEnd), "sectionA"));
        REQUIRE(markerBegin == 0.0f);
        REQUIRE(markerEnd == 22.0f);

        REQUIRE(!strcmp(animation->marker(1, &markerBegin, &markerEnd), "sectionB"));
        REQUIRE(markerBegin == 22.0f);
        REQUIRE(markerEnd == 33.0f);

        REQUIRE(!strcmp(animation->marker(2, &markerBegin, &markerEnd), "sectionC"));
        REQUIRE(markerBegin == 33.0f);
        REQUIRE(markerEnd == 63.0f);

        // Get marker with only begin
        REQUIRE(!strcmp(animation->marker(0, &markerBegin, nullptr), "sectionA"));
        REQUIRE(markerBegin == 0.0f);

        // Get marker with only end
        REQUIRE(!strcmp(animation->marker(0, nullptr, &markerEnd), "sectionA"));
        REQUIRE(markerEnd == 22.0f);

        // Get marker by invalid index
        REQUIRE(animation->marker(-1) == nullptr);
        REQUIRE(animation->marker(-1, &markerBegin, &markerEnd) == nullptr);

        REQUIRE(animation->segment(nullptr) == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Tween", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        REQUIRE(animation->tween(0.0f, 10.0f, 0.5f) == Result::InsufficientCondition);

        REQUIRE(picture->load(TEST_DIR"/test.lot") == Result::Success);

        //Set initial frame to avoid frame difference being too small
        REQUIRE(animation->frame(5.0f) == Result::Success);

        //Tween between frames with different progress values
        REQUIRE(animation->tween(0.0f, 10.0f, 0.5f) == Result::Success);
        REQUIRE(animation->tween(10.0f, 20.0f, 0.0f) == Result::Success);
        REQUIRE(animation->tween(20.0f, 30.0f, 1.0f) == Result::Success);

        //Tween with different frame ranges
        REQUIRE(animation->tween(10.0f, 50.0f, 0.25f) == Result::Success);
        REQUIRE(animation->tween(50.0f, 100.0f, 0.75f) == Result::Success);

        //Tween between distant frames
        REQUIRE(animation->tween(0.0f, 100.0f, 0.5f) == Result::Success);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Quality", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        REQUIRE(animation->quality(50) == Result::InsufficientCondition);

        REQUIRE(picture->load(TEST_DIR"/test.lot") == Result::Success);

        //Set quality with minimum value
        REQUIRE(animation->quality(0) == Result::Success);

        //Set quality with default value
        REQUIRE(animation->quality(50) == Result::Success);

        //Set quality with maximum value
        REQUIRE(animation->quality(100) == Result::Success);

        //Set quality with various values
        REQUIRE(animation->quality(25) == Result::Success);
        REQUIRE(animation->quality(75) == Result::Success);

        //Set quality with invalid value (> 100)
        REQUIRE(animation->quality(101) == Result::InvalidArguments);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Asset Resolver", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);

        auto picture = animation->picture();

        auto resolver = [](Paint* p, const char* src, void* data) -> bool {
            if (p->type() == Type::Picture) {
                string resolvedPath = string(TEST_DIR) + "/image/test.png";
                auto ret = static_cast<Picture*>(p)->load(resolvedPath.c_str());
                return (ret == Result::Success);
            } else if (p->type() == Type::Text) {
                string fontPath = string(TEST_DIR) + "/font/Arial.ttf";
                if (Text::load(fontPath.c_str()) != Result::Success) return false;
                auto ret = static_cast<Text*>(p)->font("Arial");
                return (ret == Result::Success);
            }
            return false;
        };

        // Test unset resolver
        REQUIRE(picture->resolver(resolver, nullptr) == Result::Success);
        REQUIRE(picture->resolver(nullptr, nullptr) == Result::Success);

        //Resolver Test (Image and Font)
        REQUIRE(picture->resolver(resolver, nullptr) == Result::Success);
        REQUIRE(picture->load(TEST_DIR"/resolver.json") == Result::Success);
        REQUIRE(animation->frame(animation->totalFrame() * 0.5f) == Result::Success);

        //Test that setting/unsetting resolver after load
        REQUIRE(picture->resolver(resolver, nullptr) == Result::InsufficientCondition);
        REQUIRE(picture->resolver(nullptr, nullptr) == Result::InsufficientCondition);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

TEST_CASE("Lottie Video Layer Ordering With Image And Vector", "[tvgLottie]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        constexpr uint32_t red = 0xffff0000;
        constexpr uint32_t green = 0xff00ff00;
        constexpr uint32_t blue = 0xff0000ff;

        struct ImageResolverData
        {
            int calls = 0;
            uint32_t pixels[4] = {green, green, green, green};
        } resolverData;

        LottieVideoProviderTestCtx ctx;
        ctx.colorByTime = true;
        ctx.frame0Color = red;
        ctx.frame0Width = 1;
        ctx.frame0Height = 1;
        LottieVideoProvider provider = {_testVideoOpen, _testVideoFrame, _testVideoClose, &ctx};

        auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
        REQUIRE(animation);
        REQUIRE(animation->videoProvider(&provider) == Result::Success);

        auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
        REQUIRE(canvas);

        uint32_t buffer[3 * 3] = {};
        REQUIRE(canvas->target(buffer, 3, 3, 3, ColorSpace::ARGB8888) == Result::Success);

        auto picture = animation->picture();
        auto resolver = [](Paint* p, const char*, void* data) -> bool {
            if (p->type() != Type::Picture) return false;
            auto resolverData = static_cast<ImageResolverData*>(data);
            ++resolverData->calls;
            return static_cast<Picture*>(p)->load(resolverData->pixels, 2, 2, ColorSpace::ARGB8888, true) == Result::Success;
        };

        REQUIRE(picture->resolver(resolver, &resolverData) == Result::Success);

        const char* lottie = R"({
            "v":"5.8.0","fr":30,"ip":0,"op":60,"w":3,"h":3,
            "assets":[{
                "id":"video_hero","w":1,"h":1,"u":"","p":"poster.raw","e":0,
                "x-video":{"src":"video.mp4","mime":"video/mp4","duration":2,"frameRate":30,"loop":false,"holdLastFrame":true,"muted":true}
            },{
                "id":"image_mid","w":2,"h":2,"u":"","p":"image.raw","e":0
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
                "ind":2,"ty":2,"refId":"image_mid","sr":1,"ip":0,"op":60,"st":0,
                "ks":{
                    "o":{"a":0,"k":100},
                    "r":{"a":0,"k":0},
                    "p":{"a":0,"k":[0,0,0]},
                    "a":{"a":0,"k":[0,0,0]},
                    "s":{"a":0,"k":[100,100,100]}
                }
            },{
                "ind":3,"ty":4,"sr":1,"ip":0,"op":60,"st":0,
                "shapes":[{
                    "ty":"rc","s":{"a":0,"k":[3,3]},"p":{"a":0,"k":[1.5,1.5]}
                },{
                    "ty":"fl","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":100}
                }]
            }]
        })";

        REQUIRE(picture->load(lottie, strlen(lottie), "lot", TEST_DIR, true) == Result::Success);
        REQUIRE(canvas->add(picture) == Result::Success);
        _testFillPixels(buffer, 9, 0);
        REQUIRE(canvas->draw(true) == Result::Success);
        REQUIRE(canvas->sync() == Result::Success);

        REQUIRE(ctx.framed == 1);
        REQUIRE(resolverData.calls == 1);
        REQUIRE(buffer[0] == red);
        REQUIRE(buffer[1] == green);
        REQUIRE(buffer[3] == green);
        REQUIRE(buffer[4] == green);
        REQUIRE(buffer[8] == blue);
        REQUIRE(_testCountPixels(buffer, 9, red) == 1);
        REQUIRE(_testCountPixels(buffer, 9, green) == 3);
        REQUIRE(_testCountPixels(buffer, 9, blue) == 5);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

enum class LottieVideoBenchmarkMode
{
    Poster,
    Bitmap,
    Pending
};

struct LottieVideoBenchmarkCtx
{
    vector<uint32_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t serial = 1;
    uint32_t framed = 0;
    uint32_t pending = 0;
    uint32_t pendingEvery = 0;
};

struct LottieVideoBenchmarkResult
{
    double avgMs = 0.0;
    uint32_t framed = 0;
    uint32_t pending = 0;
    uint64_t decodedBytes = 0;
};

static Result _benchmarkVideoOpen(const LottieVideoAssetInfo*, void*)
{
    return Result::Success;
}

static Result _benchmarkVideoFrame(const LottieVideoFrameRequest*, LottieVideoFrame* out, void* data)
{
    auto ctx = static_cast<LottieVideoBenchmarkCtx*>(data);
    ++ctx->framed;
    if (ctx->pendingEvery > 0 && ctx->framed > 1 && (ctx->framed % ctx->pendingEvery) == 0) {
        ++ctx->pending;
        out->type = LottieVideoFrameType::None;
        return Result::Success;
    }

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

static bool _benchmarkResolver(Paint* paint, const char*, void* data)
{
    if (paint->type() != Type::Picture) return false;
    auto ctx = static_cast<LottieVideoBenchmarkCtx*>(data);
    return static_cast<Picture*>(paint)->load(ctx->pixels.data(), ctx->width, ctx->height, ColorSpace::ARGB8888, true) == Result::Success;
}

static string _benchmarkVideoLottie(uint32_t width, uint32_t height, uint32_t layers)
{
    string ret = "{\"v\":\"5.8.0\",\"fr\":30,\"ip\":0,\"op\":240,\"w\":";
    ret += to_string(width);
    ret += ",\"h\":";
    ret += to_string(height);
    ret += ",\"assets\":[";
    for (uint32_t i = 0; i < layers; ++i) {
        if (i > 0) ret += ",";
        ret += "{\"id\":\"video_";
        ret += to_string(i);
        ret += "\",\"w\":";
        ret += to_string(width);
        ret += ",\"h\":";
        ret += to_string(height);
        ret += ",\"u\":\"\",\"p\":\"poster.raw\",\"e\":0,"
               "\"x-video\":{\"src\":\"video.mp4\",\"mime\":\"video/mp4\",\"duration\":8,\"frameRate\":30,\"loop\":true,\"holdLastFrame\":true,\"muted\":true}}";
    }
    ret += "],\"layers\":[";
    for (uint32_t i = 0; i < layers; ++i) {
        if (i > 0) ret += ",";
        ret += "{\"ind\":";
        ret += to_string(i + 1);
        ret += ",\"ty\":2,\"refId\":\"video_";
        ret += to_string(i);
        ret += "\",\"sr\":1,\"ip\":0,\"op\":240,\"st\":0,"
               "\"ks\":{\"o\":{\"a\":0,\"k\":100},\"r\":{\"a\":0,\"k\":0},"
               "\"p\":{\"a\":0,\"k\":[0,0,0]},\"a\":{\"a\":0,\"k\":[0,0,0]},"
               "\"s\":{\"a\":0,\"k\":[100,100,100]}}}";
    }
    ret += "]}";
    return ret;
}

static LottieVideoBenchmarkResult _runVideoBenchmark(uint32_t width, uint32_t height, uint32_t layers, LottieVideoBenchmarkMode mode, uint32_t iterations)
{
    LottieVideoBenchmarkCtx ctx;
    ctx.width = width;
    ctx.height = height;
    ctx.pendingEvery = (mode == LottieVideoBenchmarkMode::Pending) ? 2 : 0;

    auto count = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    ctx.pixels.resize(static_cast<size_t>(count), 0xffff0000);
    vector<uint32_t> target(static_cast<size_t>(count), 0);

    auto animation = unique_ptr<LottieAnimation>(LottieAnimation::gen());
    REQUIRE(animation);
    LottieVideoProvider provider = {_benchmarkVideoOpen, _benchmarkVideoFrame, nullptr, &ctx};
    if (mode != LottieVideoBenchmarkMode::Poster) REQUIRE(animation->videoProvider(&provider) == Result::Success);

    auto canvas = unique_ptr<SwCanvas>(SwCanvas::gen());
    REQUIRE(canvas);
    REQUIRE(canvas->target(target.data(), width, width, height, ColorSpace::ARGB8888) == Result::Success);

    auto picture = animation->picture();
    REQUIRE(picture->resolver(_benchmarkResolver, &ctx) == Result::Success);
    auto lottie = _benchmarkVideoLottie(width, height, layers);
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

    return {elapsed.count() / double(iterations), ctx.framed, ctx.pending, count * sizeof(uint32_t) * layers};
}

TEST_CASE("Lottie Video Performance Benchmarks", "[.][benchmark][lottieVideo]")
{
    REQUIRE(Initializer::init() == Result::Success);
    {
        auto report = [](const char* name, uint32_t width, uint32_t height, uint32_t layers, LottieVideoBenchmarkMode mode) {
            auto result = _runVideoBenchmark(width, height, layers, mode, 2);
            WARN(name << ": " << result.avgMs << " ms/frame, provider frames=" << result.framed
                      << ", pending=" << result.pending << ", decoded bytes=" << result.decodedBytes);
        };

        report("poster fallback baseline", 256, 144, 1, LottieVideoBenchmarkMode::Poster);
        report("cpu bitmap 720p", 1280, 720, 1, LottieVideoBenchmarkMode::Bitmap);
        report("cpu bitmap 1080p", 1920, 1080, 1, LottieVideoBenchmarkMode::Bitmap);
        report("cpu bitmap 4k", 3840, 2160, 1, LottieVideoBenchmarkMode::Bitmap);
        report("cpu bitmap 2 layers 720p", 1280, 720, 2, LottieVideoBenchmarkMode::Bitmap);
        report("cpu bitmap 4 layers 720p", 1280, 720, 4, LottieVideoBenchmarkMode::Bitmap);
        report("pending stress 720p", 1280, 720, 1, LottieVideoBenchmarkMode::Pending);
    }
    REQUIRE(Initializer::term() == Result::Success);
}

#endif
