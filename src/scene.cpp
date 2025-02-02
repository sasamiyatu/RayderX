#include "scene.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

bool load_scene(const char* path, std::vector<Mesh>& meshes, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, std::vector<MeshDraw>& mesh_draws)
{
	meshes.clear();
	indices.clear();
	vertices.clear();
	mesh_draws.clear();

	cgltf_options options = {};
	cgltf_data* data = nullptr;
	cgltf_result res = cgltf_parse_file(&options, path, &data);
	if (res != cgltf_result_success)
	{
		printf("Failed to load file '%s'\n", path);
		return false;
	}

	res = cgltf_load_buffers(&options, data, path);
	if (res != cgltf_result_success)
	{
		printf("Failed to load buffers for file '%s'\n", path);
		return false;
	}

	for (uint32_t i = 0; i < data->meshes_count; ++i)
	{
		const cgltf_mesh& mesh = data->meshes[i];
		for (uint32_t j = 0; j < mesh.primitives_count; ++j)
		{
			const cgltf_primitive& prim = mesh.primitives[j];
			uint32_t first_vertex = (uint32_t)vertices.size();
			uint32_t vertex_count = (uint32_t)prim.attributes[0].data->count;
			uint32_t first_index = (uint32_t)indices.size();
			uint32_t index_count = (uint32_t)prim.indices->count;	

			Mesh m{
				.first_vertex = first_vertex,
				.vertex_count = vertex_count,
				.first_index = first_index,
				.index_count = index_count
			};

			meshes.push_back(m);

			indices.resize(first_index + prim.indices->count);
			cgltf_accessor_unpack_indices(prim.indices, indices.data() + first_index, sizeof(uint32_t), index_count);
			std::vector<Vertex> verts(prim.attributes[0].data->count);
			if (const cgltf_accessor* pos = cgltf_find_accessor(&prim, cgltf_attribute_type_position, 0))
			{
				assert(cgltf_num_components(pos->type) == 3);
				std::vector<glm::vec3> positions(pos->count);
				cgltf_accessor_unpack_floats(pos, glm::value_ptr(positions[0]), positions.size() * 3);
				for (uint32_t i = 0; i < positions.size(); ++i)
				{
					verts[i].position = positions[i];
				}
			}
			if (const cgltf_accessor* normal = cgltf_find_accessor(&prim, cgltf_attribute_type_normal, 0))
			{
				assert(cgltf_num_components(normal->type) == 3);
				std::vector<glm::vec3> normals(normal->count);
				cgltf_accessor_unpack_floats(normal, glm::value_ptr(normals[0]), normals.size() * 3);
				for (uint32_t i = 0; i < normals.size(); ++i)
				{
					verts[i].normal = normals[i];
				}
			}
			if (const cgltf_accessor* tan = cgltf_find_accessor(&prim, cgltf_attribute_type_tangent, 0))
			{
				assert(cgltf_num_components(tan->type) == 4);
				std::vector<glm::vec4> tangents(tan->count);
				cgltf_accessor_unpack_floats(tan, glm::value_ptr(tangents[0]), tangents.size() * 4);
				for (uint32_t i = 0; i < tangents.size(); ++i)
				{
					verts[i].tangent = tangents[i];
				}
			}
			if (const cgltf_accessor* uv = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0))
			{
				assert(cgltf_num_components(uv->type) == 2);
				std::vector<glm::vec4> uvs(uv->count);
				cgltf_accessor_unpack_floats(uv, glm::value_ptr(uvs[0]), uvs.size() * 2);
				for (uint32_t i = 0; i < uvs.size(); ++i)
				{
					verts[i].uv = uvs[i];
				}
			}
			vertices.insert(vertices.end(), verts.begin(), verts.end());
		}
	}

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node& node = data->nodes[i];
		if (node.mesh)
		{
			glm::mat4 transform;
			cgltf_node_transform_world(&node, glm::value_ptr(transform));
			MeshDraw draw{
				.transform = transform,
				.mesh_index = (uint32_t)cgltf_mesh_index(data, node.mesh)
			};

			mesh_draws.push_back(draw);
		}
	}

    return true;
}
