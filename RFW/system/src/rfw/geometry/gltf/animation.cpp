#include <rfw/rfw.h>

#include <rfw/internal.h>

#include <tiny_gltf.h>

#include "animation.h"

using namespace rfw;
using namespace geometry;
using namespace gltf;

SceneAnimation::Sampler creategLTFSampler(const tinygltf::AnimationSampler &gltfSampler,
										  const tinygltf::Model &gltfModel)
{
	SceneAnimation::Sampler sampler = {};

	if (gltfSampler.interpolation == "STEP")
		sampler.method = SceneAnimation::Sampler::STEP;
	else if (gltfSampler.interpolation == "CUBICSPLINE")
		sampler.method = SceneAnimation::Sampler::SPLINE;
	else if (gltfSampler.interpolation == "LINEAR")
		sampler.method = SceneAnimation::Sampler::LINEAR;

	// Extract animation times
	const auto &inputAccessor = gltfModel.accessors[gltfSampler.input];
	assert(inputAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

	auto bufferView = gltfModel.bufferViews[inputAccessor.bufferView];
	auto buffer = gltfModel.buffers[bufferView.buffer];

	const auto *a = (const float *)(buffer.data.data() + bufferView.byteOffset + inputAccessor.byteOffset);

	size_t count = inputAccessor.count;
	for (int i = 0; i < count; i++)
		sampler.key_frames.push_back(a[i]);

	// extract animation keys
	auto outputAccessor = gltfModel.accessors[gltfSampler.output];
	bufferView = gltfModel.bufferViews[outputAccessor.bufferView];
	buffer = gltfModel.buffers[bufferView.buffer];

	const auto *b = (const unsigned char *)(buffer.data.data() + bufferView.byteOffset + outputAccessor.byteOffset);
	if (outputAccessor.type == TINYGLTF_TYPE_VEC3)
	{
		// b is an array of floats (for scale or translation)
		auto f = (float *)b;
		const int N = (int)outputAccessor.count;
		for (int i = 0; i < N; i++)
			sampler.vec3_key.push_back(vec3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]));
	}
	else if (outputAccessor.type == TINYGLTF_TYPE_SCALAR)
	{
		// b can be FLOAT, BYTE, UBYTE, SHORT or USHORT... (for weights)
		std::vector<float> fdata;
		const int N = (int)outputAccessor.count;
		switch (outputAccessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
		{
			for (int k = 0; k < N; k++, b += 4)
			{
				float data;
				memcpy(&data, b, sizeof(float));
				fdata.push_back(data);
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		{
			for (int k = 0; k < N; k++, b++)
			{
				char data;
				memcpy(&data, b, sizeof(char));
				fdata.push_back(max(float(data) / 127.0f, -1.0f));
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		{
			for (int k = 0; k < N; k++, b++)
			{
				unsigned char data;
				memcpy(&data, b, sizeof(unsigned char));
				fdata.push_back(float(data) / 255.0f);
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		{
			for (int k = 0; k < N; k++, b += 2)
			{
				short data;
				memcpy(&data, b, sizeof(short));
				fdata.push_back(max(float(data) / 32767.0f, -1.0f));
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			for (int k = 0; k < N; k++, b += 2)
			{
				unsigned short data;
				memcpy(&data, b, sizeof(unsigned short));
				fdata.push_back(float(data) / 65535.0f);
			}
			break;
		}
		}

		for (int i = 0; i < N; i++)
		{
			sampler.float_key.push_back(fdata[i]);
		}
	}
	else if (outputAccessor.type == TINYGLTF_TYPE_VEC4)
	{
		// b can be FLOAT, BYTE, UBYTE, SHORT or USHORT... (for rotation)
		std::vector<float> fdata;
		const int N = (int)outputAccessor.count * 4;
		switch (outputAccessor.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
		{
			for (int k = 0; k < N; k++, b += 4)
			{
				float data;
				memcpy(&data, b, sizeof(float));
				fdata.push_back(data);
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		{
			for (int k = 0; k < N; k++, b++)
			{
				char data;
				memcpy(&data, b, sizeof(char));
				fdata.push_back(max(float(data) / 127.0f, -1.0f));
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		{
			for (int k = 0; k < N; k++, b++)
			{
				unsigned char data;
				memcpy(&data, b, sizeof(unsigned char));
				fdata.push_back(float(data) / 255.0f);
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		{
			for (int k = 0; k < N; k++, b += 2)
			{
				short data;
				memcpy(&data, b, sizeof(short));
				fdata.push_back(max(data / 32767.0f, -1.0f));
			}
			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			for (int k = 0; k < N; k++, b += 2)
			{
				for (int k = 0; k < N; k++, b += 2)
				{
					unsigned short data;
					memcpy(&data, b, sizeof(unsigned short));
					fdata.push_back(max(data / 65535.0f, -1.0f));
				}
			}
			break;
		}
		}
		for (int i = 0; i < outputAccessor.count; i++)
			sampler.quat_key.emplace_back(fdata[i * 4 + 3], fdata[i * 4], fdata[i * 4 + 1], fdata[i * 4 + 2]);
	}
	else
	{
		assert(false);
	}

	return sampler;
}

SceneAnimation::Channel creategLTFChannel(const tinygltf::AnimationChannel &gltfChannel,
										  const tinygltf::Model &gltfModel, const int nodeBase)
{
	SceneAnimation::Channel channel = {};

	channel.samplerIDs.push_back(gltfChannel.sampler);
	channel.nodeIdx = gltfChannel.target_node + nodeBase;
	if (gltfChannel.target_path == "translation")
		channel.targets.push_back(SceneAnimation::Channel::TRANSLATION);
	else if (gltfChannel.target_path == "rotation")
		channel.targets.push_back(SceneAnimation::Channel::ROTATION);
	else if (gltfChannel.target_path == "scale")
		channel.targets.push_back(SceneAnimation::Channel::SCALE);
	else if (gltfChannel.target_path == "weights")
		channel.targets.push_back(SceneAnimation::Channel::WEIGHTS);

	return channel;
}

SceneAnimation creategLTFAnim(SceneObject *object, tinygltf::Animation &gltfAnim, tinygltf::Model &gltfModel,
							  const int nodeBase)
{
	assert(object);

	SceneAnimation anim = {};

	for (const auto &sampler : gltfAnim.samplers)
		anim.samplers.push_back(creategLTFSampler(sampler, gltfModel));

	for (const auto &channel : gltfAnim.channels)
		anim.channels.push_back(creategLTFChannel(channel, gltfModel, nodeBase));

	anim.object = object;

	return anim;
}

void SceneAnimation::setTime(float currentTime)
{
	for (auto &channel : channels)
		channel.setTime(object, currentTime, samplers);
}

float SceneAnimation::Sampler::sampleFloat(float currentTime, int k, int i, int count) const
{
	const float t0 = key_frames[k];
	const float t1 = key_frames[(k + 1)];
	const float f = (currentTime - t0) / (t1 - t0);

	if (f <= 0)
		return float_key[0];

	switch (method)
	{
	case Sampler::SPLINE:
	{
		const float t = f;
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float p0 = float_key.at((k * count + i) * 3 + 1);
		const float m0 = (t1 - t0) * float_key.at((k * count + i) * 3 + 2);
		const float p1 = float_key.at(((k + 1) * count + i) * 3 + 1);
		const float m1 = (t1 - t0) * float_key[((k + 1) * count + i) * 3];
		return m0 * (t3 - 2 * t2 + t) + p0 * (2 * t3 - 3 * t2 + 1) + p1 * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
	}
	case Sampler::STEP:
		return float_key[k];
	case Sampler::LINEAR:
	default:
		return (1.0f - f) * float_key[k * count + i] + f * float_key[(k + 1) * count + i];
	};
}

glm::vec3 SceneAnimation::Sampler::sampleVec3(float currentTime, int k) const
{
	const float t0 = key_frames[k];
	const float t1 = key_frames[k + 1];
	const float f = (currentTime - t0) / (t1 - t0);

	if (f <= 0)
		return vec3_key[0];
	switch (method)
	{
	case SPLINE:
	{
		const float t = f, t2 = t * t, t3 = t2 * t;
		const vec3 p0 = vec3_key[k * 3 + 1];
		const vec3 m0 = (t1 - t0) * vec3_key[k * 3 + 2];
		const vec3 p1 = vec3_key[(k + 1) * 3 + 1];
		const vec3 m1 = (t1 - t0) * vec3_key[(k + 1) * 3];
		return m0 * (t3 - 2 * t2 + t) + p0 * (2 * t3 - 3 * t2 + 1) + p1 * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
	}
	case STEP:
		return vec3_key[k];
	default:
		return (1 - f) * vec3_key[k] + f * vec3_key[k + 1];
	};
}

glm::quat SceneAnimation::Sampler::sampleQuat(float currentTime, int k) const
{
	// determine interpolation parameters
	const float t0 = key_frames[k];
	const float t1 = key_frames[k + 1];
	const float f = (currentTime - t0) / (t1 - t0);

	glm::quat key;
	if (f <= 0)
		key = quat_key[0];
	else
	{
		switch (method)
		{
		case SPLINE:
		{
			const float t = f, t2 = t * t, t3 = t2 * t;
			const quat p0 = quat_key[k * 3 + 1];
			const quat m0 = quat_key[k * 3 + 2] * (t1 - t0);
			const quat p1 = quat_key[(k + 1) * 3 + 1];
			const quat m1 = quat_key[(k + 1) * 3] * (t1 - t0);
			key = m0 * (t3 - 2 * t2 + t) + p0 * (2 * t3 - 3 * t2 + 1) + p1 * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
			break;
		}
		case STEP:
		{
			key = quat_key[k];
			break;
		default:
			key = (quat_key[k] * (1 - f)) + (quat_key[k + 1] * f);
			break;
		}
		};
	}
	return glm::normalize(key);
}

void SceneAnimation::Channel::update(SceneObject *object, const float deltaTime, const std::vector<Sampler> &samplers)
{
	for (int i = 0, s = static_cast<int>(samplerIDs.size()); i < s; i++)
	{
		const auto &sampler = samplers[samplerIDs[i]];
		const auto &target = targets[i];

		// Advance animation timer
		const int keyCount = int(sampler.key_frames.size());
		const float animDuration = sampler.key_frames[keyCount - 1];
		time += deltaTime;
		if (time > animDuration)
		{
			key = 0;
			time = std::fmod(time, animDuration);
		}

		while (time > sampler.key_frames.at(key + 1))
			key++;

		// Determine interpolation parameters
		auto &node = object->nodes.at(nodeIdx);

		// Apply animation key
		if (target == SceneAnimation::Channel::TRANSLATION) // translation
		{
			node.translation = sampler.sampleVec3(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::ROTATION) // rotation
		{
			node.rotation = sampler.sampleQuat(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::SCALE) // scale
		{
			node.scale = sampler.sampleVec3(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::WEIGHTS) // weight
		{
			const auto weightCount = static_cast<int>(node.weights.size());
			for (int i = 0; i < weightCount; i++)
				node.weights[i] = sampler.sampleFloat(time, key, i, weightCount);

			node.morphed = true;
		}
	}
}

void SceneAnimation::Channel::setTime(SceneObject *object, float currentTime, const std::vector<Sampler> &samplers)
{
	for (int i = 0, s = static_cast<int>(samplerIDs.size()); i < s; i++)
	{
		const auto &sampler = samplers[samplerIDs[i]];
		const auto &target = targets[i];

		// Advance animation timer
		const int keyCount = int(sampler.key_frames.size());
		const float animDuration = sampler.key_frames[keyCount - 1];
		time = currentTime;
		if (currentTime > animDuration)
		{
			key = 0;
			time = std::fmod(currentTime, animDuration);
		}

		while (time > sampler.key_frames.at(key + 1))
			key++;

		// Determine interpolation parameters
		auto &node = object->nodes.at(nodeIdx);

		// Apply animation key
		if (target == SceneAnimation::Channel::TRANSLATION) // translation
		{
			node.translation = sampler.sampleVec3(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::ROTATION) // rotation
		{
			node.rotation = sampler.sampleQuat(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::SCALE) // scale
		{
			node.scale = sampler.sampleVec3(time, key);
			node.transformed = true;
		}
		else if (target == SceneAnimation::Channel::WEIGHTS) // weight
		{
			auto weightCount = node.weights.size();
			for (int j = 0; j < weightCount; j++)
				node.weights[j] = sampler.sampleFloat(time, key, j, int(weightCount));

			node.morphed = true;
		}
	}
}
