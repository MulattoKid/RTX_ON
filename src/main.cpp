/*
Copyright (c) 2018-2019 Daniel Fedai Larsen
LICENSE: See end of file for license information.
*/

/*
This is the main file used for the project. The 'main' function is located at the
bottom of the file. The setup can appear a bit messy, but has been written like
this for the sake of simplicity and easy modification.
*/

#define REBUILD_ACC_STRUCT 0
#define MOVE_LIGHTS_HORIZONTALLY 0
#define MOVE_LIGHTS_VERTICALLY 0
#define DEFERRED_PASS 1
#define AO_PASS 1
#define BLUR_PASS 1
#define TEMPORAL_INTEGRATION_PASS 1

#include "BrhanFile.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/gtc/constants.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/trigonometric.hpp"
#include "glm/vec3.hpp"
#include "shaders/include/Defines.glsl"
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "VulkanApp.h"

VulkanApp* vulkanApp;
uint32_t blurVariable;
uint32_t renderOnscreen = 1;
uint32_t firstOffscreenFrame = 0;

// Has some issue, but I'm not going to spend time on it
// It was done just to have some basic and quick camera rotation
void MouseCallback(GLFWwindow * window, double xpos, double ypos)
{
	const double horizontalRotationSpeed = glm::two_pi<double>() / 2.0;
	const double verticalRotationSpeed = glm::two_pi<double>() / 2.0;
	// Center xpos and ypos as they are relative to the top-left window corner
	// Range goes from [0,1] to [-0.5, 0.5]
	xpos = xpos / double(vulkanApp->camera.filmWidth) - 0.5;
	ypos = ypos / double(vulkanApp->camera.filmHeight) - 0.5;
	
	glm::vec3 cameraRight = glm::normalize(glm::cross(vulkanApp->camera.viewDir, vulkanApp->camera.up));
	// Get rotation for camera's view direction around Y-axis according to difference
	// in x-position
	double diff = xpos - vulkanApp->lastMouseX;
	glm::mat4x4 rot = glm::rotate(glm::mat4(1.0f), float(diff * horizontalRotationSpeed), -vulkanApp->camera.up);
	// Get rotation for camera's view direction around X-axis according to difference
	// in y-position
	diff = ypos - vulkanApp->lastMouseY;
	rot = glm::rotate(rot, float(diff * verticalRotationSpeed), cameraRight);
	
	// Update camera's view direction
	vulkanApp->camera.viewDir = glm::normalize(glm::vec3(rot * glm::vec4(vulkanApp->camera.viewDir, 1.0f)));
	
	vulkanApp->lastMouseX = xpos;
	vulkanApp->lastMouseY = ypos;
}

// It works, but is FAR from smooth...good enough though
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	glm::vec3 cameraRight = glm::normalize(glm::cross(vulkanApp->camera.viewDir, vulkanApp->camera.up));
	glm::vec3 cameraOriginDelta(0.0f);
	glm::vec3 cameraViewDirDelta(0.0f);
	switch (key)
	{
		case GLFW_KEY_W:
			switch (action)
			{
				case GLFW_PRESS:
    				cameraOriginDelta += 0.1f * vulkanApp->camera.viewDir;
					break;
				case GLFW_REPEAT:
    				cameraOriginDelta += 0.1f * vulkanApp->camera.viewDir;
					break;
				default:
					break;
			}
			break;
		case GLFW_KEY_S:
			switch (action)
			{
				case GLFW_PRESS:
    				cameraOriginDelta -= 0.1f * vulkanApp->camera.viewDir;
					break;
				case GLFW_REPEAT:
    				cameraOriginDelta -= 0.1f * vulkanApp->camera.viewDir;
					break;
				default:
					break;
			}
			break;
		case GLFW_KEY_D:
			switch (action)
			{
				case GLFW_PRESS:
					cameraOriginDelta += 0.1f * cameraRight;
					break;
				case GLFW_REPEAT:
					cameraOriginDelta += 0.1f * cameraRight;
					break;
				default:
					break;
			}
			break;
		case GLFW_KEY_A:
			switch (action)
			{
				case GLFW_PRESS:
					cameraOriginDelta -= 0.1f * cameraRight;
					break;
				case GLFW_REPEAT:
					cameraOriginDelta -= 0.1f * cameraRight;
					break;
				default:
					break;
			}
			break;
		case GLFW_KEY_B:
			switch (action)
			{
				case GLFW_PRESS:
					blurVariable ^= 1;
					break;
				default:
					break;
			}
			break;
		case GLFW_KEY_O:
			switch (action)
			{
				case GLFW_PRESS:
					renderOnscreen ^= 1;
					firstOffscreenFrame = renderOnscreen ^ 1;
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}
    
    vulkanApp->camera.origin += cameraOriginDelta;
}

struct RayTracingPipelineData
{
	std::vector<VkShaderModule> shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> rayTracingShaderStageInfos;
	std::vector<VkRayTracingShaderGroupCreateInfoNV> rayTracingGroupInfos;
	uint32_t numDescriptorSets;
	std::vector<VkDescriptorSetLayoutCreateInfo> descriptorSetLayoutInfos;
	std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptorSetLayoutBindings;
	std::vector<VkDescriptorSetLayout> rayTracingPipelineDescriptorSetLayouts;
	VkPipelineLayout rayTracingPipelineLayout;
	VkPipeline rayTracingPipeline;
	uint32_t shaderGroupHandleSize;
	VkBuffer shaderBindingTableBuffer;
	VkDeviceMemory shaderBindindTableBufferMemory;
	VkDeviceSize shaderBindingTableBufferSize;
	std::vector<char> shaderGroupHandles;
	
	std::vector<VkDescriptorSet> descriptorSets;
};

void CreateRayTracingColorPositionPipelineAndData(VulkanApp& vkApp, RayTracingPipelineData* rtpd)
{
	const uint32_t numShaders = 5;
	rtpd->shaderModules.resize(numShaders);
	const uint32_t rayGenShaderIndex = 0;
	const uint32_t primaryCHitShaderIndex = 1;
	const uint32_t secondaryCHitShaderIndex = 2;
	const uint32_t primaryMissShaderIndex = 3;
	const uint32_t secondaryMissShaderIndex = 4;
	vkApp.CreateShaderModule("src/shaders/out/color_position/primary_rgen.spv", &rtpd->shaderModules[rayGenShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/color_position/primary_rchit.spv", &rtpd->shaderModules[primaryCHitShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/color_position/secondary_rchit.spv", &rtpd->shaderModules[secondaryCHitShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/color_position/primary_rmiss.spv", &rtpd->shaderModules[primaryMissShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/color_position/secondary_rmiss.spv", &rtpd->shaderModules[secondaryMissShaderIndex]);
	
	rtpd->rayTracingShaderStageInfos.resize(numShaders);
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].module = rtpd->shaderModules[rayGenShaderIndex];
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].module = rtpd->shaderModules[primaryCHitShaderIndex];
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].module = rtpd->shaderModules[secondaryCHitShaderIndex];
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[secondaryCHitShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].stage = VK_SHADER_STAGE_MISS_BIT_NV;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].module = rtpd->shaderModules[primaryMissShaderIndex];
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].stage = VK_SHADER_STAGE_MISS_BIT_NV;
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].module = rtpd->shaderModules[secondaryMissShaderIndex];
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[secondaryMissShaderIndex].pSpecializationInfo = NULL;
	
	rtpd->rayTracingGroupInfos.resize(numShaders);
	rtpd->rayTracingGroupInfos[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[0].pNext = NULL;
	rtpd->rayTracingGroupInfos[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[0].generalShader = rayGenShaderIndex;
	rtpd->rayTracingGroupInfos[0].closestHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[0].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[0].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[1].pNext = NULL;
	rtpd->rayTracingGroupInfos[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
	rtpd->rayTracingGroupInfos[1].generalShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].closestHitShader = primaryCHitShaderIndex;
	rtpd->rayTracingGroupInfos[1].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[2].pNext = NULL;
	rtpd->rayTracingGroupInfos[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[2].generalShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].closestHitShader = secondaryCHitShaderIndex;
	rtpd->rayTracingGroupInfos[2].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[3].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[3].pNext = NULL;
	rtpd->rayTracingGroupInfos[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[3].generalShader = primaryMissShaderIndex;
	rtpd->rayTracingGroupInfos[3].closestHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[3].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[3].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[4].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[4].pNext = NULL;
	rtpd->rayTracingGroupInfos[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[4].generalShader = secondaryMissShaderIndex;
	rtpd->rayTracingGroupInfos[4].closestHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[4].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[4].intersectionShader = VK_SHADER_UNUSED_NV;
	
	rtpd->numDescriptorSets = 2;
	rtpd->descriptorSetLayoutInfos.resize(rtpd->numDescriptorSets);
	rtpd->descriptorSetLayoutBindings.resize(rtpd->numDescriptorSets);
	rtpd->descriptorSetLayoutBindings[0].resize(RT0_DESCRIPTOR_SET_0_NUM_BINDINGS);
	rtpd->descriptorSetLayoutBindings[1].resize(RT0_DESCRIPTOR_SET_1_NUM_BINDINGS);
	rtpd->rayTracingPipelineDescriptorSetLayouts.resize(rtpd->numDescriptorSets);
	
	//Desriptor set 0
	VkDescriptorSetLayoutBinding& accelerationStructureDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION];
	accelerationStructureDescriptorSetLayoutBinding.binding = RT0_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION;
	accelerationStructureDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
	accelerationStructureDescriptorSetLayoutBinding.descriptorCount = 1;
	accelerationStructureDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	accelerationStructureDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingColorImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_COLOR_IMAGE_BINDING_LOCATION];
	rayTracingColorImageDescriptorSetLayoutBinding.binding = RT0_COLOR_IMAGE_BINDING_LOCATION;
	rayTracingColorImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	rayTracingColorImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingColorImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingColorImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingPositionImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_POSITION_IMAGE_BINDING_LOCATION];
	rayTracingPositionImageDescriptorSetLayoutBinding.binding = RT0_POSITION_IMAGE_BINDING_LOCATION;
	rayTracingPositionImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	rayTracingPositionImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingPositionImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingPositionImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingNormalImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_NORMAL_IMAGE_BINDING_LOCATION];
	rayTracingNormalImageDescriptorSetLayoutBinding.binding = RT0_NORMAL_IMAGE_BINDING_LOCATION;
	rayTracingNormalImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	rayTracingNormalImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingNormalImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingNormalImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& cameraDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_CAMERA_BUFFER_BINDING_LOCATION];
	cameraDescriptorSetLayoutBinding.binding = RT0_CAMERA_BUFFER_BINDING_LOCATION;
	cameraDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraDescriptorSetLayoutBinding.descriptorCount = 1;
	cameraDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	cameraDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& lightsDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_LIGHTS_BUFFER_BINDING_LOCATION];
	lightsDescriptorSetLayoutBinding.binding = RT0_LIGHTS_BUFFER_BINDING_LOCATION;
	lightsDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightsDescriptorSetLayoutBinding.descriptorCount = 1;
	lightsDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	lightsDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& otherDataDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT0_OTHER_DATA_BUFFER_BINDING_LOCATION];
	otherDataDescriptorSetLayoutBinding.binding = RT0_OTHER_DATA_BUFFER_BINDING_LOCATION;
	otherDataDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	otherDataDescriptorSetLayoutBinding.descriptorCount = 1;
	otherDataDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	otherDataDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutCreateInfo& descriptorSetLayoutInfo0 = rtpd->descriptorSetLayoutInfos[0];
	descriptorSetLayoutInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo0.pNext = NULL;
	descriptorSetLayoutInfo0.flags = 0;
	descriptorSetLayoutInfo0.bindingCount = rtpd->descriptorSetLayoutBindings[0].size();
	descriptorSetLayoutInfo0.pBindings = rtpd->descriptorSetLayoutBindings[0].data();
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(vkApp.vkDevice, &descriptorSetLayoutInfo0, NULL, &rtpd->rayTracingPipelineDescriptorSetLayouts[0]))
	
	//Descriptor set 1
	VkDescriptorSetLayoutBinding& customIDToAttributeArrayIndexDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[1][RT0_CUSTOM_ID_TO_ATTRIBUTE_ARRAY_INDEX_BUFFER_BINDING_LOCATION];
	customIDToAttributeArrayIndexDescriptorSetLayoutBinding.binding = RT0_CUSTOM_ID_TO_ATTRIBUTE_ARRAY_INDEX_BUFFER_BINDING_LOCATION;
	customIDToAttributeArrayIndexDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	customIDToAttributeArrayIndexDescriptorSetLayoutBinding.descriptorCount = 1;
	customIDToAttributeArrayIndexDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	customIDToAttributeArrayIndexDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& perMeshAttributesDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[1][RT0_PER_MESH_ATTRIBUTES_BINDING_LOCATION];
	perMeshAttributesDescriptorSetLayoutBinding.binding = RT0_PER_MESH_ATTRIBUTES_BINDING_LOCATION;
	perMeshAttributesDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	perMeshAttributesDescriptorSetLayoutBinding.descriptorCount = 1;
	perMeshAttributesDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	perMeshAttributesDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& perVertexAttributesDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[1][RT0_PER_VERTEX_ATTRIBUTES_BINDING_LOCATION];
	perVertexAttributesDescriptorSetLayoutBinding.binding = RT0_PER_VERTEX_ATTRIBUTES_BINDING_LOCATION;
	perVertexAttributesDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	perVertexAttributesDescriptorSetLayoutBinding.descriptorCount = 1;
	perVertexAttributesDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	perVertexAttributesDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutCreateInfo& descriptorSetLayoutInfo1 = rtpd->descriptorSetLayoutInfos[1];
	descriptorSetLayoutInfo1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo1.pNext = NULL;
	descriptorSetLayoutInfo1.flags = 0;
	descriptorSetLayoutInfo1.bindingCount = rtpd->descriptorSetLayoutBindings[1].size();
	descriptorSetLayoutInfo1.pBindings = rtpd->descriptorSetLayoutBindings[1].data();
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(vkApp.vkDevice, &descriptorSetLayoutInfo1, NULL, &rtpd->rayTracingPipelineDescriptorSetLayouts[1]))
	
	//Pipeline
	VkPipelineLayoutCreateInfo rayTracingPipelineLayoutInfo = {};
	rayTracingPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	rayTracingPipelineLayoutInfo.pNext = NULL;
	rayTracingPipelineLayoutInfo.flags = 0;
	rayTracingPipelineLayoutInfo.setLayoutCount = rtpd->rayTracingPipelineDescriptorSetLayouts.size();
	rayTracingPipelineLayoutInfo.pSetLayouts = rtpd->rayTracingPipelineDescriptorSetLayouts.data();
	rayTracingPipelineLayoutInfo.pushConstantRangeCount = 0;
	rayTracingPipelineLayoutInfo.pPushConstantRanges = NULL;
	CHECK_VK_RESULT(vkCreatePipelineLayout(vkApp.vkDevice, &rayTracingPipelineLayoutInfo, NULL, &rtpd->rayTracingPipelineLayout))
	
	VkRayTracingPipelineCreateInfoNV rayTracingPipelineInfo = {};
	rayTracingPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
	rayTracingPipelineInfo.pNext = NULL;
	rayTracingPipelineInfo.flags = 0;
	rayTracingPipelineInfo.stageCount = rtpd->rayTracingShaderStageInfos.size();
	rayTracingPipelineInfo.pStages = rtpd->rayTracingShaderStageInfos.data();
	rayTracingPipelineInfo.groupCount = rtpd->rayTracingGroupInfos.size();
	rayTracingPipelineInfo.pGroups = rtpd->rayTracingGroupInfos.data();
	rayTracingPipelineInfo.maxRecursionDepth = 1;
	rayTracingPipelineInfo.layout = rtpd->rayTracingPipelineLayout;
	rayTracingPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	rayTracingPipelineInfo.basePipelineIndex = -1;
	CHECK_VK_RESULT(vkCreateRayTracingPipelinesNV(vkApp.vkDevice, VK_NULL_HANDLE, 1, &rayTracingPipelineInfo, NULL, &rtpd->rayTracingPipeline))
	
	////////////////////////////
	////SHADER BINDING TABLE////
	////////////////////////////
	VkPhysicalDeviceRayTracingPropertiesNV physicalDeviceRayTracingProperties = {};
	physicalDeviceRayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
	physicalDeviceRayTracingProperties.pNext = NULL;
	VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {};
	physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	physicalDeviceProperties2.pNext = &physicalDeviceRayTracingProperties;
	vkGetPhysicalDeviceProperties2(vkApp.vkPhysicalDevice, &physicalDeviceProperties2);
	rtpd->shaderGroupHandleSize = physicalDeviceRayTracingProperties.shaderGroupHandleSize;
	rtpd->shaderBindingTableBufferSize = rtpd->shaderGroupHandleSize * rtpd->rayTracingGroupInfos.size();
	rtpd->shaderGroupHandles.resize(rtpd->shaderBindingTableBufferSize);
	CHECK_VK_RESULT(vkGetRayTracingShaderGroupHandlesNV(vkApp.vkDevice, rtpd->rayTracingPipeline, 0, rtpd->rayTracingGroupInfos.size(), rtpd->shaderBindingTableBufferSize, (void*)(rtpd->shaderGroupHandles.data())))
	vkApp.CreateDeviceBuffer(rtpd->shaderBindingTableBufferSize, (void*)(rtpd->shaderGroupHandles.data()), VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, &rtpd->shaderBindingTableBuffer, &rtpd->shaderBindindTableBufferMemory);
}

void CreateDescriptorSetLayoutsColorPosition(VulkanApp& vkApp, VkDescriptorPool& descriptorPool, VulkanAccelerationStructure& accStruct, VkBuffer& cameraBuffer, VkDeviceSize& cameraBufferSize, VkBuffer& lightsBuffer, VkDeviceSize& lightsBufferSize, VkBuffer& otherDataBuffer, VkDeviceSize& otherDataBufferSize, VkBuffer& customIDToAttributeArrayIndexBuffer, VkDeviceSize& customIDToAttributeArrayIndexBufferSize, VkBuffer& perMeshAttributeBuffer, VkDeviceSize& perMeshAttributeBufferSize, VkBuffer& perVertexAttributeBuffer, VkDeviceSize& perVertexAttributeBufferSize, VkImageView& rayTracingColorImageView, VkImageView& rayTracingPositionImageView, VkImageView rayTracingNormalImageView, RayTracingPipelineData* rtpd)
{
	//Descriptor sets
	rtpd->descriptorSets.resize(rtpd->numDescriptorSets);
	
	//Descriptor set 0
	VkDescriptorSet& descriptorSet0 = rtpd->descriptorSets[0];
    std::vector<VkWriteDescriptorSet> descriptorSet0Writes(RT0_DESCRIPTOR_SET_0_NUM_BINDINGS);
    
	VkDescriptorSetAllocateInfo descriptorSet0AllocateInfo = {};
	descriptorSet0AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSet0AllocateInfo.pNext = NULL;
	descriptorSet0AllocateInfo.descriptorPool = descriptorPool;
	descriptorSet0AllocateInfo.descriptorSetCount = 1;
	descriptorSet0AllocateInfo.pSetLayouts = &rtpd->rayTracingPipelineDescriptorSetLayouts[0];
	CHECK_VK_RESULT(vkAllocateDescriptorSets(vkApp.vkDevice, &descriptorSet0AllocateInfo, &descriptorSet0))
	
	VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo = {};
    descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
    descriptorAccelerationStructureInfo.pNext = NULL;
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &accStruct.topAccStruct.accelerationStructure;

    VkWriteDescriptorSet& accelerationStructureWrite = descriptorSet0Writes[RT0_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION];
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo; // Notice that pNext is assigned here!
    accelerationStructureWrite.dstSet = descriptorSet0;
    accelerationStructureWrite.dstBinding = RT0_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    accelerationStructureWrite.pImageInfo = NULL;
    accelerationStructureWrite.pBufferInfo = NULL;
    accelerationStructureWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingColorImageInfo = {};
    descriptorRayTracingColorImageInfo.sampler = VK_NULL_HANDLE;
    descriptorRayTracingColorImageInfo.imageView = rayTracingColorImageView;
    descriptorRayTracingColorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingColorImageWrite = descriptorSet0Writes[RT0_COLOR_IMAGE_BINDING_LOCATION];
    rayTracingColorImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingColorImageWrite.pNext = NULL;
    rayTracingColorImageWrite.dstSet = descriptorSet0;
    rayTracingColorImageWrite.dstBinding = RT0_COLOR_IMAGE_BINDING_LOCATION;
    rayTracingColorImageWrite.dstArrayElement = 0;
    rayTracingColorImageWrite.descriptorCount = 1;
    rayTracingColorImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rayTracingColorImageWrite.pImageInfo = &descriptorRayTracingColorImageInfo;
    rayTracingColorImageWrite.pBufferInfo = NULL;
    rayTracingColorImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingPositionImageInfo = {};
    descriptorRayTracingPositionImageInfo.sampler = VK_NULL_HANDLE;
    descriptorRayTracingPositionImageInfo.imageView = rayTracingPositionImageView;
    descriptorRayTracingPositionImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingPositionImageWrite = descriptorSet0Writes[RT0_POSITION_IMAGE_BINDING_LOCATION];
    rayTracingPositionImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingPositionImageWrite.pNext = NULL;
    rayTracingPositionImageWrite.dstSet = descriptorSet0;
    rayTracingPositionImageWrite.dstBinding = RT0_POSITION_IMAGE_BINDING_LOCATION;
    rayTracingPositionImageWrite.dstArrayElement = 0;
    rayTracingPositionImageWrite.descriptorCount = 1;
    rayTracingPositionImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rayTracingPositionImageWrite.pImageInfo = &descriptorRayTracingPositionImageInfo;
    rayTracingPositionImageWrite.pBufferInfo = NULL;
    rayTracingPositionImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingNormalImageInfo = {};
    descriptorRayTracingNormalImageInfo.sampler = VK_NULL_HANDLE;
    descriptorRayTracingNormalImageInfo.imageView = rayTracingNormalImageView;
    descriptorRayTracingNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingNormalImageWrite = descriptorSet0Writes[RT0_NORMAL_IMAGE_BINDING_LOCATION];
    rayTracingNormalImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingNormalImageWrite.pNext = NULL;
    rayTracingNormalImageWrite.dstSet = descriptorSet0;
    rayTracingNormalImageWrite.dstBinding = RT0_NORMAL_IMAGE_BINDING_LOCATION;
    rayTracingNormalImageWrite.dstArrayElement = 0;
    rayTracingNormalImageWrite.descriptorCount = 1;
    rayTracingNormalImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rayTracingNormalImageWrite.pImageInfo = &descriptorRayTracingNormalImageInfo;
    rayTracingNormalImageWrite.pBufferInfo = NULL;
    rayTracingNormalImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorCameraInfo = {};
    descriptorCameraInfo.buffer = cameraBuffer;
    descriptorCameraInfo.offset = 0;
    descriptorCameraInfo.range = cameraBufferSize;
    
    VkWriteDescriptorSet& cameraWrite = descriptorSet0Writes[RT0_CAMERA_BUFFER_BINDING_LOCATION];
    cameraWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    cameraWrite.pNext = NULL;
    cameraWrite.dstSet = descriptorSet0;
    cameraWrite.dstBinding = RT0_CAMERA_BUFFER_BINDING_LOCATION;
    cameraWrite.dstArrayElement = 0;
    cameraWrite.descriptorCount = 1;
    cameraWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraWrite.pImageInfo = NULL;
    cameraWrite.pBufferInfo = &descriptorCameraInfo;
    cameraWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorLightsInfo = {};
    descriptorLightsInfo.buffer = lightsBuffer;
    descriptorLightsInfo.offset = 0;
    descriptorLightsInfo.range = lightsBufferSize;
    
    VkWriteDescriptorSet& lightsWrite = descriptorSet0Writes[RT0_LIGHTS_BUFFER_BINDING_LOCATION];
    lightsWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightsWrite.pNext = NULL;
    lightsWrite.dstSet = descriptorSet0;
    lightsWrite.dstBinding = RT0_LIGHTS_BUFFER_BINDING_LOCATION;
    lightsWrite.dstArrayElement = 0;
    lightsWrite.descriptorCount = 1;
    lightsWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightsWrite.pImageInfo = NULL;
    lightsWrite.pBufferInfo = &descriptorLightsInfo;
    lightsWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorOtherDataInfo = {};
    descriptorOtherDataInfo.buffer = otherDataBuffer;
    descriptorOtherDataInfo.offset = 0;
    descriptorOtherDataInfo.range = otherDataBufferSize;
    
    VkWriteDescriptorSet& otherDataWrite = descriptorSet0Writes[RT0_OTHER_DATA_BUFFER_BINDING_LOCATION];
    otherDataWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    otherDataWrite.pNext = NULL;
    otherDataWrite.dstSet = descriptorSet0;
    otherDataWrite.dstBinding = RT0_OTHER_DATA_BUFFER_BINDING_LOCATION;
    otherDataWrite.dstArrayElement = 0;
    otherDataWrite.descriptorCount = 1;
    otherDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    otherDataWrite.pImageInfo = NULL;
    otherDataWrite.pBufferInfo = &descriptorOtherDataInfo;
    otherDataWrite.pTexelBufferView = NULL;
    
    vkUpdateDescriptorSets(vkApp.vkDevice, descriptorSet0Writes.size(), descriptorSet0Writes.data(), 0, NULL);
    
    //Descriptor set 1
	VkDescriptorSet& descriptorSet1 = rtpd->descriptorSets[1];
    std::vector<VkWriteDescriptorSet> descriptorSet1Writes(RT0_DESCRIPTOR_SET_1_NUM_BINDINGS);
    
	VkDescriptorSetAllocateInfo descriptorSet1AllocateInfo = {};
	descriptorSet1AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSet1AllocateInfo.pNext = NULL;
	descriptorSet1AllocateInfo.descriptorPool = descriptorPool;
	descriptorSet1AllocateInfo.descriptorSetCount = 1;
	descriptorSet1AllocateInfo.pSetLayouts = &rtpd->rayTracingPipelineDescriptorSetLayouts[1];
	CHECK_VK_RESULT(vkAllocateDescriptorSets(vkApp.vkDevice, &descriptorSet1AllocateInfo, &descriptorSet1))
    
    VkDescriptorBufferInfo descriptorCustomIDToAttributeArrayIndexInfo = {};
    descriptorCustomIDToAttributeArrayIndexInfo.buffer = customIDToAttributeArrayIndexBuffer;
    descriptorCustomIDToAttributeArrayIndexInfo.offset = 0;
    descriptorCustomIDToAttributeArrayIndexInfo.range = customIDToAttributeArrayIndexBufferSize;
    
    VkWriteDescriptorSet& customIDToAttributeArrayIndexWrite = descriptorSet1Writes[RT0_CUSTOM_ID_TO_ATTRIBUTE_ARRAY_INDEX_BUFFER_BINDING_LOCATION];
    customIDToAttributeArrayIndexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    customIDToAttributeArrayIndexWrite.pNext = NULL;
    customIDToAttributeArrayIndexWrite.dstSet = descriptorSet1;
    customIDToAttributeArrayIndexWrite.dstBinding = RT0_CUSTOM_ID_TO_ATTRIBUTE_ARRAY_INDEX_BUFFER_BINDING_LOCATION;
    customIDToAttributeArrayIndexWrite.dstArrayElement = 0;
    customIDToAttributeArrayIndexWrite.descriptorCount = 1;
    customIDToAttributeArrayIndexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    customIDToAttributeArrayIndexWrite.pImageInfo = NULL;
    customIDToAttributeArrayIndexWrite.pBufferInfo = &descriptorCustomIDToAttributeArrayIndexInfo;
    customIDToAttributeArrayIndexWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorPerMeshAttributesInfo = {};
    descriptorPerMeshAttributesInfo.buffer = perMeshAttributeBuffer;
    descriptorPerMeshAttributesInfo.offset = 0;
    descriptorPerMeshAttributesInfo.range = perMeshAttributeBufferSize;
    
    VkWriteDescriptorSet& perMeshAttributesWrite = descriptorSet1Writes[RT0_PER_MESH_ATTRIBUTES_BINDING_LOCATION];
    perMeshAttributesWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    perMeshAttributesWrite.pNext = NULL;
    perMeshAttributesWrite.dstSet = descriptorSet1;
    perMeshAttributesWrite.dstBinding = RT0_PER_MESH_ATTRIBUTES_BINDING_LOCATION;
    perMeshAttributesWrite.dstArrayElement = 0;
    perMeshAttributesWrite.descriptorCount = 1;
    perMeshAttributesWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    perMeshAttributesWrite.pImageInfo = NULL;
    perMeshAttributesWrite.pBufferInfo = &descriptorPerMeshAttributesInfo;
    perMeshAttributesWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorperVertexAttributesInfo = {};
    descriptorperVertexAttributesInfo.buffer = perVertexAttributeBuffer;
    descriptorperVertexAttributesInfo.offset = 0;
    descriptorperVertexAttributesInfo.range = perVertexAttributeBufferSize;
    
    VkWriteDescriptorSet& perVerexAttributesWrite = descriptorSet1Writes[RT0_PER_VERTEX_ATTRIBUTES_BINDING_LOCATION];
    perVerexAttributesWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    perVerexAttributesWrite.pNext = NULL;
    perVerexAttributesWrite.dstSet = descriptorSet1;
    perVerexAttributesWrite.dstBinding = RT0_PER_VERTEX_ATTRIBUTES_BINDING_LOCATION;
    perVerexAttributesWrite.dstArrayElement = 0;
    perVerexAttributesWrite.descriptorCount = 1;
    perVerexAttributesWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    perVerexAttributesWrite.pImageInfo = NULL;
    perVerexAttributesWrite.pBufferInfo = &descriptorperVertexAttributesInfo;
    perVerexAttributesWrite.pTexelBufferView = NULL;
    
    vkUpdateDescriptorSets(vkApp.vkDevice, descriptorSet1Writes.size(), descriptorSet1Writes.data(), 0, NULL);
}

void CreateRayTracingAOPipelineAndData(VulkanApp& vkApp, RayTracingPipelineData* rtpd)
{
	const uint32_t numShaders = 3;
	rtpd->shaderModules.resize(numShaders);
	const uint32_t rayGenShaderIndex = 0;
	const uint32_t primaryCHitShaderIndex = 1;
	const uint32_t primaryMissShaderIndex = 2;
	vkApp.CreateShaderModule("src/shaders/out/ao/primary_rgen.spv", &rtpd->shaderModules[rayGenShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/ao/primary_rchit.spv", &rtpd->shaderModules[primaryCHitShaderIndex]);
	vkApp.CreateShaderModule("src/shaders/out/ao/primary_rmiss.spv", &rtpd->shaderModules[primaryMissShaderIndex]);
	
	rtpd->rayTracingShaderStageInfos.resize(numShaders);
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].stage = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].module = rtpd->shaderModules[rayGenShaderIndex];
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[rayGenShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV;
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].module = rtpd->shaderModules[primaryCHitShaderIndex];
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[primaryCHitShaderIndex].pSpecializationInfo = NULL;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pNext = NULL;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].flags = 0;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].stage = VK_SHADER_STAGE_MISS_BIT_NV;
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].module = rtpd->shaderModules[primaryMissShaderIndex];
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pName = "main";
	rtpd->rayTracingShaderStageInfos[primaryMissShaderIndex].pSpecializationInfo = NULL;
	
	rtpd->rayTracingGroupInfos.resize(numShaders);
	rtpd->rayTracingGroupInfos[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[0].pNext = NULL;
	rtpd->rayTracingGroupInfos[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[0].generalShader = rayGenShaderIndex;
	rtpd->rayTracingGroupInfos[0].closestHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[0].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[0].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[1].pNext = NULL;
	rtpd->rayTracingGroupInfos[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV;
	rtpd->rayTracingGroupInfos[1].generalShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].closestHitShader = primaryCHitShaderIndex;
	rtpd->rayTracingGroupInfos[1].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[1].intersectionShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
	rtpd->rayTracingGroupInfos[2].pNext = NULL;
	rtpd->rayTracingGroupInfos[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV;
	rtpd->rayTracingGroupInfos[2].generalShader = primaryMissShaderIndex;
	rtpd->rayTracingGroupInfos[2].closestHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].anyHitShader = VK_SHADER_UNUSED_NV;
	rtpd->rayTracingGroupInfos[2].intersectionShader = VK_SHADER_UNUSED_NV;
	
	rtpd->numDescriptorSets = 1;
	rtpd->descriptorSetLayoutInfos.resize(rtpd->numDescriptorSets);
	rtpd->descriptorSetLayoutBindings.resize(rtpd->numDescriptorSets);
	rtpd->descriptorSetLayoutBindings[0].resize(RT1_DESCRIPTOR_SET_NUM_BINDINGS);
	rtpd->rayTracingPipelineDescriptorSetLayouts.resize(rtpd->numDescriptorSets);
	
	//Desriptor set 0
	VkDescriptorSetLayoutBinding& accelerationStructureDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION];
	accelerationStructureDescriptorSetLayoutBinding.binding = RT1_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION;
	accelerationStructureDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
	accelerationStructureDescriptorSetLayoutBinding.descriptorCount = 1;
	accelerationStructureDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	accelerationStructureDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingPositionImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_POSITION_IMAGE_BINDING_LOCATION];
	rayTracingPositionImageDescriptorSetLayoutBinding.binding = RT1_POSITION_IMAGE_BINDING_LOCATION;
	rayTracingPositionImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rayTracingPositionImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingPositionImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingPositionImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingNormalImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_NORMAL_IMAGE_BINDING_LOCATION];
	rayTracingNormalImageDescriptorSetLayoutBinding.binding = RT1_NORMAL_IMAGE_BINDING_LOCATION;
	rayTracingNormalImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rayTracingNormalImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingNormalImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingNormalImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingAOImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_AO_IMAGE_BINDING_LOCATION];
	rayTracingAOImageDescriptorSetLayoutBinding.binding = RT1_AO_IMAGE_BINDING_LOCATION;
	rayTracingAOImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	rayTracingAOImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingAOImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingAOImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingCurrentFrameDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_CURRENT_FRAME_BINDING_LOCATION];
	rayTracingCurrentFrameDescriptorSetLayoutBinding.binding = RT1_CURRENT_FRAME_BINDING_LOCATION;
	rayTracingCurrentFrameDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	rayTracingCurrentFrameDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingCurrentFrameDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingCurrentFrameDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutBinding& rayTracingBlueNoiseImageDescriptorSetLayoutBinding = rtpd->descriptorSetLayoutBindings[0][RT1_BLUE_NOISE_IMAGE_BINDING_LOCATION];
	rayTracingBlueNoiseImageDescriptorSetLayoutBinding.binding = RT1_BLUE_NOISE_IMAGE_BINDING_LOCATION;
	rayTracingBlueNoiseImageDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rayTracingBlueNoiseImageDescriptorSetLayoutBinding.descriptorCount = 1;
	rayTracingBlueNoiseImageDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
	rayTracingBlueNoiseImageDescriptorSetLayoutBinding.pImmutableSamplers = NULL;
	
	VkDescriptorSetLayoutCreateInfo& descriptorSetLayoutInfo0 = rtpd->descriptorSetLayoutInfos[0];
	descriptorSetLayoutInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo0.pNext = NULL;
	descriptorSetLayoutInfo0.flags = 0;
	descriptorSetLayoutInfo0.bindingCount = rtpd->descriptorSetLayoutBindings[0].size();
	descriptorSetLayoutInfo0.pBindings = rtpd->descriptorSetLayoutBindings[0].data();
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(vkApp.vkDevice, &descriptorSetLayoutInfo0, NULL, &rtpd->rayTracingPipelineDescriptorSetLayouts[0]))
	
	//Pipeline
	VkPipelineLayoutCreateInfo rayTracingPipelineLayoutInfo = {};
	rayTracingPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	rayTracingPipelineLayoutInfo.pNext = NULL;
	rayTracingPipelineLayoutInfo.flags = 0;
	rayTracingPipelineLayoutInfo.setLayoutCount = rtpd->rayTracingPipelineDescriptorSetLayouts.size();
	rayTracingPipelineLayoutInfo.pSetLayouts = rtpd->rayTracingPipelineDescriptorSetLayouts.data();
	rayTracingPipelineLayoutInfo.pushConstantRangeCount = 0;
	rayTracingPipelineLayoutInfo.pPushConstantRanges = NULL;
	CHECK_VK_RESULT(vkCreatePipelineLayout(vkApp.vkDevice, &rayTracingPipelineLayoutInfo, NULL, &rtpd->rayTracingPipelineLayout))
	
	VkRayTracingPipelineCreateInfoNV rayTracingPipelineInfo = {};
	rayTracingPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
	rayTracingPipelineInfo.pNext = NULL;
	rayTracingPipelineInfo.flags = 0;
	rayTracingPipelineInfo.stageCount = rtpd->rayTracingShaderStageInfos.size();
	rayTracingPipelineInfo.pStages = rtpd->rayTracingShaderStageInfos.data();
	rayTracingPipelineInfo.groupCount = rtpd->rayTracingGroupInfos.size();
	rayTracingPipelineInfo.pGroups = rtpd->rayTracingGroupInfos.data();
	rayTracingPipelineInfo.maxRecursionDepth = 1;
	rayTracingPipelineInfo.layout = rtpd->rayTracingPipelineLayout;
	rayTracingPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	rayTracingPipelineInfo.basePipelineIndex = -1;
	CHECK_VK_RESULT(vkCreateRayTracingPipelinesNV(vkApp.vkDevice, VK_NULL_HANDLE, 1, &rayTracingPipelineInfo, NULL, &rtpd->rayTracingPipeline))
	
	////////////////////////////
	////SHADER BINDING TABLE////
	////////////////////////////
	VkPhysicalDeviceRayTracingPropertiesNV physicalDeviceRayTracingProperties = {};
	physicalDeviceRayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
	physicalDeviceRayTracingProperties.pNext = NULL;
	VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {};
	physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	physicalDeviceProperties2.pNext = &physicalDeviceRayTracingProperties;
	vkGetPhysicalDeviceProperties2(vkApp.vkPhysicalDevice, &physicalDeviceProperties2);
	rtpd->shaderGroupHandleSize = physicalDeviceRayTracingProperties.shaderGroupHandleSize;
	rtpd->shaderBindingTableBufferSize = rtpd->shaderGroupHandleSize * rtpd->rayTracingGroupInfos.size();
	rtpd->shaderGroupHandles.resize(rtpd->shaderBindingTableBufferSize);
	CHECK_VK_RESULT(vkGetRayTracingShaderGroupHandlesNV(vkApp.vkDevice, rtpd->rayTracingPipeline, 0, rtpd->rayTracingGroupInfos.size(), rtpd->shaderBindingTableBufferSize, (void*)(rtpd->shaderGroupHandles.data())))
	vkApp.CreateDeviceBuffer(rtpd->shaderBindingTableBufferSize, (void*)(rtpd->shaderGroupHandles.data()), VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, &rtpd->shaderBindingTableBuffer, &rtpd->shaderBindindTableBufferMemory);
}

void CreateDescriptorSetLayoutsAO(VulkanApp& vkApp, VkDescriptorPool& descriptorPool, VulkanAccelerationStructure& accStruct, VkImageView& rayTracingPositionImageView, VkImageView& rayTracingNormalImageView, VkSampler& positionNormalSampler, VkImageView& rayTracingAOImageView, VkBuffer currentFrameBuffer, VkImageView& rayTracingBlueNoiseImageView, VkSampler& blueNoiseSampler, RayTracingPipelineData* rtpd)
{
	//Descriptor sets
	rtpd->descriptorSets.resize(rtpd->numDescriptorSets);
	
	//Descriptor set 0
	VkDescriptorSet& descriptorSet0 = rtpd->descriptorSets[0];
    std::vector<VkWriteDescriptorSet> descriptorSet0Writes(RT1_DESCRIPTOR_SET_NUM_BINDINGS);
    
	VkDescriptorSetAllocateInfo descriptorSet0AllocateInfo = {};
	descriptorSet0AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSet0AllocateInfo.pNext = NULL;
	descriptorSet0AllocateInfo.descriptorPool = descriptorPool;
	descriptorSet0AllocateInfo.descriptorSetCount = 1;
	descriptorSet0AllocateInfo.pSetLayouts = &rtpd->rayTracingPipelineDescriptorSetLayouts[0];
	CHECK_VK_RESULT(vkAllocateDescriptorSets(vkApp.vkDevice, &descriptorSet0AllocateInfo, &descriptorSet0))
	
	VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo = {};
    descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
    descriptorAccelerationStructureInfo.pNext = NULL;
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &accStruct.topAccStruct.accelerationStructure;

    VkWriteDescriptorSet& accelerationStructureWrite = descriptorSet0Writes[RT1_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION];
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo; // Notice that pNext is assigned here!
    accelerationStructureWrite.dstSet = descriptorSet0;
    accelerationStructureWrite.dstBinding = RT1_ACCELERATION_STRUCTURE_NV_BINDING_LOCATION;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    accelerationStructureWrite.pImageInfo = NULL;
    accelerationStructureWrite.pBufferInfo = NULL;
    accelerationStructureWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingPositionImageInfo = {};
    descriptorRayTracingPositionImageInfo.sampler = positionNormalSampler;
    descriptorRayTracingPositionImageInfo.imageView = rayTracingPositionImageView;
    descriptorRayTracingPositionImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingPositionImageWrite = descriptorSet0Writes[RT1_POSITION_IMAGE_BINDING_LOCATION];
    rayTracingPositionImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingPositionImageWrite.pNext = NULL;
    rayTracingPositionImageWrite.dstSet = descriptorSet0;
    rayTracingPositionImageWrite.dstBinding = RT1_POSITION_IMAGE_BINDING_LOCATION;
    rayTracingPositionImageWrite.dstArrayElement = 0;
    rayTracingPositionImageWrite.descriptorCount = 1;
    rayTracingPositionImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rayTracingPositionImageWrite.pImageInfo = &descriptorRayTracingPositionImageInfo;
    rayTracingPositionImageWrite.pBufferInfo = NULL;
    rayTracingPositionImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingNormalImageInfo = {};
    descriptorRayTracingNormalImageInfo.sampler = positionNormalSampler;
    descriptorRayTracingNormalImageInfo.imageView = rayTracingNormalImageView;
    descriptorRayTracingNormalImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingNormalImageWrite = descriptorSet0Writes[RT1_NORMAL_IMAGE_BINDING_LOCATION];
    rayTracingNormalImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingNormalImageWrite.pNext = NULL;
    rayTracingNormalImageWrite.dstSet = descriptorSet0;
    rayTracingNormalImageWrite.dstBinding = RT1_NORMAL_IMAGE_BINDING_LOCATION;
    rayTracingNormalImageWrite.dstArrayElement = 0;
    rayTracingNormalImageWrite.descriptorCount = 1;
    rayTracingNormalImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rayTracingNormalImageWrite.pImageInfo = &descriptorRayTracingNormalImageInfo;
    rayTracingNormalImageWrite.pBufferInfo = NULL;
    rayTracingNormalImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingAOImageInfo = {};
    descriptorRayTracingAOImageInfo.sampler = VK_NULL_HANDLE;
    descriptorRayTracingAOImageInfo.imageView = rayTracingAOImageView;
    descriptorRayTracingAOImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    
    VkWriteDescriptorSet& rayTracingAOImageWrite = descriptorSet0Writes[RT1_AO_IMAGE_BINDING_LOCATION];
    rayTracingAOImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingAOImageWrite.pNext = NULL;
    rayTracingAOImageWrite.dstSet = descriptorSet0;
    rayTracingAOImageWrite.dstBinding = RT1_AO_IMAGE_BINDING_LOCATION;
    rayTracingAOImageWrite.dstArrayElement = 0;
    rayTracingAOImageWrite.descriptorCount = 1;
    rayTracingAOImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    rayTracingAOImageWrite.pImageInfo = &descriptorRayTracingAOImageInfo;
    rayTracingAOImageWrite.pBufferInfo = NULL;
    rayTracingAOImageWrite.pTexelBufferView = NULL;
    
    VkDescriptorBufferInfo descriptorRayTracingCurrentFrameInfo = {};
    descriptorRayTracingCurrentFrameInfo.buffer = currentFrameBuffer;
    descriptorRayTracingCurrentFrameInfo.offset = 0;
    descriptorRayTracingCurrentFrameInfo.range = VK_WHOLE_SIZE;
    
    VkWriteDescriptorSet& rayTracingCurrentFrameWrite = descriptorSet0Writes[RT1_CURRENT_FRAME_BINDING_LOCATION];
    rayTracingCurrentFrameWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingCurrentFrameWrite.pNext = NULL;
    rayTracingCurrentFrameWrite.dstSet = descriptorSet0;
    rayTracingCurrentFrameWrite.dstBinding = RT1_CURRENT_FRAME_BINDING_LOCATION;
    rayTracingCurrentFrameWrite.dstArrayElement = 0;
    rayTracingCurrentFrameWrite.descriptorCount = 1;
    rayTracingCurrentFrameWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    rayTracingCurrentFrameWrite.pImageInfo = NULL;
    rayTracingCurrentFrameWrite.pBufferInfo = &descriptorRayTracingCurrentFrameInfo;
    rayTracingCurrentFrameWrite.pTexelBufferView = NULL;
    
    VkDescriptorImageInfo descriptorRayTracingBlueNoiseImageInfo = {};
    descriptorRayTracingBlueNoiseImageInfo.sampler = blueNoiseSampler;
    descriptorRayTracingBlueNoiseImageInfo.imageView = rayTracingBlueNoiseImageView;
    descriptorRayTracingBlueNoiseImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    VkWriteDescriptorSet& rayTracingBlueNoiseImageWrite = descriptorSet0Writes[RT1_BLUE_NOISE_IMAGE_BINDING_LOCATION];
    rayTracingBlueNoiseImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingBlueNoiseImageWrite.pNext = NULL;
    rayTracingBlueNoiseImageWrite.dstSet = descriptorSet0;
    rayTracingBlueNoiseImageWrite.dstBinding = RT1_BLUE_NOISE_IMAGE_BINDING_LOCATION;
    rayTracingBlueNoiseImageWrite.dstArrayElement = 0;
    rayTracingBlueNoiseImageWrite.descriptorCount = 1;
    rayTracingBlueNoiseImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rayTracingBlueNoiseImageWrite.pImageInfo = &descriptorRayTracingBlueNoiseImageInfo;
    rayTracingBlueNoiseImageWrite.pBufferInfo = NULL;
    rayTracingBlueNoiseImageWrite.pTexelBufferView = NULL;
    
    vkUpdateDescriptorSets(vkApp.vkDevice, descriptorSet0Writes.size(), descriptorSet0Writes.data(), 0, NULL);
}

void Raytrace(const char* brhanFile)
{
	BrhanFile sceneFile(brhanFile);

	std::vector<const char*> validationLayerNames = {
		"VK_LAYER_LUNARG_standard_validation"
	};
	std::vector<const char*> extensionNames = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_NV_RAY_TRACING_EXTENSION_NAME
	};
	VulkanAppCreateInfo vkAppInfo = {};
	vkAppInfo.graphicsApp = VK_TRUE;
	vkAppInfo.windowWidth = sceneFile.filmWidth;
	vkAppInfo.windowHeight = sceneFile.filmHeight;
	vkAppInfo.windowName = "Vulkan RTX";
	vkAppInfo.appName = "Vulkan RTX";
	vkAppInfo.engineName = "VRTX";
	vkAppInfo.validationLayerCount = validationLayerNames.size();
	vkAppInfo.validationLayerNames = validationLayerNames.data();
	vkAppInfo.extensionCount = extensionNames.size();
	vkAppInfo.extensionNames = extensionNames.data();
	vkAppInfo.maxFramesInFlight = 2;
	vulkanApp = new VulkanApp(&vkAppInfo);
	VulkanApp& vkApp = *vulkanApp;
	
	////////////////////////////
	///////////MOUSE////////////
	////////////////////////////
	vkApp.lastMouseX = 0.0;
	vkApp.lastMouseY = 0.0;
	
	////////////////////////////
	///////////CAMERA///////////
	////////////////////////////
	vkApp.camera = Camera(sceneFile.filmWidth, sceneFile.filmHeight, sceneFile.cameraVerticalFOV, sceneFile.cameraOrigin, sceneFile.cameraViewDir);
	vkApp.previousFrameCamera = vkApp.camera;
	glm::mat4x4 previousViewProjection = vkApp.previousFrameCamera.GetViewProjectionMatrix();
	std::vector<float> cameraData = {
		vkApp.camera.origin.x, vkApp.camera.origin.y, vkApp.camera.origin.z, 0.0f,
		vkApp.camera.topLeftCorner.x, vkApp.camera.topLeftCorner.y, vkApp.camera.topLeftCorner.z, 0.0f,
		vkApp.camera.horizontalEnd.x, vkApp.camera.horizontalEnd.y, vkApp.camera.horizontalEnd.z, 0.0f,
		vkApp.camera.verticalEnd.x, vkApp.camera.verticalEnd.y, vkApp.camera.verticalEnd.z, 0.0f,
		
		previousViewProjection[0][0], previousViewProjection[0][1], previousViewProjection[0][2], previousViewProjection[0][3],
		previousViewProjection[1][0], previousViewProjection[1][1], previousViewProjection[1][2], previousViewProjection[1][3],
		previousViewProjection[2][0], previousViewProjection[2][1], previousViewProjection[2][2], previousViewProjection[2][3],
		previousViewProjection[3][0], previousViewProjection[3][1], previousViewProjection[3][2], previousViewProjection[3][3]
	};
	VkDeviceSize cameraBufferSize = cameraData.size() * sizeof(float);
	VkBuffer cameraBuffer;
	VkDeviceMemory cameraBufferMemory;
	vkApp.CreateHostVisibleBuffer(cameraBufferSize, (void*)(cameraData.data()), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &cameraBuffer, &cameraBufferMemory);
	
	////////////////////////////
	///////////LIGHTS///////////
	////////////////////////////
	// See shaders/include/Datalayouts.glsl for structure layout
	assert(sceneFile.sphericalLights.size() > 0);
	const int numFloatsPerLight = 8;
	std::vector<float> lights;
	for (SphericalLightFromFile sL : sceneFile.sphericalLights)
	{
		lights.push_back(sL.centerAndRadius.x);
		lights.push_back(sL.centerAndRadius.y);
		lights.push_back(sL.centerAndRadius.z);
		lights.push_back(sL.centerAndRadius.w);
		lights.push_back(sL.emittance.x);
		lights.push_back(sL.emittance.y);
		lights.push_back(sL.emittance.z);
		lights.push_back(sL.emittance.w);
	}
	VkDeviceSize lightsBufferSize = lights.size() * sizeof(float);
	VkBuffer lightsBuffer;
	VkDeviceMemory lightsBufferMemory;
	vkApp.CreateHostVisibleBuffer(lightsBufferSize, (void*)(lights.data()), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &lightsBuffer, &lightsBufferMemory);
	
	////////////////////////////
	///////FRAME COUNTER////////
	////////////////////////////
	VkDeviceSize currentFrameBufferSize = sizeof(uint32_t);
	VkBuffer currentFrameBuffer;
	VkDeviceMemory currentFrameBufferMemory;
	vkApp.CreateHostVisibleBuffer(currentFrameBufferSize, (void*)(&vkApp.currentFrame), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &currentFrameBuffer, &currentFrameBufferMemory);
	
	////////////////////////////
	/////////OTHER DATA/////////
	////////////////////////////
	// See shaders/include/Datalayouts.glsl for structure layout
	size_t otherDataNumBytes = sizeof(int);
	char* otherData = new char[otherDataNumBytes];
	*(int*)(otherData) = int(lights.size() / numFloatsPerLight);
	VkDeviceSize otherDataBufferSize = otherDataNumBytes;
	VkBuffer otherDataBuffer;
	VkDeviceMemory otherDataBufferMemory;
	vkApp.CreateDeviceBuffer(otherDataBufferSize, (void*)(otherData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &otherDataBuffer, &otherDataBufferMemory);
	delete[] otherData;
	
	////////////////////////////
	//////////GEOMETRY//////////
	////////////////////////////
	std::vector<Mesh> meshes;
	for (const ModelFromFile& mff : sceneFile.models)
	{
		vkApp.LoadMesh(mff, &meshes);
	}
	uint32_t sceneTriangleCount = 0;
	for (const Mesh& mesh : meshes)
	{
		// divide by 3 because there are 3 floats per vertex
		// divide by 3 because there are 3 vertices per triangle
		sceneTriangleCount += mesh.vertices.size() / 3 / 3;
	}
	printf("Scene triangle count: %u\n", sceneTriangleCount);
	std::vector<std::vector<float>> geometryData;
	std::vector<glm::mat4x4> transformationData;
	for (Mesh& mesh : meshes)
	{
		geometryData.push_back(mesh.vertices);
		transformationData.push_back(glm::mat4x4(1.0f));
	}
	
	std::vector<float> perMeshAttributeData;
	std::vector<float> perVertexAttributeData;
	std::vector<uint32_t> customIDToAttributeArrayIndex;
	vkApp.BuildColorAndAttributeData(meshes, &perMeshAttributeData, &perVertexAttributeData, &customIDToAttributeArrayIndex);
	
	VkDeviceSize perMeshAttributeBufferSize = perMeshAttributeData.size() * sizeof(float);
	VkBuffer perMeshAttributeBuffer;
	VkDeviceMemory perMeshAttributeBufferMemory;
	vkApp.CreateDeviceBuffer(perMeshAttributeBufferSize, (void*)(perMeshAttributeData.data()), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &perMeshAttributeBuffer, &perMeshAttributeBufferMemory);
	perMeshAttributeData.resize(0);
	
	VkDeviceSize perVertexAttributeBufferSize = perVertexAttributeData.size() * sizeof(float);
	VkBuffer perVertexAttributeBuffer;
	VkDeviceMemory perVertexAttributeBufferMemory;
	vkApp.CreateDeviceBuffer(perVertexAttributeBufferSize, (void*)(perVertexAttributeData.data()), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &perVertexAttributeBuffer, &perVertexAttributeBufferMemory);
	perVertexAttributeData.resize(0);
	
	VkDeviceSize customIDToAttributeArrayIndexBufferSize = customIDToAttributeArrayIndex.size() * sizeof(uint32_t);
	VkBuffer customIDToAttributeArrayIndexBuffer;
	VkDeviceMemory customIDToAttributeArrayIndexBufferMemory;
	vkApp.CreateDeviceBuffer(customIDToAttributeArrayIndexBufferSize, (void*)(customIDToAttributeArrayIndex.data()), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &customIDToAttributeArrayIndexBuffer, &customIDToAttributeArrayIndexBufferMemory);
	customIDToAttributeArrayIndex.resize(0);
	
	////////////////////////////
	//////////SAMPLER///////////
	////////////////////////////
	VkSampler nearestSampler, linearSampler, nearestRepeatSampler;
	vkApp.CreateDefaultSampler(&nearestSampler, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	vkApp.CreateDefaultSampler(&linearSampler, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	vkApp.CreateDefaultSampler(&nearestRepeatSampler, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	
	////////////////////////////
	///ACCELERATION STRUCTURE///
	////////////////////////////
	VulkanAccelerationStructure accStruct;
	vkApp.CreateVulkanAccelerationStructure(geometryData, &accStruct);
	vkApp.BuildAccelerationStructure(accStruct);
	geometryData.resize(0);
	
	/////////////////////////////
	////RAY TRACING PIPELINES////
	/////////////////////////////
	RayTracingPipelineData rtpdColorPosition, rtpdAO;
	CreateRayTracingColorPositionPipelineAndData(vkApp, &rtpdColorPosition);
	CreateRayTracingAOPipelineAndData(vkApp, &rtpdAO);
	
	//////////////////////////
	////RAY TRACING IMAGES////
	//////////////////////////
	//Ray tracing COLOR image
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.pNext = NULL;
	imageInfo.flags = 0;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = vkApp.vkSurfaceExtent.width;
	imageInfo.extent.height = vkApp.vkSurfaceExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImage rayTracingColorImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &rayTracingColorImage))
	
	VkMemoryRequirements imageMemoryRequirements;
	vkGetImageMemoryRequirements(vkApp.vkDevice, rayTracingColorImage, &imageMemoryRequirements);
	VkMemoryAllocateInfo imageAllocateInfo = {};
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory rayTracingColorImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &rayTracingColorImageMemory))
	vkBindImageMemory(vkApp.vkDevice, rayTracingColorImage, rayTracingColorImageMemory, 0);
	
	VkImageViewCreateInfo imageViewInfo;
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.pNext = NULL;
    imageViewInfo.flags = 0;
    imageViewInfo.image = rayTracingColorImage;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.levelCount = 1;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.layerCount = 1;
    imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	VkImageView rayTracingColorImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &rayTracingColorImageView))
    
    //Transition ray tracing COLOR image layout
	vkApp.TransitionImageLayoutSingle(rayTracingColorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
    
    //Ray tracing POSITION image
	imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.extent.width = vkApp.vkSurfaceExtent.width;
	imageInfo.extent.height = vkApp.vkSurfaceExtent.height;
	VkImage rayTracingPositionImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &rayTracingPositionImage))
	
	vkGetImageMemoryRequirements(vkApp.vkDevice, rayTracingPositionImage, &imageMemoryRequirements);
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory rayTracingPositionImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &rayTracingPositionImageMemory))
	vkBindImageMemory(vkApp.vkDevice, rayTracingPositionImage, rayTracingPositionImageMemory, 0);
    
    imageViewInfo.image = rayTracingPositionImage;
    imageViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkImageView rayTracingPositionImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &rayTracingPositionImageView))
    
    //Transition ray tracing POSITION image layout
	vkApp.TransitionImageLayoutSingle(rayTracingPositionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
	
	//Ray tracing NORMAL image
	imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.extent.width = vkApp.vkSurfaceExtent.width;
	imageInfo.extent.height = vkApp.vkSurfaceExtent.height;
	VkImage rayTracingNormalImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &rayTracingNormalImage))
	
	vkGetImageMemoryRequirements(vkApp.vkDevice, rayTracingNormalImage, &imageMemoryRequirements);
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory rayTracingNormalImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &rayTracingNormalImageMemory))
	vkBindImageMemory(vkApp.vkDevice, rayTracingNormalImage, rayTracingNormalImageMemory, 0);
    
    imageViewInfo.image = rayTracingNormalImage;
    imageViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkImageView rayTracingNormalImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &rayTracingNormalImageView))
    
    //Transition ray tracing NORMAL image layout
	vkApp.TransitionImageLayoutSingle(rayTracingNormalImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
    
    //Ray tracing AO image
    VkExtent3D aoImageExtent = { vkApp.vkSurfaceExtent.width / 2, vkApp.vkSurfaceExtent.height / 2, 1 };
	imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imageInfo.extent = aoImageExtent;
	VkImage rayTracingAOImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &rayTracingAOImage))
	
	vkGetImageMemoryRequirements(vkApp.vkDevice, rayTracingAOImage, &imageMemoryRequirements);
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory rayTracingAOImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &rayTracingAOImageMemory))
	vkBindImageMemory(vkApp.vkDevice, rayTracingAOImage, rayTracingAOImageMemory, 0);
    
    imageViewInfo.image = rayTracingAOImage;
    imageViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	VkImageView rayTracingAOImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &rayTracingAOImageView))
    
    //Transition ray tracing AO image layout
	vkApp.TransitionImageLayoutSingle(rayTracingAOImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
	
	// Blue noise rotation image
	VulkanTexture blueNoiseTexture;
	//vkApp.CreateTexture("data/textures/BlueNoise64x64@2048.bmp", VK_FORMAT_R8_UNORM, &blueNoiseTexture);
	vkApp.CreateTexture("data/textures/LDR64x64.png", VK_FORMAT_R8_UNORM, &blueNoiseTexture);
	
	//Descriptor pool
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.flags = 0;
	descriptorPoolInfo.maxSets = rtpdColorPosition.numDescriptorSets + rtpdAO.numDescriptorSets;
	descriptorPoolInfo.poolSizeCount = poolSizes.size();
	descriptorPoolInfo.pPoolSizes = poolSizes.data();
	VkDescriptorPool descriptorPool;
	CHECK_VK_RESULT(vkCreateDescriptorPool(vkApp.vkDevice, &descriptorPoolInfo, NULL, &descriptorPool))
	
	CreateDescriptorSetLayoutsColorPosition(vkApp, descriptorPool, accStruct, cameraBuffer, cameraBufferSize, lightsBuffer, lightsBufferSize, otherDataBuffer, otherDataBufferSize, customIDToAttributeArrayIndexBuffer, customIDToAttributeArrayIndexBufferSize, perMeshAttributeBuffer, perMeshAttributeBufferSize, perVertexAttributeBuffer, perVertexAttributeBufferSize, rayTracingColorImageView, rayTracingPositionImageView, rayTracingNormalImageView, &rtpdColorPosition);
	
	CreateDescriptorSetLayoutsAO(vkApp, descriptorPool, accStruct, rayTracingPositionImageView, rayTracingNormalImageView, nearestSampler, rayTracingAOImageView, currentFrameBuffer, blueNoiseTexture.imageView, nearestRepeatSampler, &rtpdAO);
    
    ////////////////////////////
	/////GRAPHICS PIPELINE//////
	////////////////////////////
	VkShaderModule vertexShaderModule, fragmentShaderModuleSubpass0, fragmentShaderModuleSubpass1;
	vkApp.CreateShaderModule("src/shaders/out/vert.spv", &vertexShaderModule);
	vkApp.CreateShaderModule("src/shaders/out/fragBlur.spv", &fragmentShaderModuleSubpass0);
	vkApp.CreateShaderModule("src/shaders/out/fragTemporalIntegration.spv", &fragmentShaderModuleSubpass1);

	// Vertex buffer
	// UV coordinates are a bit weird as the image has to be flipped to
	// be shown correctly when sampled (it's upside-down)
	std::vector<float> vertexData = {
		-1.0f, -1.0f,	0.0f, 0.0f,
		-1.0f,  1.0f,	0.0f, 1.0f,
		 1.0f,  1.0f,	1.0f, 1.0f,
		 1.0f, -1.0f,	1.0f, 0.0f
	};
	VkDeviceSize vertexBufferSize = vertexData.size() * sizeof(float);
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	vkApp.CreateDeviceBuffer(vertexBufferSize, (void*)(vertexData.data()), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertexBuffer, &vertexBufferMemory);
	// Index buffer
	std::vector<uint32_t> indexData = {
		0, 1, 2,
		2, 3, 0
	};
	VkDeviceSize indexBufferSize = indexData.size() * sizeof(uint32_t);
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
	vkApp.CreateDeviceBuffer(indexBufferSize, (void*)(indexData.data()), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &indexBuffer, &indexBufferMemory);
	// Blur buffer
	blurVariable = 0;
	VkDeviceSize blurBufferSize = sizeof(uint32_t);
	VkBuffer blurBuffer;
	VkDeviceMemory blurBufferMemory;
	vkApp.CreateHostVisibleBuffer(blurBufferSize, (void*)(&blurVariable), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &blurBuffer, &blurBufferMemory);
	
	// PREVIOUS FRAME IMAGE
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.format = vkApp.GetDefaultFramebufferFormat();
	imageInfo.extent.width = vkApp.vkSurfaceExtent.width;
	imageInfo.extent.height = vkApp.vkSurfaceExtent.height;
	VkImage previousFrameImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &previousFrameImage))

	vkGetImageMemoryRequirements(vkApp.vkDevice, previousFrameImage, &imageMemoryRequirements);
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory previousFrameImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &previousFrameImageMemory))
	vkBindImageMemory(vkApp.vkDevice, previousFrameImage, previousFrameImageMemory, 0);
    
    imageViewInfo.image = previousFrameImage;
    imageViewInfo.format = vkApp.GetDefaultFramebufferFormat();
	VkImageView previousFrameImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &previousFrameImageView))
    
    //Transition previous frame image layout
	vkApp.TransitionImageLayoutSingle(previousFrameImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
    
    // CURRENT FRAME IMAGE
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    imageInfo.format = vkApp.GetDefaultFramebufferFormat();
	imageInfo.extent.width = vkApp.vkSurfaceExtent.width;
	imageInfo.extent.height = vkApp.vkSurfaceExtent.height;
	VkImage currentFrameImage;
	CHECK_VK_RESULT(vkCreateImage(vkApp.vkDevice, &imageInfo, NULL, &currentFrameImage))

	vkGetImageMemoryRequirements(vkApp.vkDevice, currentFrameImage, &imageMemoryRequirements);
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageMemoryRequirements.size;
	imageAllocateInfo.memoryTypeIndex = vkApp.FindMemoryType(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory currentFrameImageMemory;
	CHECK_VK_RESULT(vkAllocateMemory(vkApp.vkDevice, &imageAllocateInfo, NULL, &currentFrameImageMemory))
	vkBindImageMemory(vkApp.vkDevice, currentFrameImage, currentFrameImageMemory, 0);
    
    imageViewInfo.image = currentFrameImage;
    imageViewInfo.format = vkApp.GetDefaultFramebufferFormat();
	VkImageView currentFrameImageView;
    CHECK_VK_RESULT(vkCreateImageView(vkApp.vkDevice, &imageViewInfo, NULL, &currentFrameImageView))
    
    //Transition current frame image layout
	vkApp.TransitionImageLayoutSingle(currentFrameImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);


	// Set up pipeline
	// Shaders for subpass 0
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfosSubpass0(2);
	shaderStageInfosSubpass0[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfosSubpass0[0].pNext = NULL;
	shaderStageInfosSubpass0[0].flags = 0;
	shaderStageInfosSubpass0[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageInfosSubpass0[0].module = vertexShaderModule;
	shaderStageInfosSubpass0[0].pName = "main";
	shaderStageInfosSubpass0[0].pSpecializationInfo = NULL;
	shaderStageInfosSubpass0[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfosSubpass0[1].pNext = NULL;
	shaderStageInfosSubpass0[1].flags = 0;
	shaderStageInfosSubpass0[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageInfosSubpass0[1].module = fragmentShaderModuleSubpass0;
	shaderStageInfosSubpass0[1].pName = "main";
	shaderStageInfosSubpass0[1].pSpecializationInfo = NULL;
	// Shaders for subpass 1
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfosSubpass1(2);
	shaderStageInfosSubpass1[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfosSubpass1[0].pNext = NULL;
	shaderStageInfosSubpass1[0].flags = 0;
	shaderStageInfosSubpass1[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageInfosSubpass1[0].module = vertexShaderModule;
	shaderStageInfosSubpass1[0].pName = "main";
	shaderStageInfosSubpass1[0].pSpecializationInfo = NULL;
	shaderStageInfosSubpass1[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfosSubpass1[1].pNext = NULL;
	shaderStageInfosSubpass1[1].flags = 0;
	shaderStageInfosSubpass1[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageInfosSubpass1[1].module = fragmentShaderModuleSubpass1;
	shaderStageInfosSubpass1[1].pName = "main";
	shaderStageInfosSubpass1[1].pSpecializationInfo = NULL;
	
	VkVertexInputBindingDescription inputDesc = {};
	inputDesc.binding = 0;
	inputDesc.stride = 4 * sizeof(float);
	inputDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::vector<VkVertexInputAttributeDescription> attributeDescs(2);
	VkVertexInputAttributeDescription& vertexDesc = attributeDescs[0];
	vertexDesc.location = 0;
	vertexDesc.binding = 0;
	vertexDesc.format = VK_FORMAT_R32G32_SFLOAT;
	vertexDesc.offset = 0;
	VkVertexInputAttributeDescription& uvDesc = attributeDescs[1];
	uvDesc.location = 1;
	uvDesc.binding = 0;
	uvDesc.format = VK_FORMAT_R32G32_SFLOAT;
	uvDesc.offset = 2 * sizeof(float);

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.pNext = NULL;
	vertexInputInfo.flags = 0;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &inputDesc;
	vertexInputInfo.vertexAttributeDescriptionCount = attributeDescs.size();
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.pNext = NULL;
	inputAssemblyInfo.flags = 0;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = vkApp.GetDefaultViewport();
	VkRect2D scissor = vkApp.GetDefaultScissor();
	VkPipelineViewportStateCreateInfo viewportInfo = {};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.pNext = NULL;
	viewportInfo.flags = 0;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
	rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationInfo.pNext = NULL;
	rasterizationInfo.flags = 0;
	rasterizationInfo.depthClampEnable = VK_FALSE;
	rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationInfo.depthBiasEnable = VK_FALSE;
	/* IGNORED
	rasterization_info.depthBiasConstantFactor
	rasterization_info.depthBiasClamp
	rasterization_info.depthBiasSlopeFactor*/
	rasterizationInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
	multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleInfo.pNext = NULL;
	multisampleInfo.flags = 0;
	multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleInfo.sampleShadingEnable = VK_FALSE;
	/* IGNORED
	multisample_info.minSampleShading
	multisample_info.pSampleMask*/
	multisampleInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleInfo.alphaToOneEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.blendEnable = VK_FALSE;
	/* IGNORED
	color_blend_attachment.srcColorBlendFactor 
	color_blend_attachment.dstColorBlendFactor
	color_blend_attachment.colorBlendOp
	color_blend_attachment.srcAlphaBlendFactor
	color_blend_attachment.dstAlphaBlendFactor
	color_blend_attachment.alphaBlendOp*/
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
	colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfo.pNext = NULL;
	colorBlendInfo.flags = 0;
	colorBlendInfo.logicOpEnable = VK_FALSE;
	/* INGORED
	color_blend_info.logicOp*/
	colorBlendInfo.attachmentCount = 1;
	colorBlendInfo.pAttachments = &colorBlendAttachment;
	colorBlendInfo.blendConstants[0] = 0.0f;
	colorBlendInfo.blendConstants[1] = 0.0f;
	colorBlendInfo.blendConstants[2] = 0.0f;
	colorBlendInfo.blendConstants[3] = 0.0f;

	// Descriptors setup SUBPASS 0
	std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindingsSubpass0(RS0_DESCRIPTOR_SET_NUM_BINDINGS);
	VkDescriptorSetLayoutBinding& rayTracingImageBinding = descriptorSetLayoutBindingsSubpass0[RS0_RAY_TRACING_IMAGE_BINDING_LOCATION];
	rayTracingImageBinding.binding = RS0_RAY_TRACING_IMAGE_BINDING_LOCATION;
	rayTracingImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rayTracingImageBinding.descriptorCount = 1;
	rayTracingImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	rayTracingImageBinding.pImmutableSamplers = NULL;
	VkDescriptorSetLayoutBinding& rayTracingAOImageBinding = descriptorSetLayoutBindingsSubpass0[RS0_AO_IMAGE_BINDING_LOCATION];
	rayTracingAOImageBinding.binding = RS0_AO_IMAGE_BINDING_LOCATION;
	rayTracingAOImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	rayTracingAOImageBinding.descriptorCount = 1;
	rayTracingAOImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	rayTracingAOImageBinding.pImmutableSamplers = NULL;
	VkDescriptorSetLayoutBinding& blurUniformBinding = descriptorSetLayoutBindingsSubpass0[RS0_BLUR_VARIABLE_BINDING_LOCATION];
	blurUniformBinding.binding = RS0_BLUR_VARIABLE_BINDING_LOCATION;
	blurUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	blurUniformBinding.descriptorCount = 1;
	blurUniformBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	blurUniformBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutCreateInfo descriptorSetInfoGraphics = {};
	descriptorSetInfoGraphics.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetInfoGraphics.pNext = NULL;
	descriptorSetInfoGraphics.flags = 0;
	descriptorSetInfoGraphics.bindingCount = descriptorSetLayoutBindingsSubpass0.size();
	descriptorSetInfoGraphics.pBindings = descriptorSetLayoutBindingsSubpass0.data();
	VkDescriptorSetLayout descriptorSetLayoutGraphicsSubpass0;
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(vkApp.vkDevice, &descriptorSetInfoGraphics, NULL, &descriptorSetLayoutGraphicsSubpass0))

	VkPipelineLayoutCreateInfo pipelineLayoutInfoGraphics = {};
	pipelineLayoutInfoGraphics.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfoGraphics.pNext = NULL;
	pipelineLayoutInfoGraphics.flags = 0;
	pipelineLayoutInfoGraphics.setLayoutCount = 1;
	pipelineLayoutInfoGraphics.pSetLayouts = &descriptorSetLayoutGraphicsSubpass0;
	pipelineLayoutInfoGraphics.pushConstantRangeCount = 0;
	pipelineLayoutInfoGraphics.pPushConstantRanges = NULL;
	VkPipelineLayout pipelineLayoutGraphicsSubpass0;
	CHECK_VK_RESULT(vkCreatePipelineLayout(vkApp.vkDevice, &pipelineLayoutInfoGraphics, NULL, &pipelineLayoutGraphicsSubpass0))
	
	// Descriptors setup SUBPASS 1
	std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindingsSubpass1(RS1_DESCRIPTOR_SET_NUM_BINDINGS);
	VkDescriptorSetLayoutBinding& previousFrameImageBinding = descriptorSetLayoutBindingsSubpass1[RS1_PREVIOUS_FRAME_IMAGE_BINDING_LOCATION];
	previousFrameImageBinding.binding = RS1_PREVIOUS_FRAME_IMAGE_BINDING_LOCATION;
	previousFrameImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	previousFrameImageBinding.descriptorCount = 1;
	previousFrameImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	previousFrameImageBinding.pImmutableSamplers = NULL;
	VkDescriptorSetLayoutBinding& positionImageBinding = descriptorSetLayoutBindingsSubpass1[RS1_POSITION_IMAGE_BINDING_LOCATION];
	positionImageBinding.binding = RS1_POSITION_IMAGE_BINDING_LOCATION;
	positionImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	positionImageBinding.descriptorCount = 1;
	positionImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	positionImageBinding.pImmutableSamplers = NULL;
	VkDescriptorSetLayoutBinding& currentFrameImageBinding = descriptorSetLayoutBindingsSubpass1[RS1_CURRENT_FRAME_IMAGE_BINDING_LOCATION];
	currentFrameImageBinding.binding = RS1_CURRENT_FRAME_IMAGE_BINDING_LOCATION;
	currentFrameImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	currentFrameImageBinding.descriptorCount = 1;
	currentFrameImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	currentFrameImageBinding.pImmutableSamplers = NULL;
	VkDescriptorSetLayoutBinding& cameraImageBinding = descriptorSetLayoutBindingsSubpass1[RS1_CAMERA_BUFFER_BINDING_LOCATION];
	cameraImageBinding.binding = RS1_CAMERA_BUFFER_BINDING_LOCATION;
	cameraImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraImageBinding.descriptorCount = 1;
	cameraImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	cameraImageBinding.pImmutableSamplers = NULL;

	descriptorSetInfoGraphics.bindingCount = descriptorSetLayoutBindingsSubpass1.size();
	descriptorSetInfoGraphics.pBindings = descriptorSetLayoutBindingsSubpass1.data();
	VkDescriptorSetLayout descriptorSetLayoutGraphicsSubpass1;
	CHECK_VK_RESULT(vkCreateDescriptorSetLayout(vkApp.vkDevice, &descriptorSetInfoGraphics, NULL, &descriptorSetLayoutGraphicsSubpass1))

	pipelineLayoutInfoGraphics.setLayoutCount = 1;
	pipelineLayoutInfoGraphics.pSetLayouts = &descriptorSetLayoutGraphicsSubpass1;
	VkPipelineLayout pipelineLayoutGraphicsSubpass1;
	CHECK_VK_RESULT(vkCreatePipelineLayout(vkApp.vkDevice, &pipelineLayoutInfoGraphics, NULL, &pipelineLayoutGraphicsSubpass1))
	
	// Create descriptor pool
	std::vector<VkDescriptorPoolSize> poolSizesGraphics = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1 },
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfoGraphics = {};
	descriptorPoolInfoGraphics.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfoGraphics.pNext = NULL;
	descriptorPoolInfoGraphics.flags = 0;
	descriptorPoolInfoGraphics.maxSets = 2;
	descriptorPoolInfoGraphics.poolSizeCount = poolSizesGraphics.size();
	descriptorPoolInfoGraphics.pPoolSizes = poolSizesGraphics.data();
	VkDescriptorPool descriptorPoolGraphics;
	CHECK_VK_RESULT(vkCreateDescriptorPool(vkApp.vkDevice, &descriptorPoolInfoGraphics, NULL, &descriptorPoolGraphics))
	
	// Allocate sets
	// Subpass 0
	VkDescriptorSetAllocateInfo descriptorSetGraphicsAllocateInfo = {};
	descriptorSetGraphicsAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetGraphicsAllocateInfo.pNext = NULL;
	descriptorSetGraphicsAllocateInfo.descriptorPool = descriptorPoolGraphics;
	descriptorSetGraphicsAllocateInfo.descriptorSetCount = 1;
	descriptorSetGraphicsAllocateInfo.pSetLayouts = &descriptorSetLayoutGraphicsSubpass0;
	VkDescriptorSet descriptorSetGraphicsSubpass0;
	CHECK_VK_RESULT(vkAllocateDescriptorSets(vkApp.vkDevice, &descriptorSetGraphicsAllocateInfo, &descriptorSetGraphicsSubpass0))
	// Subpass 1
	descriptorSetGraphicsAllocateInfo.pSetLayouts = &descriptorSetLayoutGraphicsSubpass1;
	VkDescriptorSet descriptorSetGraphicsSubpass1;
	CHECK_VK_RESULT(vkAllocateDescriptorSets(vkApp.vkDevice, &descriptorSetGraphicsAllocateInfo, &descriptorSetGraphicsSubpass1))
	
	// Update set for subpass 0
    std::vector<VkWriteDescriptorSet> descriptorSetGraphicsWritesSubpass0(RS0_DESCRIPTOR_SET_NUM_BINDINGS);
    // Color image
	VkDescriptorImageInfo descriptorRayTracingColorImageInfoGraphics = {};
    descriptorRayTracingColorImageInfoGraphics.sampler = linearSampler;
    descriptorRayTracingColorImageInfoGraphics.imageView = rayTracingColorImageView;
    descriptorRayTracingColorImageInfoGraphics.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet& rayTracingImageWriteGraphics = descriptorSetGraphicsWritesSubpass0[RS0_RAY_TRACING_IMAGE_BINDING_LOCATION];
    rayTracingImageWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingImageWriteGraphics.pNext = NULL;
    rayTracingImageWriteGraphics.dstSet = descriptorSetGraphicsSubpass0;
    rayTracingImageWriteGraphics.dstBinding = RS0_RAY_TRACING_IMAGE_BINDING_LOCATION;
    rayTracingImageWriteGraphics.dstArrayElement = 0;
    rayTracingImageWriteGraphics.descriptorCount = 1;
    rayTracingImageWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rayTracingImageWriteGraphics.pImageInfo = &descriptorRayTracingColorImageInfoGraphics;
    rayTracingImageWriteGraphics.pBufferInfo = NULL;
    rayTracingImageWriteGraphics.pTexelBufferView = NULL;
    // AO image
    VkDescriptorImageInfo descriptorRayTracingAOImageInfoGraphics = {};
    descriptorRayTracingAOImageInfoGraphics.sampler = linearSampler;
    descriptorRayTracingAOImageInfoGraphics.imageView = rayTracingAOImageView;
    descriptorRayTracingAOImageInfoGraphics.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet& rayTracingOcclusionImageWriteGraphics = descriptorSetGraphicsWritesSubpass0[RS0_AO_IMAGE_BINDING_LOCATION];
    rayTracingOcclusionImageWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    rayTracingOcclusionImageWriteGraphics.pNext = NULL;
    rayTracingOcclusionImageWriteGraphics.dstSet = descriptorSetGraphicsSubpass0;
    rayTracingOcclusionImageWriteGraphics.dstBinding = RS0_AO_IMAGE_BINDING_LOCATION;
    rayTracingOcclusionImageWriteGraphics.dstArrayElement = 0;
    rayTracingOcclusionImageWriteGraphics.descriptorCount = 1;
    rayTracingOcclusionImageWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    rayTracingOcclusionImageWriteGraphics.pImageInfo = &descriptorRayTracingAOImageInfoGraphics;
    rayTracingOcclusionImageWriteGraphics.pBufferInfo = NULL;
    rayTracingOcclusionImageWriteGraphics.pTexelBufferView = NULL;
    // Blur variable uniform
    VkDescriptorBufferInfo blurBufferInfoGraphics = {};
    blurBufferInfoGraphics.buffer = blurBuffer;
    blurBufferInfoGraphics.offset = 0;
    blurBufferInfoGraphics.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet& blurBufferWriteGraphics = descriptorSetGraphicsWritesSubpass0[RS0_BLUR_VARIABLE_BINDING_LOCATION];
    blurBufferWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    blurBufferWriteGraphics.pNext = NULL;
    blurBufferWriteGraphics.dstSet = descriptorSetGraphicsSubpass0;
    blurBufferWriteGraphics.dstBinding = RS0_BLUR_VARIABLE_BINDING_LOCATION;
    blurBufferWriteGraphics.dstArrayElement = 0;
    blurBufferWriteGraphics.descriptorCount = 1;
    blurBufferWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    blurBufferWriteGraphics.pImageInfo = NULL;
    blurBufferWriteGraphics.pBufferInfo = &blurBufferInfoGraphics;
    blurBufferWriteGraphics.pTexelBufferView = NULL;
    // Update
    vkUpdateDescriptorSets(vkApp.vkDevice, descriptorSetGraphicsWritesSubpass0.size(), descriptorSetGraphicsWritesSubpass0.data(), 0, NULL);
    
    // Update set for subpass 1
    std::vector<VkWriteDescriptorSet> descriptorSetGraphicsWritesSubpass1(RS1_DESCRIPTOR_SET_NUM_BINDINGS);
    // Previous frame image
	VkDescriptorImageInfo descriptorPreviousFrameImageInfoGraphics = {};
    descriptorPreviousFrameImageInfoGraphics.sampler = linearSampler;
    descriptorPreviousFrameImageInfoGraphics.imageView = previousFrameImageView;
    descriptorPreviousFrameImageInfoGraphics.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet& previousFrameImageWriteGraphics = descriptorSetGraphicsWritesSubpass1[RS1_PREVIOUS_FRAME_IMAGE_BINDING_LOCATION];
    previousFrameImageWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    previousFrameImageWriteGraphics.pNext = NULL;
    previousFrameImageWriteGraphics.dstSet = descriptorSetGraphicsSubpass1;
    previousFrameImageWriteGraphics.dstBinding = RS1_PREVIOUS_FRAME_IMAGE_BINDING_LOCATION;
    previousFrameImageWriteGraphics.dstArrayElement = 0;
    previousFrameImageWriteGraphics.descriptorCount = 1;
    previousFrameImageWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    previousFrameImageWriteGraphics.pImageInfo = &descriptorPreviousFrameImageInfoGraphics;
    previousFrameImageWriteGraphics.pBufferInfo = NULL;
    previousFrameImageWriteGraphics.pTexelBufferView = NULL;
    // Position image
    VkDescriptorImageInfo descriptorPositionImageInfoGraphics = {};
    descriptorPositionImageInfoGraphics.sampler = nearestSampler;
    descriptorPositionImageInfoGraphics.imageView = rayTracingPositionImageView;
    descriptorPositionImageInfoGraphics.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet& positionImageWriteGraphics = descriptorSetGraphicsWritesSubpass1[RS1_POSITION_IMAGE_BINDING_LOCATION];
    positionImageWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    positionImageWriteGraphics.pNext = NULL;
    positionImageWriteGraphics.dstSet = descriptorSetGraphicsSubpass1;
    positionImageWriteGraphics.dstBinding = RS1_POSITION_IMAGE_BINDING_LOCATION;
    positionImageWriteGraphics.dstArrayElement = 0;
    positionImageWriteGraphics.descriptorCount = 1;
    positionImageWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    positionImageWriteGraphics.pImageInfo = &descriptorPositionImageInfoGraphics;
    positionImageWriteGraphics.pBufferInfo = NULL;
    positionImageWriteGraphics.pTexelBufferView = NULL;
    // Current frame image
    VkDescriptorImageInfo descriptorCurrentFrameImageInfoGraphics = {};
    descriptorCurrentFrameImageInfoGraphics.sampler = VK_NULL_HANDLE;
    descriptorCurrentFrameImageInfoGraphics.imageView = currentFrameImageView;
    descriptorCurrentFrameImageInfoGraphics.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet& currentFrameImageWriteGraphics = descriptorSetGraphicsWritesSubpass1[RS1_CURRENT_FRAME_IMAGE_BINDING_LOCATION];
    currentFrameImageWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    currentFrameImageWriteGraphics.pNext = NULL;
    currentFrameImageWriteGraphics.dstSet = descriptorSetGraphicsSubpass1;
    currentFrameImageWriteGraphics.dstBinding = RS1_CURRENT_FRAME_IMAGE_BINDING_LOCATION;
    currentFrameImageWriteGraphics.dstArrayElement = 0;
    currentFrameImageWriteGraphics.descriptorCount = 1;
    currentFrameImageWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    currentFrameImageWriteGraphics.pImageInfo = &descriptorCurrentFrameImageInfoGraphics;
    currentFrameImageWriteGraphics.pBufferInfo = NULL;
    currentFrameImageWriteGraphics.pTexelBufferView = NULL;
    // Camera buffer
    VkDescriptorBufferInfo cameraBufferInfoGraphics = {};
    cameraBufferInfoGraphics.buffer = cameraBuffer;
    cameraBufferInfoGraphics.offset = 0;
    cameraBufferInfoGraphics.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet& cameraBufferWriteGraphics = descriptorSetGraphicsWritesSubpass1[RS1_CAMERA_BUFFER_BINDING_LOCATION];
    cameraBufferWriteGraphics.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    cameraBufferWriteGraphics.pNext = NULL;
    cameraBufferWriteGraphics.dstSet = descriptorSetGraphicsSubpass1;
    cameraBufferWriteGraphics.dstBinding = RS1_CAMERA_BUFFER_BINDING_LOCATION;
    cameraBufferWriteGraphics.dstArrayElement = 0;
    cameraBufferWriteGraphics.descriptorCount = 1;
    cameraBufferWriteGraphics.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBufferWriteGraphics.pImageInfo = NULL;
    cameraBufferWriteGraphics.pBufferInfo = &cameraBufferInfoGraphics;
    cameraBufferWriteGraphics.pTexelBufferView = NULL;
    // Update
    vkUpdateDescriptorSets(vkApp.vkDevice, descriptorSetGraphicsWritesSubpass1.size(), descriptorSetGraphicsWritesSubpass1.data(), 0, NULL);

	std::vector<VkAttachmentDescription> attachments(2);
	attachments[0].flags = 0;
	attachments[0].format = vkApp.GetDefaultFramebufferFormat();
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachments[1].flags = 0;
	attachments[1].format = vkApp.GetDefaultFramebufferFormat();
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkAttachmentReference colorAttachmentRefsSubpass0;
	colorAttachmentRefsSubpass0.attachment = 0;
	colorAttachmentRefsSubpass0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	
	VkAttachmentReference inputAttachmentRefsSubpass1;
	inputAttachmentRefsSubpass1.attachment = 0;
	inputAttachmentRefsSubpass1.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	
	VkAttachmentReference colorAttachmentRefsSubpass1;
	colorAttachmentRefsSubpass1.attachment = 1;
	colorAttachmentRefsSubpass1.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	std::vector<VkSubpassDescription> subpasses(2);
	subpasses[0].flags = 0;
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].inputAttachmentCount = 0;
	subpasses[0].pInputAttachments = NULL;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = &colorAttachmentRefsSubpass0;
	subpasses[0].pResolveAttachments = NULL;
	subpasses[0].pDepthStencilAttachment = NULL;
	subpasses[0].preserveAttachmentCount = 0;
	subpasses[0].pPreserveAttachments = NULL;
	subpasses[1].flags = 0;
	subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[1].inputAttachmentCount = 1;
	subpasses[1].pInputAttachments = &inputAttachmentRefsSubpass1;
	subpasses[1].colorAttachmentCount = 1;
	subpasses[1].pColorAttachments = &colorAttachmentRefsSubpass1;
	subpasses[1].pResolveAttachments = NULL;
	subpasses[1].pDepthStencilAttachment = NULL;
	subpasses[1].preserveAttachmentCount = 0;
	subpasses[1].pPreserveAttachments = NULL;

	std::vector<VkSubpassDependency> subpassDependencies(2);
	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].srcAccessMask = 0;
	subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[0].dependencyFlags = 0;
	subpassDependencies[1].srcSubpass = 0;
	subpassDependencies[1].dstSubpass = 1;
	subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	subpassDependencies[1].dependencyFlags = 0;


	VkRenderPassCreateInfo renderpassInfo = {};
	renderpassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderpassInfo.pNext = NULL;
	renderpassInfo.flags = 0;
	renderpassInfo.attachmentCount = attachments.size();
	renderpassInfo.pAttachments = attachments.data();
	renderpassInfo.subpassCount = subpasses.size();
	renderpassInfo.pSubpasses = subpasses.data();
	renderpassInfo.dependencyCount = subpassDependencies.size();
	renderpassInfo.pDependencies = subpassDependencies.data();
	VkRenderPass renderPass;
	CHECK_VK_RESULT(vkCreateRenderPass(vkApp.vkDevice, &renderpassInfo, NULL, &renderPass))

	// Create pipeline for subpass 0
	VkGraphicsPipelineCreateInfo graphicsPipelineInfo;
	graphicsPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineInfo.pNext = NULL;
	graphicsPipelineInfo.flags = 0;
	graphicsPipelineInfo.stageCount = shaderStageInfosSubpass0.size();
	graphicsPipelineInfo.pStages = shaderStageInfosSubpass0.data();
	graphicsPipelineInfo.pVertexInputState = &vertexInputInfo;
	graphicsPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	graphicsPipelineInfo.pTessellationState = NULL;
	graphicsPipelineInfo.pViewportState = &viewportInfo;
	graphicsPipelineInfo.pRasterizationState = &rasterizationInfo;
	graphicsPipelineInfo.pMultisampleState = &multisampleInfo;
	graphicsPipelineInfo.pDepthStencilState = NULL;
	graphicsPipelineInfo.pColorBlendState = &colorBlendInfo;
	graphicsPipelineInfo.pDynamicState = NULL;
	graphicsPipelineInfo.layout = pipelineLayoutGraphicsSubpass0;
	graphicsPipelineInfo.renderPass = renderPass;
	graphicsPipelineInfo.subpass = 0;
	graphicsPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	VkPipeline graphicsPipelineSubpass0;
	CHECK_VK_RESULT(vkCreateGraphicsPipelines(vkApp.vkDevice, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, NULL, &graphicsPipelineSubpass0))
	// Create pipeline for subpass 1
	graphicsPipelineInfo.stageCount = shaderStageInfosSubpass1.size();
	graphicsPipelineInfo.pStages = shaderStageInfosSubpass1.data();
	graphicsPipelineInfo.layout = pipelineLayoutGraphicsSubpass1;
	graphicsPipelineInfo.subpass = 1;
	graphicsPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	VkPipeline graphicsPipelineSubpass1;
	CHECK_VK_RESULT(vkCreateGraphicsPipelines(vkApp.vkDevice, VK_NULL_HANDLE, 1, &graphicsPipelineInfo, NULL, &graphicsPipelineSubpass1))
    
    // FRAMEBUFFERS
	std::vector<VkFramebuffer> framebuffersRenderPass;
	//vkApp.CreateDefaultFramebuffers(framebuffersRenderPass, renderPass);
	std::vector<std::vector<VkImageView>> framebufferImageViews(vkApp.vkSwapchainImageViews.size());
	for (size_t i = 0; i < vkApp.vkSwapchainImageViews.size(); i++)
	{
		framebufferImageViews[i] = {
			currentFrameImageView,
			vkApp.vkSwapchainImageViews[i]
		};
	}
	vkApp.CreateRenderPassFramebuffers(framebufferImageViews, vkApp.vkSurfaceExtent.width, vkApp.vkSurfaceExtent.height, framebuffersRenderPass, renderPass);
    
	////////////////////////////
	///////////RECORD///////////
	////////////////////////////
    std::vector<VkCommandBuffer> graphicsQueueCommandBuffers;
	vkApp.AllocateDefaultGraphicsQueueCommandBuffers(graphicsQueueCommandBuffers);
	VkDeviceSize offset = 0;
	for (size_t i = 0; i < graphicsQueueCommandBuffers.size(); i++)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.pNext = NULL;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
		beginInfo.pInheritanceInfo = NULL;
		CHECK_VK_RESULT(vkBeginCommandBuffer(graphicsQueueCommandBuffers[i], &beginInfo))
		
#if REBUILD_ACC_STRUCT
		// Rebuild acceleration structure
		vkCmdBuildAccelerationStructureNV(graphicsQueueCommandBuffers[i], &accStruct.topAccStruct.accelerationStructureInfo, accStruct.geometryInstancesBuffer, 0, VK_FALSE, accStruct.topAccStruct.accelerationStructure, VK_NULL_HANDLE, accStruct.scratchBuffer, 0);
		//Barrier before tracing can begin
		VkMemoryBarrier memoryBarrier = {};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.pNext = NULL;
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV;
		vkCmdPipelineBarrier(graphicsQueueCommandBuffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);
#endif
		
		// Ray trace color, position and normal
#if DEFERRED_PASS
		vkCmdBindPipeline(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtpdColorPosition.rayTracingPipeline);
		vkCmdBindDescriptorSets(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtpdColorPosition.rayTracingPipelineLayout, 0, rtpdColorPosition.descriptorSets.size(), rtpdColorPosition.descriptorSets.data(), 0, NULL);
		vkCmdTraceRaysNV(graphicsQueueCommandBuffers[i], 
			rtpdColorPosition.shaderBindingTableBuffer, 0 * rtpdColorPosition.shaderGroupHandleSize,
			rtpdColorPosition.shaderBindingTableBuffer, 3 * rtpdColorPosition.shaderGroupHandleSize, rtpdColorPosition.shaderGroupHandleSize,
			rtpdColorPosition.shaderBindingTableBuffer, 1 * rtpdColorPosition.shaderGroupHandleSize, rtpdColorPosition.shaderGroupHandleSize,
			 VK_NULL_HANDLE, 0, 0, vkApp.vkSurfaceExtent.width, vkApp.vkSurfaceExtent.height, 1);
#endif
		
		// Barrier - wait for ray tracing to finish and transition images
		vkApp.TransitionImageLayoutInProgress(rayTracingColorImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingPositionImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingNormalImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, graphicsQueueCommandBuffers[i]);
		
		// Ray trace AO
#if AO_PASS
		vkCmdBindPipeline(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtpdAO.rayTracingPipeline);
		vkCmdBindDescriptorSets(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rtpdAO.rayTracingPipelineLayout, 0, rtpdAO.descriptorSets.size(), rtpdAO.descriptorSets.data(), 0, NULL);
		vkCmdTraceRaysNV(graphicsQueueCommandBuffers[i], 
			rtpdAO.shaderBindingTableBuffer, 0 * rtpdAO.shaderGroupHandleSize,
			rtpdAO.shaderBindingTableBuffer, 2 * rtpdAO.shaderGroupHandleSize, rtpdAO.shaderGroupHandleSize,
			rtpdAO.shaderBindingTableBuffer, 1 * rtpdAO.shaderGroupHandleSize, rtpdAO.shaderGroupHandleSize,
			 VK_NULL_HANDLE, 0, 0, aoImageExtent.width, aoImageExtent.height, 1);
#endif
			 
		// Barrier - wait for ray tracing to finish and transition images
		vkApp.TransitionImageLayoutInProgress(rayTracingAOImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, graphicsQueueCommandBuffers[i]);
		
		// Blur ambient occlusion result and combine with light result
		VkClearColorValue clearColorValue = { 0.0f, 0.0f, 0.0f, 1.0f };
		VkClearValue clearValues[] = { clearColorValue };
		// Begin render pass
		VkRenderPassBeginInfo renderpassInfo = {};
		renderpassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderpassInfo.pNext = NULL;
		renderpassInfo.renderPass = renderPass;
		renderpassInfo.framebuffer = framebuffersRenderPass[i];
		renderpassInfo.renderArea.offset = { 0, 0 };
		renderpassInfo.renderArea.extent = vkApp.vkSurfaceExtent;
		renderpassInfo.clearValueCount = 1;
		renderpassInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(graphicsQueueCommandBuffers[i], &renderpassInfo, VK_SUBPASS_CONTENTS_INLINE);
		// Subpass 0
#if BLUR_PASS
		vkCmdBindPipeline(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineSubpass0);
		vkCmdBindVertexBuffers(graphicsQueueCommandBuffers[i], 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(graphicsQueueCommandBuffers[i], indexBuffer, offset, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayoutGraphicsSubpass0, 0, 1, &descriptorSetGraphicsSubpass0, 0, NULL);
		vkCmdDrawIndexed(graphicsQueueCommandBuffers[i], uint32_t(indexData.size()), 1, 0, 0, 0);
#endif
		// Subpass 1
		vkCmdNextSubpass(graphicsQueueCommandBuffers[i], VK_SUBPASS_CONTENTS_INLINE);
#if TEMPORAL_INTEGRATION_PASS
		vkCmdBindPipeline(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineSubpass1);
		vkCmdBindVertexBuffers(graphicsQueueCommandBuffers[i], 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(graphicsQueueCommandBuffers[i], indexBuffer, offset, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(graphicsQueueCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayoutGraphicsSubpass1, 0, 1, &descriptorSetGraphicsSubpass1, 0, NULL);
		vkCmdDrawIndexed(graphicsQueueCommandBuffers[i], uint32_t(indexData.size()), 1, 0, 0, 0);
#endif
		// End render pass
		vkCmdEndRenderPass(graphicsQueueCommandBuffers[i]);
		
		// Barrier - wait for blurring and TAA to finish and transition image
		vkApp.TransitionImageLayoutInProgress(previousFrameImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, 0, graphicsQueueCommandBuffers[i]);
		
		// Blit swap chain image to previous image
		VkImageBlit blitRegion = {};
		blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		VkOffset3D offsetStart = { 0, 0, 0 };
		VkOffset3D offsetEnd = { int32_t(vkApp.vkSurfaceExtent.width), int32_t(vkApp.vkSurfaceExtent.height), 1 };
		blitRegion.srcOffsets[0] = offsetStart;
		blitRegion.srcOffsets[1] = offsetEnd;
		blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		blitRegion.dstOffsets[0] = offsetStart;
		blitRegion.dstOffsets[1] = offsetEnd;
		vkCmdBlitImage(graphicsQueueCommandBuffers[i], vkApp.vkSwapchainImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, previousFrameImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_LINEAR);
		
		//Barrier - wait for blit to finish and transition images
		vkApp.TransitionImageLayoutInProgress(vkApp.vkSwapchainImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(previousFrameImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingColorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingPositionImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingNormalImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		vkApp.TransitionImageLayoutInProgress(rayTracingAOImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, graphicsQueueCommandBuffers[i]);
		
		CHECK_VK_RESULT(vkEndCommandBuffer(graphicsQueueCommandBuffers[i]))
	}
	
	// Render
	glfwSetCursorPosCallback(vkApp.window, MouseCallback);
	glfwSetInputMode(vkApp.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetKeyCallback(vkApp.window, KeyCallback);
	float totalRenderTimeOnscreen = 0.0f;
	float totalRenderTimeOffscreen = 0.0f;
	uint32_t frameCountOnscreen = 0;
	uint32_t frameCountOffscreen = 0;
	while (!glfwWindowShouldClose(vkApp.window))
	{
		glfwPollEvents();
		// Camera
		vkApp.camera.Update();
		//printf("Origin: %f %f %f\n", vkApp.camera.origin.x, vkApp.camera.origin.y, vkApp.camera.origin.z);
		//printf("Dir: %f %f %f\n", vkApp.camera.viewDir.x, vkApp.camera.viewDir.y, vkApp.camera.viewDir.z);
		previousViewProjection = vkApp.previousFrameCamera.GetViewProjectionMatrix();
		cameraData = {
			vkApp.camera.origin.x, vkApp.camera.origin.y, vkApp.camera.origin.z, 0.0f,
			vkApp.camera.topLeftCorner.x, vkApp.camera.topLeftCorner.y, vkApp.camera.topLeftCorner.z, 0.0f,
			vkApp.camera.horizontalEnd.x, vkApp.camera.horizontalEnd.y, vkApp.camera.horizontalEnd.z, 0.0f,
			vkApp.camera.verticalEnd.x, vkApp.camera.verticalEnd.y, vkApp.camera.verticalEnd.z, 0.0f,
		};
		memcpy(cameraData.data() + 16, &previousViewProjection[0][0], 16 * sizeof(float));
		// Update buffers
		vkApp.UpdateHostVisibleBuffer(cameraBufferSize, cameraData.data(), cameraBufferMemory);
		vkApp.UpdateHostVisibleBuffer(currentFrameBufferSize, &vkApp.currentFrame, currentFrameBufferMemory);
		vkApp.UpdateHostVisibleBuffer(blurBufferSize, &blurVariable, blurBufferMemory);
		vkApp.previousFrameCamera = vkApp.camera;
		
		// Update the transformation for each mesh
		for (glm::mat4x4& transformation : transformationData)
		{
			glm::mat4 translateM = glm::translate(glm::mat4x4(1.0f), glm::vec3(0.01f, 0.0f, 0.0f));
			transformation *= translateM;
		}
		// Update acceleration structure
		vkApp.UpdateAccelerationStructureTransforms(accStruct, transformationData);
		// Lights
#if MOVE_LIGHTS_HORIZONTALLY
		assert(lights.size() == 8); // Only one light is allowed
		for (int i = 0; i < lights.size() / numFloatsPerLight; i++)
		{
			lights[i * numFloatsPerLight] = 4.0 * std::sin(totalRenderTimeOnscreen / 500.0f);
		}
		vkApp.UpdateHostVisibleBuffer(lightsBufferSize, lights.data(), lightsBufferMemory);
#elif MOVE_LIGHTS_VERTICALLY
		assert(lights.size() == 8); // Only one light is allowed
		for (int i = 0; i < lights.size() / numFloatsPerLight; i++)
		{
			lights[i * numFloatsPerLight + 1] = 3.0f + (2.0 * std::sin(totalRenderTimeOnscreen / 500.0f));
		}
		vkApp.UpdateHostVisibleBuffer(lightsBufferSize, lights.data(), lightsBufferMemory);
#endif
		
		if (renderOnscreen)
		{
			totalRenderTimeOnscreen += vkApp.Render(graphicsQueueCommandBuffers.data(), 0.0f);
			frameCountOnscreen++;
		}
		else
		{
			if (firstOffscreenFrame)
			{
				int previousFrame = vkApp.currentFrame - 1;
				if (previousFrame < 0)
				{
					previousFrame = vkApp.maxFramesInFlight - 1;
				}
				vkWaitForFences(vkApp.vkDevice, 1, &vkApp.vkInFlightFences[previousFrame], VK_TRUE, std::numeric_limits<uint32_t>::max());
				firstOffscreenFrame = 0;
			}
			totalRenderTimeOffscreen += vkApp.RenderOffscreen(graphicsQueueCommandBuffers.data(), 0.0f);
			frameCountOffscreen++;
		}
	}
	vkDeviceWaitIdle(vkApp.vkDevice);
	
	printf("\nAverage frame time onscreen: %f\n", totalRenderTimeOnscreen / float(frameCountOnscreen));
	printf("Average frame time offscreen: %f\n", totalRenderTimeOffscreen / float(frameCountOffscreen));
}

int main(int argc, char** argv)
{
	Raytrace(argv[1]);
	return EXIT_SUCCESS;
}

/*
MIT License

Copyright (c) 2018-2019 Daniel Fedai Larsen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
