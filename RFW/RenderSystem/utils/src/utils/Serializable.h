//
// Created by Mèir Noordermeer on 2019-08-20.
//

#ifndef RENDERING_FW_SRC_UTILS_Serializable_HPP
#define RENDERING_FW_SRC_UTILS_Serializable_HPP

#include <array>
#include <cassert>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/ext.hpp>

namespace rfw::utils
{

template <typename T, unsigned int Dimensions> class Serializable
{
  public:
	explicit Serializable(const std::array<unsigned int, Dimensions> &count)
	{
		memcpy(m_Dims, count.data(), Dimensions * sizeof(unsigned int));
		unsigned int size = count[0];
		for (unsigned int i = 1; i < Dimensions; i++)
			size *= count[i];

		m_Data.resize(count);
	}

	Serializable(const std::vector<T> &data, const std::array<unsigned int, Dimensions> &size) : m_Data(data)
	{
		memcpy(m_Dims, size.data(), Dimensions * sizeof(unsigned int));
	}

	Serializable(const T &data) : m_Data({data})
	{
		m_Data.resize(Dimensions);
		for (unsigned int i = 0; i < Dimensions; i++)
		{
			m_Dims[i] = 1 * sizeof(T);
			m_Data.at(i) = data;
		}
	}

	const T &get(const unsigned int index) const
	{
		assert(index < m_Data.size());
		return m_Data[index];
	}

	T &get(const unsigned int index)
	{
		assert(index < m_Data.size());
		return m_Data[index];
	}

	void set(const T &data, const unsigned int index) { m_Data[index] = data; }

	void set(const T *data, const unsigned int offset, const unsigned int count)
	{
		assert(offset + count < m_Data.size());
		memcpy(m_Data.data() + offset, data, count * sizeof(T));
	}

	void resize(const std::array<unsigned int, Dimensions> &count)
	{
		unsigned int size = count[0];
		for (unsigned int i = 1; i < Dimensions; i++)
			size *= count[i];

		m_Data.resize(size);
	}

	[[nodiscard]] std::vector<char> serialize() const
	{
		constexpr unsigned int charsInUint = sizeof(unsigned int) / sizeof(char);

		const unsigned int charCount = static_cast<unsigned int>(m_Data.size()) * sizeof(T);
		std::vector<char> data((Dimensions + 1) * charsInUint + charCount);

		std::array<unsigned int, Dimensions + 1> sizeData;
		sizeData[0] = Dimensions;
		for (unsigned int i = 0; i < Dimensions; i++)
			sizeData[i + 1] = m_Dims[i];

		memcpy(data.data(), sizeData.data(), sizeData.size() * sizeof(unsigned int));
		memcpy(data.data() + (sizeData.size() * charsInUint), m_Data.data(), m_Data.size() * sizeof(T));
		return data;
	}

	static Serializable<T, Dimensions> deserialize(const std::vector<char> &data)
	{
		constexpr unsigned int charsInUint = sizeof(unsigned int) / sizeof(char);
		constexpr unsigned int charMultiple = sizeof(T) / sizeof(char);

		std::array<unsigned int, Dimensions + 1> sizeData;
		memcpy(sizeData.data(), data.data(), sizeof(unsigned int));
		assert(Dimensions == sizeData[0]);

		memcpy(sizeData.data(), data.data(), sizeData.size() * sizeof(unsigned int));

		std::array<unsigned int, Dimensions> dims;
		memcpy(dims.data(), sizeData.data() + 1, Dimensions * sizeof(unsigned int));

		unsigned int count = dims[0];
		for (unsigned int i = 1; i < dims.size(); i++)
			count *= dims[i];

		std::vector<T> d(count);
		if (data.size() > 0)
			memcpy(d.data(), data.data() + ((Dimensions + 1) * charsInUint), std::min(count * sizeof(T), data.size()));

		return Serializable<T, Dimensions>(d, dims);
	}

	const std::array<unsigned int, Dimensions> getDimensions() const
	{
		std::array<unsigned int, Dimensions> data;
		memcpy(data.data(), m_Dims, Dimensions * sizeof(unsigned int));
		return data;
	}

	const T *get_data() const { return m_Data.data(); }

	T *get_data() { return m_Data.data(); }

  private:
	// First bytes of buffer indicating its size
	unsigned int m_Dims[Dimensions]{};
	// Data buffer
	std::vector<T> m_Data;
};
} // namespace rfw::utils
#endif // RENDERING_FW_SRC_UTILS_Serializable_HPP
