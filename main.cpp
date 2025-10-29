#include <iostream>
#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu-raii.hpp"
#include <cassert>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

using namespace wgpu;

class App{
public:
    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();

private:
    raii::Instance instance;
    raii::Device device;
    GLFWwindow* window;
    raii::Surface surface;
    raii::Queue queue;
    raii::RenderPipeline pipeline;
    TextureFormat surfaceFormat = TextureFormat::Undefined;

    void InitializePipeline();
    std::pair<SurfaceTexture, raii::TextureView> GetNextSurfaceViewData();
    void InitializeTexture();
};

auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
        std::cout << "Uncaptured device error: type " << type;
        if (message) std::cout << " (" << message << ")";
        std::cout << std::endl;
};

bool App::Initialize() {
    // instance
    InstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
    instance = createInstance(desc);
    if (!instance) {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return false;
    }
    // adapter
    RequestAdapterOptions options = {};
    raii::Adapter adapter = instance->requestAdapter(options);

    // device
    DeviceDescriptor devDesc = {};
    devDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
    std::cout << "Device lost: reason " << reason;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
    };
    device = adapter->requestDevice(devDesc);
    wgpuDeviceSetUncapturedErrorCallback(*device, onDeviceError, nullptr /* pUserData */);
    
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
    std::cerr << "Could not initialize GLFW!" << std::endl;
        return false;
    }
    window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);
    if (!window) {
    std::cerr << "Could not open window!" << std::endl;
    glfwTerminate();
    return 1;
    }
    // surface
    *surface = glfwGetWGPUSurface(*instance, window);
    SurfaceConfiguration config = {};
    config.nextInChain = nullptr;
    config.width = 640;
    config.height = 480;
    surfaceFormat = surface->getPreferredFormat(*adapter);
    config.format = surfaceFormat;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.device = *device;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    surface->configure(config);
    // queue
    queue = device->getQueue();
    InitializePipeline();
    return true;
}
void App::Terminate(){
    glfwDestroyWindow(window);
    glfwTerminate();
}
void App::MainLoop(){
    glfwPollEvents();
    auto [ surfaceTexture, targetView ] = GetNextSurfaceViewData();
    if (!targetView) return;
    RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain = nullptr;
    // describe render pass
    RenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view = *targetView;
    renderPassColorAttachment.resolveTarget = nullptr;
    renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
    renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue = Color{ 0.6, 0.4, 1.0, 1.0 };
    renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;
    // encoder
    CommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain = nullptr;
    encoderDesc.label = "My command encoder";
    CommandEncoder encoder = device->createCommandEncoder(encoderDesc);
    // render pass
    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    renderPass.setPipeline(*pipeline);
    renderPass.draw(3, 1, 0, 0);
    renderPass.end();
    CommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label = "Command buffer";
    CommandBuffer command = encoder.finish(cmdBufferDescriptor);
    // std::cout << "Submitting command..." << std::endl;
    queue->submit(1, &command);
    // std::cout << "Command submitted." << std::endl;

    renderPass.release();
    surface->present();
    targetView->release();
    wgpuTextureRelease(surfaceTexture.texture);
    command.release();
    encoder.release();
    instance->processEvents();
}
bool App::IsRunning(){
    return !glfwWindowShouldClose(window);
} 
std::pair<SurfaceTexture, raii::TextureView> App::GetNextSurfaceViewData() {
    // next texture
    SurfaceTexture surfaceTexture;
    surface->getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        return { surfaceTexture, raii::TextureView() };
    }
    // surface texture view
    TextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = "Surface texture view";
    raii::Texture tex = Texture(surfaceTexture.texture);
    viewDescriptor.format = tex->getFormat();
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    raii::TextureView targetView = tex->createView(viewDescriptor);
    return { surfaceTexture, targetView };
}
void App::InitializePipeline(){
    const char* shaderSource = R"(
    @vertex
    fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }
    @fragment
    fn fs_main() -> @location(0) vec4f {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }
    )";
    // create shader module
    ShaderModuleDescriptor shaderDesc;
    ShaderModuleWGSLDescriptor shaderCodeDesc;
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = shaderSource;
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    ShaderModule shaderModule = device->createShaderModule(shaderDesc);

    // pipeline
    RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.label = "Pipeline";
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers = nullptr;
    // vertex shader
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants = nullptr;
    pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace = FrontFace::CCW;
    pipelineDesc.primitive.cullMode = CullMode::None;
    // fragment shader
    FragmentState fragmentState;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;
    // blending
    BlendState blendState;
    blendState.color.srcFactor = BlendFactor::SrcAlpha;
    blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
    blendState.color.operation = BlendOperation::Add;
    blendState.alpha.srcFactor = BlendFactor::Zero;
    blendState.alpha.dstFactor = BlendFactor::One;
    blendState.alpha.operation = BlendOperation::Add;
    ColorTargetState colorTarget;
    colorTarget.format = surfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = ColorWriteMask::All;
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.depthStencil = nullptr;
    // multisampling
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    pipelineDesc.layout = nullptr;

    pipeline = device->createRenderPipeline(pipelineDesc);
}
void App::InitializeTexture(){

}

int main (int, char**) {
    App app;
    if (!app.Initialize()){
        return 1;
    }
    while (app.IsRunning()) {
        app.MainLoop();
    }
    app.Terminate();
    return 0;
}