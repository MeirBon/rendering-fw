#ifndef RENDERING_FW_SRC_RENDERCONTEXT_H
#define RENDERING_FW_SRC_RENDERCONTEXT_H

#include <vector>
#include <memory>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <rfw/context/structs.h>

#include <rfw/utils/window.h>

#include <rfw/context/camera.h>

namespace rfw
{

enum RenderStatus
{
	Reset = 0,
	Converge = 1,
};

// TODO: Not used currently, but intention is to support multiple types of render targets
// Not every rendercontext needs to support every rendertarget, instead, supported render targets should be queried
enum RenderTarget
{
	VULKAN_TEXTURE,
	OPENGL_TEXTURE,
	METAL_TEXTURE,
	BUFFER,
	WINDOW
};

struct AvailableRenderSettings
{
	std::vector<std::string> settingKeys;
	std::vector<std::vector<std::string>> settingValues;
};

struct RenderSetting
{
	RenderSetting(const std::string &key, std::string val) : name(key), value(std::move(val)) {}

	std::string name;
	std::string value;
};

struct RenderStats
{
	RenderStats() { clear(); }
	void clear() { memset(this, 0, sizeof(RenderStats)); }

	float primaryTime;
	unsigned int primaryCount;

	float secondaryTime;
	unsigned int secondaryCount;

	float deepTime;
	unsigned int deepCount;

	float shadowTime;
	unsigned int shadowCount;

	float shadeTime;
	float finalizeTime;

	float animationTime;
	float renderTime;
};

class RenderContext
{
  public:
	RenderContext() = default;
	virtual ~RenderContext() = default;

	[[nodiscard]] virtual std::vector<rfw::RenderTarget> get_supported_targets() const = 0;

	// Initialization methods, by default these throw to indicate the chosen rendercontext does not support the
	// specified target
	virtual void init(std::shared_ptr<rfw::utils::window> &window)
	{
		throw std::runtime_error("RenderContext does not support given target type.");
	};
	virtual void init(GLuint *glTextureID, uint width, uint height)
	{
		throw std::runtime_error("RenderContext does not support given target type.");
	};

	virtual void cleanup() = 0;
	virtual void render_frame(const rfw::Camera &camera, rfw::RenderStatus status) = 0;
	virtual void set_materials(const std::vector<rfw::DeviceMaterial> &materials,
							   const std::vector<rfw::MaterialTexIds> &texDescriptors) = 0;
	virtual void set_textures(const std::vector<rfw::TextureData> &textures) = 0;
	virtual void set_mesh(size_t index, const rfw::Mesh &mesh) = 0;
	virtual void set_instance(size_t i, size_t meshIdx, const mat4 &transform, const mat3 &inverse_transform) = 0;
	virtual void set_sky(const std::vector<glm::vec3> &pixels, size_t width, size_t height) = 0;
	virtual void set_lights(rfw::LightCount lightCount, const rfw::DeviceAreaLight *areaLights,
							const rfw::DevicePointLight *pointLights, const rfw::DeviceSpotLight *spotLights,
							const rfw::DeviceDirectionalLight *directionalLights) = 0;
	virtual void get_probe_results(unsigned int *instanceIndex, unsigned int *primitiveIndex,
								   float *distance) const = 0;
	virtual rfw::AvailableRenderSettings get_settings() const = 0;
	virtual void set_setting(const rfw::RenderSetting &setting) = 0;
	virtual void update() = 0;
	virtual void set_probe_index(glm::uvec2 probePos) = 0;
	virtual rfw::RenderStats get_stats() const = 0;
};

} // namespace rfw

#endif // RENDERING_FW_SRC_RENDERCONTEXT_H
