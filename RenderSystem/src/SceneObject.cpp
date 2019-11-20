#include "SceneObject.h"

#include "utils/ArrayProxy.h"

using namespace rfw;

bool rfw::SceneObject::transformTo(float timeInSeconds)
{
	const float time = timeInSeconds / 1000.0f;

	for (auto &anim : animations)
		anim.update(time);

	bool changed = false;

	for (auto idx : rootNodes)
	{
		glm::mat4 matrix = glm::identity<glm::mat4>();
		changed |= nodes.at(idx).update(matrix);
	}

	return changed;
}

void rfw::SceneObject::updateTriangles(uint offset, uint last)
{
	for (uint i = offset; i < last; i++)
	{
		const auto idx = i * 3;
		Triangle &tri = triangles.at(i);
		const vec3 &v0 = vertices.at(idx + 0);
		const vec3 &v1 = vertices.at(idx + 1);
		const vec3 &v2 = vertices.at(idx + 2);

		const vec3 &n0 = normals.at(idx + 0);
		const vec3 &n1 = normals.at(idx + 1);
		const vec3 &n2 = normals.at(idx + 2);

		vec3 N = normalize(cross(v1 - v0, v2 - v0));

		if (dot(N, n0) < 0.0f && dot(N, n1) < 0.0f && dot(N, n1) < 0.0f)
			N *= -1.0f; // flip if not consistent with vertex normals

		tri.vertex0 = v0;
		tri.vertex1 = v1;
		tri.vertex2 = v2;

		tri.Nx = N.x;
		tri.Ny = N.y;
		tri.Nz = N.z;

		tri.vN0 = n0;
		tri.vN1 = n1;
		tri.vN2 = n2;

		tri.material = materialIndices.at(i);
	}
}

void rfw::SceneObject::updateTriangles(rfw::MaterialList *matList, utils::ArrayProxy<glm::vec2> uvs)
{

	for (uint i = 0, s = static_cast<uint>(triangles.size()); i < s; i++)
	{
		const auto idx = i * 3;
		Triangle &tri = triangles.at(i);

		if (!uvs.empty())
		{
			tri.u0 = uvs.at(idx + 0).x;
			tri.v0 = uvs.at(idx + 0).y;

			tri.u1 = uvs.at(idx + 1).x;
			tri.v1 = uvs.at(idx + 1).y;

			tri.u2 = uvs.at(idx + 2).x;
			tri.v2 = uvs.at(idx + 2).y;
		}

		const HostMaterial &mat = matList->get(tri.material);
		int texID = mat.map[0].textureID;
		if (texID > -1)
		{
			const Texture &texture = matList->getTextures().at(texID);

			const float Ta = static_cast<float>(texture.width * texture.height) *
							 abs((tri.u1 - tri.u0) * (tri.v2 - tri.v0) - (tri.u2 - tri.u0) * (tri.v1 - tri.v0));
			const float Pa = length(cross(tri.vertex1 - tri.vertex0, tri.vertex2 - tri.vertex0));
			tri.LOD = 0.5f * log2f(Ta / Pa);
		}
	}
}