//
// Created by Mèir Noordermeer on 2019-08-20.
//

#ifndef RENDERING_FW_SRC_UTILS_STRING_HPP
#define RENDERING_FW_SRC_UTILS_STRING_HPP

#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ArrayProxy.h"

namespace rfw
{
namespace utils
{
namespace string
{

static void format_list(char *buffer, const char *format, va_list args) { vsprintf(buffer, format, args); }

static void format(char *buffer, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	va_end(args);
}

static bool ends_with(std::string_view value, std::string_view ending)
{
	if (ending.size() > value.size())
		return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static bool ends_with(std::string_view value, ArrayProxy<std::string_view> endings)
{
	for (const auto ending : endings)
	{
		if (std::equal(ending.rbegin(), ending.rend(), value.rbegin()))
			return true;
	}

	return false;
}

} // namespace string
} // namespace utils
} // namespace rfw
#endif // RENDERING_FW_SRC_UTILS_STRING_HPP
