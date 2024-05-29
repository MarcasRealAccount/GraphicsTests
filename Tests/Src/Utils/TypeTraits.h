#pragma once

#include <cstddef>
#include <cstdint>

#include <utility>

namespace Details
{
	template <size_t Index, class... Ts>
	struct NthTypeS
	{
		using Type = void;
	};

	template <size_t Index, class T, class... Ts>
	struct NthTypeS<Index, T, Ts...>
	{
		using Type = typename NthTypeS<Index - 1, Ts...>::Type;
	};

	template <class T, class... Ts>
	struct NthTypeS<0, T, Ts...>
	{
		using Type = T;
	};
} // namespace Details

template <size_t Index, class... Ts>
using NthType = typename Details::NthTypeS<Index, Ts...>::Type;