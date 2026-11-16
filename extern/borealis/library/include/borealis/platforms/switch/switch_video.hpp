/*
Copyright 2020 Adubbz
Copyright 2020-2021 Luis Scheurenbrand
Copyright 2021 natinusala

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#ifdef __SWITCH2__

#include <EGL/egl.h>
#include <switch.h>

#include <borealis/core/video.hpp>
#include <functional>

typedef Event _LibNXEvent; // "Event" alone clashes with brls::Event

namespace brls
{

// Switch 2 EGL + OpenGL ES 3.0 (OunceVK) video context
class SwitchVideoContext : public VideoContext
{
  public:
    SwitchVideoContext();
    ~SwitchVideoContext() override;

    void clear(NVGcolor color) override;
    void resetState() override;
    void beginFrame() override;
    void endFrame() override;
    void setSwapInterval(int interval) override;
    double getScaleFactor() override;
    NVGcontext* getNVGContext() override;

    void appletCallback(AppletHookType hookType);

    void captureNextFrame(std::function<void()> callback) { frameCapture = std::move(callback); }

    uint32_t getFramebufferWidth() const { return static_cast<uint32_t>(framebufferWidth); }
    uint32_t getFramebufferHeight() const { return static_cast<uint32_t>(framebufferHeight); }

  private:
    std::function<void()> frameCapture;
    _LibNXEvent defaultDisplayResolutionChangeEvent {};
    bool displayResolutionChangeEventReady = true;

    void resetFramebuffer();
    void updateWindowSize();

    float framebufferWidth = 1280.0f;
    float framebufferHeight = 720.0f;

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    NVGcontext* nvgContext = nullptr;
};

} // namespace brls

#else // !__SWITCH2__

#include <nanovg/framework/CMemPool.h>
#include <switch.h>

#include <borealis/core/video.hpp>
#include <deko3d.hpp>
#include <nanovg/dk_renderer.hpp>
#include <optional>
#include <functional>

typedef Event _LibNXEvent; // "Event" alone clashes with brls::Event

namespace brls
{

static constexpr const unsigned FRAMEBUFFERS_COUNT = 3;

// deko3d video context
class SwitchVideoContext : public VideoContext
{
  public:
    SwitchVideoContext();
    ~SwitchVideoContext() override;

    void clear(NVGcolor color) override;
    void resetState() override;
    void beginFrame() override;
    void endFrame() override;
    void setSwapInterval(int interval) override;
    double getScaleFactor() override;
    NVGcontext* getNVGContext() override;

    void appletCallback(AppletHookType hookType);

    void recordStaticCommands();

    dk::Device getDeko3dDevice();

    dk::Queue getQueue();

    dk::Image* getFramebuffer();

    // One UI-thread callback after rendering, while the acquired image is ours.
    void captureNextFrame(std::function<void()> callback) { frameCapture = std::move(callback); }

    int allocateImageIndex();
    bool freeImageIndex(int index);
    bool updateImageDescriptor(dk::CmdBuf cmdbuf, int index, dk::ImageDescriptor const& descriptor);
    void invalidateImageDescriptors(dk::CmdBuf cmdbuf);

    void queueWaitFence(DkFence* fence);

    void queueSignalFence(DkFence* fence, bool flash = false);

    void queueFlush();

  private:
    std::function<void()> frameCapture;
    _LibNXEvent defaultDisplayResolutionChangeEvent;
    bool displayResolutionChangeEventReady = true;

    void resetFramebuffer(); // triggered by either display resolution change event or operation mode change event
    void updateWindowSize();
    void createFramebufferResources();

    void destroyFramebufferResources();

    float framebufferWidth, framebufferHeight;
    int imageSlot;

    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    dk::UniqueCmdBuf cmdbuf;
    dk::UniqueSwapchain swapchain;

    dk::Image depthBuffer;
    CMemPool::Handle depthBufferHandle;

    dk::Image framebuffers[FRAMEBUFFERS_COUNT];
    CMemPool::Handle framebuffersHandles[FRAMEBUFFERS_COUNT];

    DkCmdList framebuffersCmdLists[FRAMEBUFFERS_COUNT];
    DkCmdList renderCmdlist;

    std::optional<CMemPool> imagesPool;
    std::optional<CMemPool> codePool;
    std::optional<CMemPool> dataPool;

    std::optional<nvg::DkRenderer> renderer;
    NVGcontext* nvgContext;
};

} // namespace brls

#endif // !__SWITCH2__
