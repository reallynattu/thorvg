/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd All Rights Reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *               http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
#ifndef _TVG_SHAPE_CPP_
#define _TVG_SHAPE_CPP_

#include "tvgCommon.h"
#include "tvgShapeImpl.h"

/************************************************************************/
/* Internal Class Implementation                                        */
/************************************************************************/
constexpr auto PATH_KAPPA = 0.552284f;


/************************************************************************/
/* External Class Implementation                                        */
/************************************************************************/

Shape :: Shape() : pImpl(make_unique<Impl>())
{
}


Shape :: ~Shape()
{
}


unique_ptr<Shape> Shape::gen() noexcept
{
    return unique_ptr<Shape>(new Shape);
}


Result Shape::reset() noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->reset();

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


uint32_t Shape::pathCommands(const PathCommand** cmds) const noexcept
{
    if (!cmds) return 0;

    auto impl = pImpl.get();
    if (!impl || !impl->path) return 0;

    *cmds = impl->path->cmds;

    return impl->path->cmdCnt;
}


uint32_t Shape::pathCoords(const Point** pts) const noexcept
{
    if (!pts) return 0;

    auto impl = pImpl.get();
    if (!impl || !impl->path) return 0;

    *pts = impl->path->pts;

    return impl->path->ptsCnt;
}


Result Shape::appendPath(const PathCommand *cmds, uint32_t cmdCnt, const Point* pts, uint32_t ptsCnt) noexcept
{
    if (cmdCnt < 0 || ptsCnt < 0 || !pts || !ptsCnt) return Result::InvalidArguments;

    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->grow(cmdCnt, ptsCnt);
    impl->path->append(cmds, cmdCnt, pts, ptsCnt);

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::moveTo(float x, float y) noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->moveTo(x, y);

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::lineTo(float x, float y) noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->lineTo(x, y);

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::cubicTo(float cx1, float cy1, float cx2, float cy2, float x, float y) noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->cubicTo(cx1, cy1, cx2, cy2, x, y);

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::close() noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    impl->path->close();

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::appendCircle(float cx, float cy, float radiusW, float radiusH) noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    auto halfKappaW = radiusW * PATH_KAPPA;
    auto halfKappaH = radiusH * PATH_KAPPA;

    impl->path->grow(6, 13);
    impl->path->moveTo(cx, cy - radiusH);
    impl->path->cubicTo(cx + halfKappaW, cy - radiusH, cx + radiusW, cy - halfKappaH, cx + radiusW, cy);
    impl->path->cubicTo(cx + radiusW, cy + halfKappaH, cx + halfKappaW, cy + radiusH, cx, cy + radiusH);
    impl->path->cubicTo(cx - halfKappaW, cy + radiusH, cx - radiusW, cy + halfKappaH, cx - radiusW, cy);
    impl->path->cubicTo(cx - radiusW, cy - halfKappaH, cx - halfKappaW, cy - radiusH, cx, cy - radiusH);
    impl->path->close();

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::appendRect(float x, float y, float w, float h, float cornerRadius) noexcept
{
    auto impl = pImpl.get();
    if (!impl || !impl->path) return Result::MemoryCorruption;

    //clamping cornerRadius by minimum size
    auto min = (w < h ? w : h) * 0.5f;
    if (cornerRadius > min) cornerRadius = min;

    //rectangle
    if (cornerRadius == 0) {
        impl->path->grow(5, 4);
        impl->path->moveTo(x, y);
        impl->path->lineTo(x + w, y);
        impl->path->lineTo(x + w, y + h);
        impl->path->lineTo(x, y + h);
        impl->path->close();
    //circle
    } else if (w == h && cornerRadius * 2 == w) {
        return appendCircle(x + (w * 0.5f), y + (h * 0.5f), cornerRadius, cornerRadius);
    } else {
        auto halfKappa = cornerRadius * 0.5;
        impl->path->grow(10, 17);
        impl->path->moveTo(x + cornerRadius, y);
        impl->path->lineTo(x + w - cornerRadius, y);
        impl->path->cubicTo(x + w - cornerRadius + halfKappa, y, x + w, y + cornerRadius - halfKappa, x + w, y + cornerRadius);
        impl->path->lineTo(x + w, y + h - cornerRadius);
        impl->path->cubicTo(x + w, y + h - cornerRadius + halfKappa, x + w - cornerRadius + halfKappa, y + h, x + w - cornerRadius, y + h);
        impl->path->lineTo(x + cornerRadius, y + h);
        impl->path->cubicTo(x + cornerRadius - halfKappa, y + h, x, y + h - cornerRadius + halfKappa, x, y + h - cornerRadius);
        impl->path->lineTo(x, y + cornerRadius);
        impl->path->cubicTo(x, y + cornerRadius - halfKappa, x + cornerRadius - halfKappa, y, x + cornerRadius, y);
        impl->path->close();
    }

    impl->flag |= RenderUpdateFlag::Path;

    return Result::Success;
}


Result Shape::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    impl->color[0] = r;
    impl->color[1] = g;
    impl->color[2] = b;
    impl->color[3] = a;
    impl->flag |= RenderUpdateFlag::Fill;

    return Result::Success;
}


Result Shape::fill(uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) const noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (r) *r = impl->color[0];
    if (g) *g = impl->color[1];
    if (b) *b = impl->color[2];
    if (a) *a = impl->color[3];

    return Result::Success;
}


Result Shape::scale(float factor) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->scale(factor)) return Result::FailedAllocation;

    return Result::Success;
}


Result Shape::rotate(float degree) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->rotate(degree)) return Result::FailedAllocation;

    return Result::Success;
}


Result Shape::translate(float x, float y) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    impl->translate(x, y);

    return Result::Success;
}


Result Shape::bounds(float* x, float* y, float* w, float* h) const noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->bounds(x, y, w, h)) return Result::InsufficientCondition;

    return Result::Success;
}


Result Shape::stroke(float width) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->strokeWidth(width)) return Result::FailedAllocation;

    return Result::Success;
}


float Shape::strokeWidth() const noexcept
{
    auto impl = pImpl.get();
    if (!impl) return 0;

    if (!impl->stroke) return 0;
    return impl->stroke->width;
}


Result Shape::stroke(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->strokeColor(r, g, b, a)) return Result::FailedAllocation;

    return Result::Success;
}


Result Shape::strokeColor(uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) const noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->stroke) return Result::InsufficientCondition;

    if (r) *r = impl->stroke->color[0];
    if (g) *g = impl->stroke->color[1];
    if (b) *b = impl->stroke->color[2];
    if (a) *a = impl->stroke->color[3];

    return Result::Success;
}


Result Shape::stroke(const float* dashPattern, uint32_t cnt) noexcept
{
    if (cnt < 2 || !dashPattern) return Result::InvalidArguments;

    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->strokeDash(dashPattern, cnt)) return Result::FailedAllocation;

    return Result::Success;
}


uint32_t Shape::strokeDash(const float** dashPattern) const noexcept
{
    auto impl = pImpl.get();
    assert(impl);

    if (!impl->stroke) return 0;

    if (dashPattern) *dashPattern = impl->stroke->dashPattern;
    return impl->stroke->dashCnt;
}


Result Shape::stroke(StrokeCap cap) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->strokeCap(cap)) return Result::FailedAllocation;

    return Result::Success;
}


Result Shape::stroke(StrokeJoin join) noexcept
{
    auto impl = pImpl.get();
    if (!impl) return Result::MemoryCorruption;

    if (!impl->strokeJoin(join)) return Result::FailedAllocation;

    return Result::Success;
}


StrokeCap Shape::strokeCap() const noexcept
{
    auto impl = pImpl.get();
    assert(impl);

    if (!impl->stroke) return StrokeCap::Square;

    return impl->stroke->cap;
}


StrokeJoin Shape::strokeJoin() const noexcept
{
    auto impl = pImpl.get();
    assert(impl);

    if (!impl->stroke) return StrokeJoin::Bevel;

    return impl->stroke->join;
}


#endif //_TVG_SHAPE_CPP_
