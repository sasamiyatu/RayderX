#include "scene.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

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
	const Buffer& scratch)
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

	// Load meshes
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
				std::vector<glm::vec2> uvs(uv->count);
				cgltf_accessor_unpack_floats(uv, glm::value_ptr(uvs[0]), uvs.size() * 2);
				for (uint32_t i = 0; i < uvs.size(); ++i)
				{
					verts[i].uv = uvs[i];
				}
			}
			vertices.insert(vertices.end(), verts.begin(), verts.end());
		}
	}

	std::vector<bool> texture_is_srgb(data->textures_count);
	for (uint32_t i = 0; i < data->materials_count; ++i)
	{
		const cgltf_material& m = data->materials[i];

		//assert(m.has_pbr_metallic_roughness);
		//assert(m.occlusion_texture.texture);
		//assert(m.pbr_metallic_roughness.base_color_texture.texture);
		//assert(m.pbr_metallic_roughness.metallic_roughness_texture.texture);
		//assert(m.normal_texture.texture);
		assert(m.name);

		Material::Type type = Material::STANDARD;
		std::string name = m.name;
		for (auto& c : name) c = std::tolower(c);
		if (name.find("skin") != std::string::npos) type = Material::SKIN;
		else if (name.find("cornea") != std::string::npos) type = Material::EYES;
		else if (name.find("eye_occlusion") != std::string::npos) type = Material::EYE_OCCLUSION;
		else if (name.find("tearline") != std::string::npos) type = Material::TEARLINE;

		int basecolor_index = m.pbr_metallic_roughness.base_color_texture.texture ? (int)cgltf_texture_index(data, m.pbr_metallic_roughness.base_color_texture.texture) : -1;
		int normal_index = m.normal_texture.texture ? (int)cgltf_texture_index(data, m.normal_texture.texture) : -1;
		int emissive_index = m.emissive_texture.texture ? (int)cgltf_texture_index(data, m.emissive_texture.texture) : -1;
		int metallic_roughness_index = m.pbr_metallic_roughness.metallic_roughness_texture.texture ? (int)cgltf_texture_index(data, m.pbr_metallic_roughness.metallic_roughness_texture.texture) : -1;
		int occlusion_index = m.occlusion_texture.texture ? (int)cgltf_texture_index(data, m.occlusion_texture.texture) : -1;
		if (basecolor_index >= 0) texture_is_srgb[basecolor_index] = true;
		if (emissive_index >= 0) texture_is_srgb[emissive_index] = true;
		Material mat{
			.type = type,
			.basecolor_texture = basecolor_index,
			.normal_texture = normal_index,
			.metallic_roughness_texture = metallic_roughness_index,
			.specular_texture = -1,
			.occlusion_texture = occlusion_index,
			.emissive_texture = emissive_index,

			.basecolor_factor = glm::make_vec4(m.pbr_metallic_roughness.base_color_factor),
			.metallic_factor = m.pbr_metallic_roughness.metallic_factor,
			.roughness_factor = m.pbr_metallic_roughness.roughness_factor
		};

		materials.push_back(mat);
	}

	for (uint32_t i = 0; i < data->textures_count; ++i)
	{
		const cgltf_texture& t = data->textures[i];

		size_t size = t.image->buffer_view->size;
		const uint8_t* data = cgltf_buffer_view_data(t.image->buffer_view);

		bool is_srgb = texture_is_srgb[i];
		Texture tex;
		if (!load_png_or_jpg_texture(tex, data, size, device, allocator, command_pool, command_buffer, queue, scratch, is_srgb))
		{
			printf("Failed to load texture\n");
			return false;
		}

		textures.push_back(tex);
	}

	generate_mipmaps(textures, device, allocator, command_pool, command_buffer, queue, scratch);

	for (size_t i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node& node = data->nodes[i];
		if (node.mesh)
		{
			assert(node.mesh->primitives_count == 1);
			glm::mat4 transform;
			cgltf_node_transform_world(&node, glm::value_ptr(transform));
			MeshDraw draw{
				.transform = transform,
				.mesh_index = (uint32_t)cgltf_mesh_index(data, node.mesh),
				.material_index = (int)cgltf_material_index(data, node.mesh->primitives[0].material)
			};

			mesh_draws.push_back(draw);
		}
	}

    return true;
}
