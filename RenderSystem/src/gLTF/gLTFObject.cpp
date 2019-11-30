#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tiny_gltf.h>

#include "gLTFObject.h"

#include "../utils/File.h"

rfw::SceneAnimation creategLTFAnim(rfw::SceneObject *object, tinygltf::Animation &gltfAnim, tinygltf::Model &gltfModel, int nodeBase);

rfw::MeshSkin convertSkin(const tinygltf::Skin &skin, const tinygltf::Model &model)
{
	rfw::MeshSkin s = {};
	s.name = skin.name;
	s.jointNodes.reserve(skin.joints.size());
	for (auto joint : skin.joints)
		s.jointNodes.emplace_back(joint);

	if (skin.inverseBindMatrices > -1)
	{
		const auto &accessor = model.accessors.at(skin.inverseBindMatrices);
		const auto &bufferView = model.bufferViews.at(accessor.bufferView);
		const auto &buffer = model.buffers.at(bufferView.buffer);

		s.inverseBindMatrices.resize(accessor.count);
		memcpy(s.inverseBindMatrices.data(), &buffer.data.at(accessor.byteOffset + bufferView.byteOffset), accessor.count * sizeof(glm::mat4));

		s.jointMatrices.resize(accessor.count, glm::mat4(1.0f));
	}

	return s;
}

rfw::SceneNode createNode(rfw::gLTFObject &object, const tinygltf::Node &node, const std::vector<std::vector<rfw::TmpPrim>> &meshes)
{
	rfw::SceneNode::Transform T = {};
	glm::mat4 transform = mat4(1.0f);

	if (node.matrix.size() == 16)
		transform = glm::make_mat4(node.matrix.data());

	if (node.translation.size() == 3)
		T.translation = vec3(node.translation[0], node.translation[1], node.translation[2]);

	if (node.rotation.size() == 4)
		T.rotation = quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);

	if (node.scale.size() == 3)
		T.scale = vec3(node.scale[0], node.scale[1], node.scale[2]);

	auto n = rfw::SceneNode(&object.scene, node.name, node.children, {node.mesh}, {node.skin}, meshes, T, transform);
	return n;
}

rfw::gLTFObject::gLTFObject(std::string_view filename, MaterialList *matList, uint ID, const glm::mat4 &matrix, int material) : file(filename.data())
{
	using namespace tinygltf;

	Model model;
	TinyGLTF loader;
	std::string err, warn;

	bool ret = false;
	if (utils::string::ends_with(filename, ".glb"))
		ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename.data()); // for binary glTF(.glb)
	else
		ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename.data());

	if (!warn.empty())
		WARNING("%s", warn.data());
	if (!err.empty())
	{
		WARNING("%s", err.data());
		throw LoadException(err);
	}

	if (!ret)
	{
		const std::string message = std::string("Could not load \"") + filename.data() + "\"";
		WARNING("%s", message.data());
		throw LoadException(message);
	}

	m_BaseMaterialIdx = matList->getMaterials().size();
	const auto baseTextureIdx = matList->getTextures().size();

	for (const auto &tinyMat : model.materials)
	{
		// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#materials
		HostMaterial mat = {};
		mat.name = tinyMat.name;
		for (const auto &value : tinyMat.values)
		{
			if (value.first == "baseColorFactor")
			{
				tinygltf::Parameter p = value.second;
				mat.color = vec3(p.number_array[0], p.number_array[1], p.number_array[2]);
			}
			if (value.first == "metallicFactor")
			{
				if (value.second.has_number_value)
					mat.metallic = (float)value.second.number_value;
			}
			if (value.first == "roughnessFactor")
			{
				if (value.second.has_number_value)
					mat.roughness = (float)value.second.number_value;
			}
			if (value.first == "baseColorTexture")
			{
				for (auto &item : value.second.json_double_value)
				{
					if (item.first == "index")
						mat.map[TEXTURE0].textureID = (int)item.second + baseTextureIdx;
					if (item.first == "scale")
						mat.map[TEXTURE0].uvscale = vec2(item.second);
					if (item.first == "offset")
						mat.map[TEXTURE0].uvoffset = vec2(item.second);
				}
			}
			if (value.first == "normalTexture")
			{
				mat.setFlag(HasNormalMap);
				for (auto &item : value.second.json_double_value)
				{
					if (item.first == "index")
						mat.map[NORMALMAP0].textureID = (int)item.second + baseTextureIdx;
					if (item.first == "scale")
						mat.map[TEXTURE0].uvscale = vec2(item.second);
					if (item.first == "offset")
						mat.map[TEXTURE0].uvoffset = vec2(item.second);
				}
			}
			if (value.first == "emissiveFactor")
			{
				if (value.second.has_number_value)
				{
					tinygltf::Parameter p = value.second;
					mat.color = vec3(1) + vec3(p.number_array[0], p.number_array[1], p.number_array[2]);
				}
			}
		}

		matList->add(mat);
	}

	for (size_t i = 0; i < model.textures.size(); i++)
	{
		const auto &gltfTex = model.textures.at(i);
		const auto &gltfImage = model.images.at(gltfTex.source);

		auto texture = Texture((uint *)gltfImage.image.data(), gltfImage.width, gltfImage.height);
		matList->add(texture);
	}

	scene.skins.resize(model.skins.size());
	for (size_t i = 0; i < model.skins.size(); i++)
		scene.skins.at(i) = convertSkin(model.skins.at(i), model);

	scene.animations.resize(model.animations.size());
	for (size_t i = 0; i < model.animations.size(); i++)
		scene.animations.at(i) = creategLTFAnim(&scene, model.animations.at(i), model, 0);

	std::vector<std::vector<TmpPrim>> meshes(model.meshes.size());
	for (size_t i = 0; i < model.meshes.size(); i++)
	{
		const auto &mesh = model.meshes.at(i);

		meshes.at(i).resize(mesh.primitives.size());

		for (size_t s = mesh.primitives.size(), j = 0; j < s; j++)
		{
			const Primitive &prim = mesh.primitives.at(j);

			const Accessor &accessor = model.accessors.at(prim.indices);
			const BufferView &view = model.bufferViews.at(accessor.bufferView);
			const Buffer &buffer = model.buffers.at(view.buffer);
			const unsigned char *a = buffer.data.data() + view.byteOffset + accessor.byteOffset;
			const int byteStride = accessor.ByteStride(view);
			const size_t count = accessor.count;

			std::vector<int> &tmpIndices = meshes[i][j].indices;
			std::vector<glm::vec3> &tmpNormals = meshes[i][j].normals;
			std::vector<glm::vec3> &tmpVertices = meshes[i][j].vertices;
			std::vector<glm::vec2> &tmpUvs = meshes[i][j].uvs;
			std::vector<glm::uvec4> &tmpJoints = meshes[i][j].joints;
			std::vector<glm::vec4> &tmpWeights = meshes[i][j].weights;
			std::vector<rfw::SceneMesh::Pose> &tmpPoses = meshes[i][j].poses;

			meshes[i][j].matID = prim.material > -1 ? prim.material + m_BaseMaterialIdx : 0;

			switch (accessor.componentType)
			{
			case (TINYGLTF_COMPONENT_TYPE_BYTE):
				for (int k = 0; k < count; k++, a += byteStride)
				{
					char value = 0;
					memcpy(&value, a, sizeof(char));
					tmpIndices.push_back(value);
				}
				break;
			case (TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE):
				for (int k = 0; k < count; k++, a += byteStride)
				{
					unsigned char value = 0;
					memcpy(&value, a, sizeof(unsigned char));
					tmpIndices.push_back(value);
				}
				break;
			case (TINYGLTF_COMPONENT_TYPE_SHORT):
				for (int k = 0; k < count; k++, a += byteStride)
				{
					short value = 0;
					memcpy(&value, a, sizeof(unsigned short));
					tmpIndices.push_back(value);
				}
				break;
			case (TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT):
				for (int k = 0; k < count; k++, a += byteStride)
				{
					unsigned short value = 0;
					memcpy(&value, a, sizeof(unsigned short));
					tmpIndices.push_back(value);
				}
				break;
			case (TINYGLTF_COMPONENT_TYPE_INT):
				for (int k = 0; k < count; k++, a += byteStride)
				{
					int value = 0;
					memcpy(&value, a, sizeof(int));
					tmpIndices.push_back(value);
				}
				break;
			case (TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT):
				for (int k = 0; k < count; k++, a += byteStride)
					tmpIndices.push_back(*((unsigned int *)a));
				break;
			default:
				break;
			}

			if (prim.mode == TINYGLTF_MODE_TRIANGLE_FAN)
			{
				auto fan = move(tmpIndices);
				tmpIndices.clear();
				for (size_t sj = fan.size(), p = 2; p < sj; p++)
				{
					tmpIndices.push_back(fan.at(0));
					tmpIndices.push_back(fan.at(p - 1));
					tmpIndices.push_back(fan.at(p));
				}
			}
			else if (prim.mode == TINYGLTF_MODE_TRIANGLE_STRIP)
			{
				auto strip = move(tmpIndices);
				tmpIndices.clear();
				for (size_t sj = strip.size(), p = 2; p < sj; p++)
				{
					tmpIndices.push_back(strip.at(p - 2));
					tmpIndices.push_back(strip.at(p - 1));
					tmpIndices.push_back(strip.at(p));
				}
			}
			else if (prim.mode != TINYGLTF_MODE_TRIANGLES)
				continue;

			for (const auto &attribute : prim.attributes)
			{
				const Accessor &attribAccessor = model.accessors.at(attribute.second);
				const BufferView &bufferView = model.bufferViews.at(attribAccessor.bufferView);
				const Buffer &buffer = model.buffers.at(bufferView.buffer);
				const unsigned char *a = buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
				const int byteStride = attribAccessor.ByteStride(bufferView);
				const size_t count = attribAccessor.count;

				if (attribute.first == "POSITION")
				{
					if (attribAccessor.type == TINYGLTF_TYPE_VEC3)
					{
						if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							for (size_t p = 0; p < count; p++, a += byteStride)
							{
								tmpVertices.push_back(*((glm::vec3 *)a));
							}
						}
						else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
						{
							// WARNING("%s", "Double precision positions are not supported (yet).");
							for (size_t p = 0; p < count; p++, a += byteStride)
							{
								tmpVertices.emplace_back(*((glm::dvec3 *)a));
							}
						}
					}
					else
					{
						throw LoadException("Unsupported position definition in gLTF file.");
					}
				}
				else if (attribute.first == "NORMAL")
				{
					if (attribAccessor.type == TINYGLTF_TYPE_VEC3)
					{
						if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpNormals.push_back(*((glm::vec3 *)a));
							}
						}
						else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
						{
							// WARNING("%s", "Double precision positions are not supported (yet).");
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpNormals.emplace_back(*((glm::dvec3 *)a));
							}
						}
					}
					else
					{
						throw LoadException("Unsupported normal definition in gLTF file.");
					}
				}
				else if (attribute.first == "TANGENT")
					continue;
				else if (attribute.first == "TEXCOORD_0")
				{
					if (attribAccessor.type == TINYGLTF_TYPE_VEC2)
					{
						if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpUvs.push_back(*((glm::vec2 *)a));
							}
						}
						else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
						{
							// WARNING("%s", "Double precision normals are not supported (yet).");
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpUvs.emplace_back(*((glm::dvec2 *)a));
							}
						}
					}
					else
					{
						throw LoadException("Unsupported UV definition in gLTF file.");
					}
				}
				else if (attribute.first == "TEXCOORD_1")
					continue;
				else if (attribute.first == "COLOR_0")
					continue;
				else if (attribute.first == "JOINTS_0")
				{
					if (attribAccessor.type == TINYGLTF_TYPE_VEC4)
					{
						if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
						{
							using unshort = unsigned short;

							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpJoints.emplace_back(*((unshort *)a), *((unshort *)(a + 2)), *((unshort *)(a + 4)), *((unshort *)(a + 6)));
							}
						}
						else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
						{
							using uchar = unsigned char;
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								tmpJoints.emplace_back(*((uchar *)a), *((uchar *)(a + 1)), *((uchar *)(a + 2)), *((uchar *)(a + 3)));
							}
						}
						else
						{
							throw LoadException("Expected unsigned shorts or bytes for joints in gLTF file.");
						}
					}
					else
					{
						throw LoadException("Unsupported joint definition in gLTF file.");
					}
				}
				else if (attribute.first == "WEIGHTS_0")
				{
					if (attribAccessor.type == TINYGLTF_TYPE_VEC4)
					{
						if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
						{
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								glm::vec4 w4;
								memcpy(&w4, a, sizeof(glm::vec4));
								float norm = 1.0f / (w4.x + w4.y + w4.z + w4.w);
								w4 *= norm;
								tmpWeights.push_back(w4);
							}
						}
						else if (attribAccessor.componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE)
						{
							// WARNING("%s", "Double precision weights are not supported (yet).");
							for (size_t f = 0; f < count; f++, a += byteStride)
							{
								glm::dvec4 w4;
								memcpy(&w4, a, sizeof(glm::dvec4));
								const float norm = 1.0 / (w4.x + w4.y + w4.z + w4.w);
								w4 *= norm;
								tmpWeights.emplace_back(w4);
							}
						}
					}
					else
					{
						throw LoadException("Unsupported weight definition in gLTF file.");
					}
				}
				else
				{
					WARNING("Unknown property: \"%s\"", attribute.first.data());
				}
			}

			tmpPoses.reserve(mesh.weights.size() + 1);

			// Store base pose if morph target present
			if (!mesh.weights.empty())
			{
				tmpPoses.emplace_back();
				for (size_t sv = tmpVertices.size(), n = 0; n < sv; n++)
				{
					tmpPoses.at(0).positions.push_back(tmpVertices.at(n));
					tmpPoses.at(0).normals.push_back(tmpNormals.at(n));
				}
			}

			for (size_t o = 0; o < mesh.weights.size(); o++)
			{
				tmpPoses.emplace_back();
				auto &pose = tmpPoses.at(tmpPoses.size() - 1);

				for (const auto &target : prim.targets.at(o))
				{
					const Accessor &acc = model.accessors.at(target.second);
					const BufferView &vi = model.bufferViews.at(acc.bufferView);
					const auto *va = (const float *)(model.buffers.at(vi.buffer).data.data() + vi.byteOffset + acc.byteOffset);

					for (size_t p = 0; p < acc.count; p++)
					{
						const auto v = glm::vec3(va[p * 3], va[p * 3 + 1], va[p * 3 + 2]);

						if (target.first == "POSITION")
							pose.positions.push_back(v);
						else if (target.first == "NORMAL")
							pose.normals.push_back(v);
					}
				}
			}
		}
	}

	const bool hasTransform = matrix != glm::mat4(1.0f);

	if (model.scenes.size() > 1)
		WARNING("gLTF files with more than 1 scene are not supported (yet).");

	scene.nodes.reserve(model.nodes.size() + 1);
	tinygltf::Scene &gltfScene = model.scenes.at(0);

	for (size_t s = model.nodes.size(), i = 0; i < s; i++)
	{
		const auto &node = model.nodes.at(i);
		scene.nodes.emplace_back(createNode(*this, node, meshes));
	}

	for (size_t i = 0; i < gltfScene.nodes.size(); i++)
		scene.rootNodes.push_back(i);

	if (hasTransform)
	{
		for (int i : scene.rootNodes)
		{
			auto &node = scene.nodes.at(i);
			node.matrix = matrix * node.matrix;
			node.transformed = true;
		}
	}

	scene.vertices.resize(scene.baseVertices.size(), vec4(0, 0, 0, 1));
	scene.normals.resize(scene.baseNormals.size(), vec3(0.0f));

	scene.transformTo(0.0f);

	scene.updateTriangles();
	// Update triangle data that only has to be calculated once
	scene.updateTriangles(matList, scene.texCoords);

	utils::logger::log("Loaded file: %s with %u vertices and %u triangles", filename.data(), scene.vertices.size(), scene.triangles.size());
}

void rfw::gLTFObject::transformTo(float timeInSeconds) { scene.transformTo(timeInSeconds); }

rfw::Triangle *rfw::gLTFObject::getTriangles() { return scene.triangles.data(); }

glm::vec4 *rfw::gLTFObject::getVertices() { return scene.vertices.data(); }

rfw::Mesh rfw::gLTFObject::getMesh() const
{
	auto mesh = rfw::Mesh();
	mesh.vertices = scene.vertices.data();
	mesh.normals = scene.normals.data();
	mesh.triangles = scene.triangles.data();
	if (scene.indices.empty())
	{
		mesh.indices = nullptr;
		mesh.triangleCount = scene.vertices.size() / 3;
	}
	else
	{
		mesh.indices = scene.indices.data();
		mesh.triangleCount = scene.indices.size();
	}
	mesh.vertexCount = scene.vertices.size();
	mesh.texCoords = scene.texCoords.data();
	return mesh;
}

bool rfw::gLTFObject::isAnimated() const { return !scene.animations.empty(); }

uint rfw::gLTFObject::getAnimationCount() const { return uint(scene.animations.size()); }

void rfw::gLTFObject::setAnimation(uint index)
{
	// TODO
}

uint rfw::gLTFObject::getMaterialForPrim(uint primitiveIdx) const { return scene.materialIndices.at(primitiveIdx); }

std::vector<uint> rfw::gLTFObject::getLightIndices(const std::vector<bool> &matLightFlags) const
{
	std::vector<uint> indices;
	for (const auto &mesh : scene.meshes)
	{
		const size_t offset = indices.size();

		//		if (matLightFlags.at(mesh.matID))
		//		{
		//			const auto s = mesh.vertexCount / 3;
		//			const auto o = mesh.vertexOffset / 3;
		//			indices.resize(offset + s);
		//			for (uint i = 0; i < s; i++)
		//				indices.at(offset + i) = o + i;
		//		}
	}

	return indices;
}
