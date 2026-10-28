#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#define WIDTH 1280
#define HEIGHT 800
#define MAX_FRAMES 2

typedef struct { float time, aspect, cameraX, cameraY, cameraZ, yaw, pitch, moving, plantMode; } Push;
typedef struct {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR *formats; uint32_t formatCount;
    VkPresentModeKHR *modes; uint32_t modeCount;
} SwapSupport;

static GLFWwindow *window;
static VkInstance instance;
static VkSurfaceKHR surface;
static VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
static VkDevice device;
static VkQueue graphicsQueue, presentQueue;
static uint32_t graphicsFamily, presentFamily;
static VkSwapchainKHR swapchain;
static VkFormat swapFormat;
static VkExtent2D swapExtent;
static VkImage *swapImages;
static VkImageView *imageViews;
static VkImage *depthImages;
static VkDeviceMemory *depthMemories;
static VkImageView *depthViews;
static uint32_t imageCount;
static VkRenderPass renderPass;
static VkPipelineLayout pipelineLayout;
static VkPipeline pipeline;
static VkPipeline plantPipeline;
static VkFramebuffer *framebuffers;
static VkCommandPool commandPool;
static VkCommandBuffer commandBuffers[MAX_FRAMES];
static VkSemaphore imageAvailable[MAX_FRAMES], renderFinished[MAX_FRAMES];
static VkFence inFlight[MAX_FRAMES];
static uint32_t currentFrame = 0;
static int framebufferResized = 0;
static float cameraX = 0.0f, cameraZ = 4.0f;
static float cameraYaw = 0.0f, cameraPitch = -0.015f;
static double lastMouseX, lastMouseY;
static int firstMouse = 1;
static int cameraMoving;
static int fullscreenKeyWasDown;
static int jumpKeyWasDown;
static float jumpHeight;
static float jumpVelocity;
static int windowedX, windowedY, windowedWidth = WIDTH, windowedHeight = HEIGHT;

#define GRASS_GRID_SIZE 240
#define BLADES_PER_CELL 4
#define GRASS_VERTEX_COUNT (GRASS_GRID_SIZE * GRASS_GRID_SIZE * BLADES_PER_CELL * 3)
#define FAR_GRASS_GRID_SIZE 384
#define FAR_BLADES_PER_CELL 2
#define FAR_GRASS_VERTEX_COUNT (FAR_GRASS_GRID_SIZE * FAR_GRASS_GRID_SIZE * FAR_BLADES_PER_CELL * 3)
#define FLOWER_GRID_SIZE 88
#define FLOWER_VERTICES_PER_FLOWER 330
#define FLOWER_VERTEX_COUNT (FLOWER_GRID_SIZE * FLOWER_GRID_SIZE * FLOWER_VERTICES_PER_FLOWER)

static float terrainHeight(float x, float z) {
    float height = sinf(x * 0.075f + sinf(z * 0.031f) * 0.8f) * 2.6f;
    height += sinf(z * 0.052f + x * 0.027f) * 1.7f;
    height += sinf(x * 0.19f + sinf(z * 0.11f)) * 0.42f;
    height += cosf(z * 0.16f - x * 0.06f) * 0.3f;
    height += -3.0f * expf(-powf((x + 1.5f) / 8.5f, 2.0f));
    float ridges = 0.5f + 0.5f * sinf(x * 0.018f + sinf(z * 0.009f) * 1.7f +
                                      sinf(x * 0.041f + z * 0.013f) * 0.42f + cosf(z * 0.027f - x * 0.011f) * 0.3f);
    float nearFade = fmaxf(0.0f, fminf(1.0f, (-z - 110.0f) / 120.0f));
    nearFade = nearFade * nearFade * (3.0f - 2.0f * nearFade);
    float farFade = fmaxf(0.0f, fminf(1.0f, (-z - 390.0f) / 120.0f));
    farFade = farFade * farFade * (3.0f - 2.0f * farFade);
    height += nearFade * (1.0f - farFade) * (4.0f + powf(ridges, 1.35f) * 22.0f);
    return height;
}

static void onMouseMove(GLFWwindow *w, double x, double y) {
    (void)w;
    if (firstMouse) { lastMouseX = x; lastMouseY = y; firstMouse = 0; return; }
    cameraYaw += (float)(x - lastMouseX) * 0.0025f;
    cameraPitch -= (float)(y - lastMouseY) * 0.0025f;
    if (cameraPitch > 1.25f) cameraPitch = 1.25f;
    if (cameraPitch < -1.25f) cameraPitch = -1.25f;
    lastMouseX = x; lastMouseY = y;
}

static void updateCamera(float deltaSeconds) {
    const float forwardX=sinf(cameraYaw), forwardZ=-cosf(cameraYaw), rightX=cosf(cameraYaw), rightZ=sinf(cameraYaw);
    float moveX=0.0f, moveZ=0.0f;
    if(glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS){moveX+=forwardX;moveZ+=forwardZ;}
    if(glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS){moveX-=forwardX;moveZ-=forwardZ;}
    if(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS){moveX+=rightX;moveZ+=rightZ;}
    if(glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS){moveX-=rightX;moveZ-=rightZ;}
    float moveLength=sqrtf(moveX*moveX+moveZ*moveZ);
    cameraMoving=moveLength>0.0f;
    if(cameraMoving){float speed=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS?8.0f:3.2f;float step=speed*deltaSeconds;cameraX+=moveX/moveLength*step;cameraZ+=moveZ/moveLength*step;}
    int jumpKeyDown=glfwGetKey(window,GLFW_KEY_SPACE)==GLFW_PRESS;
    if(jumpKeyDown&&!jumpKeyWasDown&&jumpHeight<=0.001f)jumpVelocity=5.6f;
    jumpKeyWasDown=jumpKeyDown;
    jumpVelocity-=14.0f*deltaSeconds;
    jumpHeight+=jumpVelocity*deltaSeconds;
    if(jumpHeight<0.0f){jumpHeight=0.0f;jumpVelocity=0.0f;}
}

static void fail(const char *msg) { fprintf(stderr, "ERROR: %s\n", msg); exit(EXIT_FAILURE); }
static void check(VkResult r, const char *msg) { if (r != VK_SUCCESS) { fprintf(stderr, "%s (VkResult %d)\n", msg, (int)r); exit(EXIT_FAILURE); } }
static void onResize(GLFWwindow *w, int width, int height) { (void)w; (void)width; (void)height; framebufferResized = 1; }

static void toggleFullscreen(GLFWwindow *w) {
    GLFWmonitor *monitor = glfwGetWindowMonitor(w);
    if (monitor) {
        glfwSetWindowMonitor(w, NULL, windowedX, windowedY, windowedWidth, windowedHeight, GLFW_DONT_CARE);
        glfwSetWindowTitle(w,"Vulkan Meadow | windowed | F fullscreen");
    } else {
        glfwGetWindowPos(w, &windowedX, &windowedY);
        glfwGetWindowSize(w, &windowedWidth, &windowedHeight);
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (!mode) return;
        glfwSetWindowMonitor(w, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        glfwSetWindowTitle(w,"Vulkan Meadow | fullscreen | F windowed");
    }
    firstMouse = 1;
    framebufferResized = 1;
}

static void onKey(GLFWwindow *w, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
}

static uint32_t readSpv(const char *path, char **data) {
    FILE *f = fopen(path, "rb"); if (!f) { fprintf(stderr, "Cannot open shader: %s\n", path); exit(EXIT_FAILURE); }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    if (n <= 0 || n % 4 != 0) fail("Invalid SPIR-V file");
    *data = (char*)malloc((size_t)n); if (!*data) fail("Out of memory reading shader");
    if (fread(*data, 1, (size_t)n, f) != (size_t)n) fail("Could not read shader");
    fclose(f); return (uint32_t)n;
}
static VkShaderModule makeShader(const char *path) {
    char *bytes; uint32_t size = readSpv(path, &bytes);
    VkShaderModuleCreateInfo ci = { .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize=size, .pCode=(const uint32_t*)bytes };
    VkShaderModule m; check(vkCreateShaderModule(device, &ci, NULL, &m), "Create shader module failed"); free(bytes); return m;
}
static int hasDeviceExtension(VkPhysicalDevice d, const char *name) {
    uint32_t n=0; vkEnumerateDeviceExtensionProperties(d,NULL,&n,NULL);
    VkExtensionProperties *p=(VkExtensionProperties*)malloc(sizeof(*p)*n); vkEnumerateDeviceExtensionProperties(d,NULL,&n,p);
    int found=0; for(uint32_t i=0;i<n;i++) if(strcmp(p[i].extensionName,name)==0) found=1; free(p); return found;
}
static int queueFamilies(VkPhysicalDevice d, uint32_t *g, uint32_t *p) {
    uint32_t n=0; vkGetPhysicalDeviceQueueFamilyProperties(d,&n,NULL);
    VkQueueFamilyProperties *q=(VkQueueFamilyProperties*)malloc(sizeof(*q)*n); vkGetPhysicalDeviceQueueFamilyProperties(d,&n,q);
    int gf=0,pf=0;
    for(uint32_t i=0;i<n;i++) { if(q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT){*g=i;gf=1;} VkBool32 supported=VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d,i,surface,&supported); if(supported){*p=i;pf=1;} }
    free(q); return gf&&pf;
}
static int swapAdequate(VkPhysicalDevice d) {
    uint32_t f=0,m=0; vkGetPhysicalDeviceSurfaceFormatsKHR(d,surface,&f,NULL); vkGetPhysicalDeviceSurfacePresentModesKHR(d,surface,&m,NULL); return f>0&&m>0;
}
static void chooseDevice(void) {
    uint32_t n=0; vkEnumeratePhysicalDevices(instance,&n,NULL); if(!n) fail("No Vulkan GPU found");
    VkPhysicalDevice *ds=(VkPhysicalDevice*)malloc(sizeof(*ds)*n); vkEnumeratePhysicalDevices(instance,&n,ds);
    for(uint32_t i=0;i<n;i++) if(hasDeviceExtension(ds[i],VK_KHR_SWAPCHAIN_EXTENSION_NAME) && queueFamilies(ds[i],&graphicsFamily,&presentFamily) && swapAdequate(ds[i])) { physicalDevice=ds[i]; break; }
    free(ds); if(physicalDevice==VK_NULL_HANDLE) fail("No suitable Vulkan device (update graphics driver / enable Vulkan)");
}
static SwapSupport querySwap(void) {
    SwapSupport s={0}; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,surface,&s.caps);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,surface,&s.formatCount,NULL); s.formats=(VkSurfaceFormatKHR*)malloc(sizeof(*s.formats)*s.formatCount); vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice,surface,&s.formatCount,s.formats);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,surface,&s.modeCount,NULL); s.modes=(VkPresentModeKHR*)malloc(sizeof(*s.modes)*s.modeCount); vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice,surface,&s.modeCount,s.modes); return s;
}
static VkSurfaceFormatKHR chooseFormat(SwapSupport *s) {
    for(uint32_t i=0;i<s->formatCount;i++) if(s->formats[i].format==VK_FORMAT_B8G8R8A8_SRGB && s->formats[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return s->formats[i];
    return s->formats[0];
}
static VkPresentModeKHR choosePresent(SwapSupport *s) { for(uint32_t i=0;i<s->modeCount;i++) if(s->modes[i]==VK_PRESENT_MODE_MAILBOX_KHR) return s->modes[i]; return VK_PRESENT_MODE_FIFO_KHR; }
static VkExtent2D chooseExtent(SwapSupport *s) {
    if(s->caps.currentExtent.width!=UINT32_MAX) return s->caps.currentExtent;
    int w,h; glfwGetFramebufferSize(window,&w,&h); VkExtent2D e={(uint32_t)w,(uint32_t)h};
    if(e.width<s->caps.minImageExtent.width)e.width=s->caps.minImageExtent.width; if(e.width>s->caps.maxImageExtent.width)e.width=s->caps.maxImageExtent.width;
    if(e.height<s->caps.minImageExtent.height)e.height=s->caps.minImageExtent.height; if(e.height>s->caps.maxImageExtent.height)e.height=s->caps.maxImageExtent.height; return e;
}
static void createSwapchain(void) {
    SwapSupport s=querySwap(); VkSurfaceFormatKHR fmt=chooseFormat(&s); VkPresentModeKHR mode=choosePresent(&s); VkExtent2D ext=chooseExtent(&s);
    uint32_t count=s.caps.minImageCount+1; if(s.caps.maxImageCount && count>s.caps.maxImageCount) count=s.caps.maxImageCount;
    VkSwapchainCreateInfoKHR ci={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,.minImageCount=count,.imageFormat=fmt.format,.imageColorSpace=fmt.colorSpace,.imageExtent=ext,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,.preTransform=s.caps.currentTransform,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=mode,.clipped=VK_TRUE,.oldSwapchain=VK_NULL_HANDLE};
    uint32_t indices[]={graphicsFamily,presentFamily}; if(graphicsFamily!=presentFamily){ci.imageSharingMode=VK_SHARING_MODE_CONCURRENT;ci.queueFamilyIndexCount=2;ci.pQueueFamilyIndices=indices;} else ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateSwapchainKHR(device,&ci,NULL,&swapchain),"Create swapchain failed"); swapFormat=fmt.format; swapExtent=ext;
    vkGetSwapchainImagesKHR(device,swapchain,&imageCount,NULL); swapImages=(VkImage*)malloc(sizeof(*swapImages)*imageCount); vkGetSwapchainImagesKHR(device,swapchain,&imageCount,swapImages);
    imageViews=(VkImageView*)malloc(sizeof(*imageViews)*imageCount);
    for(uint32_t i=0;i<imageCount;i++){ VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=swapImages[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=swapFormat,.components={VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY},.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}}; check(vkCreateImageView(device,&vi,NULL,&imageViews[i]),"Create image view failed"); }
    free(s.formats);free(s.modes);
}
static uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    fail("No compatible Vulkan memory type");
}
static void createDepthResources(void) {
    depthImages = (VkImage*)calloc(imageCount, sizeof(*depthImages));
    depthMemories = (VkDeviceMemory*)calloc(imageCount, sizeof(*depthMemories));
    depthViews = (VkImageView*)calloc(imageCount, sizeof(*depthViews));
    if (!depthImages || !depthMemories || !depthViews) fail("Out of memory allocating depth resources");
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = {swapExtent.width, swapExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        check(vkCreateImage(device, &imageInfo, NULL, &depthImages[i]), "Create depth image failed");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, depthImages[i], &requirements);
        VkMemoryAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        check(vkAllocateMemory(device, &allocation, NULL, &depthMemories[i]), "Allocate depth image memory failed");
        check(vkBindImageMemory(device, depthImages[i], depthMemories[i], 0), "Bind depth image memory failed");
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
        };
        check(vkCreateImageView(device, &viewInfo, NULL, &depthViews[i]), "Create depth image view failed");
    }
}
static void createRenderPass(void) {
    VkAttachmentDescription attachments[2]={
        {.format=swapFormat,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
        {.format=VK_FORMAT_D32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference ref={.attachment=0,.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef={.attachment=1,.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref,.pDepthStencilAttachment=&depthRef};
    VkSubpassDependency dep={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo ci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=2,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dep}; check(vkCreateRenderPass(device,&ci,NULL,&renderPass),"Create render pass failed");
}
static void createPipeline(void) {
    VkShaderModule vs=makeShader(MEADOW_SHADER_DIR "/meadow.vert.spv"); VkShaderModule fs=makeShader(MEADOW_SHADER_DIR "/meadow.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,.primitiveRestartEnable=VK_FALSE};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.depthClampEnable=VK_FALSE,.rasterizerDiscardEnable=VK_FALSE,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.depthBiasEnable=VK_FALSE,.lineWidth=1.0f};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,.blendEnable=VK_FALSE};
    VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.logicOpEnable=VK_FALSE,.attachmentCount=1,.pAttachments=&ba};
    VkPushConstantRange pc={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,.offset=0,.size=sizeof(Push)};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.pushConstantRangeCount=1,.pPushConstantRanges=&pc}; check(vkCreatePipelineLayout(device,&li,NULL,&pipelineLayout),"Create pipeline layout failed");
    VkDynamicState dyns[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dy={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=dyns};
    VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_ALWAYS,.depthBoundsTestEnable=VK_FALSE,.stencilTestEnable=VK_FALSE};
    VkGraphicsPipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&depth,.pColorBlendState=&bs,.pDynamicState=&dy,.layout=pipelineLayout,.renderPass=renderPass,.subpass=0};
    check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&pipeline),"Create background pipeline failed");
    vkDestroyShaderModule(device,vs,NULL); vkDestroyShaderModule(device,fs,NULL);
    vs=makeShader(MEADOW_SHADER_DIR "/meadow_plant.vert.spv"); fs=makeShader(MEADOW_SHADER_DIR "/meadow_plant.frag.spv");
    stages[0].module=vs; stages[1].module=fs; depth.depthCompareOp=VK_COMPARE_OP_LESS;
    check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&plantPipeline),"Create plant pipeline failed");
    vkDestroyShaderModule(device,vs,NULL); vkDestroyShaderModule(device,fs,NULL);
}
static void createFramebuffers(void) {
    framebuffers=(VkFramebuffer*)malloc(sizeof(*framebuffers)*imageCount);
    for(uint32_t i=0;i<imageCount;i++){ VkImageView attachments[]={imageViews[i],depthViews[i]}; VkFramebufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=renderPass,.attachmentCount=2,.pAttachments=attachments,.width=swapExtent.width,.height=swapExtent.height,.layers=1}; check(vkCreateFramebuffer(device,&ci,NULL,&framebuffers[i]),"Create framebuffer failed"); }
}
static void destroySwapchainResources(void) {
    if(framebuffers){for(uint32_t i=0;i<imageCount;i++)vkDestroyFramebuffer(device,framebuffers[i],NULL);free(framebuffers);framebuffers=NULL;}
    if(pipeline){vkDestroyPipeline(device,pipeline,NULL);pipeline=VK_NULL_HANDLE;}
    if(plantPipeline){vkDestroyPipeline(device,plantPipeline,NULL);plantPipeline=VK_NULL_HANDLE;}
    if(pipelineLayout){vkDestroyPipelineLayout(device,pipelineLayout,NULL);pipelineLayout=VK_NULL_HANDLE;}
    if(renderPass){vkDestroyRenderPass(device,renderPass,NULL);renderPass=VK_NULL_HANDLE;}
    if(depthViews){for(uint32_t i=0;i<imageCount;i++)if(depthViews[i])vkDestroyImageView(device,depthViews[i],NULL);free(depthViews);depthViews=NULL;}
    if(depthImages){for(uint32_t i=0;i<imageCount;i++)if(depthImages[i])vkDestroyImage(device,depthImages[i],NULL);free(depthImages);depthImages=NULL;}
    if(depthMemories){for(uint32_t i=0;i<imageCount;i++)if(depthMemories[i])vkFreeMemory(device,depthMemories[i],NULL);free(depthMemories);depthMemories=NULL;}
    if(imageViews){for(uint32_t i=0;i<imageCount;i++)if(imageViews[i])vkDestroyImageView(device,imageViews[i],NULL);free(imageViews);imageViews=NULL;}
    if(swapImages){free(swapImages);swapImages=NULL;}
    if(swapchain){vkDestroySwapchainKHR(device,swapchain,NULL);swapchain=VK_NULL_HANDLE;}
}
static void recreateSwapchain(void) {
    int width=0,height=0;
    glfwGetFramebufferSize(window,&width,&height);
    while(width==0||height==0){glfwWaitEvents();glfwGetFramebufferSize(window,&width,&height);}
    vkDeviceWaitIdle(device);
    destroySwapchainResources();
    createSwapchain();createDepthResources();createRenderPass();createPipeline();createFramebuffers();
    framebufferResized=0;
    firstMouse=1;
}
static void createCommands(void) {
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=graphicsFamily}; check(vkCreateCommandPool(device,&pi,NULL,&commandPool),"Create command pool failed");
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commandPool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=MAX_FRAMES}; check(vkAllocateCommandBuffers(device,&ai,commandBuffers),"Allocate command buffers failed");
    for(int i=0;i<MAX_FRAMES;i++){ VkSemaphoreCreateInfo si={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT}; check(vkCreateSemaphore(device,&si,NULL,&imageAvailable[i]),"Create semaphore failed");check(vkCreateSemaphore(device,&si,NULL,&renderFinished[i]),"Create semaphore failed");check(vkCreateFence(device,&fi,NULL,&inFlight[i]),"Create fence failed"); }
}
static void recordCommand(VkCommandBuffer cb,uint32_t image) {
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(cb,&bi),"Begin command buffer failed");
    VkClearValue clears[2]={ {.color={{0.5f,0.7f,0.9f,1.0f}}}, {.depthStencil={1.0f,0}} }; VkRenderPassBeginInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=renderPass,.framebuffer=framebuffers[image],.renderArea={{0,0},swapExtent},.clearValueCount=2,.pClearValues=clears};
    vkCmdBeginRenderPass(cb,&ri,VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    VkViewport viewport={0.0f,0.0f,(float)swapExtent.width,(float)swapExtent.height,0.0f,1.0f}; VkRect2D scissor={{0,0},swapExtent}; vkCmdSetViewport(cb,0,1,&viewport); vkCmdSetScissor(cb,0,1,&scissor);
    double t=glfwGetTime(); float ground=terrainHeight(cameraX,cameraZ); Push p={(float)t,(float)swapExtent.width/(float)swapExtent.height,cameraX,ground+1.68f+jumpHeight+(cameraMoving?(float)sin(t*10.0)*0.035f:0.0f),cameraZ,cameraYaw,cameraPitch,(float)cameraMoving,0.0f};
    vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(p),&p);
    vkCmdDraw(cb,3,1,0,0);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,plantPipeline);
    p.plantMode=0.0f; vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(p),&p);
    vkCmdDraw(cb,GRASS_VERTEX_COUNT,1,0,0);
    p.plantMode=0.5f; vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(p),&p);
    vkCmdDraw(cb,FAR_GRASS_VERTEX_COUNT,1,0,0);
    p.plantMode=1.0f; vkCmdPushConstants(cb,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(p),&p);
    vkCmdDraw(cb,FLOWER_VERTEX_COUNT,1,0,0);
    vkCmdEndRenderPass(cb); check(vkEndCommandBuffer(cb),"End command buffer failed");
}
static void drawFrame(void) {
    vkWaitForFences(device,1,&inFlight[currentFrame],VK_TRUE,UINT64_MAX);
    uint32_t image; VkResult r=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,imageAvailable[currentFrame],VK_NULL_HANDLE,&image);
    if(r==VK_ERROR_OUT_OF_DATE_KHR){framebufferResized=1;return;} if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR)check(r,"Acquire swapchain image failed");
    vkResetFences(device,1,&inFlight[currentFrame]); vkResetCommandBuffer(commandBuffers[currentFrame],0); recordCommand(commandBuffers[currentFrame],image);
    VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&imageAvailable[currentFrame],.pWaitDstStageMask=&waitStage,.commandBufferCount=1,.pCommandBuffers=&commandBuffers[currentFrame],.signalSemaphoreCount=1,.pSignalSemaphores=&renderFinished[currentFrame]}; check(vkQueueSubmit(graphicsQueue,1,&si,inFlight[currentFrame]),"Submit draw failed");
    VkPresentInfoKHR pi={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&renderFinished[currentFrame],.swapchainCount=1,.pSwapchains=&swapchain,.pImageIndices=&image}; r=vkQueuePresentKHR(presentQueue,&pi);
    if(r==VK_ERROR_OUT_OF_DATE_KHR||r==VK_SUBOPTIMAL_KHR||framebufferResized)framebufferResized=1;else check(r,"Present failed"); currentFrame=(currentFrame+1)%MAX_FRAMES;
}
static void cleanup(void) {
    if(device) vkDeviceWaitIdle(device);
    for(int i=0;i<MAX_FRAMES;i++){if(inFlight[i])vkDestroyFence(device,inFlight[i],NULL);if(imageAvailable[i])vkDestroySemaphore(device,imageAvailable[i],NULL);if(renderFinished[i])vkDestroySemaphore(device,renderFinished[i],NULL);}
    if(commandPool)vkDestroyCommandPool(device,commandPool,NULL);
    if(framebuffers){for(uint32_t i=0;i<imageCount;i++)vkDestroyFramebuffer(device,framebuffers[i],NULL);free(framebuffers);}
    if(pipeline)vkDestroyPipeline(device,pipeline,NULL);if(plantPipeline)vkDestroyPipeline(device,plantPipeline,NULL);if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,NULL);if(renderPass)vkDestroyRenderPass(device,renderPass,NULL);
    if(depthViews){for(uint32_t i=0;i<imageCount;i++)if(depthViews[i])vkDestroyImageView(device,depthViews[i],NULL);free(depthViews);}
    if(depthImages){for(uint32_t i=0;i<imageCount;i++)if(depthImages[i])vkDestroyImage(device,depthImages[i],NULL);free(depthImages);}
    if(depthMemories){for(uint32_t i=0;i<imageCount;i++)if(depthMemories[i])vkFreeMemory(device,depthMemories[i],NULL);free(depthMemories);}
    if(imageViews){for(uint32_t i=0;i<imageCount;i++)vkDestroyImageView(device,imageViews[i],NULL);free(imageViews);} if(swapImages)free(swapImages);if(swapchain)vkDestroySwapchainKHR(device,swapchain,NULL);
    if(device)vkDestroyDevice(device,NULL);if(surface)vkDestroySurfaceKHR(instance,surface,NULL);if(instance)vkDestroyInstance(instance,NULL);if(window)glfwDestroyWindow(window);glfwTerminate();
}
int main(void) {
    if(!glfwInit()) fail("GLFW initialization failed"); glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_RESIZABLE,GLFW_TRUE);
    window=glfwCreateWindow(WIDTH,HEIGHT,"Vulkan Meadow | WASD move, Shift run, mouse look, F fullscreen",NULL,NULL);if(!window)fail("Could not create GLFW window");glfwSetFramebufferSizeCallback(window,onResize);glfwSetCursorPosCallback(window,onMouseMove);glfwSetKeyCallback(window,onKey);glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
    uint32_t extCount=0;const char **ext=glfwGetRequiredInstanceExtensions(&extCount);if(!ext)fail("GLFW could not provide Vulkan instance extensions");
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Vulkan Meadow",.applicationVersion=VK_MAKE_VERSION(1,0,0),.pEngineName="Meadow procedural renderer",.engineVersion=VK_MAKE_VERSION(1,0,0),.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ii={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=extCount,.ppEnabledExtensionNames=ext};check(vkCreateInstance(&ii,NULL,&instance),"Create Vulkan instance failed");
    check(glfwCreateWindowSurface(instance,window,NULL,&surface),"Create window surface failed");chooseDevice();
    float priority=1.0f;VkDeviceQueueCreateInfo qi[2];uint32_t qn=0;qi[qn++]=(VkDeviceQueueCreateInfo){.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=graphicsFamily,.queueCount=1,.pQueuePriorities=&priority};if(presentFamily!=graphicsFamily)qi[qn++]=(VkDeviceQueueCreateInfo){.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=presentFamily,.queueCount=1,.pQueuePriorities=&priority};
    const char *devExt[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};VkPhysicalDeviceFeatures features={0};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=qn,.pQueueCreateInfos=qi,.enabledExtensionCount=1,.ppEnabledExtensionNames=devExt,.pEnabledFeatures=&features};check(vkCreateDevice(physicalDevice,&di,NULL,&device),"Create logical device failed");vkGetDeviceQueue(device,graphicsFamily,0,&graphicsQueue);vkGetDeviceQueue(device,presentFamily,0,&presentQueue);
    createSwapchain();createDepthResources();createRenderPass();createPipeline();createFramebuffers();createCommands();
    double previousFrameTime=glfwGetTime();
    while(!glfwWindowShouldClose(window)){glfwPollEvents();int fullscreenKeyDown=glfwGetKey(window,GLFW_KEY_F)==GLFW_PRESS;if(fullscreenKeyDown&&!fullscreenKeyWasDown)toggleFullscreen(window);fullscreenKeyWasDown=fullscreenKeyDown;double now=glfwGetTime();float deltaSeconds=(float)(now-previousFrameTime);if(deltaSeconds>0.05f)deltaSeconds=0.05f;previousFrameTime=now;updateCamera(deltaSeconds);if(framebufferResized){recreateSwapchain();continue;}drawFrame();if(framebufferResized)recreateSwapchain();}
    cleanup();return 0;
}
