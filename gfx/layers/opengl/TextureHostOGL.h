/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#ifndef MOZILLA_GFX_TEXTUREOGL_H
#define MOZILLA_GFX_TEXTUREOGL_H
 
#include "ImageLayerOGL.h"
#include "GLContextTypes.h"
#include "gfx2DGlue.h"
#include "mozilla/layers/Effects.h"
#include "gfxReusableSurfaceWrapper.h"
#include "TiledLayerBuffer.h" // for TILEDLAYERBUFFER_TILE_SIZE

#ifdef MOZ_WIDGET_GONK
#include <ui/GraphicBuffer.h>
#endif

namespace mozilla {
namespace layers {

/*
 * TextureHost implementations for the OpenGL backend.
 *
 * Note that it is important to becareful about the ownership model with
 * the OpenGL backend, due to some widget limitation on Linux: before
 * the nsBaseWidget associated with our OpenGL context has been completely 
 * deleted, every resource belonging to the OpenGL context MUST have been
 * released. At the moment the teardown sequence happens in the middle of 
 * the nsBaseWidget's destructor, meaning that at a given moment we must be
 * able to easily find and release all the GL resources.
 * The point is: be careful about the ownership model and limit the number 
 * of objects sharing references to GL resources to make the tear down 
 * sequence as simple as possible. 
 */

/**
 * TextureSourceOGL provides the necessary API for CompositorOGL to composite
 * a TextureSource.
 */
class TextureSourceOGL
{
public:
  virtual bool IsValid() const = 0;
  virtual void BindTexture(GLenum aTextureUnit) = 0;
  virtual gfx::IntSize GetSize() const = 0;
  virtual gl::ShaderProgramType GetShaderProgram() const {
    MOZ_NOT_REACHED("unhandled shader type");
  }
  virtual GLenum GetTextureTarget() const { return LOCAL_GL_TEXTURE_2D; }
  virtual GLenum GetWrapMode() const { return LOCAL_GL_REPEAT; }
};

inline gl::ShaderProgramType
GetProgramTypeForTexture(const TextureHost *aTextureHost)
{
  switch (aTextureHost->GetFormat()) {
  case gfx::FORMAT_B8G8R8A8:
    return gl::BGRALayerProgramType;;
  case gfx::FORMAT_B8G8R8X8:
    return gl::BGRXLayerProgramType;;
  case gfx::FORMAT_R8G8B8X8:
    return gl::RGBXLayerProgramType;;
  case gfx::FORMAT_R8G8B8A8:
    return gl::RGBALayerProgramType;;
  default:
    MOZ_NOT_REACHED("unhandled program type");
  }
}

/**
 * TextureHost implementation using a TextureImage as the underlying texture.
 */
class TextureImageTextureHostOGL : public TextureHost
                                 , public TextureSourceOGL
                                 , public TileIterator
{
public:
  TextureImageTextureHostOGL(gl::TextureImage* aTexImage = nullptr)
    : mTexture(aTexImage)
    , mGL(nullptr)
    , mIterating(false)
  {
    MOZ_COUNT_CTOR(TextureImageTextureHostOGL);
  }

  TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
  {
    return this;
  }

  ~TextureImageTextureHostOGL()
  {
    MOZ_COUNT_DTOR(TextureImageTextureHostOGL);
  }

  // This is a hack that is here to not break on-main-thread ThebesLayerBuffer
  // please do not use it anywhere else, use SetCompositor instead.
  void SetGLContext(gl::GLContext* aGL)
  {
    mGL = aGL;
  }

  // TextureHost

  void UpdateImpl(const SurfaceDescriptor& aImage,
                  nsIntRegion* aRegion = nullptr);

  virtual void SetCompositor(Compositor* aCompositor);

  bool IsValid() const MOZ_OVERRIDE
  {
    return !!mTexture;
  }

  virtual bool Lock() MOZ_OVERRIDE;

  void Abort() MOZ_OVERRIDE;

  // textureSource
  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE
  {
    mTexture->BindTexture(aTextureUnit);
  }

  gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    if (mTexture) {
      if (mIterating) {
        nsIntRect rect = mTexture->GetTileRect();
        return gfx::IntSize(rect.width, rect.height);
      }
      return gfx::IntSize(mTexture->GetSize().width, mTexture->GetSize().height);
    }
    return gfx::IntSize(0, 0);
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return GetProgramTypeForTexture(this);
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE
  {
    return mTexture->GetWrapMode();
  }

  gl::TextureImage* GetTextureImage()
  {
    return mTexture;
  }
  void SetTextureImage(gl::TextureImage* aImage)
  {
    mTexture = aImage;
  }

  // TileIterator

  TileIterator* AsTileIterator() MOZ_OVERRIDE
  {
    return this;
  }

  void BeginTileIteration() MOZ_OVERRIDE
  {
    mTexture->BeginTileIteration();
    mIterating = true;
  }

  void EndTileIteration() MOZ_OVERRIDE
  {
    mIterating = false;
  }

  nsIntRect GetTileRect() MOZ_OVERRIDE
  {
    return mTexture->GetTileRect();
  }
  
  size_t GetTileCount() MOZ_OVERRIDE
  {
    return mTexture->GetTileCount();
  }

  bool NextTile() MOZ_OVERRIDE
  {
    return mTexture->NextTile();
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TextureImageTextureHostOGL"; }
#endif

protected:
  nsRefPtr<gl::TextureImage> mTexture;
  gl::GLContext* mGL;
  bool mIterating;
};


/**
 * TextureHost implementation for YCbCr images in the OpenGL backend.
 *
 * This TextureHost is a little bit particular in that it implements
 * the TextureSource interface, as it is required that a TextureHost
 * provides access to a TextureSource, but does not implement the
 * TextureHostOGL interface. Instead it contains 3 channels (one per
 * plane) that implement the TextureSourceOGL interface, and 
 * YCbCrTextureHostOGL's TextureSource implementation provide access
 * to these channels with the GetSubSource method.
 */
class YCbCrTextureHostOGL : public TextureHost
{
public:
  YCbCrTextureHostOGL()
    : mGL(nullptr)
  {
    MOZ_COUNT_CTOR(YCbCrTextureHostOGL);
    mYTexture  = new Channel;
    mCbTexture = new Channel;
    mCrTexture = new Channel;
    mFormat = gfx::FORMAT_YUV;
  }

  ~YCbCrTextureHostOGL()
  {
    MOZ_COUNT_DTOR(YCbCrTextureHostOGL);
  }

  virtual void SetCompositor(Compositor* aCompositor);

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          nsIntRegion* aRegion = nullptr) MOZ_OVERRIDE;

  virtual bool Lock() MOZ_OVERRIDE;

  virtual bool IsValid() const MOZ_OVERRIDE
  {
    return mYTexture->IsValid()
        && mCbTexture->IsValid()
        && mCrTexture->IsValid();
  }

  struct Channel : public TextureSourceOGL
                 , public TextureSource
  {
    TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
    {
      return this;
    }
    nsRefPtr<gl::TextureImage> mTexImage;

    void BindTexture(GLenum aUnit) MOZ_OVERRIDE
    {
      mTexImage->BindTexture(aUnit);
    }
    virtual bool IsValid() const MOZ_OVERRIDE
    {
      return !!mTexImage;
    }
    virtual gfx::IntSize GetSize() const MOZ_OVERRIDE
    {
      return gfx::IntSize(mTexImage->GetSize().width, mTexImage->GetSize().height);
    }
    virtual GLenum GetWrapMode() const MOZ_OVERRIDE
    {
      return mTexImage->GetWrapMode();
    }

  };

  // TextureSource implementation

  TextureSource* GetSubSource(int index) MOZ_OVERRIDE
  {
    switch (index) {
      case 0 : return mYTexture.get();
      case 1 : return mCbTexture.get();
      case 2 : return mCrTexture.get();
    }
    return nullptr;
  }

  gfx::IntSize GetSize() const
  {
    if (!mYTexture->mTexImage) {
      NS_WARNING("YCbCrTextureHost::GetSize called but no data has been set yet");
      return gfx::IntSize(0,0);
    }
    return mYTexture->GetSize();
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "YCbCrTextureHostOGL"; }
#endif

private:
  RefPtr<Channel> mYTexture;
  RefPtr<Channel> mCbTexture;
  RefPtr<Channel> mCrTexture;
  gl::GLContext* mGL;
};

class SharedTextureHostOGL : public TextureHost
                           , public TextureSourceOGL
{
public:
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  SharedTextureHostOGL()
    : mGL(nullptr)
    , mTextureHandle(0)
    , mWrapMode(LOCAL_GL_CLAMP_TO_EDGE)
    , mSharedHandle(0)
    , mShareType(GLContext::SameProcess)
  {}

  virtual void SetCompositor(Compositor* aCompositor);

  virtual ~SharedTextureHostOGL()
  {
    mGL->MakeCurrent();
    if (mSharedHandle) {
      MOZ_ASSERT(mGL);
      mGL->ReleaseSharedHandle(mShareType, mSharedHandle);
    }
    if (mTextureHandle) {
      MOZ_ASSERT(mGL);
      mGL->fDeleteTextures(1, &mTextureHandle);
    }
  }

  virtual GLuint GetTextureHandle()
  {
    return mTextureHandle;
  }

  virtual TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE { return this; }

  bool IsValid() const MOZ_OVERRIDE { return GetFormat() != gfx::FORMAT_UNKNOWN; }

  // override from TextureHost, we support both buffered
  // and unbuffered operation.
  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          nsIntRegion* aRegion = nullptr);
  virtual void SwapTexturesImpl(const SurfaceDescriptor& aImage,
                                nsIntRegion* aRegion = nullptr);
  virtual bool Lock();
  virtual void Unlock();

  virtual GLenum GetWrapMode() const { return mWrapMode; }
  virtual void SetWrapMode(GLenum aMode) { mWrapMode = aMode; }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return mShaderProgram;
  }

  gfx::IntSize GetSize() const {
    return mSize;
  }

  virtual GLenum GetTextureTarget() const MOZ_OVERRIDE
  {
    return mTextureTarget;
  }

  void BindTexture(GLenum activetex)
  {
    MOZ_ASSERT(mGL);
    mGL->fActiveTexture(activetex);
    mGL->fBindTexture(mTextureTarget, mTextureHandle);
  }
  void ReleaseTexture() {}
  GLuint GetTextureID() { return mTextureHandle; }
  ContentType GetContentType()
  {
    return (mFormat == gfx::FORMAT_B8G8R8A8) ?
             gfxASurface::CONTENT_COLOR_ALPHA :
             gfxASurface::CONTENT_COLOR;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "SharedTextureHostOGL"; }
#endif

protected:
  gfx::IntSize mSize;
  nsRefPtr<gl::GLContext> mGL;
  GLuint mTextureHandle;
  GLenum mWrapMode;
  GLenum mTextureTarget;
  gl::SharedTextureHandle mSharedHandle;
  gl::ShaderProgramType mShaderProgram;
  gl::GLContext::SharedTextureShareType mShareType;
};

class SurfaceStreamHostOGL : public TextureHost
                           , public TextureSourceOGL
{
public:
  typedef gfxASurface::gfxContentType ContentType;
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::TextureImage TextureImage;

  virtual ~SurfaceStreamHostOGL()
  {
    if (mUploadTexture) {
      MOZ_ASSERT(mGL);
      mGL->MakeCurrent();
      mGL->fDeleteTextures(1, &mUploadTexture);
    }
    *mBuffer = SurfaceDescriptor();
  }

  virtual void SetCompositor(Compositor* aCompositor);

  virtual GLuint GetTextureHandle()
  {
    return mTextureHandle;
  }

  virtual TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE { return this; }

  bool IsValid() const MOZ_OVERRIDE { return true; }

  // override from TextureHost
  virtual void SwapTexturesImpl(const SurfaceDescriptor& aImage,
                                nsIntRegion* aRegion = nullptr);
  virtual bool Lock();
  virtual void Unlock();

  virtual GLenum GetWrapMode() const {
    return mWrapMode;
  }
  virtual void SetWrapMode(GLenum aMode) {
    mWrapMode = aMode;
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return mShaderProgram;
  }

  gfx::IntSize GetSize() const {
    return mSize;
  }

  virtual GLenum GetTextureTarget() const MOZ_OVERRIDE
  {
    return LOCAL_GL_TEXTURE_2D;
  }

  void BindTexture(GLenum activetex) {
    MOZ_ASSERT(mGL);
    mGL->fActiveTexture(activetex);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }
  void ReleaseTexture() {
  }
  GLuint GetTextureID() { return mTextureHandle; }
  ContentType GetContentType() {
    return (mFormat == gfx::FORMAT_B8G8R8A8) ?
             gfxASurface::CONTENT_COLOR_ALPHA :
             gfxASurface::CONTENT_COLOR;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "SurfaceStreamHostOGL"; }
#endif

  SurfaceStreamHostOGL()
    : mGL(nullptr)
    , mTextureHandle(0)
    , mUploadTexture(0)
    , mWrapMode(LOCAL_GL_CLAMP_TO_EDGE)
  {
  }

protected:

  gfx::IntSize mSize;
  nsRefPtr<gl::GLContext> mGL;
  GLuint mTextureHandle;
  GLuint mUploadTexture;
  GLenum mWrapMode;
  gl::ShaderProgramType mShaderProgram;
};

class TiledTextureHostOGL : public TextureHost
                          , public TextureSourceOGL
{
public:
  TiledTextureHostOGL()
    : mTextureHandle(0)
    , mGL(nullptr)
  {}
  ~TiledTextureHostOGL();

  virtual void SetCompositor(Compositor* aCompositor);

  // have to pass the size in here (every time) because of DrawQuad API :-(
  virtual void Update(gfxReusableSurfaceWrapper* aReusableSurface, TextureFlags aFlags, const gfx::IntSize& aSize) MOZ_OVERRIDE;
  virtual bool Lock() MOZ_OVERRIDE;
  virtual void Unlock() MOZ_OVERRIDE {}

  virtual TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE { return this; }
  virtual bool IsValid() const MOZ_OVERRIDE { return true; }
  virtual void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE
  {
    mGL->fActiveTexture(aTextureUnit);
    mGL->fBindTexture(LOCAL_GL_TEXTURE_2D, mTextureHandle);
  }
  virtual gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    return mSize;
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return GetProgramTypeForTexture(this);
  }

  virtual void SwapTexturesImpl(const SurfaceDescriptor& aImage,
                                nsIntRegion* aRegion = nullptr)
  { MOZ_ASSERT(false, "Tiles should not use this path"); }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "TiledTextureHostOGL"; }
#endif

protected:
  virtual uint64_t GetIdentifier() const MOZ_OVERRIDE {
    return static_cast<uint64_t>(mTextureHandle);
  }

private:
  GLenum GetTileType()
  {
    // Deduce the type that was assigned in GetFormatAndTileForImageFormat
    return mGLFormat == LOCAL_GL_RGB ? LOCAL_GL_UNSIGNED_SHORT_5_6_5 : LOCAL_GL_UNSIGNED_BYTE;
  }

  gfx::IntSize mSize;
  GLuint mTextureHandle;
  GLenum mGLFormat;
  gl::GLContext* mGL;
};

#ifdef MOZ_WIDGET_GONK

// For direct texturing with gralloc buffers. The corresponding TextureClient is TextureClientShmem,
// which automatically gets gralloc when it can, in which case the compositor sees that the
// SurfaceDescriptor is gralloc, and decides to use a GrallocTextureHostOGL to do direct texturing,
// saving the cost of a texture upload.
class GrallocTextureHostOGL
  : public TextureHost
  , public TextureSourceOGL
{
public:
  GrallocTextureHostOGL()
    : mGL(nullptr)
    , mGLTexture(0)
    , mEGLImage(0)
  {
  }

  ~GrallocTextureHostOGL();

  virtual void SetCompositor(Compositor* aCompositor) MOZ_OVERRIDE;

  virtual GLuint GetTextureHandle()
  {
    return mGLTexture;
  }

  virtual void UpdateImpl(const SurfaceDescriptor& aImage,
                          nsIntRegion* aRegion = nullptr) MOZ_OVERRIDE;
  virtual void SwapTexturesImpl(const SurfaceDescriptor& aImage,
                          nsIntRegion* aRegion = nullptr) MOZ_OVERRIDE;
  virtual bool Lock() MOZ_OVERRIDE;
  virtual void Unlock() MOZ_OVERRIDE;

  virtual gfx::IntSize GetSize() const MOZ_OVERRIDE
  {
    return gfx::IntSize(mGraphicBuffer->getWidth(), mGraphicBuffer->getHeight());;
  }

  gl::ShaderProgramType GetShaderProgram() const MOZ_OVERRIDE
  {
    return mFormat == gfx::FORMAT_B8G8R8A8 || mFormat == gfx::FORMAT_B8G8R8X8
           ? gl::BGRALayerProgramType
           : gl::RGBALayerProgramType;
  }

  GLenum GetWrapMode() const MOZ_OVERRIDE
  {
    return LOCAL_GL_CLAMP_TO_EDGE;
  }


  bool IsValid() const MOZ_OVERRIDE
  {
    return !!mGLTexture;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() { return "GrallocTextureHostOGL"; }
#endif

  void BindTexture(GLenum aTextureUnit) MOZ_OVERRIDE;

  virtual gfx::SurfaceFormat GetFormat() const;

  virtual TextureSourceOGL* AsSourceOGL() MOZ_OVERRIDE
  {
    return this;
  }

private:
  RefPtr<gl::GLContext> mGL;
  android::sp<android::GraphicBuffer> mGraphicBuffer;
  GLuint mGLTexture;
  EGLImage mEGLImage;
};
#endif

} // namespace
} // namespace
 
#endif /* MOZILLA_GFX_TEXTUREOGL_H */
