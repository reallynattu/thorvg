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

#include <cstring>
#include "catch.hpp"
#include "thorvg_capi.h"


struct CapiVideoCtx
{
    int opened = 0;
    int framed = 0;
    int closed = 0;
    int released = 0;
    uint32_t flags = 0;
    uint64_t serialHint = 0;
    double time = -1.0;
    bool pending = false;
    uint32_t pixels[4] = {};
};


static Tvg_Result _capiVideoOpen(const Tvg_Lottie_Video_Asset_Info* asset, void* data)
{
    auto ctx = static_cast<CapiVideoCtx*>(data);
    ++ctx->opened;

    REQUIRE(asset);
    REQUIRE(asset->asset_id);
    REQUIRE(!strcmp(asset->asset_id, "video_hero"));
    REQUIRE(asset->src);
    REQUIRE(strstr(asset->src, "video.mp4"));
    REQUIRE(asset->mime);
    REQUIRE(!strcmp(asset->mime, "video/mp4"));
    REQUIRE(asset->width == 2.0f);
    REQUIRE(asset->height == 2.0f);
    REQUIRE(asset->duration == 2.0f);
    REQUIRE(asset->frame_rate == 30.0f);
    REQUIRE(!asset->loop);
    REQUIRE(asset->hold_last_frame);
    REQUIRE(asset->muted);

    return TVG_RESULT_SUCCESS;
}


static void _capiVideoRelease(void* data)
{
    auto ctx = static_cast<CapiVideoCtx*>(data);
    ++ctx->released;
}


static Tvg_Result _capiVideoFrame(const Tvg_Lottie_Video_Frame_Request* request, Tvg_Lottie_Video_Frame* out, void* data)
{
    auto ctx = static_cast<CapiVideoCtx*>(data);
    ++ctx->framed;

    REQUIRE(request);
    REQUIRE(request->asset);
    REQUIRE(out);

    ctx->flags = request->flags;
    ctx->serialHint = request->serial_hint;
    ctx->time = request->time;

    if (ctx->pending) {
        out->type = TVG_LOTTIE_VIDEO_FRAME_TYPE_NONE;
        return TVG_RESULT_SUCCESS;
    }

    out->type = TVG_LOTTIE_VIDEO_FRAME_TYPE_NATIVE_HANDLE;
    out->data = ctx->pixels;
    out->width = 2;
    out->height = 2;
    out->colorspace = TVG_COLORSPACE_ARGB8888;
    out->timestamp = request->time;
    out->duration = 1.0 / 30.0;
    out->serial = 11;
    out->native_id = 7;
    out->native_target = 0x0DE1;
    out->release = _capiVideoRelease;
    out->user = data;

    return TVG_RESULT_SUCCESS;
}


static void _capiVideoClose(const char* assetId, void* data)
{
    auto ctx = static_cast<CapiVideoCtx*>(data);
    if (assetId && !strcmp(assetId, "video_hero")) ++ctx->closed;
}


TEST_CASE("CAPI Lottie Video Provider", "[tvgCapi]")
{
    static const char lottie[] =
        "{"
        "\"v\":\"5.8.0\",\"fr\":30,\"ip\":0,\"op\":60,\"w\":2,\"h\":2,"
        "\"assets\":[{"
        "\"id\":\"video_hero\",\"w\":2,\"h\":2,\"u\":\"\",\"p\":\"poster.raw\",\"e\":0,"
        "\"x-video\":{\"src\":\"video.mp4\",\"mime\":\"video/mp4\",\"duration\":2,\"frameRate\":30,\"loop\":false,\"holdLastFrame\":true,\"muted\":true}"
        "}],"
        "\"layers\":[{"
        "\"ind\":1,\"ty\":2,\"refId\":\"video_hero\",\"sr\":1,\"ip\":0,\"op\":60,\"st\":0,"
        "\"ks\":{"
        "\"o\":{\"a\":0,\"k\":100},"
        "\"r\":{\"a\":0,\"k\":0},"
        "\"p\":{\"a\":0,\"k\":[0,0,0]},"
        "\"a\":{\"a\":0,\"k\":[0,0,0]},"
        "\"s\":{\"a\":0,\"k\":[100,100,100]}"
        "}"
        "}]"
        "}";

    REQUIRE(tvg_engine_init(0) == TVG_RESULT_SUCCESS);
    {
        CapiVideoCtx ctx;
        for (auto& pixel : ctx.pixels) pixel = 0xffff0000;

        auto animation = tvg_lottie_animation_new();
        REQUIRE(animation);

        Tvg_Lottie_Video_Provider provider = {_capiVideoOpen, _capiVideoFrame, _capiVideoClose, &ctx};
        REQUIRE(tvg_lottie_animation_set_video_provider(animation, &provider) == TVG_RESULT_SUCCESS);

        auto picture = tvg_animation_get_picture(animation);
        REQUIRE(picture);
        REQUIRE(tvg_picture_load_data(picture, lottie, sizeof(lottie) - 1, "lot", TEST_DIR, true) == TVG_RESULT_SUCCESS);

        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 1);
        REQUIRE(ctx.released == 1);
        REQUIRE(ctx.serialHint == 0);
        REQUIRE(ctx.time == Approx(0.0));
        REQUIRE((ctx.flags & TVG_LOTTIE_VIDEO_FRAME_REQUEST_FLAG_BITMAP_REQUIRED) != 0);
        REQUIRE((ctx.flags & TVG_LOTTIE_VIDEO_FRAME_REQUEST_FLAG_GL_TEXTURE) == 0);
        REQUIRE((ctx.flags & TVG_LOTTIE_VIDEO_FRAME_REQUEST_FLAG_WG_TEXTURE) == 0);
        REQUIRE((ctx.flags & TVG_LOTTIE_VIDEO_FRAME_REQUEST_FLAG_NATIVE_HANDLE) == 0);

        Tvg_Lottie_Video_Provider invalidProvider = {_capiVideoOpen, nullptr, _capiVideoClose, &ctx};
        REQUIRE(tvg_lottie_animation_set_video_provider(animation, &invalidProvider) == TVG_RESULT_INVALID_ARGUMENT);
        REQUIRE(ctx.closed == 0);

        REQUIRE(tvg_animation_set_frame(animation, 30.0f) == TVG_RESULT_SUCCESS);
        REQUIRE(ctx.opened == 1);
        REQUIRE(ctx.framed == 2);
        REQUIRE(ctx.released == 2);
        REQUIRE(ctx.serialHint == 11);
        REQUIRE(ctx.time == Approx(1.0));

        CapiVideoCtx pendingCtx;
        pendingCtx.pending = true;
        Tvg_Lottie_Video_Provider pendingProvider = {_capiVideoOpen, _capiVideoFrame, _capiVideoClose, &pendingCtx};
        REQUIRE(tvg_lottie_animation_set_video_provider(animation, &pendingProvider) == TVG_RESULT_SUCCESS);
        REQUIRE(ctx.closed == 1);
        REQUIRE(pendingCtx.opened == 0);
        REQUIRE(pendingCtx.framed == 0);

        REQUIRE(tvg_animation_set_frame(animation, 45.0f) == TVG_RESULT_SUCCESS);
        REQUIRE(pendingCtx.opened == 1);
        REQUIRE(pendingCtx.framed == 1);
        REQUIRE(pendingCtx.released == 0);
        REQUIRE(pendingCtx.serialHint == 0);
        REQUIRE(pendingCtx.time == Approx(1.5));

        REQUIRE(tvg_lottie_animation_set_video_provider(animation, nullptr) == TVG_RESULT_SUCCESS);
        REQUIRE(pendingCtx.closed == 1);

        REQUIRE(tvg_animation_del(animation) == TVG_RESULT_SUCCESS);
        REQUIRE(ctx.closed == 1);
        REQUIRE(pendingCtx.closed == 1);
    }
    REQUIRE(tvg_engine_term() == TVG_RESULT_SUCCESS);
}
