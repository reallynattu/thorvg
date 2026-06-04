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

#include "tvgWgTextureMgr.h"

static WGPUTexture _nativeWgTexture(const RenderSurface* surface)
{
    if (surface->nativeType != RenderSurfaceNativeType::WgTexture) return nullptr;
    return static_cast<WGPUTexture>(surface->nativeHandle);
}

static tvg::Inlist<WgTextureEntry>& _entries(WgTextureMgr::SurfaceEntry& surfaceEntry, FilterMethod filter)
{
    return (filter == FilterMethod::Bilinear) ? surfaceEntry.bilinear : surfaceEntry.nearest;
}

static WgTextureEntry* _findEntry(tvg::Inlist<WgTextureEntry>& entries, WGPUTexture texture)
{
    INLIST_FOREACH(entries, entry)
    {
        if (entry->texture == texture) return entry;
    }
    return nullptr;
}

WgTextureMgr::SurfaceEntry* WgTextureMgr::find(const RenderSurface* surface)
{
    INLIST_FOREACH(surfaces, entry)
    {
        if (entry->surface == surface) return entry;
    }
    return nullptr;
}

WGPUTextureFormat WgTextureMgr::textureFormat(const RenderSurface* surface)
{
    if (surface->cs == ColorSpace::ABGR8888 || surface->cs == ColorSpace::ABGR8888S) return WGPUTextureFormat_RGBA8Unorm;
    if (surface->cs == ColorSpace::ARGB8888 || surface->cs == ColorSpace::ARGB8888S) return WGPUTextureFormat_BGRA8Unorm;
    return WGPUTextureFormat_R8Unorm;  // must be
}

void WgTextureMgr::upload(WgContext& context, WgTextureEntry& entry, const RenderSurface* surface, FilterMethod filter)
{
    auto bytesPerRow = surface->stride * CHANNEL_SIZE(surface->cs);
    auto dataSize = static_cast<uint64_t>(bytesPerRow) * surface->h;
    if (!context.allocateTexture(entry.texture, surface->w, surface->h, textureFormat(surface), surface->data, bytesPerRow, dataSize)) return;

    context.releaseTextureView(entry.textureView);
    entry.textureView = context.createTextureView(entry.texture);

    context.layouts.releaseBindGroup(entry.bindGroup);
    auto sampler = (filter == FilterMethod::Bilinear) ? context.samplerLinearClamp : context.samplerNearestClamp;
    entry.bindGroup = context.layouts.createBindGroupTexSampled(sampler, entry.textureView);
    entry.serial = surface->serial;
    entry.external = false;
}

static void _retainExternal(WgContext& context, WgTextureEntry& entry, WGPUTexture texture, FilterMethod filter, uint64_t serial)
{
    context.layouts.releaseBindGroup(entry.bindGroup);
    context.releaseTextureView(entry.textureView);
    if (entry.texture && !entry.external) context.releaseTexture(entry.texture);

    entry.texture = texture;
    entry.textureView = context.createTextureView(entry.texture);

    auto sampler = (filter == FilterMethod::Bilinear) ? context.samplerLinearClamp : context.samplerNearestClamp;
    entry.bindGroup = context.layouts.createBindGroupTexSampled(sampler, entry.textureView);
    entry.serial = serial;
    entry.external = true;
}

void WgTextureMgr::releaseEntry(WgContext& context, WgTextureEntry& entry)
{
    context.layouts.releaseBindGroup(entry.bindGroup);
    context.releaseTextureView(entry.textureView);
    if (!entry.external) context.releaseTexture(entry.texture);
    else entry.texture = nullptr;
    entry.refCnt = 0;
    entry.serial = 0;
    entry.external = false;
}

const WgTextureEntry* WgTextureMgr::retain(WgContext& context, const RenderSurface* surface, FilterMethod filter, bool refreshTexture)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) {
        surfaceEntry = new SurfaceEntry;
        surfaceEntry->surface = surface;
        surfaces.back(surfaceEntry);
    }

    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = entries.tail;
    auto nativeTexture = _nativeWgTexture(surface);
    if (nativeTexture) {
        if (entry && (!entry->external || entry->texture != nativeTexture) && entry->refCnt > 0) {
            entry = new WgTextureEntry;
            entries.back(entry);
        } else if (!entry) {
            entry = new WgTextureEntry;
            entries.back(entry);
        }
        if (!entry->texture || entry->texture != nativeTexture || !entry->external) _retainExternal(context, *entry, nativeTexture, filter, surface->serial);
        else entry->serial = surface->serial;
        ++entry->refCnt;
        return entry;
    } else if (entry && entry->external) {
        if (entry->refCnt > 0) {
            entry = new WgTextureEntry;
            entries.back(entry);
        } else {
            releaseEntry(context, *entry);
        }
    }

    auto serialChanged = entry && (entry->serial != surface->serial);
    if (entry && (refreshTexture || serialChanged) && entry->refCnt > 0) {
        entry = new WgTextureEntry;
        entries.back(entry);
    } else if (!entry) {
        entry = new WgTextureEntry;
        entries.back(entry);
    }
    if (!entry->texture || entry->serial != surface->serial) upload(context, *entry, surface, filter);

    ++entry->refCnt;
    return entry;
}

void WgTextureMgr::release(WgContext& context, const RenderSurface* surface, FilterMethod filter, WGPUTexture texture)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) return;

    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = _findEntry(entries, texture);
    if (!entry) return;

    if (entry->refCnt > 0) --entry->refCnt;
    if (entry->refCnt > 0) return;

    releaseEntry(context, *entry);
    entries.remove(entry);
    delete (entry);
    if (surfaceEntry->bilinear.empty() && surfaceEntry->nearest.empty()) {
        surfaces.remove(surfaceEntry);
        delete (surfaceEntry);
    }
}

void WgTextureMgr::clear(WgContext& context)
{
    while (auto* surfaceEntry = surfaces.front()) {
        while (auto* entry = surfaceEntry->bilinear.front()) {
            releaseEntry(context, *entry);
            delete (entry);
        }
        while (auto* entry = surfaceEntry->nearest.front()) {
            releaseEntry(context, *entry);
            delete (entry);
        }
        delete (surfaceEntry);
    }
    if (++stamp == 0) stamp = 1;
}
