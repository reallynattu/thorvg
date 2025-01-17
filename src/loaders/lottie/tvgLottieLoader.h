/*
 * Copyright (c) 2023 the ThorVG project. All rights reserved.

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

#ifndef _TVG_LOTTIE_LOADER_H_
#define _TVG_LOTTIE_LOADER_H_

#include "tvgCommon.h"
#include "tvgFrameModule.h"
#include "tvgTaskScheduler.h"

struct LottieComposition;
struct LottieBuilder;

class LottieLoader : public FrameModule, public Task
{
public:
    const char* content = nullptr;      //lottie file data
    uint32_t size = 0;                  //lottie data size
    uint32_t frameRate;
    uint32_t frameNo = 0;               //current frame number

    LottieBuilder* builder = nullptr;
    LottieComposition* comp = nullptr;

    unique_ptr<Scene> root;             //current motion frame

    bool copy = false;                  //"content" is owned by this loader

    LottieLoader();
    ~LottieLoader();

    //Lottie Loaders
    using LoadModule::open;
    bool open(const string& path) override;
    bool open(const char* data, uint32_t size, bool copy) override;
    bool resize(Paint* paint, float w, float h) override;
    bool read() override;
    bool close() override;
    unique_ptr<Paint> paint() override;

    //Frame Controls
    bool frame(uint32_t frameNo) override;
    uint32_t totalFrame() override;
    uint32_t curFrame() override;
    float duration() override;

private:
    bool header();
    void clear();
    void run(unsigned tid) override;
};


#endif //_TVG_LOTTIELOADER_H_
