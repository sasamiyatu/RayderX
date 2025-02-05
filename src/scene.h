#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "resources.h"

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec4 tangent;
	glm::vec2 uv;
};

struct Mesh
{
	uint32_t first_vertex;
	uint32_t vertex_count;
	uint32_t first_index;
	uint32_t index_count;
};

struct MeshDraw
{
	glm::mat4 transform;
	uint32_t mesh_index;
	int material_index;
};

struct Material
{
	enum Type
	{
		STANDARD = 0,
		SKIN,
		EYES,
		EYE_OCCLUSION,
		TEARLINE,
	};

	Type type;

	int basecolor_texture;
	int normal_texture;
	int metallic_roughness_texture;
	int specular_texture;
	int occlusion_texture;
	int emissive_texture;

	glm::vec4 basecolor_factor;
	float metallic_factor;
	float roughness_factor;
};
	
bool load_scene(
	const char* path, 
	std::vector<Mesh>& meshes, 
	std::vector<Material>& materials, 
	std::vector<Texture>& textures, 
	std::vector<Vertex>& vertices,
	std::vector<uint32_t>& indices, 
	std::vector<MeshDraw>& mesh_draws,
	VkDevice device, 
	VmaAllocator allocator, 
	VkCommandPool command_pool, 
	VkCommandBuffer command_buffer, 
	VkQueue queue, 
	const Buffer& scratch);