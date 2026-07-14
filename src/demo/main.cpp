#include "tinygltf/tiny_gltf.h"


#include <glm/fwd.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include "GLFW/glfw3.h"
#include <iostream>
#include <optional>
#include <set>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <random>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <foundation/camera.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer/src/meshoptimizer.h>
#include <vulkan/vulkan_core.h>
#include "tracy/TracyVulkan.hpp"

#define MAX_SHADOW_CASCADES 4
#define SHADOW_MAP_SIZE 2048
#define COMMAND_BUFFER_COUNT 2
#define GBUFFER_ATTACHMENT_COUNT 10
#define BISTRO_TEXTURE_COUNT 255
#define MESH_COUNT 2
#define MAX_TEXTURE_COUNT 1024
#define MAX_SETS 1
#define PERSISTENT 0
#define PER_FRAME 1
#define DRAGON_TEXTURES_COUNT 2
#define SSAO_KERNEL_SIZE  32
#define SSAO_NOISE_DIM 4
#define MAX_POINT_LIGHTS      1
#define MAX_DIRECTIONAL_LIGHT 1

VkFormat swapchainFormat[1] = {VK_FORMAT_B8G8R8A8_SRGB};
TracyVkCtx tracyGpuContext;
std::set<std::string> requiredExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME,
    VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME};

const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_COLOR_WRITE_ENABLE_EXTENSION_NAME,
    VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME

};
VkDeviceAddress vertexBufferAddress;

VkPhysicalDeviceColorWriteEnableFeaturesEXT colorWriteFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT,
        .colorWriteEnable = VK_TRUE};

VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
    .extendedDynamicState3ColorBlendEnable = VK_TRUE};

VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeature = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
    .bufferDeviceAddress = VK_TRUE};

VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeature = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
    .shaderSampledImageArrayNonUniformIndexing = VK_TRUE};

VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
    .pNext = &eds3,
    .dynamicRendering = VK_TRUE,

};

std::vector<glm::vec4> ssaoKernels;

VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputFeature = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
    .pNext = &dynamicRenderingFeature,
    .vertexInputDynamicState = VK_TRUE

};

Camera camera;

float gCameraYaw = -90.0f;
float gCameraPitch = 0.0f;
float gCameraMoveSpeed = 8.0f;
float gCameraTurnSpeed = 80.0f;

VkSemaphore frameSemaphores[COMMAND_BUFFER_COUNT];
VkFence commandBufferFences[COMMAND_BUFFER_COUNT];

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

std::vector<const char *> extensions =
    {VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};

struct DragonVkBuffers
{
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexMemory;
    uint32_t indexCount;
};

VkPhysicalDeviceFeatures2 deviceFeatures2;

struct alignas(16) ShadowCascadeData
{
    alignas(16) glm::mat4 viewProj = glm::mat4(1.0f);
    alignas(4) float splitDepth = 0.0f;
    alignas(4) float padding[3] = {};
};

struct alignas(16) ShadowGenerationUniform
{
    alignas(16) ShadowCascadeData cascades[MAX_SHADOW_CASCADES];
    alignas(16) glm::vec4 lightDirection = glm::vec4(0.0f);
    alignas(16) glm::uvec4 shadowMapSizeCascadeCount = glm::uvec4(0);
    alignas(16) glm::vec4 bias = glm::vec4(0.0f);
};

static_assert(sizeof(ShadowCascadeData) % 16 == 0);
static_assert(sizeof(ShadowGenerationUniform) % 16 == 0);

struct SkyLightUniform
{
    alignas(16) glm::vec3 lightDirection;
    alignas(16) glm::vec3 lightColor;
    alignas(4) float lightIntensity;
};

SkyLightUniform SkyLightUniformData;
ShadowGenerationUniform ShadowGenUniformData;

constexpr uint32_t PERSISTENT_BINDING_COUNT = 12;
constexpr uint32_t PER_FRAME_BINDING_COUNT = 3;
constexpr uint32_t FRAMES_IN_FLIGHT = 2;

std::string dragon_textures_pathes[2] = {"../external/dragon/checkerboard.png", "../external/dragon/Dragon_ThicknessMap.jpg"};

#define VK_CHECK(call)                                                                    \
    do                                                                                    \
    {                                                                                     \
        VkResult result = (call);                                                         \
        if (result != VK_SUCCESS)                                                         \
        {                                                                                 \
            throw std::runtime_error(std::string("Vulkan error in ") + #call +            \
                                     " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        }                                                                                 \
    } while (0)

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct Uniform
{
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::vec3 cameraPos;
    float nearPlane;
    float farPlane;
};

Uniform UniformData;

struct Shader
{
    VkShaderModule vert;
    VkShaderModule geom;
    VkShaderModule frag;
    VkShaderModule comp;
};

struct AppSettings
{
    uint32_t height;
    uint32_t width;
};

AppSettings *gAppSettings;

struct RenderTargetDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t arraySize = 1;
    uint32_t mipLevels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

    VkClearValue clearValue = {};

    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    const char *pName = nullptr;

    VkImageLayout startLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct samplerDesc
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float mipLodBias = 0.0f;
    VkBool32 anisotropyEnable = VK_FALSE;
    float maxAnisotropy = 1.0f;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
    float minLod = 0.0f;
    float maxLod = VK_LOD_CLAMP_NONE;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkBool32 unnormalizedCoordinates = VK_FALSE;
};

struct Sampler
{
    VkDescriptorImageInfo descInfo;
    VkSampler sampler;
};

struct Buffer
{
    VkDescriptorBufferInfo descInfo;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
};

struct bufferDesc
{
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
};

struct TriangleFilteringPushConstants
{
    VkDeviceAddress indexBuffer;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress filteredBuffers[4];
    VkDeviceAddress drawCommand;
    alignas(16) glm::mat4 modelViewProjection;
    uint32_t vertexStride;
    uint32_t indexCount;
    uint32_t vertexCount;
};

struct PointLightPushConstants
{
    alignas(16) glm::vec4 positionRadius;
    alignas(16) glm::vec4 colorIntensity;
};
struct DirectionalLightPushConstants
{
    alignas(16) glm::vec4 directionIntensity;
    alignas(16) glm::vec4 color;
};

// Compose push constants for light calculation.
struct ComposePushConstants {

    DirectionalLightPushConstants directionalLight[MAX_DIRECTIONAL_LIGHT];
    //int active_directionalLight = 0;

    PointLightPushConstants pointLight[MAX_POINT_LIGHTS];
    //int active_pointlight       = 0;

};

struct LightsData_buffer{

    DirectionalLightPushConstants directionalLight[MAX_DIRECTIONAL_LIGHT];
    //int active_directionalLight = 0;

    PointLightPushConstants pointLight[MAX_POINT_LIGHTS];
    //int active_pointlight       = 0;

};

DirectionalLightPushConstants directionalLight_buffer;
PointLightPushConstants       pointLight_buffer;

struct textureDesc
{
    int width = 0;
    int height = 0;
    int channels = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t mipLevels = 1;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
};

struct RenderTarget
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageView sampledView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t arraySize = 1;
    uint32_t mipLevels = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

    VkClearValue clearValue = {};

    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const char *pName = nullptr;

    VkSampler sampler;
    VkDescriptorImageInfo imageInfo;
};
 
struct ShadowRenderTargets
{
    RenderTarget *cascadeDepth = nullptr;
    VkImageView cascadeLayerViews[MAX_SHADOW_CASCADES] = {};
};

ShadowRenderTargets gShadowRenderTargets;



// Resource Buffers
Buffer *shadowGenBuffer[FRAMES_IN_FLIGHT];
Buffer *SkyLightsDataBuffer[FRAMES_IN_FLIGHT];
Buffer *uboBuffer[FRAMES_IN_FLIGHT];
Buffer *ssaoKernelBuffer;

Sampler *textureSampler;
Sampler *renderTargetSampler;

GLFWwindow *window = NULL;
VkInstance instance;
VkDebugUtilsMessengerEXT debugMessenger;
VkSurfaceKHR surface;
VkPhysicalDevice physicalDevice;
VkDevice device;
VkQueue graphicsQueue;
VkQueue presentQueue;
QueueFamilyIndices indecies;
VkCommandPool graphicsCommandPool = NULL;
VkDescriptorSetLayout perFrameLayout;
VkCommandBuffer commandBuffers[COMMAND_BUFFER_COUNT];
VkDescriptorSetLayout persistentLayout;
VkDescriptorPool descriptorPool;

VkDescriptorSet setsPerFrame[2];
VkDescriptorSet setsPersistent;

std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
VkSemaphore renderFinishedSemaphores[FRAMES_IN_FLIGHT];


// Triangle Filtering

VkPipeline gTFilteringPipeline;
Shader            *gTFilteringShaders = nullptr;
VkPipelineLayout  pipelineLayout;
Buffer*           gTFilteredTriangles;
Buffer*           gTFilterIndirectArgs;
// GBuffer
VkPipeline gBufferPipeline;
VkRenderingInfo gBufferRenderInfo;
Shader *gBufferShader = nullptr;
RenderTarget *gBufferNormalSpecularRT;
RenderTarget *gBufferAlbedoRT;
RenderTarget *gDepthBufferRT;
// SSAO

Shader *ssaoShader = nullptr;
Shader *ssaoBlurShader = nullptr;

RenderTarget *ssaoBlurRT;
RenderTarget *ssaoRT;

Shader *composeShader = nullptr;

VkPipeline ssaoPipeline;
VkPipeline ssaoBlurPipeline;
VkPipeline composePipeline;

VkSwapchainKHR swapchain;

std::vector<glm::vec4> ssaoNoise;

glm::mat4 IdentityMatrix = glm::mat4(1.0f);

glm::vec3 target = glm::vec3(0.0f, -0.7306479811668396f, 0.0f);
glm::vec3 eye = target + glm::vec3(0.0f, 0.5f, 2.5f);
glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

glm::mat4 view = glm::lookAt(eye, target, up);

// compute

Shader *shaderComputerFiltering = nullptr;

class VulkanglTFModel
{
public:
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec3 color;
        uint32_t textureIndex;
    };

    // The following structures roughly represent the glTF scene structure
    // To keep things simple, they only contain those properties that are required for this sample
    struct Node;

    // A primitive contains the data for a single draw call
    struct Primitive
    {
        uint32_t firstIndex;
        uint32_t indexCount;
        int32_t materialIndex;
    };

    uint32_t vertextotalCount;
    uint32_t indextotalCount; 
    // Contains the node's (optional) geometry and can be made up of an arbitrary number of primitives
    struct Mesh
    {
        std::vector<Primitive> primitives;
    };

    // A node represents an object in the glTF scene graph
    struct Node
    {
        Node *parent;
        std::vector<Node *> children;
        Mesh mesh;
        glm::mat4 matrix;
        ~Node()
        {
            for (auto &child : children)
            {
                delete child;
            }
        }
    };

    // A glTF material stores information in e.g. the texture that is attached to it ane colors
    struct Material
    {
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        uint32_t baseColorTextureIndex = 0;
    };

    struct Texture2D
    {
        // teture
        VkImage image;
        VkImageView view;
        VkSampler sampler;
        VkDeviceMemory deviceMemory;

        VkDescriptorImageInfo descInfo;
        uint32_t width;
        uint32_t height;
        uint32_t mipLevels;
        VkFormat format;
        // usage  images[i].texture.fromBuffer(buffer, bufferSize, VK_FORMAT_R8G8B8A8_UNORM, glTFImage.width, glTFImage.height, , copyQueue);

        uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
            for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
                if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                    return i;
            throw std::runtime_error("Failed to find suitable memory type");
        }

        void fromBuffer(unsigned char *buffer,
                        VkDeviceSize size,
                        VkFormat format,
                        uint32_t width,
                        uint32_t height,
                        VkDevice logicalDevice,
                        VkPhysicalDevice physicalDevice,
                        VkQueue copyQueue,
                        VkCommandPool commandPool)
        {
            this->width = width;
            this->height = height;
            this->mipLevels = 1;
            this->format = format;

            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;

            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &stagingBuffer);

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &stagingMemory);
            vkBindBufferMemory(logicalDevice, stagingBuffer, stagingMemory, 0);

            void *mapped;
            vkMapMemory(logicalDevice, stagingMemory, 0, size, 0, &mapped);
            memcpy(mapped, buffer, static_cast<size_t>(size));
            vkUnmapMemory(logicalDevice, stagingMemory);

            VkImageCreateInfo imageDesc{};
            imageDesc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageDesc.imageType = VK_IMAGE_TYPE_2D;
            imageDesc.format = format;
            imageDesc.extent = {width, height, 1};
            imageDesc.mipLevels = 1;
            imageDesc.arrayLayers = 1;
            imageDesc.samples = VK_SAMPLE_COUNT_1_BIT;
            imageDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageDesc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageDesc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(logicalDevice, &imageDesc, nullptr, &image);
            
            VkMemoryRequirements imgMemReqs;
            vkGetImageMemoryRequirements(logicalDevice, image, &imgMemReqs);

            VkMemoryAllocateInfo imgAllocInfo{};
            imgAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            imgAllocInfo.allocationSize = imgMemReqs.size;
            imgAllocInfo.memoryTypeIndex = findMemoryType(physicalDevice, imgMemReqs.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(logicalDevice, &imgAllocInfo, nullptr, &deviceMemory);
            vkBindImageMemory(logicalDevice, image, deviceMemory, 0);

            VkCommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAllocInfo.commandPool = commandPool;
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuffer;
            vkAllocateCommandBuffers(logicalDevice, &cmdAllocInfo, &cmdBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmdBuffer, &beginInfo);

            VkImageMemoryBarrier barrierToTransfer{};
            barrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToTransfer.image = image;
            barrierToTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrierToTransfer.srcAccessMask = 0;
            barrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

            VkBufferImageCopy2 buffer2image_desc{};
            buffer2image_desc.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
            buffer2image_desc.bufferOffset = 0;
            buffer2image_desc.bufferRowLength = 0;
            buffer2image_desc.bufferImageHeight = 0;
            buffer2image_desc.imageExtent = {width, height, 1};
            buffer2image_desc.imageOffset = {0, 0, 0};
            buffer2image_desc.imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1};

            VkCopyBufferToImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
            copyInfo.srcBuffer = stagingBuffer;
            copyInfo.dstImage = image;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &buffer2image_desc;

            vkCmdCopyBufferToImage2(cmdBuffer, &copyInfo);

            VkImageMemoryBarrier barrierToShader{};
            barrierToShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrierToShader.image = image;
            barrierToShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrierToShader);

            vkEndCommandBuffer(cmdBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmdBuffer;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence;
            vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence);

            vkQueueSubmit(copyQueue, 1, &submitInfo, fence);
            vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, UINT64_MAX);

            vkDestroyFence(logicalDevice, fence, nullptr);
            vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cmdBuffer);
            vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
            vkFreeMemory(logicalDevice, stagingMemory, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            VK_CHECK(vkCreateImageView(logicalDevice, &viewInfo, nullptr, &view));

            descInfo.imageView = view;
            descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descInfo.sampler = VK_NULL_HANDLE;
        }
    };

    struct Image
    {
        Texture2D texture;
        // We also store (and create) a descriptor set that's used to access this texture from the fragment shader
        VkDescriptorSet descriptorSet;
    };

    // A glTF texture stores a reference to the image and a sampler
    // In this sample, we are only interested in the image
    struct Texture
    {
        int32_t imageIndex;
    };

    /*
        Model data
    */
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Node *> nodes;
    Buffer* vertexBufferVk;
    Buffer* indexBufferVk; 
    
    ~VulkanglTFModel()
    {
        for (auto node : nodes)
        {
            delete node;
        }
        // Release all Vulkan resources allocated for the model
        vkDestroyBuffer(device, vertexBufferVk->buffer, nullptr);
        vkDestroyBuffer(device, indexBufferVk->buffer, nullptr);
        vkFreeMemory(device, vertexBufferVk->memory, nullptr);
        vkFreeMemory(device, indexBufferVk->memory, nullptr);
        
        for (Image image : images)
        {
            vkDestroyImageView(device, image.texture.view, nullptr);
            vkDestroyImage(device, image.texture.image, nullptr);
            vkDestroySampler(device, image.texture.sampler, nullptr);
            vkFreeMemory(device, image.texture.deviceMemory, nullptr);
        }
    }

    void loadImages(tinygltf::Model &input)
    {
        // Images can be stored inside the glTF (which is the case for the sample model), so instead of directly
        // loading them from disk, we fetch them from the glTF loader and upload the buffers
        images.resize(input.images.size());
        for (size_t i = 0; i < input.images.size(); i++)
        {
            tinygltf::Image &glTFImage = input.images[i];
            // Get the image data from the glTF loader
            unsigned char *buffer = nullptr;
            VkDeviceSize bufferSize = 0;
            bool deleteBuffer = false;
            // We convert RGB-only images to RGBA, as most devices don't support RGB-formats in Vulkan
            if (glTFImage.component == 3)
            {
                bufferSize = glTFImage.width * glTFImage.height * 4;
                buffer = new unsigned char[bufferSize];
                unsigned char *rgba = buffer;
                unsigned char *rgb = &glTFImage.image[0];
                for (size_t i = 0; i < glTFImage.width * glTFImage.height; ++i)
                {
                    memcpy(rgba, rgb, sizeof(unsigned char) * 3);
                    rgba += 4;
                    rgb += 3;
                }
                deleteBuffer = true;
            }
            else
            {
                buffer = &glTFImage.image[0];
                bufferSize = glTFImage.image.size();
            }
            // Load texture from image buffer
            images[i].texture.fromBuffer(buffer, bufferSize, VK_FORMAT_R8G8B8A8_UNORM, glTFImage.width, glTFImage.height, device, physicalDevice, graphicsQueue, graphicsCommandPool);

            if (deleteBuffer)
            {
                delete[] buffer;
            }
        }
    }

    void loadTextures(tinygltf::Model &input)
    {
        textures.resize(input.textures.size());
        for (size_t i = 0; i < input.textures.size(); i++)
        {
            textures[i].imageIndex = input.textures[i].source;
        }
    }

    void loadMaterials(tinygltf::Model &input)
    {
        materials.resize(input.materials.size());
        for (size_t i = 0; i < input.materials.size(); i++)
        {
            // We only read the most basic properties required for our sample
            tinygltf::Material glTFMaterial = input.materials[i];
            // Get the base color factor
            if (glTFMaterial.values.find("baseColorFactor") != glTFMaterial.values.end())
            {
                materials[i].baseColorFactor = glm::make_vec4(glTFMaterial.values["baseColorFactor"].ColorFactor().data());
            }
            // Get base color texture index
            if (glTFMaterial.values.find("baseColorTexture") != glTFMaterial.values.end())
            {
                materials[i].baseColorTextureIndex = glTFMaterial.values["baseColorTexture"].TextureIndex();
            }
        }
    }

    void loadNode(const tinygltf::Node &inputNode, const tinygltf::Model &input, VulkanglTFModel::Node *parent, std::vector<uint32_t> &indexBuffer, std::vector<VulkanglTFModel::Vertex> &vertexBuffer)
    {
        VulkanglTFModel::Node *node = new VulkanglTFModel::Node{};
        node->matrix = glm::mat4(1.0f);
        node->parent = parent;

        if (inputNode.translation.size() == 3)
        {
            node->matrix = glm::translate(node->matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
        }
        if (inputNode.rotation.size() == 4)
        {
            glm::quat q = glm::make_quat(inputNode.rotation.data());
            node->matrix *= glm::mat4(q);
        }
        if (inputNode.scale.size() == 3)
        {
            node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
        }
        if (inputNode.matrix.size() == 16)
        {
            node->matrix = glm::make_mat4x4(inputNode.matrix.data());
        };

        if (inputNode.children.size() > 0)
        {
            for (size_t i = 0; i < inputNode.children.size(); i++)
            {
                loadNode(input.nodes[inputNode.children[i]], input, node, indexBuffer, vertexBuffer);
            }
        }

        if (inputNode.mesh > -1)
        {
            const tinygltf::Mesh mesh = input.meshes[inputNode.mesh];
            // Iterate through all primitives of this node's mesh
            for (size_t i = 0; i < mesh.primitives.size(); i++)
            {
                const tinygltf::Primitive &glTFPrimitive = mesh.primitives[i];
                uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
                uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
                uint32_t indexCount = 0;
                uint32_t textureIndex = 0;
                if (glTFPrimitive.material >= 0 &&
                    static_cast<size_t>(glTFPrimitive.material) < materials.size())
                {
                    uint32_t gltfTextureIndex = materials[glTFPrimitive.material].baseColorTextureIndex;
                    if (gltfTextureIndex < textures.size() && textures[gltfTextureIndex].imageIndex >= 0)
                    {
                        textureIndex = static_cast<uint32_t>(textures[gltfTextureIndex].imageIndex);
                    }
                }
                // Vertices
                {
                    const float *positionBuffer = nullptr;
                    const float *normalsBuffer = nullptr;
                    const float *texCoordsBuffer = nullptr;
                    size_t vertexCount = 0;

                    // Get buffer data for vertex positions
                    if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end())
                    {
                        const tinygltf::Accessor &accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
                        const tinygltf::BufferView &view = input.bufferViews[accessor.bufferView];
                        positionBuffer = reinterpret_cast<const float *>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
                        vertexCount = accessor.count;
                    }
                    // Get buffer data for vertex normals
                    if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end())
                    {
                        const tinygltf::Accessor &accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
                        const tinygltf::BufferView &view = input.bufferViews[accessor.bufferView];
                        normalsBuffer = reinterpret_cast<const float *>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
                    }
                    // Get buffer data for vertex texture coordinates
                    // glTF supports multiple sets, we only load the first one
                    if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end())
                    {
                        const tinygltf::Accessor &accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
                        const tinygltf::BufferView &view = input.bufferViews[accessor.bufferView];
                        texCoordsBuffer = reinterpret_cast<const float *>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
                    }

                    // Append data to model's vertex buffer
                    for (size_t v = 0; v < vertexCount; v++)
                    {
                        Vertex vert{};
                        vert.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f);
                        vert.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
                        vert.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
                        vert.color = glm::vec3(1.0f);
                        vert.textureIndex = textureIndex;
                        vertexBuffer.push_back(vert);
                    }
                }
                // Indices
                {
                    const tinygltf::Accessor &accessor = input.accessors[glTFPrimitive.indices];
                    const tinygltf::BufferView &bufferView = input.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer &buffer = input.buffers[bufferView.buffer];

                    indexCount += static_cast<uint32_t>(accessor.count);

                    // glTF supports different component types of indices
                    switch (accessor.componentType)
                    {
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT:
                    {
                        const uint32_t *buf = reinterpret_cast<const uint32_t *>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                        for (size_t index = 0; index < accessor.count; index++)
                        {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT:
                    {
                        const uint16_t *buf = reinterpret_cast<const uint16_t *>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                        for (size_t index = 0; index < accessor.count; index++)
                        {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE:
                    {
                        const uint8_t *buf = reinterpret_cast<const uint8_t *>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
                        for (size_t index = 0; index < accessor.count; index++)
                        {
                            indexBuffer.push_back(buf[index] + vertexStart);
                        }
                        break;
                    }
                    default:
                        std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
                        return;
                    }
                }
                Primitive primitive{};
                primitive.firstIndex = firstIndex;
                primitive.indexCount = indexCount;
                primitive.materialIndex = glTFPrimitive.material;
                node->mesh.primitives.push_back(primitive);
            }
        }

        if (parent)
        {
            parent->children.push_back(node);
        }
        else
        {
            nodes.push_back(node);
        }
    }
};

VulkanglTFModel::Texture2D *DRAGON_TEXTURES[DRAGON_TEXTURES_COUNT];
VulkanglTFModel::Texture2D *ssaoNoiseTexture;
VulkanglTFModel model_gltf;

class vkApp
{
public:
    PFN_vkCmdSetColorWriteEnableEXT pfnCmdSetColorWriteEnableEXT = nullptr;
    PFN_vkCmdSetColorBlendEnableEXT pfnCmdSetColorBlendEnableEXT = nullptr;
    PFN_vkCmdSetVertexInputEXT pfnCmdSetVertexInputEXT = nullptr;

    void LoadExtensionFunctions()
    {
        pfnCmdSetColorWriteEnableEXT = (PFN_vkCmdSetColorWriteEnableEXT)
            vkGetDeviceProcAddr(device, "vkCmdSetColorWriteEnableEXT");
        pfnCmdSetColorBlendEnableEXT = (PFN_vkCmdSetColorBlendEnableEXT)
            vkGetDeviceProcAddr(device, "vkCmdSetColorBlendEnableEXT");
        pfnCmdSetVertexInputEXT = (PFN_vkCmdSetVertexInputEXT)
            vkGetDeviceProcAddr(device, "vkCmdSetVertexInputEXT");

        assert(pfnCmdSetColorWriteEnableEXT && "vkCmdSetColorWriteEnableEXT not found");
        assert(pfnCmdSetColorBlendEnableEXT && "vkCmdSetColorBlendEnableEXT not found");
        assert(pfnCmdSetVertexInputEXT && "vkCmdSetVertexInputEXT not found");
    }

    VkPhysicalDeviceDescriptorIndexingFeaturesEXT DescriptorIndexingFeatures = { 
     .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    };

    VkPhysicalDeviceDynamicRenderingFeaturesKHR DynamicRenderingFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &DescriptorIndexingFeatures,
        .dynamicRendering = VK_TRUE
    };
    
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        assert(false && "Failed to find suitable memory type!");
        return 0;
    }

    bool IsDepthFormat(VkFormat format)
    {
        return format == VK_FORMAT_D16_UNORM ||
                format == VK_FORMAT_D32_SFLOAT ||
                format == VK_FORMAT_D16_UNORM_S8_UINT ||
                format == VK_FORMAT_D24_UNORM_S8_UINT ||
                format 
                == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    void addSemaphore(VkSemaphore &sem)
    {
        VkSemaphoreCreateInfo semDesc{};
        semDesc.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkResult result = vkCreateSemaphore(device, &semDesc, nullptr, &sem);
        assert(result == VK_SUCCESS);
    }

    void addFence(VkFence &fence)
    {
        VkFenceCreateInfo fenceDesc{};
        fenceDesc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceDesc.flags = VkFenceCreateFlagBits::VK_FENCE_CREATE_SIGNALED_BIT;
        VkResult result = vkCreateFence(device, &fenceDesc, nullptr, &fence);
        assert(result == VK_SUCCESS);
    }

    void addBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                   const bufferDesc &desc, Buffer **ppBuffer)
    {
        assert(ppBuffer);
        assert(desc.size > 0);

        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = desc.size;
        info.usage = desc.usage;
        info.sharingMode = desc.sharingMode;

        Buffer *pBuffer = new Buffer();
        pBuffer->size = desc.size;

        VK_CHECK(vkCreateBuffer(device, &info, nullptr, &pBuffer->buffer));

        // Allocate memory
        VkMemoryRequirements memReqs = {};
        vkGetBufferMemoryRequirements(device, pBuffer->buffer, &memReqs);

        VkPhysicalDeviceMemoryProperties memProps = {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & desc.memoryProperties) == desc.memoryProperties)
            {
                memTypeIndex = i;
                break;
            }
        }
        assert(memTypeIndex != UINT32_MAX && "No suitable memory type found");

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memTypeIndex;

        VkMemoryAllocateFlagsInfo allocFlagsInfo = {};
        if (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            allocInfo.pNext = &allocFlagsInfo;
        }

        VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &pBuffer->memory));
        VK_CHECK(vkBindBufferMemory(device, pBuffer->buffer, pBuffer->memory, 0));

        pBuffer->descInfo.buffer = pBuffer->buffer;
        pBuffer->descInfo.offset = 0;
        pBuffer->descInfo.range = desc.size;

        *ppBuffer = pBuffer;
    }

    void removeBuffer(VkDevice device, Buffer *pBuffer)
    {
        assert(pBuffer);
        vkDestroyBuffer(device, pBuffer->buffer, nullptr);
        vkFreeMemory(device, pBuffer->memory, nullptr);
        delete pBuffer;
    }
    void addImage(VkDevice device, VkPhysicalDevice physicalDevice,
                    const textureDesc &desc, VkImage image)
    {
        assert(desc.width > 0 && desc.height > 0 && "Wrong image size paramteres");
        VkImageCreateInfo imageDesc;
        
        // Image
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = desc.format;
        imageInfo.extent = {(uint32_t)desc.width, (uint32_t)desc.height, 1};
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = desc.usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &image));
    }
    
    void addTexture(VkDevice device, VkPhysicalDevice physicalDevice,
                        const textureDesc &desc, VulkanglTFModel::Texture2D **ppTexture)
    {
        assert(ppTexture && "Given an initialized texture");
        
        assert(desc.width > 0 && desc.height > 0);

        VulkanglTFModel::Texture2D *pTexture = new VulkanglTFModel::Texture2D();
        pTexture->width = desc.width;
        pTexture->height = desc.height;
        pTexture->mipLevels = desc.mipLevels;
        pTexture->format = desc.format;
        
        // Image
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = desc.format;
        imageInfo.extent = {(uint32_t)desc.width, (uint32_t)desc.height, 1};
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = desc.usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &pTexture->image));

        // Memory
        VkMemoryRequirements memReqs = {};
        vkGetImageMemoryRequirements(device, pTexture->image, &memReqs);

        VkPhysicalDeviceMemoryProperties memProps = {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memTypeIndex = i;
                break;
            }
        }
        assert(memTypeIndex != UINT32_MAX);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &pTexture->deviceMemory));
        VK_CHECK(vkBindImageMemory(device, pTexture->image, pTexture->deviceMemory, 0));

        // Image view
        VkImageViewCreateInfo viewInfo = {};

        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if (desc.format == VK_FORMAT_D32_SFLOAT ||
            desc.format == VK_FORMAT_D16_UNORM)
        {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else if (desc.format == VK_FORMAT_D24_UNORM_S8_UINT ||
                 desc.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT; 
                                                
        }
        viewInfo.subresourceRange.aspectMask = aspect;

        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pTexture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = desc.format;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &pTexture->view));

        // descInfo ready to plug into VkWriteDescriptorSet
        pTexture->descInfo.imageView = pTexture->view;
        pTexture->descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pTexture->descInfo.sampler = VK_NULL_HANDLE; // set externally from your sampler

        *ppTexture = pTexture;
    }


    static VkDeviceSize formatByteSize(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
            return 2;
        default:
            assert(false && "formatByteSize: unknown format");
            return 0;
        }
    }

    void mapTextureData(VkDevice device, VkPhysicalDevice physicalDevice,
                        VkCommandPool cmdPool, VkQueue queue,
                        VulkanglTFModel::Texture2D *pTexture, const void *pData, VkDeviceSize dataSize)
    {
        assert(pTexture);
        assert(pData);

        VkDeviceSize expectedSize = (VkDeviceSize)pTexture->width * pTexture->height * formatByteSize(pTexture->format); 
        if (dataSize != expectedSize)
        {
            fprintf(stderr, "mapTextureData: size mismatch! "
                            "passed %llu bytes but texture expects %llu bytes "
                            "(%ux%u fmt=%d)\n",
                    (unsigned long long)dataSize,
                    (unsigned long long)expectedSize,
                    pTexture->width, pTexture->height, (int)pTexture->format);
            assert(false && "mapTextureData: dataSize does not match texture dimensions");
        }

        // Staging buffer
        Buffer *pStaging = nullptr;
        bufferDesc stagingDesc;
        stagingDesc.size = dataSize;
        stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingDesc.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        addBuffer(device, physicalDevice, stagingDesc, &pStaging);

        void *pMapped = nullptr;
        VK_CHECK(vkMapMemory(device, pStaging->memory, 0, dataSize, 0, &pMapped));
        memcpy(pMapped, pData, (size_t)dataSize);
        vkUnmapMemory(device, pStaging->memory);

        // One-shot command buffer
        VkCommandBufferAllocateInfo cmdAlloc = {};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = cmdPool;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device, &cmdAlloc, &cmd));

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        // Transition UNDEFINED → TRANSFER_DST
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = pTexture->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = pTexture->mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer → image
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {pTexture->width, pTexture->height, 1};
        vkCmdCopyBufferToImage(cmd, pStaging->buffer, pTexture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition TRANSFER_DST → SHADER_READ_ONLY
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(queue));

        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
        removeBuffer(device, pStaging);
    }

    void removeTexture(VkDevice device, VulkanglTFModel::Texture2D *pTexture)
    {
        assert(pTexture);
        vkDestroyImageView(device, pTexture->view, nullptr);
        vkDestroyImage(device, pTexture->image, nullptr);
        vkFreeMemory(device, pTexture->deviceMemory, nullptr);
        delete pTexture;
    }

    void updateSkyLight(uint32_t frameIndex)
    {
        UpdateShadowGenerationData(hasShadowRenderTargets());

        if (shadowGenBuffer[frameIndex])
        {
            mapBuffer(device, shadowGenBuffer[frameIndex], &ShadowGenUniformData);
        }

        if (SkyLightsDataBuffer[frameIndex])
        {
            mapBuffer(device, SkyLightsDataBuffer[frameIndex], &SkyLightUniformData);
        }
    }

    void setupUBO()
    {
        camera.setPosition( glm::vec3(0.0f, 2.0f, 10.0f));
        camera.setDirection(glm::vec3(0.0f, 0.0f, -1.0f));
        camera.setWorldUp(  glm::vec3(0.0f, 1.0f, 0.0f));
        camera.changeLens(  45.0f, 0.1f, 4000.0f);
        
        camera.updateAspectRatio((float)gAppSettings->width / (float)gAppSettings->height);

        // Update matrices
        camera.updateProjMatrix();
        camera.updateViewMatrix();

        // Fill uniform data
        UniformData.cameraPos = camera.pos;
        UniformData.nearPlane = 0.1f;
        UniformData.farPlane = 4000.0f;
        UniformData.model = IdentityMatrix;
        UniformData.proj = camera.getProjMatrix();
        UniformData.view = camera.getViewMatrix();
    }

    float lerp(float a, float b, float f)
    {
        return a + f * (b - a);
    }

    std::vector<glm::vec4> generateSSAOKernel(uint32_t kernelSize, bool deterministicSeed = false)
    {
        std::default_random_engine rndEngine(deterministicSeed ? 0 : (unsigned)time(nullptr));
        std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

        std::vector<glm::vec4> ssaoKernel(kernelSize);
        for (uint32_t i = 0; i < kernelSize; ++i)
        {
            glm::vec3 sample(
                rndDist(rndEngine) * 2.0f - 1.0f,
                rndDist(rndEngine) * 2.0f - 1.0f,
                rndDist(rndEngine));
            sample = glm::normalize(sample);
            sample *= rndDist(rndEngine);

            float scale = float(i) / float(kernelSize);
            scale = lerp(0.1f, 1.0f, scale * scale);

            ssaoKernel[i] = glm::vec4(sample * scale, 0.0f);
        }
        return ssaoKernel;
    }

    void addBuffers()
    {
        
        for(uint32_t curr_buffer = 0; curr_buffer < FRAMES_IN_FLIGHT; ++curr_buffer) 
        {
            {
                setupUBO();

                bufferDesc bufferDescUBO        = {};
                bufferDescUBO.size = sizeof(Uniform);
                bufferDescUBO.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;        
                bufferDescUBO.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                bufferDescUBO.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                addBuffer(device, physicalDevice, bufferDescUBO, &uboBuffer[curr_buffer]);

                mapBuffer(device, uboBuffer[curr_buffer], &UniformData);
            }
        
            {
        
                bufferDesc bufferDescShadowGen = {};
                bufferDescShadowGen.size = sizeof(ShadowGenerationUniform);
                bufferDescShadowGen.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                bufferDescShadowGen.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                bufferDescShadowGen.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                addBuffer(device, physicalDevice, bufferDescShadowGen, &shadowGenBuffer[curr_buffer]);

                mapBuffer(device, shadowGenBuffer[curr_buffer], &ShadowGenUniformData);
            }

            {

                bufferDesc bufferDescLightsData = {};

                bufferDescLightsData.size = sizeof(SkyLightUniform);
                bufferDescLightsData.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                bufferDescLightsData.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                bufferDescLightsData.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                addBuffer(device, physicalDevice, bufferDescLightsData, &SkyLightsDataBuffer[curr_buffer]);

                mapBuffer(device, SkyLightsDataBuffer[curr_buffer], &SkyLightUniformData);
            }
            
        }
        
        {
            ssaoKernels = generateSSAOKernel(SSAO_KERNEL_SIZE, true);

            bufferDesc bufferSSAOKernelsDesc = {};
            bufferSSAOKernelsDesc.size = sizeof(glm::vec4) * ssaoKernels.size();
            bufferSSAOKernelsDesc.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
            bufferSSAOKernelsDesc.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferSSAOKernelsDesc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            addBuffer(device, physicalDevice, bufferSSAOKernelsDesc, &ssaoKernelBuffer);

            mapBuffer(device, ssaoKernelBuffer, ssaoKernels.data());
        }
    }
    
    void addSampler(VkDevice device, const samplerDesc &desc, Sampler **ppSampler)
    {
        assert(ppSampler);

        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = desc.magFilter;
        info.minFilter = desc.minFilter;
        info.mipmapMode = desc.mipmapMode;
        info.addressModeU = desc.addressModeU;
        info.addressModeV = desc.addressModeV;
        info.addressModeW = desc.addressModeW;
        info.mipLodBias = desc.mipLodBias;
        info.anisotropyEnable = desc.anisotropyEnable;
        info.maxAnisotropy = desc.maxAnisotropy;
        info.compareEnable = desc.compareEnable;
        info.compareOp = desc.compareOp;
        info.minLod = desc.minLod;
        info.maxLod = desc.maxLod;
        info.borderColor = desc.borderColor;
        info.unnormalizedCoordinates = desc.unnormalizedCoordinates;

        Sampler *pSampler = new Sampler();
        VK_CHECK(vkCreateSampler(device, &info, nullptr, &pSampler->sampler));

        pSampler->descInfo.sampler = pSampler->sampler;
        pSampler->descInfo.imageView = VK_NULL_HANDLE;
        pSampler->descInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        *ppSampler = pSampler;
    }

    void removeSampler(VkDevice device, Sampler *pSampler)
    {
        assert(pSampler);
        vkDestroySampler(device, pSampler->sampler, nullptr);
        delete pSampler;
    }

    void addSwapChain()
    {

        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);

        uint32_t imageCount = surfaceCapabilities.minImageCount;
        if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount)
        {
            imageCount = surfaceCapabilities.maxImageCount;
        }

        VkExtent2D extent;
        if (surfaceCapabilities.currentExtent.width != UINT32_MAX)
        {
            extent = surfaceCapabilities.currentExtent;
        }
        else
        {
            extent = {gAppSettings->width, gAppSettings->height};
            extent.width = std::clamp(extent.width,
                                        surfaceCapabilities.minImageExtent.width,
                                        surfaceCapabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height,
                                        surfaceCapabilities.minImageExtent.height,
                                        surfaceCapabilities.maxImageExtent.height);
        }

        VkSwapchainCreateInfoKHR swapchainDesc{};
        swapchainDesc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainDesc.pNext = nullptr;
        swapchainDesc.flags = 0;
        swapchainDesc.surface = surface;
        swapchainDesc.minImageCount = imageCount;
        swapchainDesc.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
        swapchainDesc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapchainDesc.imageExtent = extent;
        swapchainDesc.imageArrayLayers = 1;
        swapchainDesc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = {indecies.graphicsFamily.value(), indecies.presentFamily.value()};
        if (queueFamilyIndices[0] != queueFamilyIndices[1])
        {
            swapchainDesc.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainDesc.queueFamilyIndexCount = 2;
            swapchainDesc.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            swapchainDesc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainDesc.queueFamilyIndexCount = 0;
            swapchainDesc.pQueueFamilyIndices = nullptr;
        }

        swapchainDesc.preTransform = surfaceCapabilities.currentTransform;
        swapchainDesc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainDesc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainDesc.clipped = VK_TRUE;
        swapchainDesc.oldSwapchain = VK_NULL_HANDLE;

        VK_CHECK(vkCreateSwapchainKHR(device, &swapchainDesc, nullptr, &swapchain));
        
        {
            uint32_t count = 0;
            vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
            swapchainImages.resize(count);
            vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());

            swapchainImageViews.resize(count);
            for (uint32_t i = 0; i < count; i++)
            {
                VkImageViewCreateInfo viewInfo = {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = swapchainImages[i];
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;
                VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[i]));
            }

            for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
                addSemaphore(renderFinishedSemaphores[i]);
        }
    
    }


    void addTextures()
    {
        textureDesc TextureDesc;
        
        for(uint32_t current_texture = 0; current_texture < DRAGON_TEXTURES_COUNT; current_texture++)
        {
            unsigned char *data = nullptr;
            if (!load_stb_image(&data, dragon_textures_pathes[current_texture], TextureDesc.width, TextureDesc.height, TextureDesc.channels))
            {
                throw std::runtime_error("Failed to load texture: " + dragon_textures_pathes[current_texture]);
            }

            TextureDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
            TextureDesc.mipLevels = 1;
            TextureDesc.usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            addTexture(device, physicalDevice, TextureDesc, &DRAGON_TEXTURES[current_texture]);
            VkDeviceSize dataSize = TextureDesc.width * TextureDesc.height * 4;

            mapTextureData(device, physicalDevice, graphicsCommandPool, graphicsQueue, DRAGON_TEXTURES[current_texture], data, dataSize);
            stbi_image_free(data);
   
        } 
  
        {
            ssaoNoise = generateSSAONoise(SSAO_NOISE_DIM);
            
            textureDesc ssaoNoiseDesc;
            ssaoNoiseDesc.width = SSAO_NOISE_DIM;
            ssaoNoiseDesc.height = SSAO_NOISE_DIM;
            ssaoNoiseDesc.channels = 4;
            ssaoNoiseDesc.format = VK_FORMAT_R32G32B32A32_SFLOAT; // full float precision for RG rotation vectors
            ssaoNoiseDesc.mipLevels = 1;                          // no mipmaps, noise is tiled not filtered
            ssaoNoiseDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            addTexture(device, physicalDevice, ssaoNoiseDesc, &ssaoNoiseTexture);
            
            VkDeviceSize dataSize = (VkDeviceSize)ssaoNoiseDesc.width * ssaoNoiseDesc.height * formatByteSize(ssaoNoiseDesc.format);

            mapTextureData(device, physicalDevice, graphicsCommandPool, graphicsQueue, ssaoNoiseTexture, ssaoNoise.data(), dataSize);

        }
    }

    void addDescriptorSets()
    {
        /*
            typedef struct VkDescriptorSetLayoutCreateInfo {
                VkStructureType                        sType;
                const void*                            pNext;
                VkDescriptorSetLayoutCreateFlags       flags;
                uint32_t                               bindingCount;
                const VkDescriptorSetLayoutBinding*    pBindings;
            } VkDescriptorSetLayoutCreateInfo;

            typedef struct VkDescriptorSetLayoutBinding {
                uint32_t              binding;
                VkDescriptorType      descriptorType;
                uint32_t              descriptorCount;
                VkShaderStageFlags    stageFlags;
                const VkSampler*      pImmutableSamplers;
            } VkDescriptorSetLayoutBinding;


        */
        // set 0
        // per frame
        // UBO Buffer      ->       1
        // Shadow Gen Data ->       2
        // Light Data      ->       3

        // set 1
        // persistent

        // ssao RT                      -> 1
        // ssao blur RT                 -> 2
        // Albedo RT                    -> 3
        // Specular Normal RT           -> 4
        // texture array for the meshes -> 5
        // sampler for render targets   -> 6
        // sampler for render targets 2 -> 7
        // sampler for textures         -> 8
        // shadow cascade depth RT      -> 11

        VkDescriptorPoolCreateInfo poolDesc;

        poolDesc.maxSets = MAX_SETS;
        poolDesc.pNext = nullptr;
        poolDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.pNext = nullptr;
        poolInfo.flags = 0; // see flags explanation below

        VkDescriptorPoolSize poolSizes[] =
            {
                {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 3 * FRAMES_IN_FLIGHT},

                {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount = 7 + BISTRO_TEXTURE_COUNT},

                {.type = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .descriptorCount = 3},
            };

        poolInfo.maxSets = FRAMES_IN_FLIGHT + 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

        {
            VkDescriptorSetLayoutBinding perFrameBinding[PER_FRAME_BINDING_COUNT] = {};

            // binding 0 — main UBO
            perFrameBinding[0].binding = 0;
            perFrameBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            perFrameBinding[0].descriptorCount = 1;
            perFrameBinding[0].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            perFrameBinding[0].pImmutableSamplers = nullptr;

            // binding 1 — shadow generation data
            perFrameBinding[1].binding = 1;
            perFrameBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            perFrameBinding[1].descriptorCount = 1;
            perFrameBinding[1].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            perFrameBinding[1].pImmutableSamplers = nullptr;

            perFrameBinding[2].binding = 2;
            perFrameBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            perFrameBinding[2].descriptorCount = 1;
            perFrameBinding[2].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            perFrameBinding[2].pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo lPerFrameDesc = {};
            lPerFrameDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lPerFrameDesc.pNext = nullptr;
            lPerFrameDesc.flags = 0;
            lPerFrameDesc.bindingCount = PER_FRAME_BINDING_COUNT;
            lPerFrameDesc.pBindings = perFrameBinding;

            vkCreateDescriptorSetLayout(device, &lPerFrameDesc, nullptr, &perFrameLayout);
        }

        {

            VkDescriptorSetLayoutBinding persistentBinding[PERSISTENT_BINDING_COUNT] = {};
            // Depth 0 RT
            persistentBinding[0].binding = 0;
            persistentBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[0].descriptorCount = 1;
            persistentBinding[0].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[0].pImmutableSamplers = nullptr;
            
            // SSAO  1 RT
            persistentBinding[1].binding = 1;
            persistentBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[1].descriptorCount = 1;
            persistentBinding[1].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[1].pImmutableSamplers = nullptr;

            // binding 2 — SSAO Blur RT
            persistentBinding[2].binding = 2;
            persistentBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[2].descriptorCount = 1;
            persistentBinding[2].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[2].pImmutableSamplers = nullptr;

            // binding 3 — Albedo RT
            persistentBinding[3].binding = 3;
            persistentBinding[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[3].descriptorCount = 1;
            persistentBinding[3].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[3].pImmutableSamplers = nullptr;

            // binding 4 — Specular/Normal RT
            persistentBinding[4].binding = 4;
            persistentBinding[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[4].descriptorCount = 1;
            persistentBinding[4].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[4].pImmutableSamplers = nullptr;

            // binding 5 — Mesh texture array
            persistentBinding[5].binding = 5;
            persistentBinding[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[5].descriptorCount = BISTRO_TEXTURE_COUNT;
            persistentBinding[5].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[5].pImmutableSamplers = nullptr;

            // binding 6 — RT sampler 1
            persistentBinding[6].binding = 6;
            persistentBinding[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            persistentBinding[6].descriptorCount = 1;
            persistentBinding[6].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[6].pImmutableSamplers = nullptr;

            // binding 7 — RT sampler 2
            persistentBinding[7].binding = 7;
            persistentBinding[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            persistentBinding[7].descriptorCount = 1;
            persistentBinding[7].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[7].pImmutableSamplers = nullptr;

            // binding 8 — mesh texture sampler
            persistentBinding[8].binding = 8;
            persistentBinding[8].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            persistentBinding[8].descriptorCount = 1;
            persistentBinding[8].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[8].pImmutableSamplers = nullptr;
            
            //SSAO Noise 
            persistentBinding[9].binding = 9;
            persistentBinding[9].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[9].descriptorCount = 1;
            persistentBinding[9].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[9].pImmutableSamplers = nullptr;
            // SSAO Kernels 
            persistentBinding[10].binding = 10;
            persistentBinding[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            persistentBinding[10].descriptorCount = 1;
            persistentBinding[10].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[10].pImmutableSamplers = nullptr;

            // Shadow cascade depth RT
            persistentBinding[11].binding = 11;
            persistentBinding[11].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            persistentBinding[11].descriptorCount = 1;
            persistentBinding[11].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            persistentBinding[11].pImmutableSamplers = nullptr;

           
            VkDescriptorSetLayoutCreateInfo lPersistentDesc = {};
            lPersistentDesc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lPersistentDesc.pNext = nullptr;
            lPersistentDesc.flags = 0;
            lPersistentDesc.bindingCount = PERSISTENT_BINDING_COUNT;
            lPersistentDesc.pBindings = persistentBinding;

            vkCreateDescriptorSetLayout(device, &lPersistentDesc, nullptr, &persistentLayout);
        }

        

        VkDescriptorSetLayout layouts[] = {  persistentLayout , perFrameLayout };

        VkPushConstantRange pushConstantRanges[1];
        pushConstantRanges->offset = 0;
        pushConstantRanges->size = std::max(sizeof(TriangleFilteringPushConstants), sizeof(ComposePushConstants));
        pushConstantRanges->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = layouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges; 
       
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
        

    }

    void prepareDescriptorSetPerFrame()
    {
        // set 0
        // per frame
        // UBO Buffer       ->      0
        // Shadow Gen Data  ->      1
        // Sky Light Data   ->      2
    
        for(uint32_t cur_set = 0; cur_set < FRAMES_IN_FLIGHT; cur_set++)
        {

            VkWriteDescriptorSet writePerFrame[PER_FRAME_BINDING_COUNT] = {};

            // binding 0 — UBO Buffer
            writePerFrame[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writePerFrame[0].pNext = nullptr;
            writePerFrame[0].dstSet = setsPerFrame[cur_set];
            writePerFrame[0].dstBinding = 0;
            writePerFrame[0].dstArrayElement = 0;
            writePerFrame[0].descriptorCount = 1;
            writePerFrame[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writePerFrame[0].pBufferInfo = &uboBuffer[cur_set]->descInfo;

            // binding 1 - shadow generation data
            writePerFrame[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writePerFrame[1].pNext = nullptr;
            writePerFrame[1].dstSet = setsPerFrame[cur_set];
            writePerFrame[1].dstBinding = 1;
            writePerFrame[1].dstArrayElement = 0;
            writePerFrame[1].descriptorCount = 1;
            writePerFrame[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writePerFrame[1].pBufferInfo = &shadowGenBuffer[cur_set]->descInfo;

            // binding 2 — Sky Light Data
            writePerFrame[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writePerFrame[2].pNext = nullptr;
            writePerFrame[2].dstSet = setsPerFrame[cur_set];
            writePerFrame[2].dstBinding = 2;
            writePerFrame[2].dstArrayElement = 0;
            writePerFrame[2].descriptorCount = 1;
            writePerFrame[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writePerFrame[2].pBufferInfo = &SkyLightsDataBuffer[cur_set]->descInfo;

            vkUpdateDescriptorSets(device,
                                   PER_FRAME_BINDING_COUNT,
                                   writePerFrame,
                                   0, nullptr);
        }

   
    }
  
    std::vector<glm::vec4> generateSSAONoise(uint32_t noiseDim = SSAO_NOISE_DIM)
    {
        std::default_random_engine rndEngine(std::random_device{}());
        std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

        std::vector<glm::vec4> noise(noiseDim * noiseDim);
        for (auto &n : noise)
        {
            n = glm::vec4(
                rndDist(rndEngine) * 2.0f - 1.0f,
                rndDist(rndEngine) * 2.0f - 1.0f,
                0.0f,
                0.0f);
        }
        return noise;
    }

    void prepareDescriptorSetPerSistent()
    {

        VkDescriptorSetLayout layouts[] = {perFrameLayout, perFrameLayout, persistentLayout};

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(std::size(layouts));
        allocInfo.pSetLayouts = layouts;

        // allocate into a flat array first, then distribute
        VkDescriptorSet crSets[3] = {};
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, crSets));
        setsPerFrame[0] = crSets[0];
        setsPerFrame[1] = crSets[1];
        setsPersistent = crSets[2];
        /*

        typedef struct VkWriteDescriptorSet {
            VkStructureType                  sType;
            const void*                      pNext;
            VkDescriptorSet                  dstSet;
            uint32_t                         dstBinding;
            uint32_t                         dstArrayElement;
            uint32_t                         descriptorCount;
            VkDescriptorType                 descriptorType;
            const VkDescriptorImageInfo*     pImageInfo;
            const VkDescriptorBufferInfo*    pBufferInfo;
            const VkBufferView*              pTexelBufferView;
        } VkWriteDescriptorSet;


        */
        VkWriteDescriptorSet writePersistent[PERSISTENT_BINDING_COUNT] = {};

        writePersistent[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[0].pNext = nullptr;
        writePersistent[0].dstSet = setsPersistent;
        writePersistent[0].dstBinding = 0;
        writePersistent[0].dstArrayElement = 0;
        writePersistent[0].descriptorCount = 1;
        writePersistent[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[0].pImageInfo = &gDepthBufferRT->imageInfo;

        writePersistent[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[1].pNext = nullptr;
        writePersistent[1].dstSet = setsPersistent;
        writePersistent[1].dstBinding = 1;
        writePersistent[1].dstArrayElement = 0;
        writePersistent[1].descriptorCount = 1;
        writePersistent[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[1].pImageInfo = &ssaoRT->imageInfo;

        // binding 2 — SSAO RT
        writePersistent[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[2].pNext = nullptr;
        writePersistent[2].dstSet = setsPersistent;
        writePersistent[2].dstBinding = 2;
        writePersistent[2].dstArrayElement = 0;
        writePersistent[2].descriptorCount = 1;
        writePersistent[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[2].pImageInfo = &ssaoBlurRT->imageInfo;

        // binding 3 — SSAO Blur RT
        writePersistent[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[3].pNext = nullptr;
        writePersistent[3].dstSet = setsPersistent;
        writePersistent[3].dstBinding = 3;
        writePersistent[3].dstArrayElement = 0;
        writePersistent[3].descriptorCount = 1;
        writePersistent[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[3].pImageInfo = &gBufferAlbedoRT->imageInfo;

        // binding 4 — Specular/Normal RT
        writePersistent[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[4].pNext = nullptr;
        writePersistent[4].dstSet = setsPersistent;
        writePersistent[4].dstBinding = 4;
        writePersistent[4].dstArrayElement = 0;
        writePersistent[4].descriptorCount = 1;
        writePersistent[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[4].pImageInfo = &gBufferNormalSpecularRT->imageInfo;

        // binding 5 — Mesh texture array
        writePersistent[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[5].pNext = nullptr;
        writePersistent[5].dstSet = setsPersistent;
        writePersistent[5].dstBinding = 5;
        writePersistent[5].dstArrayElement = 0;
        writePersistent[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        std::vector<VkDescriptorImageInfo> meshImageInfos;
        meshImageInfos.reserve(BISTRO_TEXTURE_COUNT);
        if (!model_gltf.images.empty())
        {
            uint32_t textureCount = std::min<uint32_t>(static_cast<uint32_t>(model_gltf.images.size()), BISTRO_TEXTURE_COUNT);
            for (uint32_t i = 0; i < textureCount; ++i)
            {
                meshImageInfos.push_back(model_gltf.images[i].texture.descInfo);
            }
        }
        else
        {
            for (uint32_t i = 0; i < DRAGON_TEXTURES_COUNT; ++i)
            {
                if (DRAGON_TEXTURES[i])
                    meshImageInfos.push_back(DRAGON_TEXTURES[i]->descInfo);
                else
                    meshImageInfos.push_back(VkDescriptorImageInfo{});
            }
        }
        while (!meshImageInfos.empty() && meshImageInfos.size() < BISTRO_TEXTURE_COUNT)
        {
            meshImageInfos.push_back(meshImageInfos[0]);
        }
        writePersistent[5].descriptorCount = static_cast<uint32_t>(meshImageInfos.size());
        writePersistent[5].pImageInfo = meshImageInfos.data();
    
        // binding 6 — RT sampler 1
        writePersistent[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[6].pNext = nullptr;
        writePersistent[6].dstSet = setsPersistent;
        writePersistent[6].dstBinding = 6;
        writePersistent[6].dstArrayElement = 0;
        writePersistent[6].descriptorCount = 1;
        writePersistent[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writePersistent[6].pImageInfo  = &renderTargetSampler->descInfo; 

        // binding 7 — RT sampler 2
        writePersistent[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[7].pNext = nullptr;
        writePersistent[7].dstSet = setsPersistent;
        writePersistent[7].dstBinding = 7;
        writePersistent[7].dstArrayElement = 0;
        writePersistent[7].descriptorCount = 1;
        writePersistent[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writePersistent[7].pImageInfo = &renderTargetSampler->descInfo;
        
        // binding 8 — Mesh texture sampler
        writePersistent[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[8].pNext = nullptr;
        writePersistent[8].dstSet = setsPersistent;
        writePersistent[8].dstBinding = 8;
        writePersistent[8].dstArrayElement = 0;
        writePersistent[8].descriptorCount = 1;
        writePersistent[8].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writePersistent[8].pImageInfo   = &textureSampler->descInfo;
        
        // SSAO Noise 
        writePersistent[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[9].pNext = nullptr;
        writePersistent[9].dstSet = setsPersistent;
        writePersistent[9].dstBinding = 9;
        writePersistent[9].dstArrayElement = 0;
        writePersistent[9].descriptorCount = 1;
        writePersistent[9].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[9].pImageInfo = &ssaoNoiseTexture->descInfo;

        // SSAO Kernels 
        writePersistent[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[10].pNext = nullptr;
        writePersistent[10].dstSet = setsPersistent;
        writePersistent[10].dstBinding = 10;
        writePersistent[10].dstArrayElement = 0;
        writePersistent[10].descriptorCount = 1;
        writePersistent[10].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writePersistent[10].pBufferInfo = &ssaoKernelBuffer->descInfo;

        // Shadow cascade depth RT
        writePersistent[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writePersistent[11].pNext = nullptr;
        writePersistent[11].dstSet = setsPersistent;
        writePersistent[11].dstBinding = 11;
        writePersistent[11].dstArrayElement = 0;
        writePersistent[11].descriptorCount = 1;
        writePersistent[11].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writePersistent[11].pImageInfo = &gShadowRenderTargets.cascadeDepth->imageInfo;

        vkUpdateDescriptorSets(device,
                                PERSISTENT_BINDING_COUNT,
                                writePersistent,
                                0, nullptr);
    }

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer &buf, VkDeviceMemory &mem)
    {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = size;
        bufInfo.usage = usage;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bufInfo, nullptr, &buf);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, buf, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReq.memoryTypeBits, props);
        vkAllocateMemory(device, &allocInfo, nullptr, &mem);

        vkBindBufferMemory(device, buf, mem, 0);
    }

    void mapBuffer(VkDevice device, Buffer *pBuffer, const void *pData)
    {
        assert(pBuffer);
        assert(pData);

        void *pMapped = nullptr;
        VK_CHECK(vkMapMemory(device, pBuffer->memory, 0, pBuffer->size, 0, &pMapped));
        memcpy(pMapped, pData, (size_t)pBuffer->size);
        vkUnmapMemory(device, pBuffer->memory);
    }

    void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = graphicsCommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, graphicsCommandPool, 1, &cmd);
    }

    void addSamplers()
    {
        {
            samplerDesc textureSamplerDesc;
            textureSamplerDesc.magFilter = VK_FILTER_LINEAR;
            textureSamplerDesc.minFilter = VK_FILTER_LINEAR;
            textureSamplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            textureSamplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            textureSamplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            textureSamplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            textureSamplerDesc.anisotropyEnable = VK_TRUE;
            textureSamplerDesc.maxAnisotropy = 16.0f;
            textureSamplerDesc.minLod = 0.0f;
            textureSamplerDesc.maxLod = VK_LOD_CLAMP_NONE;
            textureSamplerDesc.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            addSampler(device, textureSamplerDesc, &textureSampler);
        }

        // Render target sampler — clamp to edge, no mips, no anisotropy
        {
            samplerDesc renderTargetSamplerDesc;
            renderTargetSamplerDesc.magFilter = VK_FILTER_LINEAR;
            renderTargetSamplerDesc.minFilter = VK_FILTER_LINEAR;
            renderTargetSamplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            renderTargetSamplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            renderTargetSamplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            renderTargetSamplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            renderTargetSamplerDesc.anisotropyEnable = VK_FALSE;
            renderTargetSamplerDesc.maxAnisotropy = 1.0f;
            renderTargetSamplerDesc.minLod = 0.0f;
            renderTargetSamplerDesc.maxLod = 0.0f; // single mip, no LOD
            renderTargetSamplerDesc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            addSampler(device, renderTargetSamplerDesc, &renderTargetSampler);
        }
    }

    void addRenderTarget(VkDevice device, VkPhysicalDevice physicalDevice,
                            const RenderTargetDesc *pDesc, RenderTarget **ppRenderTarget)
    {
        RenderTarget *pRT = new RenderTarget();
        *ppRenderTarget = pRT;

        pRT->width = pDesc->width;
        pRT->height = pDesc->height;
        pRT->depth = pDesc->depth;
        pRT->arraySize = pDesc->arraySize;
        pRT->mipLevels = pDesc->mipLevels;
        pRT->format = pDesc->format;
        pRT->sampleCount = pDesc->sampleCount;
        pRT->clearValue = pDesc->clearValue;
        pRT->currentLayout = pDesc->startLayout;
        pRT->pName = pDesc->pName;

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = pDesc->depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        imageInfo.format = pDesc->format;
        imageInfo.extent.width = pDesc->width;
        imageInfo.extent.height = pDesc->height;
        imageInfo.extent.depth = pDesc->depth;
        imageInfo.mipLevels = pDesc->mipLevels;
        imageInfo.arrayLayers = pDesc->arraySize;
        imageInfo.samples = pDesc->sampleCount;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = pDesc->usageFlags;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK(vkCreateImage(device, &imageInfo, nullptr, &pRT->image));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, pRT->image, &memReqs);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &pRT->memory));
        VK_CHECK(vkBindImageMemory(device, pRT->image, pRT->memory, 0));

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = pRT->image;
        viewInfo.viewType = pDesc->arraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pDesc->format;
        viewInfo.subresourceRange.aspectMask = IsDepthFormat(pDesc->format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = pDesc->mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = pDesc->arraySize;

        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &pRT->imageView));

        if (pDesc->usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &pRT->sampledView));
        }

        VkImageLayout descriptorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        pRT->imageInfo.imageLayout = descriptorLayout;
        pRT->imageInfo.imageView = pRT->sampledView != VK_NULL_HANDLE ? pRT->sampledView : pRT->imageView;
        pRT->imageInfo.sampler = VK_NULL_HANDLE; // set externally if COMBINED_IMAGE_SAMPLER
    }

    void addShadowCascadeLayerViews()
    {
        RenderTarget *shadowDepth = gShadowRenderTargets.cascadeDepth;
        assert(shadowDepth && shadowDepth->image != VK_NULL_HANDLE);

        for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex)
        {
            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = shadowDepth->image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = shadowDepth->format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = cascadeIndex;
            viewInfo.subresourceRange.layerCount = 1;

            VK_CHECK(vkCreateImageView(device,
                                       &viewInfo,
                                       nullptr,
                                       &gShadowRenderTargets.cascadeLayerViews[cascadeIndex]));
        }
    }

    bool hasShadowRenderTargets() const
    {
        const RenderTarget *shadowDepth = gShadowRenderTargets.cascadeDepth;
        if (!shadowDepth ||
            shadowDepth->image == VK_NULL_HANDLE ||
            shadowDepth->imageView == VK_NULL_HANDLE ||
            shadowDepth->sampledView == VK_NULL_HANDLE)
        {
            return false;
        }

        for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex)
        {
            if (gShadowRenderTargets.cascadeLayerViews[cascadeIndex] == VK_NULL_HANDLE)
            {
                return false;
            }
        }

        return true;
    }

    void syncShadowGenerationBuffers()
    {
        for (uint32_t frameIndex = 0; frameIndex < FRAMES_IN_FLIGHT; ++frameIndex)
        {
            if (shadowGenBuffer[frameIndex])
            {
                mapBuffer(device, shadowGenBuffer[frameIndex], &ShadowGenUniformData);
            }
        }
    }

    GLFWwindow *CreateWindow(int width, int height, const char *title)
    {

        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW!");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        GLFWwindow *window = glfwCreateWindow(width, height, title, nullptr, nullptr);

        if (!window)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window!");
        }

        return window;
    }

    std::vector<uint32_t> LoadShaderCode(const std::string &filepath)
    {

        std::ifstream file(filepath, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open shader file: " + filepath);
        }

        // Get file size and allocate buffer
        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    void addRenderTargets()
    {
        // GBuffer Render Targets
        {

            /*
                struct RenderTargetDesc
                {
                    uint32_t width = 0;
                    uint32_t height = 0;
                    uint32_t depth = 1;
                    uint32_t arraySize = 1;
                    uint32_t mipLevels = 1;

                    VkFormat format = VK_FORMAT_UNDEFINED;
                    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;

                    VkClearValue clearValue = {};

                    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

                    const char *pName = nullptr;

                    VkImageLayout startLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                };
            */
            // Gbuffer Render Targets
            {
                {
                    RenderTargetDesc gBufferRenderTargetAlbedoDesc; // --> albedo color attachment rendering
                    gBufferRenderTargetAlbedoDesc.height = gAppSettings->height;
                    gBufferRenderTargetAlbedoDesc.width = gAppSettings->width;
                    gBufferRenderTargetAlbedoDesc.arraySize = 1;
                    gBufferRenderTargetAlbedoDesc.mipLevels = 1;
                    gBufferRenderTargetAlbedoDesc.usageFlags = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    gBufferRenderTargetAlbedoDesc.format = VkFormat::VK_FORMAT_R8G8B8A8_SNORM;
                    gBufferRenderTargetAlbedoDesc.clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}};
                    gBufferRenderTargetAlbedoDesc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                    gBufferRenderTargetAlbedoDesc.startLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // --> starting as a shader resource
                    gBufferRenderTargetAlbedoDesc.pName = "GBuffer_AlbedoRT";
                    addRenderTarget(device, physicalDevice, &gBufferRenderTargetAlbedoDesc, &gBufferAlbedoRT);
                }

                {
                    RenderTargetDesc gBufferRenderTargetNormalSpecularDesc; // -->  Normal and Specular attachment rendering
                    gBufferRenderTargetNormalSpecularDesc.height = gAppSettings->height;
                    gBufferRenderTargetNormalSpecularDesc.width = gAppSettings->width;
                    gBufferRenderTargetNormalSpecularDesc.arraySize = 1;
                    gBufferRenderTargetNormalSpecularDesc.mipLevels = 1;
                    gBufferRenderTargetNormalSpecularDesc.usageFlags = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    gBufferRenderTargetNormalSpecularDesc.format = VkFormat::VK_FORMAT_R16G16B16A16_SFLOAT;
                    gBufferRenderTargetNormalSpecularDesc.clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}};
                    gBufferRenderTargetNormalSpecularDesc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                    gBufferRenderTargetNormalSpecularDesc.startLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // --> starting as a shader resource
                    gBufferRenderTargetNormalSpecularDesc.pName = "GBuffer_NormalSpecularRT";

                    addRenderTarget(device, physicalDevice, &gBufferRenderTargetNormalSpecularDesc, &gBufferNormalSpecularRT);
                }
            }
            // SSAO Render Targets
            {
                // SSAO Gen
                {
                    RenderTargetDesc gSSAORenderTargetDesc; // --> albedo color attachment rendering
                    gSSAORenderTargetDesc.height = gAppSettings->height;
                    gSSAORenderTargetDesc.width = gAppSettings->width;
                    gSSAORenderTargetDesc.arraySize = 1;
                    gSSAORenderTargetDesc.mipLevels = 1;
                    gSSAORenderTargetDesc.usageFlags = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    gSSAORenderTargetDesc.format = VkFormat::VK_FORMAT_R32_SFLOAT;
                    gSSAORenderTargetDesc.clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}};
                    gSSAORenderTargetDesc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                    gSSAORenderTargetDesc.startLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // --> starting as a shader resource
                    gSSAORenderTargetDesc.pName = "SSAO_GenRT";
                    addRenderTarget(device, physicalDevice, &gSSAORenderTargetDesc, &ssaoRT);
                }

                // SSAO Blur
                {
                    RenderTargetDesc gSSAOBlurRenderTargetDesc; // -->  Normal and Specular attachment rendering
                    gSSAOBlurRenderTargetDesc.height = gAppSettings->height;
                    gSSAOBlurRenderTargetDesc.width = gAppSettings->width;
                    gSSAOBlurRenderTargetDesc.arraySize = 1;
                    gSSAOBlurRenderTargetDesc.mipLevels = 1;
                    gSSAOBlurRenderTargetDesc.usageFlags = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    gSSAOBlurRenderTargetDesc.format = VkFormat::VK_FORMAT_R32_SFLOAT;
                    gSSAOBlurRenderTargetDesc.clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}};
                    gSSAOBlurRenderTargetDesc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                    gSSAOBlurRenderTargetDesc.startLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // --> starting as a shader resource
                    gSSAOBlurRenderTargetDesc.pName = "SSAO_BlurRT";

                    addRenderTarget(device, physicalDevice, &gSSAOBlurRenderTargetDesc, &ssaoBlurRT);
                }
                {
                    RenderTargetDesc depthDesc{};
                    depthDesc.width = gAppSettings->width;
                    depthDesc.height = gAppSettings->height;
                    depthDesc.format = VK_FORMAT_D32_SFLOAT;
                    depthDesc.usageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    depthDesc.clearValue = {.depthStencil = {1.0f, 0}};
                    depthDesc.startLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    depthDesc.pName = "DepthBuffer";
                    addRenderTarget(device, physicalDevice, &depthDesc, &gDepthBufferRT);
                }
            }

            // Shadow Render Targets
            {
                RenderTargetDesc shadowDepthDesc{};
                shadowDepthDesc.width = SHADOW_MAP_SIZE;
                shadowDepthDesc.height = SHADOW_MAP_SIZE;
                shadowDepthDesc.arraySize = MAX_SHADOW_CASCADES;
                shadowDepthDesc.mipLevels = 1;
                shadowDepthDesc.format = VK_FORMAT_D32_SFLOAT;
                shadowDepthDesc.usageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                shadowDepthDesc.clearValue = {.depthStencil = {1.0f, 0}};
                shadowDepthDesc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
                shadowDepthDesc.startLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                shadowDepthDesc.pName = "Shadow_CascadeDepthRT";

                addRenderTarget(device, physicalDevice, &shadowDepthDesc, &gShadowRenderTargets.cascadeDepth);
                addShadowCascadeLayerViews();

                if (!hasShadowRenderTargets())
                {
                    throw std::runtime_error("Shadow render targets were not created correctly");
                }

                UpdateShadowGenerationData(true);
                syncShadowGenerationBuffers();
            }
        }
    }
    void CreateCommandPool()
    {

        VkCommandPoolCreateInfo commandPoolDesc;
        commandPoolDesc.flags = VkCommandPoolCreateFlagBits::VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolDesc.pNext = nullptr;
        commandPoolDesc.queueFamilyIndex = indecies.graphicsFamily.value();
        commandPoolDesc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        vkCreateCommandPool(device, &commandPoolDesc, nullptr, &graphicsCommandPool);
    }

    void CreateCommandBuffer()
    {
        VkCommandBufferAllocateInfo commandBufferDesc;
        commandBufferDesc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferDesc.commandBufferCount = COMMAND_BUFFER_COUNT;
        commandBufferDesc.commandPool = graphicsCommandPool;
        commandBufferDesc.level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferDesc.pNext = nullptr;

        vkAllocateCommandBuffers(device, &commandBufferDesc, commandBuffers);
    }
   
    bool load_stb_image(unsigned char **data, std::string& filename, int& width, int& height, int& channels)
    {
        *data = stbi_load(
            filename.data(),
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        );

        if (*data == NULL)
        {
            std::cout << "Error loading image file: " << filename << std::endl;
            return false;
        }
        return true;
    }

    void Update(uint32_t frameIndex)
    {
        static double previousTime = glfwGetTime();
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        glm::vec3 moveDelta(0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(camera.dir.x, 0.0f, camera.dir.z));
        glm::vec3 right = glm::normalize(glm::cross(forward, camera.worldUp));
        float moveAmount = gCameraMoveSpeed * deltaTime;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            moveDelta += forward * moveAmount;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            moveDelta -= forward * moveAmount;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            moveDelta += right * moveAmount;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            moveDelta -= right * moveAmount;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            moveDelta += camera.worldUp * moveAmount;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            moveDelta -= camera.worldUp * moveAmount;

        if (glm::length(moveDelta) > 0.0f)
        {
            camera.setPosition(camera.pos + moveDelta);
        }

        float turnAmount = gCameraTurnSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            gCameraYaw -= turnAmount;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            gCameraYaw += turnAmount;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            gCameraPitch += turnAmount;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            gCameraPitch -= turnAmount;

        gCameraPitch = std::clamp(gCameraPitch, -89.0f, 89.0f);

        glm::vec3 direction;
        direction.x = cos(glm::radians(gCameraYaw)) * cos(glm::radians(gCameraPitch));
        direction.y = sin(glm::radians(gCameraPitch));
        direction.z = sin(glm::radians(gCameraYaw)) * cos(glm::radians(gCameraPitch));
        camera.setDirection(glm::normalize(direction));
        camera.updateViewMatrix();

        UniformData.cameraPos = camera.pos;
        UniformData.proj = camera.getProjMatrix();
        UniformData.view = camera.getViewMatrix();
        mapBuffer(device, uboBuffer[frameIndex], &UniformData);
        updateSkyLight(frameIndex);
    }

    void Draw()
    {
 
        static uint32_t frameIndex = 0;

        VkFence frameFence = commandBufferFences[frameIndex];
        VK_CHECK(vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &frameFence));

        uint32_t imageIndex = 0;
        VkSemaphore frameSemaphore = frameSemaphores[frameIndex];
        VK_CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frameSemaphore, VK_NULL_HANDLE, &imageIndex));

        VkDrawIndexedIndirectCommand filteringDrawCommand = {};
        filteringDrawCommand.instanceCount = 1;
        if (gTFilterIndirectArgs)
        {
            mapBuffer(device, gTFilterIndirectArgs, &filteringDrawCommand);
        }

        Update(frameIndex);
        
        VkCommandBuffer cmd = commandBuffers[frameIndex];
 
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
       
        {
        TracyVkZone(tracyGpuContext, cmd, "Scene Rendering Pass");
  
        {
            VkImageMemoryBarrier barriers[3] = {};

            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = gBufferAlbedoRT->image;
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[0].srcAccessMask = 0;
            barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].image = gBufferNormalSpecularRT->image;
            barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[1].srcAccessMask = 0;
            barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            barriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[2].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[2].image = gDepthBufferRT->image;
            barriers[2].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            barriers[2].srcAccessMask = 0;
            barriers[2].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                 0, 0, nullptr, 0, nullptr, 3, barriers);
        }

        VkRenderingAttachmentInfo gbufferColorAttachments[2] = {};
        gbufferColorAttachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        gbufferColorAttachments[0].imageView = gBufferNormalSpecularRT->imageView;
        gbufferColorAttachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        gbufferColorAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        gbufferColorAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        gbufferColorAttachments[0].clearValue = gBufferNormalSpecularRT->clearValue;

        gbufferColorAttachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        gbufferColorAttachments[1].imageView = gBufferAlbedoRT->imageView;
        gbufferColorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        gbufferColorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        gbufferColorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        gbufferColorAttachments[1].clearValue = gBufferAlbedoRT->clearValue;

        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = gDepthBufferRT->imageView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue = {.depthStencil = {1.0f, 0}};

        VkRenderingInfo gbufferRenderingInfo = {};
        gbufferRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        gbufferRenderingInfo.renderArea = {{0, 0}, {gAppSettings->width, gAppSettings->height}};
        gbufferRenderingInfo.layerCount = 1;
        gbufferRenderingInfo.colorAttachmentCount = 2;
        gbufferRenderingInfo.pColorAttachments = gbufferColorAttachments;
        gbufferRenderingInfo.pDepthAttachment = &depthAttachment;

        VkViewport viewport = {};
        viewport.width = (float)gAppSettings->width;
        viewport.height = (float)gAppSettings->height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.extent = {gAppSettings->width, gAppSettings->height};

        // compute triangle filtering
        {
            auto getBufferAddress = [this](VkBuffer buffer)
            {
                VkBufferDeviceAddressInfo addressInfo = {};
                addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                addressInfo.buffer = buffer;
                return vkGetBufferDeviceAddress(device, &addressInfo);
            };

            TriangleFilteringPushConstants filteringPushConstants = {};
            filteringPushConstants.indexBuffer = getBufferAddress(model_gltf.indexBufferVk->buffer);
            filteringPushConstants.vertexBuffer = getBufferAddress(model_gltf.vertexBufferVk->buffer);
            if (gTFilteredTriangles)
            {
                filteringPushConstants.filteredBuffers[0] = getBufferAddress(gTFilteredTriangles->buffer);
            }
            if (gTFilterIndirectArgs)
            {
                filteringPushConstants.drawCommand = getBufferAddress(gTFilterIndirectArgs->buffer);
            }
            filteringPushConstants.modelViewProjection = UniformData.proj * UniformData.view * UniformData.model;
            filteringPushConstants.vertexStride = sizeof(VulkanglTFModel::Vertex);
            filteringPushConstants.indexCount = model_gltf.indextotalCount;
            filteringPushConstants.vertexCount = model_gltf.vertextotalCount;

            constexpr uint32_t filteringThreadGroupSize = 256;
            const uint32_t triangleCount = (model_gltf.indextotalCount + 2) / 3;
            const uint32_t groupCount = (triangleCount + filteringThreadGroupSize - 1) / filteringThreadGroupSize;

            if (gTFilteringPipeline && gTFilteredTriangles && gTFilterIndirectArgs && groupCount > 0)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gTFilteringPipeline);
                vkCmdPushConstants(cmd,
                                   pipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   sizeof(TriangleFilteringPushConstants),
                                   &filteringPushConstants);
                vkCmdDispatch(cmd, groupCount, 1, 1);

                VkBufferMemoryBarrier filterBarriers[2] = {};
                filterBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                filterBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                filterBarriers[0].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
                filterBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                filterBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                filterBarriers[0].buffer = gTFilteredTriangles->buffer;
                filterBarriers[0].offset = 0;
                filterBarriers[0].size = gTFilteredTriangles->size;

                filterBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                filterBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                filterBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                filterBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                filterBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                filterBarriers[1].buffer = gTFilterIndirectArgs->buffer;
                filterBarriers[1].offset = 0;
                filterBarriers[1].size = gTFilterIndirectArgs->size;

                vkCmdPipelineBarrier(cmd,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                     0, 0, nullptr, 2, filterBarriers, 0, nullptr);
            }
        }

        vkCmdBeginRendering(cmd, &gbufferRenderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferPipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDescriptorSet gbufferSets[2] = {setsPersistent, setsPerFrame[frameIndex]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, gbufferSets, 0, nullptr);

        VkDeviceSize offset = 0;
       
        {
            vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
            VkBool32 colorWriteEnables[2] = {VK_TRUE, VK_TRUE};
            pfnCmdSetColorWriteEnableEXT(cmd, 2, colorWriteEnables);


        }

        /*
           // vertex buffer
            {
                bufferDesc sponzaVertexBufDesc = {};
                sponzaVertexBufDesc.size = sizeof(VulkanglTFModel::Vertex) * vertexBuffer.size();
                sponzaVertexBufDesc.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                sponzaVertexBufDesc.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                sponzaVertexBufDesc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

                addBuffer(device, physicalDevice, sponzaVertexBufDesc, &model_gltf.vertexBufferVk);

                mapBuffer(device, model_gltf.vertexBufferVk, &vertexBuffer);

            }
            // index buffer
            {
                bufferDesc sponzaIndexBuffer = {};

                sponzaIndexBuffer.size = sizeof(uint32_t) * indexBuffer.size();
                sponzaIndexBuffer.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                sponzaIndexBuffer.usage = VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                sponzaIndexBuffer.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

                addBuffer(device, physicalDevice, sponzaIndexBuffer, &model_gltf.indexBufferVk);

                mapBuffer(device, model_gltf.indexBufferVk, &indexBuffer);
            }
        }
        */

        vkCmdBindVertexBuffers(cmd, 0, 1, &model_gltf.vertexBufferVk->buffer, &offset);
        if (gTFilteredTriangles && gTFilterIndirectArgs)
        {
            vkCmdBindIndexBuffer(cmd, gTFilteredTriangles->buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(cmd, gTFilterIndirectArgs->buffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
        }
        else
        {
            vkCmdBindIndexBuffer(cmd, model_gltf.indexBufferVk->buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, model_gltf.indextotalCount, 1, 0, 0, 0);
        }

        vkCmdEndRendering(cmd);

        {
            VkImageMemoryBarrier barriers[4] = {};

            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = gBufferNormalSpecularRT->image;
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].image = gDepthBufferRT->image;
            barriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
            barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[2].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[2].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[2].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[2].image = ssaoRT->image;
            barriers[2].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[2].srcAccessMask = 0;
            barriers[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            barriers[3].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[3].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[3].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[3].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[3].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[3].image = gBufferAlbedoRT->image;
            barriers[3].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0, 0, nullptr, 0, nullptr, 4, barriers);
        }

        VkRenderingAttachmentInfo ssaoColorAttachment = {};
        ssaoColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ssaoColorAttachment.imageView = ssaoRT->imageView;
        ssaoColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ssaoColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ssaoColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ssaoColorAttachment.clearValue = ssaoRT->clearValue;

        VkRenderingInfo ssaoRenderingInfo = {};
        ssaoRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ssaoRenderingInfo.renderArea = {{0, 0}, {gAppSettings->width, gAppSettings->height}};
        ssaoRenderingInfo.layerCount = 1;
        ssaoRenderingInfo.colorAttachmentCount = 1;
        ssaoRenderingInfo.pColorAttachments = &ssaoColorAttachment;

        vkCmdBeginRendering(cmd, &ssaoRenderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDescriptorSet ssaoSets[2] = {setsPersistent, setsPerFrame[frameIndex]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, ssaoSets, 0, nullptr);

        VkBool32 colorWriteEnables[1] = {VK_TRUE};
       
        pfnCmdSetColorWriteEnableEXT(cmd, 1, colorWriteEnables);

        VkBool32 blendEnable = VK_FALSE;
        pfnCmdSetColorBlendEnableEXT(cmd, 0, 1, &blendEnable);

        pfnCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
     
        VkBool32 ssaoWrite = VK_TRUE;
        pfnCmdSetColorWriteEnableEXT(cmd, 1, &ssaoWrite); 

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        {
            VkImageMemoryBarrier barriers[2] = {};

            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = ssaoRT->image;
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].image = ssaoBlurRT->image;
            barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[1].srcAccessMask = 0;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, barriers);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ssaoBlurPipeline);

        VkDescriptorSet blurSets[2] = {setsPersistent, setsPerFrame[frameIndex]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 2, blurSets, 0, nullptr);
        vkCmdDispatch(cmd, (gAppSettings->width + 7) / 8, (gAppSettings->height + 7) / 8, 1);

        {
            VkImageMemoryBarrier barriers[2] = {};

            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[0].image = ssaoBlurRT->image;
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[1].image = swapchainImages[imageIndex];
            barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[1].srcAccessMask = 0;
            barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0, 0, nullptr, 0, nullptr, 2, barriers);
        }

        VkRenderingAttachmentInfo swapchainAttachment = {};
        swapchainAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        swapchainAttachment.imageView = swapchainImageViews[imageIndex];
        swapchainAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapchainAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swapchainAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swapchainAttachment.clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};

        VkRenderingInfo composeRenderingInfo = {};
        composeRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        composeRenderingInfo.renderArea = {{0, 0}, {gAppSettings->width, gAppSettings->height}};
        composeRenderingInfo.layerCount = 1;
        composeRenderingInfo.colorAttachmentCount = 1;
        composeRenderingInfo.pColorAttachments = &swapchainAttachment;

        vkCmdBeginRendering(cmd, &composeRenderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composePipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkBool32 compWrite = VK_TRUE;
        pfnCmdSetColorWriteEnableEXT(cmd, 1, &compWrite);

        VkDescriptorSet composeSets[2] = {setsPersistent, setsPerFrame[frameIndex]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, composeSets, 0, nullptr);
        {
            VkBool32 colorWriteEnables[1] = {VK_TRUE};
            pfnCmdSetColorWriteEnableEXT(cmd, 1, colorWriteEnables);
        }

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        {
            VkImageMemoryBarrier presentBarrier = {};
            presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            presentBarrier.image = swapchainImages[imageIndex];
            presentBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            presentBarrier.dstAccessMask = 0;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
        }
        }
        TracyVkCollect(tracyGpuContext, cmd);
        
        VK_CHECK(vkEndCommandBuffer(cmd));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSemaphore renderFinishedSemaphore = renderFinishedSemaphores[frameIndex];

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &frameSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameFence));

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VK_CHECK(vkQueuePresentKHR(presentQueue, &presentInfo));

        frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;

    }

    void addShaders()
    {
        // GBuffer shaders
        {
            gBufferShader = new Shader();

            std::vector<uint32_t> GBufferVertCode = LoadShaderCode("shaders/gbuffer.vert.spv");
            std::vector<uint32_t> GBufferFragCode = LoadShaderCode("shaders/gbuffer.frag.spv");

            assert(GBufferVertCode.size() !=  0);
            assert(GBufferFragCode.size() != 0);

            VkShaderModuleCreateInfo shaderModuleDesc = {};
            shaderModuleDesc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(GBufferVertCode.data());
            shaderModuleDesc.codeSize = 4 * GBufferVertCode.size();

            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &gBufferShader->vert));

            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(GBufferFragCode.data());
            shaderModuleDesc.codeSize = 4 * GBufferFragCode.size();
            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &gBufferShader->frag));
        }
        // SSAO Shader 
        {
            ssaoShader = new Shader();

            auto ssaoShaderVertCode = LoadShaderCode("shaders/fullscreen.vert.spv");
            auto ssaoShaderFragCode = LoadShaderCode("shaders/ssao.frag.spv");

            assert(ssaoShaderVertCode.size() != 0);
            assert(ssaoShaderFragCode.size() != 0);

            VkShaderModuleCreateInfo shaderModuleDesc = {};
            shaderModuleDesc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(ssaoShaderVertCode.data());
            shaderModuleDesc.codeSize = ssaoShaderVertCode.size() * 4;

            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &ssaoShader->vert));

            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(ssaoShaderFragCode.data());
            shaderModuleDesc.codeSize = 4 * ssaoShaderFragCode.size();
            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &ssaoShader->frag));
        }
        // Blur Shader
        {
            ssaoBlurShader = new Shader();

            auto ssaoBlurShaderCompCode = LoadShaderCode("shaders/ssao_blur.comp.spv");

            VkShaderModuleCreateInfo shaderModuleDesc = {};
            shaderModuleDesc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(ssaoBlurShaderCompCode.data());
            shaderModuleDesc.codeSize = ssaoBlurShaderCompCode.size() * 4;

            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &ssaoBlurShader->comp));
        }
        // Compose Shaders 
        {
            composeShader = new Shader();

            auto composeShaderVertCode = LoadShaderCode("shaders/fullscreen.vert.spv");
            auto composeShaderFragCode = LoadShaderCode("shaders/compose.frag.spv");

            VkShaderModuleCreateInfo shaderModuleDesc = {};
            shaderModuleDesc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(composeShaderVertCode.data());
            shaderModuleDesc.codeSize = composeShaderVertCode.size() * 4;

            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &composeShader->vert));

            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(composeShaderFragCode.data());
            shaderModuleDesc.codeSize = composeShaderFragCode.size() * 4;
            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &composeShader->frag));
        }
    
        // Compute and Filtering Shaders 
        {
            shaderComputerFiltering = new Shader();

            auto triangleFilteringShaders = LoadShaderCode("shaders/filtering.comp.spv");
            assert(triangleFilteringShaders.size() != 0);

            VkShaderModuleCreateInfo shaderModuleDesc = {};
            shaderModuleDesc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderModuleDesc.pCode = reinterpret_cast<const uint32_t *>(triangleFilteringShaders.data());
            shaderModuleDesc.codeSize = triangleFilteringShaders.size() * 4;

            VK_CHECK(vkCreateShaderModule(device, &shaderModuleDesc, nullptr, &shaderComputerFiltering->comp));

        } 
    
    }
    
    void addPipelines()
    {
        
        VkPipelineMultisampleStateCreateInfo multisampleState = {};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // Or match your main pass
        multisampleState.sampleShadingEnable = VK_FALSE;
        multisampleState.minSampleShading = 1.0f;
        multisampleState.pSampleMask = nullptr;
        multisampleState.alphaToCoverageEnable = VK_FALSE;
        multisampleState.alphaToOneEnable = VK_FALSE;

        
        /*
            typedef struct VkGraphicsPipelineCreateInfo {
                VkStructureType                                  sType;
                const void*                                      pNext;
                VkPipelineCreateFlags                            flags;
                uint32_t                                         stageCount;
                const VkPipelineShaderStageCreateInfo*           pStages;
                const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
                const VkPipelineTessellationStateCreateInfo*     pTessellationState;
                const VkPipelineViewportStateCreateInfo*         pViewportState;
                const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
                const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
                const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
                const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
                const VkPipelineDynamicStateCreateInfo*          pDynamicState;
                VkPipelineLayout                                 layout;
                VkRenderPass                                     renderPass;
                uint32_t                                         subpass;
                VkPipeline                                       basePipelineHandle;
                int32_t                                          basePipelineIndex;
            } VkGraphicsPipelineCreateInfo;

        */
       
        VkPipelineRasterizationStateCreateInfo pRasterizationState{};
        pRasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        pRasterizationState.pNext = nullptr;
        pRasterizationState.flags = 0;
        pRasterizationState.depthClampEnable = VK_FALSE;
        pRasterizationState.rasterizerDiscardEnable = VK_FALSE;
        pRasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        pRasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
        pRasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pRasterizationState.depthBiasEnable = VK_FALSE;
        pRasterizationState.depthBiasConstantFactor = 0.0f;
        pRasterizationState.depthBiasClamp = 0.0f;
        pRasterizationState.depthBiasSlopeFactor = 0.0f;
        pRasterizationState.lineWidth = 1.0f;
       
        // Triangle filtering pipeline  
        if (shaderComputerFiltering)
        {
            VkPipelineShaderStageCreateInfo shaderStageDesc{};
            // Comp stage
            
            shaderStageDesc.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageDesc.pNext = nullptr;
            shaderStageDesc.flags = 0;
            shaderStageDesc.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            shaderStageDesc.module = shaderComputerFiltering->comp;
            shaderStageDesc.pName = "main";
            shaderStageDesc.pSpecializationInfo = nullptr;

            // dynamic rendering stage
            VkPipelineRenderingCreateInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 2;
            VkFormat colorsAttachments[2] = {gBufferNormalSpecularRT->format, gBufferAlbedoRT->format};

            VkComputePipelineCreateInfo computeDesc{};
            computeDesc.layout = pipelineLayout;
            computeDesc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            VkPipelineShaderStageCreateInfo shaderComputeStageCreateInfo{}; 
            shaderComputeStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderComputeStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            shaderComputeStageCreateInfo.module = shaderComputerFiltering->comp;
            shaderComputeStageCreateInfo.pName = "main";
            computeDesc.stage =  shaderComputeStageCreateInfo;
            
            VK_CHECK(vkCreateComputePipelines(device, nullptr, 1, &computeDesc, nullptr, &gTFilteringPipeline));       

        }

        // GBuffer Pipeline
        {
            
            VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
            depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilState.depthTestEnable = VK_TRUE;
            depthStencilState.depthWriteEnable = VK_TRUE;
            depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencilState.depthBoundsTestEnable = VK_FALSE;
            depthStencilState.stencilTestEnable = VK_FALSE;
            depthStencilState.minDepthBounds = 0.0f;
            depthStencilState.maxDepthBounds = 1.0f;
            
            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = nullptr;
            viewportState.scissorCount = 1;
            viewportState.pScissors = nullptr;

            // Vertex binding description
            VkVertexInputBindingDescription bindingDescription = {};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(VulkanglTFModel::Vertex); // 32 bytes (8 floats)
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attributeDescriptions[4] = {};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(VulkanglTFModel::Vertex, pos); // 0

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(VulkanglTFModel::Vertex, normal); // 12

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(VulkanglTFModel::Vertex, uv); // 24

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32_UINT;
            attributeDescriptions[3].offset = offsetof(VulkanglTFModel::Vertex, textureIndex);

            VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = 4;
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

            // Input assembly
            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState;
            inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyState.primitiveRestartEnable = VK_FALSE;
            inputAssemblyState.topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssemblyState.flags= 0;
            inputAssemblyState.pNext = NULL;
            
            // Initialize shader stages
            VkPipelineShaderStageCreateInfo shaderStageDesc[2]{};

            // Vertex stage
            shaderStageDesc[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageDesc[0].pNext = nullptr;
            shaderStageDesc[0].flags = 0;
            shaderStageDesc[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStageDesc[0].module = gBufferShader->vert;
            shaderStageDesc[0].pName = "main";
            shaderStageDesc[0].pSpecializationInfo = nullptr;

            // Fragment stage
            shaderStageDesc[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageDesc[1].pNext = nullptr;
            shaderStageDesc[1].flags = 0;
            shaderStageDesc[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStageDesc[1].module = gBufferShader->frag;
            shaderStageDesc[1].pName = "main";
            shaderStageDesc[1].pSpecializationInfo = nullptr;
            
            // dynamic rendering stage
            VkPipelineRenderingCreateInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 2;
            VkFormat colorsAttachments[2] = {gBufferNormalSpecularRT->format, gBufferAlbedoRT->format};

            renderingInfo.pColorAttachmentFormats = colorsAttachments;
            renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
            renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
                VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
                VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE};

            VkPipelineDynamicStateCreateInfo dynamicState = {};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 4; 
            dynamicState.pDynamicStates = dynamicStates;

            /*
                            typedef struct VkPipelineRasterizationStateCreateInfo {
                VkStructureType                            sType;
                const void*                                pNext;
                VkPipelineRasterizationStateCreateFlags    flags;
                VkBool32                                   depthClampEnable;
                VkBool32                                   rasterizerDiscardEnable;
                VkPolygonMode                              polygonMode;
                VkCullModeFlags                            cullMode;
                VkFrontFace                                frontFace;
                VkBool32                                   depthBiasEnable;
                float                                      depthBiasConstantFactor;
                float                                      depthBiasClamp;
                float                                      depthBiasSlopeFactor;
                float                                      lineWidth;
            } VkPipelineRasterizationStateCreateInfo;
            */

            VkPipelineColorBlendAttachmentState blendAttachments[2] = {};
            blendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[0].blendEnable = VK_FALSE;
            blendAttachments[1] = blendAttachments[0]; 

            VkPipelineColorBlendStateCreateInfo colorBlendState{};
            colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendState.attachmentCount = 2; 
            colorBlendState.pAttachments = blendAttachments;
            colorBlendState.logicOpEnable = VK_FALSE;


            VkGraphicsPipelineCreateInfo gBufferPipelineDesc{};
            gBufferPipelineDesc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gBufferPipelineDesc.pMultisampleState = &multisampleState;
            gBufferPipelineDesc.pNext = &renderingInfo;
            gBufferPipelineDesc.stageCount = 2;
            gBufferPipelineDesc.renderPass = VK_NULL_HANDLE;
            gBufferPipelineDesc.pStages = shaderStageDesc;
            gBufferPipelineDesc.pInputAssemblyState = &inputAssemblyState;
            gBufferPipelineDesc.pTessellationState = nullptr;
            gBufferPipelineDesc.pViewportState = &viewportState;
            gBufferPipelineDesc.pRasterizationState = &pRasterizationState;
            gBufferPipelineDesc.pDynamicState = &dynamicState;
            gBufferPipelineDesc.pDepthStencilState = &depthStencilState;
            gBufferPipelineDesc.pVertexInputState = &vertexInputInfo;
            gBufferPipelineDesc.layout = pipelineLayout;
            gBufferPipelineDesc.subpass =  0;
            gBufferPipelineDesc.pColorBlendState = &colorBlendState;
            gBufferPipelineDesc.flags = 0;

            VK_CHECK(vkCreateGraphicsPipelines(device, nullptr, 1, &gBufferPipelineDesc, nullptr, &gBufferPipeline));
        }

        // SSAO Pipleine Blur and Compute
        {

            // SSAO Generation
            {
                VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
                depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencilState.depthTestEnable = VK_FALSE;
                depthStencilState.depthWriteEnable = VK_FALSE;
                depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencilState.depthBoundsTestEnable = VK_FALSE;
                depthStencilState.stencilTestEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportState = {};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.pViewports = nullptr;
                viewportState.scissorCount = 1;
                viewportState.pScissors = nullptr;

                // Vertex binding description
                VkVertexInputBindingDescription bindingDescription = {};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(VulkanglTFModel::Vertex); // 32 bytes (8 floats)
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                // Input assembly
                VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
                inputAssemblyState.primitiveRestartEnable = VK_FALSE;
                inputAssemblyState.topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

                // Initialize shader stages
                VkPipelineShaderStageCreateInfo shaderStageDescSSAO[2]{};

                // Vertex stage
                shaderStageDescSSAO[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageDescSSAO[0].pNext = nullptr;
                shaderStageDescSSAO[0].flags = 0;
                shaderStageDescSSAO[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                shaderStageDescSSAO[0].module = ssaoShader->vert;
                shaderStageDescSSAO[0].pName = "main";
                shaderStageDescSSAO[0].pSpecializationInfo = nullptr;

                // Fragment stage
                shaderStageDescSSAO[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageDescSSAO[1].pNext = nullptr;
                shaderStageDescSSAO[1].flags = 0;
                shaderStageDescSSAO[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                shaderStageDescSSAO[1].module = ssaoShader->frag;
                shaderStageDescSSAO[1].pName = "main";
                shaderStageDescSSAO[1].pSpecializationInfo = nullptr;

                VkPipelineVertexInputStateCreateInfo emptyVI = {};
                emptyVI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

                // dynamic rendering stage
                VkPipelineRenderingCreateInfo renderingInfo = {};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &ssaoRT->format;

                VkDynamicState dynamicStates[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                    VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
                    VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
                    VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT};

                VkPipelineDynamicStateCreateInfo dynamicState = {};
                dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.dynamicStateCount = 5; 
                dynamicState.pDynamicStates = dynamicStates;

                VkPipelineColorBlendAttachmentState blendAttachments[1] = {};
                blendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
                blendAttachments[0].blendEnable = VK_FALSE;

                VkPipelineColorBlendStateCreateInfo colorBlendState{};
                colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlendState.attachmentCount = 1;
                colorBlendState.pAttachments = blendAttachments;
                colorBlendState.logicOpEnable = VK_FALSE;

                VkGraphicsPipelineCreateInfo gSSAOPipelineDesc{};
                gSSAOPipelineDesc.pMultisampleState = &multisampleState;
                gSSAOPipelineDesc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                
                gSSAOPipelineDesc.pNext = &renderingInfo;
                gSSAOPipelineDesc.stageCount = 2;
                gSSAOPipelineDesc.pStages = shaderStageDescSSAO;
                gSSAOPipelineDesc.pInputAssemblyState = &inputAssemblyState;
                gSSAOPipelineDesc.pTessellationState = nullptr;
                gSSAOPipelineDesc.pViewportState = &viewportState;
                gSSAOPipelineDesc.pDynamicState = &dynamicState;
                gSSAOPipelineDesc.pDepthStencilState = &depthStencilState;
                pRasterizationState.cullMode = VK_CULL_MODE_NONE; 
                gSSAOPipelineDesc.pRasterizationState = &pRasterizationState;
                gSSAOPipelineDesc.pVertexInputState = VK_NULL_HANDLE;
                gSSAOPipelineDesc.renderPass = VK_NULL_HANDLE;
                gSSAOPipelineDesc.layout = pipelineLayout;
                gSSAOPipelineDesc.pVertexInputState = &emptyVI;
                gSSAOPipelineDesc.flags = 0;
                gSSAOPipelineDesc.pColorBlendState = &colorBlendState;
                
                VK_CHECK(vkCreateGraphicsPipelines(device, nullptr, 1, &gSSAOPipelineDesc, nullptr, &ssaoPipeline));
            }

            // ssao blur pipeline
            {
                // Initialize shader stages
                VkPipelineShaderStageCreateInfo shaderStageDesc{};

                // Fragment stage
                shaderStageDesc.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shaderStageDesc.pNext = nullptr;
                shaderStageDesc.flags = 0;
                shaderStageDesc.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                shaderStageDesc.module = ssaoBlurShader->comp;
                shaderStageDesc.pName = "main";
                shaderStageDesc.pSpecializationInfo = nullptr;

                // dynamic rendering stage
                VkPipelineRenderingCreateInfo renderingInfo = {};
                renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachmentFormats = &ssaoRT->format;

                VkDynamicState dynamicStates[] = {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                    VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
                    VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT
                };

                VkPipelineDynamicStateCreateInfo dynamicState = {};
                dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicState.dynamicStateCount = 4; 
                dynamicState.pDynamicStates = dynamicStates;

                VkComputePipelineCreateInfo blurPipelineDesc{};
                blurPipelineDesc.layout = NULL; // 
                blurPipelineDesc.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                blurPipelineDesc.stage = shaderStageDesc;
                blurPipelineDesc.layout = pipelineLayout;
                blurPipelineDesc.flags = 0;
                blurPipelineDesc.pNext = nullptr;
                VK_CHECK(vkCreateComputePipelines(device, nullptr, 1, &blurPipelineDesc, nullptr, &ssaoBlurPipeline));
            }
        }

        {

            VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
            depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilState.depthTestEnable = VK_FALSE;
            depthStencilState.depthWriteEnable = VK_FALSE;
            depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencilState.depthBoundsTestEnable = VK_FALSE;
            depthStencilState.stencilTestEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = nullptr;
            viewportState.scissorCount = 1;
            viewportState.pScissors = nullptr;

            // Vertex binding description
            VkVertexInputBindingDescription bindingDescription = {};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(VulkanglTFModel::Vertex); // 32 bytes (8 floats)
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Input assembly
            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
            inputAssemblyState.primitiveRestartEnable = VK_FALSE;
            inputAssemblyState.topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

            // Initialize shader stages
            VkPipelineShaderStageCreateInfo shaderStageDesc[2]{};

            // Vertex stage
            shaderStageDesc[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageDesc[0].pNext = nullptr;
            shaderStageDesc[0].flags = 0;
            shaderStageDesc[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStageDesc[0].module = composeShader->vert;
            shaderStageDesc[0].pName = "main";
            shaderStageDesc[0].pSpecializationInfo = nullptr;

            // Fragment stage
            shaderStageDesc[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageDesc[1].pNext = nullptr;
            shaderStageDesc[1].flags = 0;
            shaderStageDesc[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStageDesc[1].module = composeShader->frag;
            shaderStageDesc[1].pName = "main";
            shaderStageDesc[1].pSpecializationInfo = nullptr;
            
            // dynamic rendering stage
            VkPipelineRenderingCreateInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats  = swapchainFormat; 

            VkDynamicState dynamicStates[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
                VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT,
            };

            VkPipelineDynamicStateCreateInfo dynamicState = {};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = 3;
            dynamicState.pDynamicStates = dynamicStates;

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;

            VkPipelineColorBlendAttachmentState blendAttachments[1] = {};
            blendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[0].blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlendState{};
            colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendState.attachmentCount = 1;
            colorBlendState.pAttachments = blendAttachments;
            colorBlendState.logicOpEnable = VK_FALSE;

            VkGraphicsPipelineCreateInfo gComposePipelineDesc{};
            gComposePipelineDesc.pMultisampleState = &multisampleState;
            gComposePipelineDesc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gComposePipelineDesc.pVertexInputState = & vertexInputInfo;
            gComposePipelineDesc.pNext = &renderingInfo;
            gComposePipelineDesc.stageCount = 2;
            gComposePipelineDesc.pStages = shaderStageDesc;
            gComposePipelineDesc.pInputAssemblyState = &inputAssemblyState;
            gComposePipelineDesc.pTessellationState = nullptr;
            gComposePipelineDesc.pViewportState = &viewportState;
            gComposePipelineDesc.pDynamicState = &dynamicState;
            gComposePipelineDesc.pDepthStencilState = &depthStencilState;
            gComposePipelineDesc.pRasterizationState = &pRasterizationState;
            gComposePipelineDesc.pColorBlendState = &colorBlendState;
            gComposePipelineDesc.renderPass = VK_NULL_HANDLE;
            gComposePipelineDesc.layout = pipelineLayout;
            gComposePipelineDesc.flags = 0;
            VK_CHECK(vkCreateGraphicsPipelines(device, nullptr, 1, &gComposePipelineDesc, nullptr, &composePipeline));
        }
    }
   
    VkPhysicalDeviceColorWriteEnableFeaturesEXT colorWriteFeature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT,
        .pNext = nullptr,
        .colorWriteEnable = VK_TRUE
    };

    void PickPhysicalDevice(VkInstance instance, VkPhysicalDevice &physicalDevice, VkSurfaceKHR surface)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        // feature chainig
        {
            descriptorIndexingFeature.pNext = &bufferDeviceAddressFeature;
            eds3.pNext = &descriptorIndexingFeature;
            colorWriteFeature.pNext = &vertexInputFeature;
        
        }
        // Pick the first suitable device (or rank them by score)
        for (const auto &device : devices)
        {
            VkPhysicalDeviceProperties deviceProperties;
          
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            deviceFeatures2.pNext = &colorWriteFeature;

            vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);
           
            
            indecies = FindQueueFamilies(device, surface);

            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
                
            for (const auto &extension : availableExtensions)
            {
                requiredExtensions.erase(extension.extensionName);
            }

            if (indecies.isComplete() && requiredExtensions.empty())
            {
                physicalDevice = device;
                return;
            }
        }

        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    void CreateLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice &device,
                                VkQueue &graphicsQueue, VkQueue &presentQueue,
                                VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices = FindQueueFamilies(physicalDevice, surface);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

       const std::vector<const char *> validationLayers = {
            "VK_LAYER_KHRONOS_validation"};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &DynamicRenderingFeature;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.pNext = &deviceFeatures2;
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // Validation layers (for older Vulkan implementations)
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create logical device!");
        }

        // Get queue handles
        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    }

    // Helper function to find queue families
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto &queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (presentSupport)
            {
                indices.presentFamily = i;
            }

            if (indices.isComplete())
            {
                break;
            }

            i++;
        }

        return indices;
    }

    void VulkanInit()
    {
        VkApplicationInfo appInfo{};
        // Debug messenger
        VkDebugUtilsMessengerEXT debugMessenger;
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        // Check validation layer support
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        // Setup application info
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Vulkan Application";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;
        appInfo.pNext = NULL;
        // Get required extensions
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        extensions = std::vector<const char *>(glfwExtensions, glfwExtensions + glfwExtensionCount);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        debugCreateInfo.pUserData = nullptr;

        {
            VkInstanceCreateInfo createInfo{};
            // Create instance
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
            createInfo.pNext = &debugCreateInfo;

            if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan instance!");
            }
        }

        // Create debug messenger
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(instance, &debugCreateInfo, nullptr, &debugMessenger);
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {

        std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }

    void UpdateCascades(
        const Camera& camera,
        const glm::vec3& lightDir,
        ShadowCascadeData *cascades,
        uint32_t cascadeCount)
    {
        cascadeCount = std::min<uint32_t>(cascadeCount, MAX_SHADOW_CASCADES);

        // 1. Choose split depths
        float nearPlane = camera.nearPlane;
        float farPlane  = camera.farPlane;

        for (uint32_t i = 0; i < cascadeCount; ++i)
        {
   
            float p = float(i + 1) / float(cascadeCount);
            cascades[i].splitDepth = nearPlane + (farPlane - nearPlane) * p;

            // 2. Get frustum corners for this split range
            glm::vec3 corners[8];
            camera.getFrustumCornersWorldSpace(
                (i == 0) ? nearPlane : cascades[i - 1].splitDepth,
                cascades[i].splitDepth,
                corners);

            // 3) Create light view matrix
            glm::vec3 center(0.0f);
            for (int c = 0; c < 8; ++c)
                center += corners[c];
            center /= 8.0f;

            glm::vec3 lightPos = center - lightDir * 100.0f;
            glm::mat4 lightView = glm::lookAt(
                lightPos,
                center,
                glm::vec3(0.0f, 1.0f, 0.0f));

            // 4. Fit orthographic projection to the cascade
            float minX =  FLT_MAX, minY =  FLT_MAX, minZ =  FLT_MAX;
            float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

            for (int c = 0; c < 8; ++c)
            {
                glm::vec4 tr = lightView * glm::vec4(corners[c], 1.0f);
                minX = std::min(minX, tr.x); maxX = std::max(maxX, tr.x);
                minY = std::min(minY, tr.y); maxY = std::max(maxY, tr.y);
                minZ = std::min(minZ, tr.z); maxZ = std::max(maxZ, tr.z);
            }

            glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, -maxZ - 50.0f, -minZ);
            cascades[i].viewProj = lightProj * lightView;
        
        }
    }

    void UpdateShadowGenerationData(bool shadowRenderTargetsReady)
    {
        const uint32_t cascadeCount = MAX_SHADOW_CASCADES;

        ShadowGenUniformData.lightDirection = glm::vec4(SkyLightUniformData.lightDirection, 0.0f);
        ShadowGenUniformData.shadowMapSizeCascadeCount = glm::uvec4(
            SHADOW_MAP_SIZE,
            SHADOW_MAP_SIZE,
            cascadeCount,
            shadowRenderTargetsReady ? 1u : 0u);
        ShadowGenUniformData.bias = glm::vec4(0.0005f, 0.0f, 0.0f, 0.0f);

        UpdateCascades(camera, SkyLightUniformData.lightDirection, ShadowGenUniformData.cascades, cascadeCount);
    }

    void InitSkyLight() 
    {
   
        setupUBO();
   
        SkyLightUniformData.lightColor          = glm::vec3(255, 255, 255);
        SkyLightUniformData.lightDirection      = normalize(glm::vec3(-1,-1,-1));
        SkyLightUniformData.lightIntensity      = 40; 
        
        // Cascade shadow mapping implementation of the rendering algorithm representing differenty diferent distance from the camera to have diferent quality of the shadow at each distance

        UpdateShadowGenerationData(false);
        
    }
  

    void Init()
    {
    
        window = CreateWindow(800, 600, "main");
        gAppSettings = new AppSettings(); 
        gAppSettings->height = 600;
        gAppSettings->width= 800;

        VulkanInit();
 
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface!");
        }
        
        PickPhysicalDevice(instance, physicalDevice, surface);

        CreateLogicalDevice(physicalDevice, device, graphicsQueue, presentQueue, surface);
       
        LoadExtensionFunctions();
        
        CreateCommandPool();

        CreateCommandBuffer();

        tracyGpuContext = TracyVkContext(physicalDevice, device, graphicsQueue, commandBuffers[0]);

        addSamplers();

        InitSkyLight();

        addBuffers();

        LoadSponza();

        addTextures();
    
        addDescriptorSets();

        addRenderTargets();
       
        addShaders();

        addPipelines();

        addSwapChain();

        prepareDescriptorSetPerSistent();
        prepareDescriptorSetPerFrame();
        
        for (uint32_t i = 0; i < COMMAND_BUFFER_COUNT; ++i)
        {
            addFence(commandBufferFences[i]);
            addSemaphore(frameSemaphores[i]);
        }
    
    };
   
    void LoadSponza()
    {

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;
        std::string filename = "../external/sponza/glTF/Sponza.gltf";

        bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);

        if (!warn.empty())
        {
            printf("Warn: %s\n", warn.c_str());
        }

        if (!err.empty())
        {
            printf("Err: %s\n", err.c_str());
        }

        if (!ret)
        {
            printf("Failed to parse glTF: %s\n", filename.c_str());
            throw std::runtime_error("Failed to parse glTF: " + filename);
        }

        model_gltf.nodes.reserve(model.nodes.size());
        //vertex and index buffer
        std::vector<uint32_t> indexBuffer;
        std::vector<VulkanglTFModel::Vertex> vertexBuffer;

        model_gltf.loadMaterials(model);
        model_gltf.loadTextures(model);
        model_gltf.loadImages(model);

        for (uint32_t cur_node = 0; cur_node < model.nodes.size(); cur_node++)
        {
            model_gltf.loadNode(
            
                model.nodes[cur_node], // const tinygltf::Node&
                model,                 // const tinygltf::Model&
                nullptr,               // Node* parent  (null = root level)
                indexBuffer,
                vertexBuffer

           
            );
        }

        model_gltf.indextotalCount = indexBuffer.size();
        model_gltf.vertextotalCount = vertexBuffer.size();

        size_t index_count = indexBuffer.size();
        size_t vertex_count = vertexBuffer.size();

        std::vector<unsigned int> remap(index_count);
        
        size_t unique_vertex_count = meshopt_generateVertexRemap(
            remap.data(), indexBuffer.data(), index_count,
            vertexBuffer.data(), vertex_count, sizeof(VulkanglTFModel::Vertex));

        std::vector<unsigned int> new_indices(index_count);
        meshopt_remapIndexBuffer(new_indices.data(), indexBuffer.data(), index_count, remap.data());

        std::vector<VulkanglTFModel::Vertex> new_vertices(unique_vertex_count);
        meshopt_remapVertexBuffer(new_vertices.data(), vertexBuffer.data(), vertex_count,
                                  sizeof(VulkanglTFModel::Vertex), remap.data());

        indexBuffer  = std::move(new_indices);
        vertexBuffer = std::move(new_vertices);
        vertex_count = unique_vertex_count;
        model_gltf.vertextotalCount = static_cast<uint32_t>(vertex_count);

        {
            // vertex buffer
            {
                bufferDesc sponzaVertexBufDesc = {};
                sponzaVertexBufDesc.size = sizeof(VulkanglTFModel::Vertex) * vertexBuffer.size();
                sponzaVertexBufDesc.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                sponzaVertexBufDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                sponzaVertexBufDesc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

                addBuffer(device, physicalDevice, sponzaVertexBufDesc, &model_gltf.vertexBufferVk);

                mapBuffer(device, model_gltf.vertexBufferVk, vertexBuffer.data());
            
            }
        
            // index buffer 
            {
                bufferDesc sponzaIndexBuffer = {};

                sponzaIndexBuffer.size = sizeof(uint32_t) * indexBuffer.size();
                sponzaIndexBuffer.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
                sponzaIndexBuffer.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                sponzaIndexBuffer.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

                addBuffer(device, physicalDevice, sponzaIndexBuffer, &model_gltf.indexBufferVk);

                mapBuffer(device, model_gltf.indexBufferVk, indexBuffer.data());
            }
       
            // filtered index buffer
            {
                bufferDesc filteredIndexBuffer = {};
                filteredIndexBuffer.size = sizeof(uint32_t) * indexBuffer.size();
                filteredIndexBuffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                filteredIndexBuffer.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                filteredIndexBuffer.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

                addBuffer(device, physicalDevice, filteredIndexBuffer, &gTFilteredTriangles);
            }
            // filtered draw command
            {
                bufferDesc filteredDrawCommand = {};
                filteredDrawCommand.size = sizeof(VkDrawIndexedIndirectCommand);
                filteredDrawCommand.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                filteredDrawCommand.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
                filteredDrawCommand.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                addBuffer(device, physicalDevice, filteredDrawCommand, &gTFilterIndirectArgs);
            }
        }

        vertexBufferAddress = 0;
    }

};

int main(int argc, char const *argv[])
{

    vkApp app;
    app.Init();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        app.Draw();
    }
        TracyVkDestroy(tracyGpuContext);

    return 0;
}
