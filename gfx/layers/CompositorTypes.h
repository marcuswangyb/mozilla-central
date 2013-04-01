/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_COMPOSITORTYPES_H
#define MOZILLA_LAYERS_COMPOSITORTYPES_H

namespace mozilla {
namespace layers {

typedef int32_t SurfaceDescriptorType;
static const int32_t SURFACEDESCRIPTOR_UNKNOWN = 0;

// flags used by texture clients and texture hosts
typedef uint32_t TextureFlags;
const TextureFlags NoFlags            = 0x0;
// Use nearest-neighbour texture filtering (as opposed to linear filtering).
const TextureFlags UseNearestFilter   = 0x1;
// The texture should be flipped around the y-axis when composited.
const TextureFlags NeedsYFlip         = 0x2;
// Force the texture to be represented using a single tile (note that this means
// tiled textures, not tiled layers).
const TextureFlags ForceSingleTile    = 0x4;
// Allow using 'repeat' mode for wrapping.
const TextureFlags AllowRepeat        = 0x8;
// The texture represents a tile which is newly created.
const TextureFlags NewTile            = 0x10;
// The host is responsible for tidying up any shared resources.
const TextureFlags HostRelease        = 0x20;

/**
 * The kind of memory held by the texture client/host pair. This will
 * determine how the texture client is drawn into and how the memory
 * is shared between client and host.
 */
enum TextureClientType
{
  TEXTURE_CONTENT,            // dynamically drawn content
  TEXTURE_SHMEM,              // shared memory
  TEXTURE_YCBCR,              // ShmemYCbCrImage
  TEXTURE_SHARED_GL,          // GLContext::SharedTextureHandle
  TEXTURE_SHARED_GL_EXTERNAL, // GLContext::SharedTextureHandle, the ownership of
                              // the SurfaceDescriptor passed to the texture
                              // remains with whoever passed it.
  TEXTURE_STREAM_GL           // WebGL streaming buffer
};

/**
 * How the Compositable should manage textures.
 */
enum CompositableType
{
  BUFFER_UNKNOWN,
  BUFFER_IMAGE_SINGLE,    // image/canvas with a single texture, single buffered
  BUFFER_IMAGE_BUFFERED,  // image/canvas, double buffered
  BUFFER_BRIDGE,          // image bridge protocol
  BUFFER_CONTENT,         // thebes layer interface, single buffering
  BUFFER_CONTENT_DIRECT,  // thebes layer interface, double buffering
  BUFFER_TILED,           // tiled thebes layer
  BUFFER_COUNT
};

/**
 * How the texture host is used for composition,
 */
enum TextureHostFlags
{
  TEXTURE_HOST_DEFAULT = 0,       // The default texture host for the given
                                  // SurfaceDescriptor
  TEXTURE_HOST_TILED = 1 << 0,    // A texture host that supports tiling
  TEXTURE_HOST_DIRECT = 1 << 1    // Direct texturing
};

/**
 * Sent from the compositor to the content-side LayerManager, includes properties
 * of the compositor and should (in the future) include information about what
 * kinds of buffer and texture clients to create.
 */
struct TextureFactoryIdentifier
{
  LayersBackend mParentBackend;
  int32_t mMaxTextureSize;
};

/**
 * Information required by the compositor from the content-side for creating or
 * using compositables and textures.
 */
struct TextureInfo
{
  CompositableType mCompositableType;
  uint32_t mTextureHostFlags;
  uint32_t mTextureFlags;

  TextureInfo()
    : mCompositableType(BUFFER_UNKNOWN)
    , mTextureHostFlags(0)
    , mTextureFlags(0)
  {}
};


} // namespace
} // namespace

#endif
