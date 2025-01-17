/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dom_plugins_PluginSurfaceParent_h
#define dom_plugins_PluginSurfaceParent_h

#include "mozilla/plugins/PPluginSurfaceParent.h"
#include "mozilla/plugins/PluginMessageUtils.h"

#if !defined(XP_WIN) && !defined(XP_OS2)
#error "This header is for Windows only."
#endif

class gfxASurface;

namespace mozilla {
namespace plugins {

class PluginSurfaceParent : public PPluginSurfaceParent
{
public:
  PluginSurfaceParent(const WindowsSharedMemoryHandle& handle,
                      const gfx::IntSize& size,
                      const bool transparent);
  ~PluginSurfaceParent();

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  gfxASurface* Surface() { return mSurface; }

private:
  RefPtr<gfxASurface> mSurface;
};

} // namespace plugins
} // namespace mozilla

#endif // dom_plugin_PluginSurfaceParent_h
