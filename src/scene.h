#pragma once

#include <vector>
#include <glm/glm.hpp>

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
};

bool load_scene(const char* path, std::vector<Mesh>& meshes, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<MeshDraw>& mesh_draws);