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

#include "tvgGlTextureMgr.h"

static GLuint _nativeGlTexture(const RenderSurface* surface)
{
    if (surface->nativeType != RenderSurfaceNativeType::GlTexture) return 0;
    if (surface->nativeTarget != GL_TEXTURE_2D) return 0;
    return static_cast<GLuint>(surface->nativeId);
}

static tvg::Inlist<TextureMgr::Entry>& _entries(TextureMgr::SurfaceEntry& surfaceEntry, FilterMethod filter)
{
    return (filter == FilterMethod::Bilinear) ? surfaceEntry.bilinear : surfaceEntry.nearest;
}

static TextureMgr::Entry* _findEntry(tvg::Inlist<TextureMgr::Entry>& entries, GLuint texId)
{
    INLIST_FOREACH(entries, entry)
    {
        if (entry->texId == texId) return entry;
    }
    return nullptr;
}

TextureMgr::SurfaceEntry* TextureMgr::find(const RenderSurface* surface)
{
    INLIST_FOREACH(surfaces, entry)
    {
        if (entry->surface == surface) return entry;
    }
    return nullptr;
}

void TextureMgr::upload(GLuint texId, const RenderSurface* surface, FilterMethod filter)
{
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, texId));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->data));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (filter == FilterMethod::Bilinear) ? GL_LINEAR : GL_NEAREST));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (filter == FilterMethod::Bilinear) ? GL_LINEAR : GL_NEAREST));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
}

GLuint TextureMgr::retain(const RenderSurface* surface, FilterMethod filter)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) {
        surfaceEntry = new SurfaceEntry;
        surfaceEntry->surface = surface;
        surfaces.back(surfaceEntry);
    }
    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = entries.tail;
    auto nativeTexId = _nativeGlTexture(surface);

    if (nativeTexId) {
        if (entry && (!entry->external || entry->texId != nativeTexId) && entry->refCnt > 0) {
            entry = new Entry;
            entries.back(entry);
        } else if (!entry) {
            entry = new Entry;
            entries.back(entry);
        }

        entry->texId = nativeTexId;
        entry->serial = surface->serial;
        entry->external = true;
        ++entry->refCnt;
        return entry->texId;
    } else if (entry && entry->external) {
        if (entry->refCnt > 0) {
            entry = new Entry;
            entries.back(entry);
        } else {
            entry->texId = 0;
            entry->serial = 0;
            entry->external = false;
        }
    } else if (!entry) {
        entry = new Entry;
        entries.back(entry);
    }

    if (entry->texId) {
        if (entry->serial != surface->serial) upload(entry->texId, surface, filter);
    } else {
        GL_CHECK(glGenTextures(1, &entry->texId));
        upload(entry->texId, surface, filter);
    }

    entry->serial = surface->serial;
    entry->external = false;
    ++entry->refCnt;
    return entry->texId;
}

bool TextureMgr::update(const RenderSurface* surface, FilterMethod filter, GLuint texId)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) return false;
    auto* entry = _findEntry(_entries(*surfaceEntry, filter), texId);
    if (!entry) return false;
    auto nativeTexId = _nativeGlTexture(surface);
    if (nativeTexId) {
        if (!entry->external || entry->texId != nativeTexId) return false;
        entry->serial = surface->serial;
        return true;
    }
    if (entry->external) return false;
    if (entry->serial == surface->serial) return true;

    upload(entry->texId, surface, filter);
    entry->serial = surface->serial;
    return true;
}


GLuint TextureMgr::release(const RenderSurface* surface, FilterMethod filter, GLuint texId)
{
    auto* surfaceEntry = find(surface);
    if (!surfaceEntry) return 0;
    auto& entries = _entries(*surfaceEntry, filter);
    auto* entry = _findEntry(entries, texId);
    if (!entry) return 0;

    if (entry->refCnt > 0) --entry->refCnt;
    if (entry->refCnt > 0) return 0;

    texId = entry->external ? 0 : entry->texId;
    entries.remove(entry);
    delete(entry);

    if (surfaceEntry->bilinear.empty() && surfaceEntry->nearest.empty()) {
        surfaces.remove(surfaceEntry);
        delete (surfaceEntry);
    }

    return texId;
}

void TextureMgr::clear()
{
    Array<GLuint> textures;
    textures.reserve(textures.count + surfaces.count * 2);
    INLIST_FOREACH(surfaces, surfaceEntry)
    {
        INLIST_FOREACH(surfaceEntry->bilinear, entry)
        {
            if (entry->texId && !entry->external) textures.push(entry->texId);
        }
        INLIST_FOREACH(surfaceEntry->nearest, entry)
        {
            if (entry->texId && !entry->external) textures.push(entry->texId);
        }
    }
    surfaces.free();
    if (++stamp == 0) stamp = 1;  // avoid zero stamp, which is used to indicate stale cache.
    if (!textures.empty()) GL_CHECK(glDeleteTextures(textures.count, textures.data));
}
