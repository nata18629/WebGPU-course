#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu-raii.hpp"
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace wgpu;

struct VertexData {
    std::array<float,2> position;
    std::array<float,4> color;
};

ShaderModule loadShaderModule(const std::filesystem::path& path, Device device) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return nullptr;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string shaderSource(size, ' ');
    file.seekg(0);
    file.read(shaderSource.data(), size);

    ShaderModuleWGSLDescriptor shaderCodeDesc{};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = shaderSource.c_str();

    ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    return device.createShaderModule(shaderDesc);
}

class Renderer{
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
    raii::Buffer vertexBuffer, indexBuffer, uniformBuffer;
    std::vector<VertexData> vertexData;
    std::vector<uint32_t> indexData;
    uint32_t vertexCount, indexCount;
    raii::BindGroupLayout bindGroupLayout;
    raii::BindGroup bindGroup;
    raii::PipelineLayout layout;

    void InitializeBuffers();
    void InitializeUniforms();
    void InitializeBindings();
    void InitializePipeline();
    std::pair<SurfaceTexture, raii::TextureView> GetNextSurfaceViewData();
};

auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
    std::cout << "Uncaptured device error: type " << type;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
};

bool Renderer::Initialize() {
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
    
    #ifdef __linux__
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    #endif
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
    SurfaceCapabilities capabilities;
    surface->getCapabilities(*adapter, &capabilities);
    surfaceFormat = capabilities.formats[0];
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
    InitializeBuffers();
    // InitializeUniforms();
    // InitializeBindings();
    InitializePipeline();
    return true;
}
void Renderer::Terminate(){
    glfwDestroyWindow(window);
    glfwTerminate();
}
void Renderer::MainLoop(){
    glfwPollEvents();
    float t = static_cast<float>(glfwGetTime());
    queue->writeBuffer(*uniformBuffer, 0, &t, sizeof(float));

    SurfaceTexture surfaceTexture;
    surface->getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        printf("Could not get surface texture.");
        return;
    }
    // surface texture view
    TextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = "Surface texture view";
    Texture tex = Texture(surfaceTexture.texture);
    viewDescriptor.format = tex.getFormat();
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    //TextureView targetView = tex->createView(viewDescriptor);
    raii::TextureView targetView = tex.createView(viewDescriptor);
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
    //renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
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
    raii::RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    renderPass->setPipeline(*pipeline);
    renderPass->setBindGroup(0, *bindGroup, 0, nullptr);
    renderPass->setVertexBuffer(0, *vertexBuffer, 0, vertexBuffer->getSize());
    renderPass->setIndexBuffer(*indexBuffer, IndexFormat::Uint32, 0, indexBuffer->getSize());
    renderPass->drawIndexed(indexCount, 1, 0, 0, 0);
    renderPass->end();
    CommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain = nullptr;
    cmdBufferDescriptor.label = "Command buffer";
    CommandBuffer command = encoder.finish(cmdBufferDescriptor);
    queue->submit(1, &command);
    surface->present();
}
bool Renderer::IsRunning(){
    return !glfwWindowShouldClose(window);
}
void Renderer::InitializeBuffers() {
    vertexData = {
        {position:{{+0.8, +0.7}},     color:{{+0.3, +0.1, +0.1, +1.0}}},
        {position:{{+0.4, +0.7}},     color:{{+0.5, +0.1, +0.3, +1.0}}}, 
        {position:{{-0.4, -0.3}},     color:{{+0.5, +0.1, +0.1, +1.0}}}, 

        {position:{{+0.8, +0.7}},     color:{{+0.3, +0.1, +0.1, +1.0}}}, 
        {position:{{+0.8, -0.3}},     color:{{+0.0, +0.1, +0.1, +1.0}}}, 
        {position:{{-0.4, -0.3}},     color:{{+0.5, +0.1, +0.1, +1.0}}}, 

        {position:{{-0.1, +0.7}},     color:{{+0.3, +0.1, +0.1, +1.0}}},
    };
    vertexCount = static_cast<int>(vertexData.size());

    BufferDescriptor bufferDesc;
    bufferDesc.label = "vertex data";
    bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    bufferDesc.size = vertexData.size()*sizeof(VertexData);
    bufferDesc.mappedAtCreation = false;
    vertexBuffer = device->createBuffer(bufferDesc);
    queue->writeBuffer(*vertexBuffer, 0, vertexData.data(), bufferDesc.size);

    indexData = {
        0, 1, 2,
        3, 4, 5,
        0, 6, 2,
    };

    indexCount = static_cast<int>(indexData.size());

    bufferDesc.label = "index data";
    bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
    bufferDesc.size = indexData.size() * sizeof(uint32_t);
    bufferDesc.mappedAtCreation = false;
    indexBuffer = device->createBuffer(bufferDesc);
    queue->writeBuffer(*indexBuffer, 0, indexData.data(), bufferDesc.size);
}
void Renderer::InitializeUniforms() {
    BufferDescriptor bufferDesc;
    bufferDesc.label = "uniform data";
    bufferDesc.size = 4*sizeof(float);
    bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    bufferDesc.mappedAtCreation = false;
    uniformBuffer = device->createBuffer(bufferDesc);
    
    float t = static_cast<float>(glfwGetTime());
    queue->writeBuffer(*uniformBuffer, 0, &t, sizeof(float));
}
void Renderer::InitializeBindings() {
    // The uniform time binding
    BindGroupLayoutEntry bindGroupLayoutEntry = Default;
    bindGroupLayoutEntry.binding = 0;
    bindGroupLayoutEntry.visibility = ShaderStage::Vertex;
    bindGroupLayoutEntry.buffer.type = BufferBindingType::Uniform;
    bindGroupLayoutEntry.buffer.minBindingSize = 4*sizeof(float);

    BindGroupLayoutDescriptor bindGroupLayoutDesc{};
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries = &bindGroupLayoutEntry;
    bindGroupLayout = device->createBindGroupLayout(bindGroupLayoutDesc);

    BindGroupEntry binding;
    binding.binding = 0;
    binding.buffer = *uniformBuffer;
    binding.offset = 0;
    binding.size = 4*sizeof(float);
    
    BindGroupDescriptor bindGroupDesc;
    bindGroupDesc.layout = *bindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &binding;
    bindGroup = device->createBindGroup(bindGroupDesc);
}
void Renderer::InitializePipeline(){
    // create shader module
    ShaderModule shaderModule = loadShaderModule("shaders.wgsl", *device);
    if (shaderModule == nullptr) {
        std::cerr << "Could not load shader!" << std::endl;
        exit(1);
    }
    // vertex buffer layout
    VertexBufferLayout vertexBufferLayout;
    std::vector<VertexAttribute> vertexAttribs(2);
    // position
    vertexAttribs[0].shaderLocation = 0;
    vertexAttribs[0].offset = 0;
    vertexAttribs[0].format = VertexFormat::Float32x2;
    // colors
    vertexAttribs[1].shaderLocation = 1;
    vertexAttribs[1].offset = 2 * sizeof(float);
    vertexAttribs[1].format = VertexFormat::Float32x4;
    vertexBufferLayout.attributeCount = vertexAttribs.size();
    vertexBufferLayout.attributes = vertexAttribs.data();
    vertexBufferLayout.arrayStride = 6 * sizeof(float);
    vertexBufferLayout.stepMode = VertexStepMode::Vertex;
    // pipeline
    RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.label = "Pipeline";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
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

    PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&(*bindGroupLayout);
    layout = device->createPipelineLayout(layoutDesc);

    pipelineDesc.layout = *layout;


    pipeline = device->createRenderPipeline(pipelineDesc);
}

int main (int, char**) {
    Renderer Renderer;
    if (!Renderer.Initialize()){
        return 1;
    }
    while (Renderer.IsRunning()) {
        Renderer.MainLoop();
    }
    Renderer.Terminate();
    return 0;
}
