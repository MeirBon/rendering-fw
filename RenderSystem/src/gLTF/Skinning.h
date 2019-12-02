#pragma once

#include <vector>
#include <string>

#include <MathIncludes.h>

#include <glm/gtx/matrix_major_storage.hpp>

#define ROW_MAJOR_MESH_SKIN 0

namespace rfw
{

class MeshSkin
{
  public:
	std::string name;
	std::vector<int> jointNodes;

	std::vector<glm::mat4> inverseBindMatrices;
	std::vector<glm::mat4> jointMatrices;
};

class MeshBone
{
  public:
	std::string name;
	unsigned int nodeIndex;

	std::vector<unsigned short> vertexIDs;
	std::vector<float> vertexWeights;
	glm::mat4 offsetMatrix;
};

} // namespace rfw