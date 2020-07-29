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
#ifndef _THORVG_H_
#define _THORVG_H_

#include <memory>

#ifdef TVG_BUILD
    #define TVG_EXPORT __attribute__ ((visibility ("default")))
#else
    #define TVG_EXPORT
#endif

#ifdef  LOG_TAG
#undef  LOG_TAG
#endif
#define LOG_TAG "TVG"


#ifdef __cplusplus
extern "C" {
#endif


#define _TVG_DECLARE_PRIVATE(A) \
protected: \
    struct Impl; \
    std::unique_ptr<Impl> pImpl; \
    A(const A&) = delete; \
    const A& operator=(const A&) = delete; \
    A()

#define _TVG_DISABLE_CTOR(A) \
    A() = delete; \
    ~A() = delete

#define _TVG_DECLARE_ACCESSOR(A) \
    friend A

#define _TVG_DECALRE_IDENTIFIER() \
    auto id() const { return _id; } \
protected: \
    unsigned _id

namespace tvg
{

class RenderMethod;
class Scene;
class Canvas;


enum class TVG_EXPORT Result { Success = 0, InvalidArguments, InsufficientCondition, FailedAllocation, MemoryCorruption, NonSupport, Unknown };
enum class TVG_EXPORT PathCommand { Close = 0, MoveTo, LineTo, CubicTo };
enum class TVG_EXPORT StrokeCap { Square = 0, Round, Butt };
enum class TVG_EXPORT StrokeJoin { Bevel = 0, Round, Miter };
enum class TVG_EXPORT FillSpread { Pad = 0, Reflect, Repeat };
enum class TVG_EXPORT CanvasEngine { Sw = 0, Gl };


struct Point
{
    float x, y;
};


struct Matrix
{
    float e11, e12, e13;
    float e21, e22, e23;
    float e31, e32, e33;
};


/**
 * @class Paint
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Paint
{
public:
    virtual ~Paint();

    Result rotate(float degree) noexcept;
    Result scale(float factor) noexcept;
    Result translate(float x, float y) noexcept;
    Result transform(const Matrix& m) noexcept;
    Result bounds(float* x, float* y, float* w, float* h) const noexcept;

    _TVG_DECALRE_IDENTIFIER();
    _TVG_DECLARE_PRIVATE(Paint);
};


/**
 * @class Fill
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Fill
{
public:
    struct ColorStop
    {
        float offset;
        uint8_t r, g, b, a;
    };

    virtual ~Fill();

    Result colorStops(const ColorStop* colorStops, uint32_t cnt) noexcept;
    Result spread(FillSpread s) noexcept;

    uint32_t colorStops(const ColorStop** colorStops) const noexcept;
    FillSpread spread() const noexcept;

    _TVG_DECALRE_IDENTIFIER();
    _TVG_DECLARE_PRIVATE(Fill);
};


/**
 * @class Canvas
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Canvas
{
public:
    Canvas(RenderMethod*);
    virtual ~Canvas();

    Result reserve(uint32_t n) noexcept;
    virtual Result push(std::unique_ptr<Paint> paint) noexcept;
    virtual Result clear() noexcept;
    virtual Result update() noexcept;
    virtual Result update(Paint* paint) noexcept;
    virtual Result draw(bool async = true) noexcept;
    virtual Result sync() noexcept;

    _TVG_DECLARE_ACCESSOR(Scene);
    _TVG_DECLARE_PRIVATE(Canvas);
};



/**
 * @class LinearGradient
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT LinearGradient final : public Fill
{
public:
    ~LinearGradient();

    Result linear(float x1, float y1, float x2, float y2) noexcept;
    Result linear(float* x1, float* y1, float* x2, float* y2) const noexcept;

    static std::unique_ptr<LinearGradient> gen() noexcept;

    _TVG_DECLARE_PRIVATE(LinearGradient);
};


/**
 * @class RadialGradient
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT RadialGradient final : public Fill
{
public:
    ~RadialGradient();

    Result radial(float cx, float cy, float radius) noexcept;
    Result radial(float* cx, float* cy, float* radius) const noexcept;

    static std::unique_ptr<RadialGradient> gen() noexcept;

    _TVG_DECLARE_PRIVATE(RadialGradient);
};



/**
 * @class Shape
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Shape final : public Paint
{
public:
    ~Shape();

    Result reset() noexcept;

    //Path
    Result moveTo(float x, float y) noexcept;
    Result lineTo(float x, float y) noexcept;
    Result cubicTo(float cx1, float cy1, float cx2, float cy2, float x, float y) noexcept;
    Result close() noexcept;

    //Shape
    Result appendRect(float x, float y, float w, float h, float rx, float ry) noexcept;
    Result appendCircle(float cx, float cy, float rx, float ry) noexcept;
    Result appendPath(const PathCommand* cmds, uint32_t cmdCnt, const Point* pts, uint32_t ptsCnt) noexcept;

    //Stroke
    Result stroke(float width) noexcept;
    Result stroke(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept;
    Result stroke(const float* dashPattern, uint32_t cnt) noexcept;
    Result stroke(StrokeCap cap) noexcept;
    Result stroke(StrokeJoin join) noexcept;

    //Fill
    Result fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept;
    Result fill(std::unique_ptr<Fill> f) noexcept;

    //Getters
    uint32_t pathCommands(const PathCommand** cmds) const noexcept;
    uint32_t pathCoords(const Point** pts) const noexcept;
    Result fill(uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) const noexcept;
    const Fill* fill() const noexcept;

    float strokeWidth() const noexcept;
    Result strokeColor(uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) const noexcept;
    uint32_t strokeDash(const float** dashPattern) const noexcept;
    StrokeCap strokeCap() const noexcept;
    StrokeJoin strokeJoin() const noexcept;

    static std::unique_ptr<Shape> gen() noexcept;

    _TVG_DECLARE_PRIVATE(Shape);
    _TVG_DECLARE_ACCESSOR(Canvas);
    _TVG_DECLARE_ACCESSOR(Scene);
};


/**
 * @class Scene
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Scene final : public Paint
{
public:
    ~Scene();

    Result push(std::unique_ptr<Paint> paint) noexcept;
    Result reserve(uint32_t size) noexcept;
    Result load(const std::string& path) noexcept;

    static std::unique_ptr<Scene> gen() noexcept;

    _TVG_DECLARE_ACCESSOR(Canvas);
    _TVG_DECLARE_PRIVATE(Scene);
};


/**
 * @class SwCanvas
 *
 * @ingroup ThorVG
 *
  @brief description...
 *
 */
class TVG_EXPORT SwCanvas final : public Canvas
{
public:
    ~SwCanvas();

    Result target(uint32_t* buffer, uint32_t stride, uint32_t w, uint32_t h) noexcept;

    static std::unique_ptr<SwCanvas> gen() noexcept;

    _TVG_DECLARE_PRIVATE(SwCanvas);
};


/**
 * @class GlCanvas
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT GlCanvas final : public Canvas
{
public:
    ~GlCanvas();

    //TODO: Gl Specific methods. Need gl backend configuration methods as well.
    Result target(uint32_t* buffer, uint32_t stride, uint32_t w, uint32_t h) noexcept;

    static std::unique_ptr<GlCanvas> gen() noexcept;

    _TVG_DECLARE_PRIVATE(GlCanvas);
};


/**
 * @class Engine
 *
 * @ingroup ThorVG
 *
 * @brief description...
 *
 */
class TVG_EXPORT Initializer final
{
public:

    /**
     * @brief ...
     *
     * @param[in] arg ...
     *
     * @note ...
     *
     * @return ...
     *
     * @see ...
     */
    static Result init(CanvasEngine engine) noexcept;
    static Result term(CanvasEngine engine) noexcept;

    _TVG_DISABLE_CTOR(Initializer);
};

} //namespace

#ifdef __cplusplus
}
#endif

#endif //_THORVG_H_
