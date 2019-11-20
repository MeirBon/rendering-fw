#pragma once

#include <vector>
#include <string>

#include <MathIncludes.h>

namespace rfw
{
class MeshSkin
{
  public:
	std::string name;
	int skeletonRoot = -1;

	std::vector<glm::mat4> inverseBindMatrices;
	std::vector<glm::mat4> jointMatrices;
	std::vector<int> joints;
};

} // namespace rfw