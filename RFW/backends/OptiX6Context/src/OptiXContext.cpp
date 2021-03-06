#include "OptiXContext.h"
#include "OptiXCUDABuffer.h"

#include <utils/gl/CheckGL.h>
#include <utils/gl/GLDraw.h>
#include <utils/Timer.h>

#include "kernels.h"
#include "CheckCUDA.h"
#include <BlueNoise.h>

using namespace rfw;
using namespace utils;

enum OptiXRayIndex
{
	Primary = 0u,
	Secondary = 1u,
	Shadow = 2u
};

void usageReportCallback(int error, const char *context, const char *message, void *cbdata)
{
	WARNING("OptiX (%i) %s: %s", error, context, message);
}

void OptiXContext::init(std::shared_ptr<rfw::utils::Window> &window)
{
	m_CurrentTarget = WINDOW;
	window->addResizeCallback([this](int w, int h) {
		CheckCUDA(cudaDeviceSynchronize());
		m_ScrWidth = w;
		m_ScrHeight = h;
		setupTexture();
		resizeBuffers();
		glViewport(0, 0, w, h);
		m_Accumulator->clear();
		m_SampleIndex = 0;
		setScreenDimensions(m_ScrWidth, m_ScrHeight);
		CheckCUDA(cudaDeviceSynchronize());
	});

	cudaFree(nullptr); // Initialize CUDA device
	// RenderContet shared library needs to load its own GL function pointers
	const auto error = glewInit();
	if (error != GLEW_NO_ERROR)
		FAILURE("Could not init GLEW in OptiX context (%i): %s", error, glewGetErrorString(error));

	m_Window = window;
	m_ScrWidth = m_Window->get_width();
	m_ScrHeight = m_Window->get_height();
	m_SampleIndex = 0;

	// Create shader to render texture to screen
	m_Shader = new GLShader("shaders/draw-tex.vert", "shaders/draw-tex.frag");
	m_Shader->bind();
	m_Shader->setUniform("view", mat4(1.0f)); // Identity matrix as view
	m_Shader->setUniform("Texture", m_TextureID);
	m_Shader->unbind();

	try
	{
		m_Context = optix::Context::create();
	}
	catch (const std::exception &e)
	{
		const std::string exception = std::string("OptiX Exception: ") + e.what();
		DEBUG("%s", exception.data());
		throw std::runtime_error(exception.data());
	}
	//	m_Context->setUsageReportCallback(usageReportCallback, 1, nullptr);

	m_Acceleration = m_Context->createAcceleration("Trbvh");
	m_Acceleration->setProperty("refit", "1");
	m_SceneGraph = m_Context->createGroup();
	m_SceneGraph->setAcceleration(m_Acceleration);

	int major = 0, minor = 0;
	cudaDeviceGetAttribute(&major, cudaDeviceAttr::cudaDevAttrComputeCapabilityMajor, 0);
	cudaDeviceGetAttribute(&minor, cudaDeviceAttr::cudaDevAttrComputeCapabilityMinor, 0);

	if (major < 5)
		FAILURE("Minimum CUDA compute capability for this renderer is 50, found %i%i", major, minor);

	char kernel_source_file[512];
	memset(kernel_source_file, 0, sizeof(kernel_source_file));
	utils::string::format(kernel_source_file, "Kernels%i%i.ptx", major, minor);

	WARNING("%s", kernel_source_file);

	// Retrieve PTX program handles
	try
	{
		m_AttribProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "triangleAttributes");
		m_PrimaryRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generatePrimaryRay");
		m_SecondaryRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generateSecondaryRay");
		m_ShadowRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generateShadowRay");
		m_ClosestHit = m_Context->createProgramFromPTXFile(kernel_source_file, "closestHit");
		m_ShadowMissProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "missShadow");
	}
	catch (const std::exception &e)
	{
		FAILURE("%s", e.what());
	}

	optix::Program exceptionProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "exception");

	// Setup context entry points for each ray type
	m_Context->setRayTypeCount(3);
	m_Context->setEntryPointCount(3);
	m_Context->setMaxCallableProgramDepth(1);
	m_Context->setRayGenerationProgram(Primary, m_PrimaryRayProgram);
	m_Context->setRayGenerationProgram(Secondary, m_SecondaryRayProgram);
	m_Context->setRayGenerationProgram(Shadow, m_ShadowRayProgram);
	m_Context->setMissProgram(Shadow, m_ShadowMissProgram);

#ifndef NDEBUG
	// Set exception programs
	m_Context->setExceptionProgram(Primary, exceptionProgram);
	m_Context->setExceptionProgram(Secondary, exceptionProgram);
	m_Context->setExceptionProgram(Shadow, exceptionProgram);
	m_Context->setPrintEnabled(true);
	m_Context->setExceptionEnabled(RT_EXCEPTION_ALL, true);
#endif

	m_Material = m_Context->createMaterial();
	m_Material->setClosestHitProgram(Primary, m_ClosestHit);
	m_Material->setClosestHitProgram(Secondary, m_ClosestHit);

	m_Counters = new CUDABuffer<Counters>(1);

	setCounters(m_Counters->getDevicePointer());

	m_Context["sceneRoot"]->set(m_SceneGraph);
	m_Context["sampleIndex"]->setUint(0);

	m_Context["posLensSize"]->setFloat(0.0f, 0.0f, 0.0f, 0.0f);
	m_Context["right"]->setFloat(0.0f, 0.0f, 0.0f);
	m_Context["up"]->setFloat(0.0f, 0.0f, 0.0f);
	m_Context["p1"]->setFloat(0.0f, 0.0f, 0.0f);
	m_Context["geometryEpsilon"]->setFloat(m_GeometryEpsilon);
	m_Context["scrsize"]->setInt(m_ScrWidth, m_ScrHeight, 1);

	const auto blueNoiseBuffer = createBlueNoiseBuffer();
	m_BlueNoise = new OptiXCUDABuffer<unsigned int, OptiXBufferType::Read>(m_Context, {blueNoiseBuffer.size()});
	m_BlueNoise->copy_to_device(blueNoiseBuffer);
	m_Context["blueNoise"]->setBuffer(m_BlueNoise->device_data());
	setBlueNoiseBuffer(m_BlueNoise->device_data());

	m_CameraView = new CUDABuffer<CameraView>(1, ON_ALL);
	setCameraView(m_CameraView->getDevicePointer());

	resizeBuffers();

	setGeometryEpsilon(1e-5f);
	setClampValue(10.0f);
	setScreenDimensions(m_ScrWidth, m_ScrHeight);
	setupTexture(); // Create drawable surface

	m_Initialized = true;
}

void OptiXContext::init(GLuint *glTextureID, uint width, uint height)
{
	cudaFree(nullptr); // Initialize CUDA device
	CheckCUDA(cudaDeviceSynchronize());

	m_CurrentTarget = OPENGL_TEXTURE;
	m_TextureID = *glTextureID;
	m_ScrWidth = width;
	m_ScrHeight = height;
	m_SampleIndex = 0;

	if (!m_Initialized)
	{
		const auto error = glewInit();
		if (error != GLEW_NO_ERROR)
			FAILURE("Could not init GLEW in OptiX context (%i): %s", error, glewGetErrorString(error));

		try
		{
			m_Context = optix::Context::create();
		}
		catch (const std::exception &e)
		{
			const std::string exception = std::string("OptiX Exception: ") + e.what();
			DEBUG("%s", exception.data());
			throw std::runtime_error(exception.data());
		}

		m_Acceleration = m_Context->createAcceleration("Trbvh");
		m_Acceleration->setProperty("refit", "1");
		m_SceneGraph = m_Context->createGroup();
		m_SceneGraph->setAcceleration(m_Acceleration);

		int major = 0, minor = 0;
		cudaDeviceGetAttribute(&major, cudaDeviceAttr::cudaDevAttrComputeCapabilityMajor, 0);
		cudaDeviceGetAttribute(&minor, cudaDeviceAttr::cudaDevAttrComputeCapabilityMinor, 0);

		if (major < 5)
			FAILURE("Minimum CUDA compute capability for this renderer is 50, found %i%i", major, minor);

		char kernel_source_file[512];
		memset(kernel_source_file, 0, sizeof(kernel_source_file));
		utils::string::format(kernel_source_file, "Kernels%i%i.ptx", major, minor);

		WARNING("%s", kernel_source_file);

		// Retrieve PTX program handles
		try
		{
			m_AttribProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "triangleAttributes");
			m_PrimaryRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generatePrimaryRay");
			m_SecondaryRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generateSecondaryRay");
			m_ShadowRayProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "generateShadowRay");
			m_ClosestHit = m_Context->createProgramFromPTXFile(kernel_source_file, "closestHit");
			m_ShadowMissProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "missShadow");
		}
		catch (const std::exception &e)
		{
			FAILURE("%s", e.what());
		}

		optix::Program exceptionProgram = m_Context->createProgramFromPTXFile(kernel_source_file, "exception");

		// Setup context entry points for each ray type
		m_Context->setRayTypeCount(3);
		m_Context->setEntryPointCount(3);
		m_Context->setMaxCallableProgramDepth(1);
		m_Context->setRayGenerationProgram(Primary, m_PrimaryRayProgram);
		m_Context->setRayGenerationProgram(Secondary, m_SecondaryRayProgram);
		m_Context->setRayGenerationProgram(Shadow, m_ShadowRayProgram);
		m_Context->setMissProgram(Shadow, m_ShadowMissProgram);

#ifndef NDEBUG
		// Set exception programs
		m_Context->setExceptionProgram(Primary, exceptionProgram);
		m_Context->setExceptionProgram(Secondary, exceptionProgram);
		m_Context->setExceptionProgram(Shadow, exceptionProgram);
		m_Context->setPrintEnabled(true);
		m_Context->setExceptionEnabled(RT_EXCEPTION_ALL, true);
#endif

		m_Material = m_Context->createMaterial();
		m_Material->setClosestHitProgram(Primary, m_ClosestHit);
		m_Material->setClosestHitProgram(Secondary, m_ClosestHit);

		m_Counters = new CUDABuffer<Counters>(1);

		setCounters(m_Counters->getDevicePointer());

		m_Context["sceneRoot"]->set(m_SceneGraph);
		m_Context["posLensSize"]->setFloat(0.0f, 0.0f, 0.0f, 0.0f);
		m_Context["right"]->setFloat(0.0f, 0.0f, 0.0f);
		m_Context["up"]->setFloat(0.0f, 0.0f, 0.0f);
		m_Context["p1"]->setFloat(0.0f, 0.0f, 0.0f);
		m_Context["geometryEpsilon"]->setFloat(m_GeometryEpsilon);

		const auto blueNoiseBuffer = createBlueNoiseBuffer();
		m_BlueNoise = new OptiXCUDABuffer<uint, OptiXBufferType::Read>(m_Context, blueNoiseBuffer.size());
		m_BlueNoise->copy_to_device(blueNoiseBuffer);
		m_Context["blueNoise"]->setBuffer(m_BlueNoise->buffer());
		setBlueNoiseBuffer(m_BlueNoise->device_data());

		m_CameraView = new CUDABuffer<CameraView>(1, ON_ALL);
		setCameraView(m_CameraView->getDevicePointer());

		setGeometryEpsilon(1e-5f);
		setClampValue(10.0f);
		m_Initialized = true;
	}

	m_Context["sampleIndex"]->setUint(0);
	m_Context["scrsize"]->setInt(m_ScrWidth, m_ScrHeight, 1);
	setScreenDimensions(m_ScrWidth, m_ScrHeight);
	resizeBuffers();
	setupTexture();

	m_ResetFrame = true;
}

void OptiXContext::cleanup()
{
	for (auto *mesh : m_Meshes)
		delete mesh;
	m_Meshes.clear();

	if (m_TextureID)
	{
		glDeleteTextures(1, &m_TextureID);
		m_TextureID = 0;
	}

	delete m_Shader;
	m_Shader = nullptr;

	delete m_Accumulator;
	m_Accumulator = nullptr;
	delete m_PathStates;
	m_PathStates = nullptr;
	delete m_PathOrigins;
	m_PathOrigins = nullptr;
	delete m_PathDirections;
	m_PathDirections = nullptr;
	delete m_PathThroughputs;
	m_PathThroughputs = nullptr;
	delete m_ConnectData;
	m_ConnectData = nullptr;
	delete m_NormalBuffer;
	m_NormalBuffer = nullptr;
	delete m_AlbedoBuffer;
	m_AlbedoBuffer = nullptr;
	delete m_InputNormalBuffer;
	m_InputNormalBuffer = nullptr;
	delete m_InputAlbedoBuffer;
	m_InputAlbedoBuffer = nullptr;
	delete m_InputPixelBuffer;
	m_InputPixelBuffer = nullptr;
	delete m_OutputPixelBuffer;
	m_OutputPixelBuffer = nullptr;

	delete m_FloatTextures;
	m_FloatTextures = nullptr;
	delete m_UintTextures;
	m_UintTextures = nullptr;

	delete m_Counters;
	m_Counters = nullptr;
	delete m_CameraView;
	m_CameraView = nullptr;
	delete m_BlueNoise;
	m_BlueNoise = nullptr;
}

void OptiXContext::render_frame(const Camera &camera, RenderStatus status)
{
	Counters *counters = m_Counters->getHostPointer();
	if (status == Reset || m_ResetFrame)
	{
		m_ResetFrame = false;
		cudaMemsetAsync(m_Accumulator->device_data(), 0, m_Accumulator->size() * sizeof(vec4));
		m_Accumulator->clear_async();
		m_SampleIndex = 0;

		const auto view = camera.get_view();
		*m_CameraView->getHostPointer() = view;
		m_CameraView->copyToDeviceAsync();
		const vec3 right = view.p2 - view.p1;
		const vec3 up = view.p3 - view.p1;
		const vec4 posLensSize = vec4(view.pos, view.aperture);
		const int dims[3] = {m_ScrWidth, m_ScrHeight, 1};

		m_Context["stride"]->setUint(static_cast<uint>(m_ScrWidth * m_ScrHeight));
		m_Context["posLensSize"]->set4fv(value_ptr(posLensSize));
		m_Context["right"]->set3fv(value_ptr(right));
		m_Context["up"]->set3fv(value_ptr(up));
		m_Context["p1"]->set3fv(value_ptr(view.p1));
		m_Context["geometryEpsilon"]->setFloat(m_GeometryEpsilon);
		m_Context["scrsize"]->set3iv(dims);

		CheckCUDA(cudaGetLastError());
	}

	counters->samplesTaken = m_SampleIndex;
	m_Counters->copyToDevice();
	m_Context["sampleIndex"]->setUint(m_SampleIndex);
	m_Context["pathLength"]->setUint(0);

	cudaDeviceSynchronize();
	try
	{
		m_Context->validate();
	}
	catch (const std::exception &e)
	{
		WARNING("OptiX exception: %s", e.what());
		throw std::runtime_error(e.what());
	}

	m_RenderStats.clear();
	Timer timer = {};

	try
	{
		timer.reset();
		m_Context->launch(Primary, m_ScrWidth * m_ScrHeight);
		CheckCUDA(cudaGetLastError());

		m_RenderStats.primaryCount += m_ScrWidth * m_ScrHeight;
		m_RenderStats.primaryTime += timer.elapsed();
	}
	catch (const std::exception &e)
	{
		WARNING("OptiX exception: %s", e.what());
		throw std::runtime_error(e.what());
	}

	glm::mat3 toEyeMatrix = transpose(inverse(camera.get_matrix()));
	uint pathLength = 0;

	const auto pixelCount = static_cast<uint>(m_ScrWidth * m_ScrHeight);
	timer.reset();
	InitCountersForExtend(pixelCount);
	CheckCUDA(launchShade(pixelCount, pathLength, toEyeMatrix));
	CheckCUDA(cudaDeviceSynchronize());
	m_RenderStats.shadeTime += timer.elapsed();

	m_Counters->copyToHost();
	uint activePaths = counters->extensionRays;

	while (activePaths > 0 && pathLength < MAX_PATH_LENGTH)
	{
		if (counters->shadowRays > 0)
		{
			m_RenderStats.shadowCount += counters->shadowRays;

			timer.reset();
			m_Context->launch(Shadow, counters->shadowRays);
			m_RenderStats.shadowTime += timer.elapsed();
		}

		InitCountersSubsequent();
		pathLength = pathLength + 1;
		m_Context["pathLength"]->setUint(pathLength);

		timer.reset();
		m_Context->launch(Secondary, activePaths);
		if (pathLength == 1)
		{
			m_RenderStats.secondaryCount += activePaths;
			m_RenderStats.secondaryTime += timer.elapsed();
		}
		else
		{
			m_RenderStats.deepCount += activePaths;
			m_RenderStats.deepTime += timer.elapsed();
		}

		timer.reset();
		CheckCUDA(launchShade(activePaths, pathLength, toEyeMatrix));
		CheckCUDA(cudaDeviceSynchronize());
		m_RenderStats.shadeTime += timer.elapsed();

		m_Counters->copyToHost();
		activePaths = counters->extensionRays;
	}

	if (m_SampleIndex == 0)
	{
		m_ProbedInstance = counters->probedInstanceId;
		m_ProbedPrim = counters->probedPrimId;
		m_ProbedDistance = counters->probedDistance;
		m_ProbedPoint = counters->probedPoint;
	}

	m_SampleIndex++;
	CheckCUDA(cudaDeviceSynchronize());
	m_CUDASurface.bindSurface();

	CheckCUDA(launchFinalize(!m_Denoise, m_ScrWidth, m_ScrHeight, m_SampleIndex, camera.brightness, camera.contrast));
#if ALLOW_DENOISER
	if (m_Denoise)
	{
		CheckCUDA(cudaDeviceSynchronize());
		try
		{
			m_DenoiseCommandList->execute();
			CheckCUDA(cudaDeviceSynchronize());
		}
		catch (const std::exception &e)
		{
			WARNING(e.what());
			throw std::runtime_error(e.what());
		}

		CheckCUDA(blitBuffer(m_ScrWidth, m_ScrHeight));
		CheckCUDA(cudaDeviceSynchronize());
	}
#endif

	m_CUDASurface.unbindSurface();

	// Blit to surface if current target is a window surface
	if (m_CurrentTarget == WINDOW)
	{
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_TextureID);
		m_Shader->bind();
		drawQuad();
		m_Shader->unbind();
	}

	counters->samplesTaken = m_SampleIndex;
	counters->activePaths = m_ScrWidth * m_ScrHeight;
	m_Counters->copyToDeviceAsync();
}

void OptiXContext::set_materials(const std::vector<rfw::DeviceMaterial> &materials,
								 const std::vector<rfw::MaterialTexIds> &texDescriptors)
{
	std::vector<rfw::DeviceMaterial> mats(materials.size());
	memcpy(mats.data(), materials.data(), materials.size() * sizeof(rfw::Material));

	for (size_t i = 0; i < materials.size(); i++)
	{
		auto &mat = reinterpret_cast<Material &>(mats.at(i));
		const MaterialTexIds &ids = texDescriptors[i];
		if (ids.texture[0] != -1)
			mat.texaddr0 = m_TexDescriptors[ids.texture[0]].texAddr;
		if (ids.texture[1] != -1)
			mat.texaddr1 = m_TexDescriptors[ids.texture[1]].texAddr;
		if (ids.texture[2] != -1)
			mat.texaddr2 = m_TexDescriptors[ids.texture[2]].texAddr;
		if (ids.texture[3] != -1)
			mat.nmapaddr0 = m_TexDescriptors[ids.texture[3]].texAddr;
		if (ids.texture[4] != -1)
			mat.nmapaddr1 = m_TexDescriptors[ids.texture[4]].texAddr;
		if (ids.texture[5] != -1)
			mat.nmapaddr2 = m_TexDescriptors[ids.texture[5]].texAddr;
		if (ids.texture[6] != -1)
			mat.smapaddr = m_TexDescriptors[ids.texture[6]].texAddr;
		if (ids.texture[7] != -1)
			mat.rmapaddr = m_TexDescriptors[ids.texture[7]].texAddr;
		if (ids.texture[9] != -1)
			mat.cmapaddr = m_TexDescriptors[ids.texture[9]].texAddr;
		if (ids.texture[10] != -1)
			mat.amapaddr = m_TexDescriptors[ids.texture[10]].texAddr;
	}

	if (!m_Materials || m_Materials->getElementCount() < materials.size())
	{
		delete m_Materials;
		m_Materials = new CUDABuffer<DeviceMaterial>(mats.size(), ON_DEVICE);
	}

	m_Materials->copyToDevice(mats);

	::setMaterials(m_Materials->getDevicePointer());
}

void OptiXContext::set_textures(const std::vector<rfw::TextureData> &textures)
{
	m_TexDescriptors = textures;

	delete m_FloatTextures;
	delete m_UintTextures;
	m_FloatTextures = nullptr;
	m_UintTextures = nullptr;

	size_t uintTexelCount = 0;
	size_t floatTexelCount = 0;

	std::vector<glm::vec4> floatTexs;
	std::vector<uint> uintTexs;

	for (const auto &tex : textures)
	{
		switch (tex.type)
		{
		case (TextureData::FLOAT4):
			floatTexelCount += tex.texelCount;
			break;
		case (TextureData::UINT):
			uintTexelCount += tex.texelCount;
			break;
		}
	}

	floatTexs.resize(std::max(floatTexelCount, static_cast<size_t>(4)));
	uintTexs.resize(std::max(uintTexelCount, static_cast<size_t>(4)));

	if (floatTexelCount > 0)
	{
		size_t texelOffset = 0;
		for (size_t i = 0; i < textures.size(); i++)
		{
			const auto &tex = textures.at(i);

			if (tex.type != TextureData::FLOAT4)
				continue;

			assert((texelOffset + static_cast<size_t>(tex.texelCount)) < floatTexs.size());
			m_TexDescriptors.at(i).texAddr = static_cast<uint>(texelOffset);

			memcpy(&floatTexs.at(texelOffset), tex.data, tex.texelCount * 4 * sizeof(float));
			texelOffset += tex.texelCount;
		}
	}

	if (uintTexelCount > 0)
	{
		size_t texelOffset = 0;
		for (size_t i = 0; i < textures.size(); i++)
		{
			const auto &tex = textures.at(i);

			if (tex.type != TextureData::UINT)
				continue;

			assert((texelOffset + static_cast<size_t>(tex.texelCount)) <= uintTexs.size());
			m_TexDescriptors.at(i).texAddr = static_cast<uint>(texelOffset);

			memcpy(&uintTexs.at(texelOffset), tex.data, tex.texelCount * sizeof(uint));
			texelOffset += tex.texelCount;
		}
	}

	m_FloatTextures = new CUDABuffer<glm::vec4>(floatTexs, ON_DEVICE);
	m_UintTextures = new CUDABuffer<uint>(uintTexs, ON_DEVICE);

	setFloatTextures(m_FloatTextures->getDevicePointer());
	setUintTextures(m_UintTextures->getDevicePointer());
}

void OptiXContext::set_mesh(const size_t index, const rfw::Mesh &mesh)
{
	m_AnyMeshChanged = true;

	if (m_Meshes.size() <= index)
	{
		while (m_Meshes.size() <= index)
		{
			m_MeshChanged.push_back(true);
			m_Meshes.push_back(new OptiXMesh(m_Context, m_AttribProgram));
		}
	}

	m_MeshChanged[index] = true;
	m_Meshes[index]->setData(mesh);
}

void OptiXContext::set_instance(const size_t instanceIdx, const size_t meshIdx, const mat4 &transform,
								const mat3 &inverse_transform)
{
	try
	{
		bool addInsteadOfSet = false;
		while (m_Instances.size() <= instanceIdx)
		{
			m_Instances.push_back(m_Context->createTransform());
			m_InstanceDescriptors.emplace_back();
			m_InstanceMeshes.emplace_back(0);

			auto &instance = m_Instances[instanceIdx];

			auto *mesh = m_Meshes[meshIdx];

			auto geometryInstance = m_Context->createGeometryInstance(mesh->optixTriangles, m_Material);
			geometryInstance["instanceIdx"]->setUint(static_cast<uint>(instanceIdx));

			auto group = m_Context->createGeometryGroup();
			group->setChildCount(1);
			group->setChild(0, geometryInstance);
			group->setAcceleration(m_Context->createAcceleration("Trbvh"));
			group->validate();

			m_InstanceMeshes[instanceIdx] = meshIdx;
			instance->setChild(group);
			instance->setMatrix(true, value_ptr(transform), value_ptr(transpose(transform)));
			instance->validate();

			m_SceneGraph->addChild(instance);
			m_SceneGraph->validate();
		}

		auto &instance = m_Instances[instanceIdx];
		if (m_InstanceMeshes[instanceIdx] != meshIdx)
		{
			auto group = instance->getChild<optix::GeometryGroup>();
			group->setChildCount(1);
			group->getChild(0)->setGeometryTriangles(m_Meshes[meshIdx]->optixTriangles);
			group->validate();
		}

		instance->setMatrix(true, value_ptr(transform), value_ptr(transpose(transform)));
		instance->validate();

		m_InstanceMeshes[instanceIdx] = meshIdx;

		m_InstanceDescriptors[instanceIdx].invTransform = inverse_transform;
		m_InstanceDescriptors[instanceIdx].triangles =
			reinterpret_cast<DeviceTriangle *>(m_Meshes[meshIdx]->triangles->getDevicePointer());

		m_Acceleration->markDirty();
		m_Acceleration->validate();
	}
	catch (const std::exception &e)
	{
		WARNING("OptiX exception: %s", e.what());
		throw std::runtime_error(e.what());
	}
}

void OptiXContext::set_sky(const std::vector<glm::vec3> &pixels, size_t width, size_t height)
{
	m_Skybox = new CUDABuffer<glm::vec3>(width * height, ON_DEVICE);
	m_Skybox->copyToDeviceAsync(pixels.data(), width * height);
	setSkybox(m_Skybox->getDevicePointer());
	setSkyDimensions(static_cast<uint>(width), static_cast<uint>(height));
}

void OptiXContext::set_lights(rfw::LightCount lightCount, const rfw::DeviceAreaLight *areaLights,
							  const rfw::DevicePointLight *pointLights, const rfw::DeviceSpotLight *spotLights,
							  const rfw::DeviceDirectionalLight *directionalLights)
{
	CheckCUDA(cudaDeviceSynchronize());

	if (lightCount.areaLightCount > 0)
	{
		if (!m_AreaLights || m_AreaLights->getElementCount() < lightCount.areaLightCount)
		{
			delete m_AreaLights;
			m_AreaLights = new CUDABuffer<DeviceAreaLight>(lightCount.areaLightCount, ON_DEVICE);
			setAreaLights(m_AreaLights->getDevicePointer());
		}
		m_AreaLights->copyToDeviceAsync(areaLights, lightCount.areaLightCount);
	}

	if (lightCount.pointLightCount > 0)
	{
		if (!m_PointLights || m_PointLights->getElementCount() < lightCount.pointLightCount)
		{
			delete m_PointLights;
			m_PointLights = new CUDABuffer<DevicePointLight>(lightCount.pointLightCount, ON_DEVICE);
			setPointLights(m_PointLights->getDevicePointer());
		}
		m_PointLights->copyToDeviceAsync(pointLights, lightCount.pointLightCount);
	}

	if (lightCount.spotLightCount)
	{
		if (!m_SpotLights || m_SpotLights->getElementCount() < lightCount.spotLightCount)
		{
			delete m_SpotLights;
			m_SpotLights = new CUDABuffer<DeviceSpotLight>(lightCount.spotLightCount, ON_DEVICE);
			setSpotLights(m_SpotLights->getDevicePointer());
		}
		m_SpotLights->copyToDeviceAsync(spotLights, lightCount.spotLightCount);
	}

	if (lightCount.directionalLightCount > 0)
	{
		if (!m_DirectionalLights || m_DirectionalLights->getElementCount())
		{
			delete m_DirectionalLights;
			m_DirectionalLights =
				new CUDABuffer<DeviceDirectionalLight>(directionalLights, lightCount.directionalLightCount, ON_DEVICE);
			setDirectionalLights(m_DirectionalLights->getDevicePointer());
		}
		m_DirectionalLights->copyToDeviceAsync(directionalLights, lightCount.directionalLightCount);
	}

	setLightCount(lightCount);
	CheckCUDA(cudaDeviceSynchronize());
}

void OptiXContext::update()
{
	for (int i = 0, s = static_cast<int>(m_Instances.size()); i < s; i++)
	{
		const auto meshID = m_InstanceMeshes[i];

		if (!m_MeshChanged[i])
			continue;

		auto *mesh = m_Meshes[meshID];
		auto &instance = m_Instances[i];

		auto group = instance->getChild<optix::GeometryGroup>();
		group->getAcceleration()->markDirty();
		group->getAcceleration()->validate();
		group->validate();

		m_InstanceDescriptors[i].triangles = reinterpret_cast<DeviceTriangle *>(mesh->triangles->getDevicePointer());
	}

	if (m_AnyMeshChanged)
		m_Acceleration->markDirty();

	m_AnyMeshChanged = false;
	for (int i = 0, s = int(m_MeshChanged.size()); i < s; i++)
		m_MeshChanged[i] = false;

	if (!m_DeviceInstanceDescriptors || m_InstanceDescriptors.size() > m_DeviceInstanceDescriptors->getElementCount())
	{
		delete m_DeviceInstanceDescriptors;
		m_DeviceInstanceDescriptors = new CUDABuffer<DeviceInstanceDescriptor>(
			m_InstanceDescriptors.size() + (m_InstanceDescriptors.size() % 32), ON_DEVICE);
		m_DeviceInstanceDescriptors->copyToDeviceAsync(m_InstanceDescriptors);
		setInstanceDescriptors(m_DeviceInstanceDescriptors->getDevicePointer());
	}
}

void OptiXContext::set_probe_index(glm::uvec2 probePos)
{
	Counters *counters = m_Counters->getHostPointer();
	counters->probeIdx = probePos.x + probePos.y * m_ScrWidth;
}

rfw::RenderStats OptiXContext::get_stats() const { return m_RenderStats; }

void OptiXContext::get_probe_results(unsigned int *instanceIndex, unsigned int *primitiveIndex, float *distance) const
{
	*instanceIndex = m_ProbedInstance;
	*primitiveIndex = m_ProbedPrim;
	*distance = m_ProbedDistance;
}

rfw::AvailableRenderSettings OptiXContext::get_settings() const
{
	AvailableRenderSettings settings;
#if ALLOW_DENOISER
	settings.settingKeys = {"DENOISE"};
	settings.settingValues = {{"0", "1"}};
#endif
	return settings;
}

void OptiXContext::set_setting(const rfw::RenderSetting &setting)
{
	if (setting.name == "DENOISE")
		m_Denoise = (setting.value == "1");
}

void OptiXContext::resizeBuffers()
{
	const uint pixelCount = m_ScrWidth * m_ScrHeight;

	delete m_Accumulator;
	delete m_PathStates;
	delete m_PathOrigins;
	delete m_PathDirections;
	delete m_PathThroughputs;
	delete m_ConnectData;
	delete m_NormalBuffer;
	delete m_AlbedoBuffer;
	delete m_InputNormalBuffer;
	delete m_InputAlbedoBuffer;
	delete m_InputPixelBuffer;
	delete m_OutputPixelBuffer;

	m_Accumulator = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, {pixelCount});
	m_PathStates = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, {pixelCount * 2});
	m_PathOrigins = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, {pixelCount * 2});
	m_PathDirections = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, {pixelCount * 2});
	m_PathThroughputs = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, {pixelCount * 2});
	m_ConnectData = new OptiXCUDABuffer<PotentialContribution, OptiXBufferType::ReadWrite>(m_Context, {pixelCount});

	const std::array<size_t, 2> dimensions = {static_cast<size_t>(m_ScrWidth), static_cast<size_t>(m_ScrHeight)};
	m_NormalBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);
	m_AlbedoBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);
	m_InputNormalBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);
	m_InputAlbedoBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);
	m_InputPixelBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);
	m_OutputPixelBuffer = new OptiXCUDABuffer<glm::vec4, OptiXBufferType::ReadWrite>(m_Context, dimensions);

	m_Context["accumulator"]->setBuffer(m_Accumulator->buffer());
	m_Context["pathStates"]->setBuffer(m_PathStates->buffer());
	m_Context["pathOrigins"]->setBuffer(m_PathOrigins->buffer());
	m_Context["pathDirections"]->setBuffer(m_PathDirections->buffer());
	m_Context["connectData"]->setBuffer(m_ConnectData->buffer());

	setAccumulator(m_Accumulator->device_data());
	setStride(pixelCount);
	setPathStates(m_PathStates->device_data());
	setPathOrigins(m_PathOrigins->device_data());
	setPathDirections(m_PathDirections->device_data());
	setPathThroughputs(m_PathThroughputs->device_data());
	setPotentialContributions(m_ConnectData->device_data());
	setNormalBuffer(m_NormalBuffer->device_data());
	setAlbedoBuffer(m_AlbedoBuffer->device_data());
	setInputNormalBuffer(m_InputNormalBuffer->device_data());
	setInputAlbedoBuffer(m_InputAlbedoBuffer->device_data());
	setInputPixelBuffer(m_InputPixelBuffer->device_data());
	setOutputPixelBuffer(m_OutputPixelBuffer->device_data());

	setScreenDimensions(m_ScrWidth, m_ScrHeight);

#if ALLOW_DENOISER
	// Setup OptiX denoiser
	m_Denoiser = m_Context->createBuiltinPostProcessingStage("DLDenoiser");

	m_Denoiser->declareVariable("input_buffer")->setBuffer(m_InputPixelBuffer->buffer());
	m_Denoiser->declareVariable("output_buffer")->setBuffer(m_OutputPixelBuffer->buffer());
	m_Denoiser->declareVariable("blend")->setFloat(0.2f);
	m_Denoiser->declareVariable("input_albedo_buffer")->setBuffer(m_InputAlbedoBuffer->buffer());
	m_Denoiser->declareVariable("input_normal_buffer")->setBuffer(m_InputNormalBuffer->buffer());

	m_Denoiser->validate();

	m_DenoiseCommandList = m_Context->createCommandList();
	m_DenoiseCommandList->appendPostprocessingStage(m_Denoiser, m_ScrWidth, m_ScrHeight);
	m_DenoiseCommandList->finalize();

	m_DenoiseCommandList->validate();
#endif
}

void OptiXContext::setupTexture()
{
	if (m_CurrentTarget == WINDOW)
	{
		if (m_TextureID)
		{
			glDeleteTextures(1, &m_TextureID);
			m_TextureID = 0;
		}

		glGenTextures(1, &m_TextureID);
		glBindTexture(GL_TEXTURE_2D, m_TextureID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_ScrWidth, m_ScrHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
		CheckGL();
	}

	glBindTexture(GL_TEXTURE_2D, m_TextureID);
	m_CUDASurface.setTexture(m_TextureID);
	m_CUDASurface.linkToSurface(getOutputSurfaceReference());
	glBindTexture(GL_TEXTURE_2D, 0);
}

rfw::RenderContext *createRenderContext() { return new OptiXContext(); }

void destroyRenderContext(rfw::RenderContext *ptr)
{
	ptr->cleanup();
	delete ptr;
}
