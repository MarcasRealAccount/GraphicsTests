#pragma once

#include "TypeTraits.h"

#include <limits>
#include <stdexcept>
#include <tuple>

#include <Memory/Memory.h>

namespace Details
{
	template <class... Ts>
	requires(sizeof...(Ts) > 0)
	struct TupleVectorOffsets
	{
	public:
		size_t Offsets[sizeof...(Ts) + 1];

		constexpr TupleVectorOffsets()
		{
			Fill(std::make_index_sequence<sizeof...(Ts)> {});
		}

		template <size_t... Indices>
		constexpr void Fill(std::index_sequence<Indices...>)
		{
			Offsets[0] = 0;
			((Offsets[Indices + 1] = Offsets[Indices] + sizeof(Ts)), ...);
		}

		constexpr size_t operator[](size_t index) const { return Offsets[index]; }
	};
} // namespace Details

template <class... Ts>
struct TupleVector;

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
struct TupleVectorIter
{
public:
	using Vec            = std::conditional_t<Const, const TupleVector<Ts...>*, TupleVector<Ts...>*>;
	using ref_tuple_type = std::conditional_t<Const, std::tuple<const Ts&...>, std::tuple<Ts&...>>;
	template <size_t... Columns>
	using ref_sub_tuple_type = std::conditional_t<Const, std::tuple<const NthType<Columns, Ts...>&...>, std::tuple<NthType<Columns, Ts...>&...>>;

public:
	TupleVectorIter();
	TupleVectorIter(const TupleVectorIter& copy);
	TupleVectorIter(Vec vec, size_t row);

	template <size_t... Columns>
	ref_sub_tuple_type<Columns...> columns();

	ref_tuple_type operator*();

	TupleVectorIter&       operator++();
	TupleVectorIter        operator++(int);
	TupleVectorIter&       operator--();
	TupleVectorIter        operator--(int);
	TupleVectorIter&       operator+=(ptrdiff_t n);
	TupleVectorIter&       operator-=(ptrdiff_t n);
	friend TupleVectorIter operator+(TupleVectorIter iter, ptrdiff_t n);
	friend TupleVectorIter operator+(ptrdiff_t n, TupleVectorIter iter);
	friend TupleVectorIter operator-(TupleVectorIter iter, ptrdiff_t n);
	friend ptrdiff_t       operator-(TupleVectorIter lhs, TupleVectorIter rhs);

	bool operator==(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row == m_Row;
	}
	bool operator!=(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row != m_Row;
	}
	bool operator<(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row < m_Row;
	}
	bool operator<=(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row <= m_Row;
	}
	bool operator>(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row > m_Row;
	}
	bool operator>=(TupleVectorIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row >= m_Row;
	}

private:
	Vec    m_Vec;
	size_t m_Row;
};

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
struct TupleVectorReverseIter
{
public:
	using Vec            = std::conditional_t<Const, const TupleVector<Ts...>*, TupleVector<Ts...>*>;
	using ref_tuple_type = std::conditional_t<Const, std::tuple<const Ts&...>, std::tuple<Ts&...>>;
	template <size_t... Columns>
	using ref_sub_tuple_type = std::conditional_t<Const, std::tuple<const NthType<Columns, Ts...>&...>, std::tuple<NthType<Columns, Ts...>&...>>;

public:
	TupleVectorReverseIter();
	TupleVectorReverseIter(const TupleVectorReverseIter& copy);
	TupleVectorReverseIter(Vec vec, size_t row);

	template <size_t... Columns>
	ref_sub_tuple_type<Columns...> columns();

	ref_tuple_type operator*();

	TupleVectorReverseIter&       operator++();
	TupleVectorReverseIter        operator++(int);
	TupleVectorReverseIter&       operator--();
	TupleVectorReverseIter        operator--(int);
	TupleVectorReverseIter&       operator+=(ptrdiff_t n);
	TupleVectorReverseIter&       operator-=(ptrdiff_t n);
	friend TupleVectorReverseIter operator+(TupleVectorReverseIter iter, ptrdiff_t n);
	friend TupleVectorReverseIter operator+(ptrdiff_t n, TupleVectorReverseIter iter);
	friend TupleVectorReverseIter operator-(TupleVectorReverseIter iter, ptrdiff_t n);
	friend ptrdiff_t              operator-(TupleVectorReverseIter lhs, TupleVectorReverseIter rhs);

	bool operator==(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row == m_Row;
	}
	bool operator!=(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row != m_Row;
	}
	bool operator<(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row < m_Row;
	}
	bool operator<=(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row <= m_Row;
	}
	bool operator>(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row > m_Row;
	}
	bool operator>=(TupleVectorReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row >= m_Row;
	}

private:
	Vec    m_Vec;
	size_t m_Row;
};

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
struct TupleVectorSubIter
{
public:
	using ref_tuple_type = typename Vec::template ref_sub_tuple_type<Columns...>;

public:
	TupleVectorSubIter();
	TupleVectorSubIter(const TupleVectorSubIter& copy);
	TupleVectorSubIter(Vec* vec, size_t row);

	ref_tuple_type operator*();

	TupleVectorSubIter&       operator++();
	TupleVectorSubIter        operator++(int);
	TupleVectorSubIter&       operator--();
	TupleVectorSubIter        operator--(int);
	TupleVectorSubIter&       operator+=(ptrdiff_t n);
	TupleVectorSubIter&       operator-=(ptrdiff_t n);
	friend TupleVectorSubIter operator+(TupleVectorSubIter iter, ptrdiff_t n);
	friend TupleVectorSubIter operator+(ptrdiff_t n, TupleVectorSubIter iter);
	friend TupleVectorSubIter operator-(TupleVectorSubIter iter, ptrdiff_t n);
	friend ptrdiff_t          operator-(TupleVectorSubIter lhs, TupleVectorSubIter rhs);

	bool operator==(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row == m_Row;
	}
	bool operator!=(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row != m_Row;
	}
	bool operator<(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row < m_Row;
	}
	bool operator<=(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row <= m_Row;
	}
	bool operator>(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row > m_Row;
	}
	bool operator>=(TupleVectorSubIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row >= m_Row;
	}

private:
	Vec*   m_Vec;
	size_t m_Row;
};

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
struct TupleVectorSubReverseIter
{
public:
	using ref_tuple_type = typename Vec::template ref_sub_tuple_type<Columns...>;

public:
	TupleVectorSubReverseIter();
	TupleVectorSubReverseIter(const TupleVectorSubReverseIter& copy);
	TupleVectorSubReverseIter(Vec* vec, size_t row);

	ref_tuple_type operator*();

	TupleVectorSubReverseIter&       operator++();
	TupleVectorSubReverseIter        operator++(int);
	TupleVectorSubReverseIter&       operator--();
	TupleVectorSubReverseIter        operator--(int);
	TupleVectorSubReverseIter&       operator+=(ptrdiff_t n);
	TupleVectorSubReverseIter&       operator-=(ptrdiff_t n);
	friend TupleVectorSubReverseIter operator+(TupleVectorSubReverseIter iter, ptrdiff_t n);
	friend TupleVectorSubReverseIter operator+(ptrdiff_t n, TupleVectorSubReverseIter iter);
	friend TupleVectorSubReverseIter operator-(TupleVectorSubReverseIter iter, ptrdiff_t n);
	friend ptrdiff_t                 operator-(TupleVectorSubReverseIter lhs, TupleVectorSubReverseIter rhs);

	bool operator==(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row == m_Row;
	}
	bool operator!=(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row != m_Row;
	}
	bool operator<(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row < m_Row;
	}
	bool operator<=(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row <= m_Row;
	}
	bool operator>(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row > m_Row;
	}
	bool operator>=(TupleVectorSubReverseIter other) const
	{
		if (m_Vec != other.m_Vec)
			throw std::runtime_error("Incompatible iterators");
		return m_Row >= m_Row;
	}

private:
	Vec*   m_Vec;
	size_t m_Row;
};

template <class... Ts>
requires(sizeof...(Ts) > 0)
struct TupleVector<Ts...>
{
public:
	using tuple_type               = std::tuple<Ts...>;
	using pointer_tuple_type       = std::tuple<Ts*...>;
	using const_pointer_tuple_type = std::tuple<const Ts*...>;
	using ref_tuple_type           = std::tuple<Ts&...>;
	using const_ref_tuple_type     = std::tuple<const Ts&...>;

	template <size_t... Columns>
	using pointer_sub_tuple_type = std::tuple<NthType<Columns, Ts...>*...>;
	template <size_t... Columns>
	using const_pointer_sub_tuple_type = std::tuple<const NthType<Columns, Ts...>*...>;
	template <size_t... Columns>
	using ref_sub_tuple_type = std::tuple<NthType<Columns, Ts...>&...>;
	template <size_t... Columns>
	using const_ref_sub_tuple_type = std::tuple<const NthType<Columns, Ts...>&...>;

	using size_type              = size_t;
	using difference_type        = ptrdiff_t;
	using iterator               = TupleVectorIter<false, Ts...>;
	using const_iterator         = TupleVectorIter<true, Ts...>;
	using reverse_iterator       = TupleVectorReverseIter<false, Ts...>;
	using const_reverse_iterator = TupleVectorReverseIter<true, Ts...>;
	template <size_t... Columns>
	using sub_iterator = TupleVectorSubIter<false, TupleVector, Columns...>;
	template <size_t... Columns>
	using sub_const_iterator = TupleVectorSubIter<true, TupleVector, Columns...>;
	template <size_t... Columns>
	using sub_reverse_iterator = TupleVectorSubReverseIter<false, TupleVector, Columns...>;
	template <size_t... Columns>
	using sub_const_reverse_iterator = TupleVectorSubReverseIter<true, TupleVector, Columns...>;

	static constexpr size_t ColumnCount = sizeof...(Ts);
	static constexpr auto   Offsets     = Details::TupleVectorOffsets<Ts...> {};
	static constexpr size_t RowSize     = Offsets[ColumnCount];

public:
	TupleVector();
	TupleVector(size_t count, const tuple_type& value);
	TupleVector(size_t count, const Ts&... columns);
	explicit TupleVector(size_t count);
	template <class InputIt>
	requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
	TupleVector(InputIt first, InputIt last);
	TupleVector(std::initializer_list<tuple_type> init);
	TupleVector(const TupleVector& copy);
	TupleVector(TupleVector&& move) noexcept;
	~TupleVector();

	TupleVector& operator=(std::initializer_list<tuple_type> init);
	TupleVector& operator=(const TupleVector& copy);
	TupleVector& operator=(TupleVector&& move) noexcept;

	void assign(size_t count, const tuple_type& value);
	void assign(size_t count, const Ts&... columns);
	template <class InputIt>
	requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
	void assign(InputIt first, InputIt last);
	void assign(std::initializer_list<tuple_type> init);

	ref_tuple_type           at(size_t row);
	const_ref_tuple_type     at(size_t row) const;
	ref_tuple_type           operator[](size_t row);
	const_ref_tuple_type     operator[](size_t row) const;
	ref_tuple_type           front();
	const_ref_tuple_type     front() const;
	ref_tuple_type           back();
	const_ref_tuple_type     back() const;
	pointer_tuple_type       data();
	const_pointer_tuple_type data() const;
	template <size_t... Columns>
	ref_sub_tuple_type<Columns...> sub_row(size_t row);
	template <size_t... Columns>
	const_ref_sub_tuple_type<Columns...> sub_row(size_t row) const;
	template <size_t... Columns>
	pointer_sub_tuple_type<Columns...> sub_data();
	template <size_t... Columns>
	const_pointer_sub_tuple_type<Columns...> sub_data() const;
	template <size_t Column>
	NthType<Column, Ts...>& entry(size_t row);
	template <size_t Column>
	const NthType<Column, Ts...>& entry(size_t row) const;
	template <size_t Column>
	NthType<Column, Ts...>* column();
	template <size_t Column>
	const NthType<Column, Ts...>* column() const;

	iterator               begin() { return iterator { this, 0 }; }
	iterator               end() { return iterator { this, m_Size }; }
	const_iterator         begin() const { return const_iterator { this, 0 }; }
	const_iterator         end() const { return const_iterator { this, m_Size }; }
	const_iterator         cbegin() const { return const_iterator { this, 0 }; }
	const_iterator         cend() const { return const_iterator { this, m_Size }; }
	reverse_iterator       rbegin() { return reverse_iterator { this, 0 }; }
	reverse_iterator       rend() { return reverse_iterator { this, m_Size }; }
	const_reverse_iterator rbegin() const { return const_reverse_iterator { this, 0 }; }
	const_reverse_iterator rend() const { return const_reverse_iterator { this, m_Size }; }
	const_reverse_iterator crbegin() const { return const_reverse_iterator { this, 0 }; }
	const_reverse_iterator crend() const { return const_reverse_iterator { this, m_Size }; }
	template <size_t... Columns>
	sub_iterator<Columns...> sub_begin()
	{
		return sub_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_iterator<Columns...> sub_end()
	{
		return sub_iterator<Columns...> { this, m_Size };
	}
	template <size_t... Columns>
	sub_const_iterator<Columns...> sub_begin() const
	{
		return sub_const_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_const_iterator<Columns...> sub_end() const
	{
		return sub_const_iterator<Columns...> { this, m_Size };
	}
	template <size_t... Columns>
	sub_const_iterator<Columns...> sub_cbegin() const
	{
		return sub_const_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_const_iterator<Columns...> sub_cend() const
	{
		return sub_const_iterator<Columns...> { this, m_Size };
	}
	template <size_t... Columns>
	sub_reverse_iterator<Columns...> sub_rbegin()
	{
		return sub_reverse_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_reverse_iterator<Columns...> sub_rend()
	{
		return sub_reverse_iterator<Columns...> { this, m_Size };
	}
	template <size_t... Columns>
	sub_const_reverse_iterator<Columns...> sub_rbegin() const
	{
		return sub_const_reverse_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_const_reverse_iterator<Columns...> sub_rend() const
	{
		return sub_const_reverse_iterator<Columns...> { this, m_Size };
	}
	template <size_t... Columns>
	sub_const_reverse_iterator<Columns...> sub_crbegin() const
	{
		return sub_const_reverse_iterator<Columns...> { this, 0 };
	}
	template <size_t... Columns>
	sub_const_reverse_iterator<Columns...> sub_crend() const
	{
		return sub_const_reverse_iterator<Columns...> { this, m_Size };
	}

	bool                    empty() const { return m_Size == 0; }
	size_t                  size() const { return m_Size; }
	static constexpr size_t max_size() { return std::numeric_limits<size_t>::max() / RowSize; }
	void                    reserve(size_t newCapacity);
	size_t                  capacity() const { return m_Capacity; }
	void                    shrink_to_fit();

	void     clear();
	iterator insert(const_iterator pos, const tuple_type& value);
	iterator insert(const_iterator pos, tuple_type&& value);
	iterator insert(const_iterator pos, const Ts&... columns);
	iterator insert(const_iterator pos, Ts&&... columns);
	iterator insert(const_iterator pos, size_t count, const tuple_type& value);
	template <class InputIt>
	requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
	iterator       insert(const_iterator pos, InputIt first, InputIt last);
	iterator       insert(const_iterator pos, std::initializer_list<tuple_type> init);
	iterator       emplace(const_iterator pos);
	iterator       erase(const_iterator pos);
	iterator       erase(const_iterator first, const_iterator last);
	void           push_back(const tuple_type& value);
	void           push_back(tuple_type&& value);
	void           push_back(const Ts&... columns);
	void           push_back(Ts&&... columns);
	ref_tuple_type emplace_back();
	void           pop_back();
	void           resize(size_t newSize);
	void           resize(size_t newSize, const tuple_type& value);
	void           resize(size_t newSize, const Ts&... columns);
	void           swap(TupleVector& other) noexcept;

private:
	template <size_t... Indices>
	ref_tuple_type at_internal(size_t row, std::index_sequence<Indices...>);
	template <size_t... Indices>
	const_ref_tuple_type at_internal(size_t row, std::index_sequence<Indices...>) const;
	template <size_t... Indices>
	pointer_tuple_type data_internal(std::index_sequence<Indices...>);
	template <size_t... Indices>
	const_pointer_tuple_type data_internal(std::index_sequence<Indices...>) const;

	static void ShiftColumns(void* data, size_t capacity, size_t start, size_t end, ptrdiff_t count);
	template <size_t... Indices>
	static void ShiftColumns2(void* data, size_t capacity, size_t start, size_t end, ptrdiff_t count, std::index_sequence<Indices...>);
	template <size_t Column>
	static void ShiftColumn(void* column, size_t start, size_t end, ptrdiff_t count);

	static void MoveColumns(void* newData, size_t newCapacity, void* oldData, size_t oldCapacity, size_t count);
	template <size_t... Indices>
	static void MoveColumns2(void* newData, size_t newCapacity, void* oldData, size_t oldCapacity, size_t count, std::index_sequence<Indices...>);
	template <size_t Column>
	static void MoveColumn(void* newColumn, void* oldColumn, size_t count);

	static void DestroyColumns(void* data, size_t capacity, size_t count);
	template <size_t... Indices>
	static void DestroyColumns2(void* data, size_t capacity, size_t count, std::index_sequence<Indices...>);
	template <size_t Column>
	static void DestroyColumn(void* column, size_t count);

	static void DestroyRow(void* data, size_t capacity, size_t row);
	template <size_t... Indices>
	static void DestroyRow2(void* data, size_t capacity, size_t row, std::index_sequence<Indices...>);

	static void DefaultConstructRow(void* data, size_t capacity, size_t row);
	template <size_t... Indices>
	static void DefaultConstructRow2(void* data, size_t capacity, size_t row, std::index_sequence<Indices...>);

	static void CopyConstructRow(void* data, size_t capacity, size_t row, const tuple_type& value);
	static void CopyConstructRow(void* data, size_t capacity, size_t row, const Ts&... columns);
	template <size_t... Indices>
	static void CopyConstructRow2(void* data, size_t capacity, size_t row, const tuple_type& value, std::index_sequence<Indices...>);
	template <size_t... Indices>
	static void CopyConstructRow2(void* data, size_t capacity, size_t row, const Ts&... columns, std::index_sequence<Indices...>);

	static void MoveConstructRow(void* data, size_t capacity, size_t row, tuple_type&& value);
	static void MoveConstructRow(void* data, size_t capacity, size_t row, Ts&&... columns);
	template <size_t... Indices>
	static void MoveConstructRow2(void* data, size_t capacity, size_t row, tuple_type&& value, std::index_sequence<Indices...>);
	template <size_t... Indices>
	static void MoveConstructRow2(void* data, size_t capacity, size_t row, Ts&&... columns, std::index_sequence<Indices...>);

private:
	size_t m_Capacity;
	size_t m_Size;
	void*  m_Data;
};

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector()
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(size_t count, const tuple_type& value)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	resize(count, value);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(size_t count, const Ts&... columns)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	resize(count, columns...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(size_t count)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	resize(count);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <class InputIt>
requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
TupleVector<Ts...>::TupleVector(InputIt first, InputIt last)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	reserve(std::distance(first, last));
	for (auto it = first; it != last; ++it)
		push_back(*it);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(std::initializer_list<tuple_type> init)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	reserve(init.size());
	for (auto it = init.begin(); it != init.end(); ++it)
		push_back(*it);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(const TupleVector& copy)
	: m_Capacity(0),
	  m_Size(0),
	  m_Data(nullptr)
{
	reserve(copy.size());
	for (size_t i = 0; i < copy.size(); ++i)
		push_back(copy[i]);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::TupleVector(TupleVector&& move) noexcept
	: m_Capacity(move.m_Capacity),
	  m_Size(move.m_Size),
	  m_Data(move.m_Data)
{
	move.m_Capacity = 0;
	move.m_Size     = 0;
	move.m_Data     = nullptr;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::~TupleVector()
{
	clear();
	Memory::Free(m_Data);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>& TupleVector<Ts...>::operator=(std::initializer_list<tuple_type> init)
{
	clear();
	reserve(init.size());
	for (auto it = init.begin(); it != init.end(); ++it)
		push_back(*it);
	return *this;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>& TupleVector<Ts...>::operator=(const TupleVector& copy)
{
	clear();
	reserve(copy.size());
	for (size_t i = 0; i < copy.size(); ++i)
		push_back(copy[i]);
	return *this;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>& TupleVector<Ts...>::operator=(TupleVector&& move) noexcept
{
	clear();
	Memory::Free(m_Data);
	m_Capacity      = move.m_Capacity;
	m_Size          = move.m_Size;
	m_Data          = move.m_Data;
	move.m_Capacity = 0;
	move.m_Size     = 0;
	move.m_Data     = nullptr;
	return *this;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::assign(size_t count, const tuple_type& value)
{
	clear();
	resize(count, value);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::assign(size_t count, const Ts&... columns)
{
	clear();
	resize(count, columns...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <class InputIt>
requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
void TupleVector<Ts...>::assign(InputIt first, InputIt last)
{
	clear();
	reserve(std::distance(first, last));
	for (auto it = first; it != last; ++it)
		push_back(*it);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::assign(std::initializer_list<tuple_type> init)
{
	clear();
	reserve(init.size());
	for (auto it = init.begin(); it != init.end(); ++it)
		push_back(*it);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::at(size_t row)
{
	return at_internal(row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::const_ref_tuple_type TupleVector<Ts...>::at(size_t row) const
{
	return at_internal(row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::operator[](size_t row)
{
	return at_internal(row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::const_ref_tuple_type TupleVector<Ts...>::operator[](size_t row) const
{
	return at_internal(row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::front()
{
	return at_internal(0, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::const_ref_tuple_type TupleVector<Ts...>::front() const
{
	return at_internal(0, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::back()
{
	return at_internal(m_Size - 1, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::const_ref_tuple_type TupleVector<Ts...>::back() const
{
	return at_internal(m_Size - 1, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::pointer_tuple_type TupleVector<Ts...>::data()
{
	return data_internal(std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::const_pointer_tuple_type TupleVector<Ts...>::data() const
{
	return data_internal(std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVector<Ts...>::ref_sub_tuple_type<Columns...> TupleVector<Ts...>::sub_row(size_t row)
{
	return at_internal(row, std::index_sequence<Columns...> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVector<Ts...>::const_ref_sub_tuple_type<Columns...> TupleVector<Ts...>::sub_row(size_t row) const
{
	return at_internal(row, std::index_sequence<Columns...> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVector<Ts...>::pointer_sub_tuple_type<Columns...> TupleVector<Ts...>::sub_data()
{
	return data_internal(std::index_sequence<Columns...> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVector<Ts...>::const_pointer_sub_tuple_type<Columns...> TupleVector<Ts...>::sub_data() const
{
	return data_internal(std::index_sequence<Columns...> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
NthType<Column, Ts...>& TupleVector<Ts...>::entry(size_t row)
{
	return ((NthType<Column, Ts...>*) ((uint8_t*) m_Data + Offsets[Column] * m_Capacity))[row];
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
const NthType<Column, Ts...>& TupleVector<Ts...>::entry(size_t row) const
{
	return ((const NthType<Column, Ts...>*) ((const uint8_t*) m_Data + Offsets[Column] * m_Capacity))[row];
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
NthType<Column, Ts...>* TupleVector<Ts...>::column()
{
	return (NthType<Column, Ts...>*) ((uint8_t*) m_Data + Offsets[Column] * m_Capacity);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
const NthType<Column, Ts...>* TupleVector<Ts...>::column() const
{
	return (const NthType<Column, Ts...>*) ((const uint8_t*) m_Data + Offsets[Column] * m_Capacity);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::reserve(size_t newCapacity)
{
	if (newCapacity <= m_Capacity)
		return;
	newCapacity   = std::bit_ceil(newCapacity);
	void* newData = Memory::Malloc(RowSize * newCapacity);
	MoveColumns(newData, newCapacity, m_Data, m_Capacity, m_Size);
	Memory::Free(m_Data);
	m_Capacity = newCapacity;
	m_Data     = newData;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::shrink_to_fit()
{
	if (m_Capacity == m_Size)
		return;

	void* newData = Memory::Malloc(RowSize * m_Size);
	MoveColumns(newData, m_Size, m_Data, m_Capacity, m_Size);
	Memory::Free(m_Data);
	m_Capacity = m_Size;
	m_Data     = newData;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::clear()
{
	DestroyColumns(m_Data, m_Capacity, m_Size);
	m_Size = 0;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, const tuple_type& value)
{
	reserve(m_Size + 1);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, 1);
	CopyConstructRow(m_Data, m_Capacity, row, value);
	++m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, tuple_type&& value)
{
	reserve(m_Size + 1);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, 1);
	MoveConstructRow(m_Data, m_Capacity, row, std::move(value));
	++m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, const Ts&... columns)
{
	reserve(m_Size + 1);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, 1);
	CopyConstructRow(m_Data, m_Capacity, row, columns...);
	++m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, Ts&&... columns)
{
	reserve(m_Size + 1);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, 1);
	MoveConstructRow(m_Data, m_Capacity, row, std::move(columns)...);
	++m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, size_t count, const tuple_type& value)
{
	reserve(m_Size + count);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, count);
	for (size_t i = 0; i < count; ++i)
		CopyConstructRow(m_Data, m_Capacity, row + i, value);
	m_Size += count;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <class InputIt>
requires(std::convertible_to<decltype(*std::declval<InputIt>()), typename TupleVector<Ts...>::tuple_type>)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, InputIt first, InputIt last)
{
	size_t count = std::distance(first, last);
	reserve(m_Size + count);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, m_Size, count);
	size_t offset = row;
	for (auto it = first; it != last; ++it, ++offset)
		CopyConstructRow(m_Data, m_Capacity, offset, *it);
	m_Size += count;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::insert(const_iterator pos, std::initializer_list<tuple_type> init)
{
	reserve(m_Size + init.size());
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, init.size());
	size_t offset = row;
	for (auto it = init.begin(); it != init.end(); ++it, ++offset)
		CopyConstructRow(m_Data, m_Capacity, offset, *it);
	m_Size += init.size();
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::emplace(const_iterator pos)
{
	reserve(m_Size + 1);
	size_t row = pos - cbegin();
	ShiftColumns(m_Data, m_Capacity, row, 1);
	DefaultConstructRow(m_Data, m_Capacity, row);
	++m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::erase(const_iterator pos)
{
	size_t row = pos - cbegin();
	DestroyRow(m_Data, m_Capacity, row);
	ShiftColumns(m_Data, m_Capacity, row + 1, m_Size, -1);
	--m_Size;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::iterator TupleVector<Ts...>::erase(const_iterator first, const_iterator last)
{
	size_t    row   = first - cbegin();
	ptrdiff_t count = last - first;
	if (count < 0)
		return end();
	for (size_t i = 0; i < count; ++i)
		DestroyRow(m_Data, m_Capacity, row + i);
	ShiftColumns(m_Data, m_Capacity, row + count, m_Size, -count);
	m_Size -= count;
	return begin() + row;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::push_back(const tuple_type& value)
{
	reserve(m_Size + 1);
	CopyConstructRow(m_Data, m_Capacity, m_Size, value);
	++m_Size;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::push_back(tuple_type&& value)
{
	reserve(m_Size + 1);
	MoveConstructRow(m_Data, m_Capacity, m_Size, std::move(value));
	++m_Size;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::push_back(const Ts&... columns)
{
	reserve(m_Size + 1);
	CopyConstructRow(m_Data, m_Capacity, m_Size, columns...);
	++m_Size;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::push_back(Ts&&... columns)
{
	reserve(m_Size + 1);
	MoveConstructRow(m_Data, m_Capacity, m_Size, std::move(columns)...);
	++m_Size;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::emplace_back()
{
	reserve(m_Size + 1);
	DefaultConstructRow(m_Data, m_Capacity, m_Size);
	return at(m_Size++);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::pop_back()
{
	if (empty())
		return;
	DestroyRow(m_Data, m_Capacity, m_Size - 1);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::resize(size_t newSize)
{
	if (newSize < m_Size)
	{
		for (size_t i = newSize; i < m_Size; ++i)
			DestroyRow(m_Data, m_Capacity, i);
	}
	else
	{
		reserve(newSize);
		for (size_t i = m_Size; i < newSize; ++i)
			DefaultConstructRow(m_Data, m_Capacity, i);
	}
	m_Size = newSize;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::resize(size_t newSize, const tuple_type& value)
{
	if (newSize < m_Size)
	{
		for (size_t i = newSize; i < m_Size; ++i)
			DestroyRow(m_Data, m_Capacity, i);
	}
	else
	{
		reserve(newSize);
		for (size_t i = m_Size; i < newSize; ++i)
			CopyConstructRow(m_Data, m_Capacity, i, value);
	}
	m_Size = newSize;
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::resize(size_t newSize, const Ts&... columns)
{
	if (newSize < m_Size)
	{
		for (size_t i = newSize; i < m_Size; ++i)
			DestroyRow(m_Data, m_Capacity, i);
	}
	else
	{
		reserve(newSize);
		for (size_t i = m_Size; i < newSize; ++i)
			CopyConstructRow(m_Data, m_Capacity, i, columns...);
	}
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::swap(TupleVector& other) noexcept
{
	std::swap(m_Capacity, other.m_Capacity);
	std::swap(m_Size, other.m_Size);
	std::swap(m_Data, other.m_Data);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
TupleVector<Ts...>::ref_tuple_type TupleVector<Ts...>::at_internal(size_t row, std::index_sequence<Indices...>)
{
	return ref_tuple_type((((Ts*) ((uint8_t*) m_Data + Offsets[Indices] * m_Capacity))[row])...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
TupleVector<Ts...>::const_ref_tuple_type TupleVector<Ts...>::at_internal(size_t row, std::index_sequence<Indices...>) const
{
	return const_ref_tuple_type((((const Ts*) ((const uint8_t*) m_Data + Offsets[Indices] * m_Capacity))[row])...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
TupleVector<Ts...>::pointer_tuple_type TupleVector<Ts...>::data_internal(std::index_sequence<Indices...>)
{
	return pointer_tuple_type((Ts*) ((uint8_t*) m_Data + Offsets[Indices] * m_Capacity)...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
TupleVector<Ts...>::const_pointer_tuple_type TupleVector<Ts...>::data_internal(std::index_sequence<Indices...>) const
{
	return const_pointer_tuple_type((const Ts*) ((const uint8_t*) m_Data + Offsets[Indices] * m_Capacity)...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::ShiftColumns(void* data, size_t capacity, size_t start, size_t end, ptrdiff_t count)
{
	if (start == end)
		return;

	ShiftColumns2(data, capacity, start, end, count, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::ShiftColumns2(void* data, size_t capacity, size_t start, size_t end, ptrdiff_t count, std::index_sequence<Indices...>)
{
	(ShiftColumn<Indices>((uint8_t*) data + Offsets[Indices] * capacity, start, end, count), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
void TupleVector<Ts...>::ShiftColumn(void* column, size_t start, size_t end, ptrdiff_t count)
{
	using T = NthType<Column, Ts...>;
	T* ptr0 = (T*) column + start + count;
	T* ptr1 = (T*) column + end + count - 1;
	if (count > 0)
	{
		for (ptrdiff_t i = 0; i < count; ++i, --ptr0, --ptr1)
			new (ptr1) T(std::move(*ptr0));
	}
	else
	{
		count = -count;
		for (ptrdiff_t i = 0; i < count; ++i, ++ptr0, ++ptr1)
			new (ptr0) T(std::move(*ptr1));
	}
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::MoveColumns(void* newData, size_t newCapacity, void* oldData, size_t oldCapacity, size_t count)
{
	MoveColumns2(newData, newCapacity, oldData, oldCapacity, count, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::MoveColumns2(void* newData, size_t newCapacity, void* oldData, size_t oldCapacity, size_t count, std::index_sequence<Indices...>)
{
	(MoveColumn<Indices>((uint8_t*) newData + Offsets[Indices] * newCapacity, (uint8_t*) oldData + Offsets[Indices] * oldCapacity, count), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
void TupleVector<Ts...>::MoveColumn(void* newColumn, void* oldColumn, size_t count)
{
	using T      = NthType<Column, Ts...>;
	T* newColPtr = (T*) newColumn;
	T* oldColPtr = (T*) oldColumn;
	for (size_t i = 0; i < count; ++i)
		new (&newColPtr[i]) T(std::move(oldColPtr[i]));
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::DestroyColumns(void* data, size_t capacity, size_t count)
{
	DestroyColumns2(data, capacity, count, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::DestroyColumns2(void* data, size_t capacity, size_t count, std::index_sequence<Indices...>)
{
	(DestroyColumn<Indices>((uint8_t*) data + Offsets[Indices] * capacity, count), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t Column>
void TupleVector<Ts...>::DestroyColumn(void* column, size_t count)
{
	using T = NthType<Column, Ts...>;
	T* ptr  = (T*) column;
	for (size_t i = 0; i < count; ++i)
		ptr[i].~T();
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::DestroyRow(void* data, size_t capacity, size_t row)
{
	DestroyRow2(data, capacity, row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::DestroyRow2(void* data, size_t capacity, size_t row, std::index_sequence<Indices...>)
{
	(((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row)->~decltype(Ts)(), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::DefaultConstructRow(void* data, size_t capacity, size_t row)
{
	DefaultConstructRow2(data, capacity, row, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::DefaultConstructRow2(void* data, size_t capacity, size_t row, std::index_sequence<Indices...>)
{
	(new ((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row) Ts(), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::CopyConstructRow(void* data, size_t capacity, size_t row, const tuple_type& value)
{
	CopyConstructRow2(data, capacity, row, value, std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::CopyConstructRow(void* data, size_t capacity, size_t row, const Ts&... columns)
{
	CopyConstructRow2(data, capacity, row, columns..., std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::CopyConstructRow2(void* data, size_t capacity, size_t row, const tuple_type& value, std::index_sequence<Indices...>)
{
	(new ((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row) Ts(std::get<Indices>(value)), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::CopyConstructRow2(void* data, size_t capacity, size_t row, const Ts&... columns, std::index_sequence<Indices...>)
{
	(new ((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row) Ts(columns), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::MoveConstructRow(void* data, size_t capacity, size_t row, tuple_type&& value)
{
	MoveConstructRow2(data, capacity, row, std::move(value), std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
void TupleVector<Ts...>::MoveConstructRow(void* data, size_t capacity, size_t row, Ts&&... columns)
{
	MoveConstructRow2(data, capacity, row, std::move(columns)..., std::make_index_sequence<sizeof...(Ts)> {});
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::MoveConstructRow2(void* data, size_t capacity, size_t row, tuple_type&& value, std::index_sequence<Indices...>)
{
	(new ((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row) Ts(std::move(std::get<Indices>(value))), ...);
}

template <class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Indices>
void TupleVector<Ts...>::MoveConstructRow2(void* data, size_t capacity, size_t row, Ts&&... columns, std::index_sequence<Indices...>)
{
	(new ((Ts*) ((uint8_t*) data + Offsets[Indices] * capacity) + row) Ts(std::move(columns)), ...);
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>::TupleVectorIter()
	: m_Vec(nullptr),
	  m_Row(0)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>::TupleVectorIter(const TupleVectorIter& copy)
	: m_Vec(copy.m_Vec),
	  m_Row(copy.m_Row)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>::TupleVectorIter(Vec vec, size_t row)
	: m_Vec(vec),
	  m_Row(row)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVectorIter<Const, Ts...>::ref_sub_tuple_type<Columns...> TupleVectorIter<Const, Ts...>::columns()
{
	return (*m_Vec).sub_row<Columns...>(m_Row);
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>::ref_tuple_type TupleVectorIter<Const, Ts...>::operator*()
{
	return (*m_Vec)[m_Row];
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>& TupleVectorIter<Const, Ts...>::operator++()
{
	if (++m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...> TupleVectorIter<Const, Ts...>::operator++(int)
{
	auto copy = *this;
	++(*this);
	return copy;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>& TupleVectorIter<Const, Ts...>::operator--()
{
	if (--m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...> TupleVectorIter<Const, Ts...>::operator--(int)
{
	auto copy = *this;
	--(*this);
	return copy;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>& TupleVectorIter<Const, Ts...>::operator+=(ptrdiff_t n)
{
	if ((m_Row += n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...>& TupleVectorIter<Const, Ts...>::operator-=(ptrdiff_t n)
{
	if ((m_Row -= n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...> operator+(TupleVectorIter<Const, Ts...> iter, ptrdiff_t n)
{
	return iter += n;
}

template <bool Const, class... Ts>
TupleVectorIter<Const, Ts...> operator+(ptrdiff_t n, TupleVectorIter<Const, Ts...> iter)
{
	return iter += n;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorIter<Const, Ts...> operator-(TupleVectorIter<Const, Ts...> iter, ptrdiff_t n)
{
	return iter -= n;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
ptrdiff_t operator-(TupleVectorIter<Const, Ts...> lhs, TupleVectorIter<Const, Ts...> rhs)
{
	if (lhs.m_Vec != rhs.m_Vec)
		throw std::runtime_error("Incompatible iterators");
	return lhs.m_Row - rhs.m_Row;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>::TupleVectorReverseIter()
	: m_Vec(nullptr),
	  m_Row(0)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>::TupleVectorReverseIter(const TupleVectorReverseIter& copy)
	: m_Vec(copy.m_Vec),
	  m_Row(copy.m_Row)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>::TupleVectorReverseIter(Vec vec, size_t row)
	: m_Vec(vec),
	  m_Row(row)
{
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
template <size_t... Columns>
TupleVectorReverseIter<Const, Ts...>::ref_sub_tuple_type<Columns...> TupleVectorReverseIter<Const, Ts...>::columns()
{
	return (*m_Vec).sub_row<Columns...>(m_Vec->size() - m_Row - 1);
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>::ref_tuple_type TupleVectorReverseIter<Const, Ts...>::operator*()
{
	return (*m_Vec)[m_Vec->size() - m_Row - 1];
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>& TupleVectorReverseIter<Const, Ts...>::operator++()
{
	if (++m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...> TupleVectorReverseIter<Const, Ts...>::operator++(int)
{
	auto copy = *this;
	++(*this);
	return copy;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>& TupleVectorReverseIter<Const, Ts...>::operator--()
{
	if (--m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...> TupleVectorReverseIter<Const, Ts...>::operator--(int)
{
	auto copy = *this;
	--(*this);
	return copy;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>& TupleVectorReverseIter<Const, Ts...>::operator+=(ptrdiff_t n)
{
	if ((m_Row += n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...>& TupleVectorReverseIter<Const, Ts...>::operator-=(ptrdiff_t n)
{
	if ((m_Row -= n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...> operator+(TupleVectorReverseIter<Const, Ts...> iter, ptrdiff_t n)
{
	return iter += n;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...> operator+(ptrdiff_t n, TupleVectorReverseIter<Const, Ts...> iter)
{
	return iter += n;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
TupleVectorReverseIter<Const, Ts...> operator-(TupleVectorReverseIter<Const, Ts...> iter, ptrdiff_t n)
{
	return iter -= n;
}

template <bool Const, class... Ts>
requires(sizeof...(Ts) > 0)
ptrdiff_t operator-(TupleVectorReverseIter<Const, Ts...> lhs, TupleVectorReverseIter<Const, Ts...> rhs)
{
	if (lhs.m_Vec != rhs.m_Vec)
		throw std::runtime_error("Incompatible iterators");
	return lhs.m_Row - rhs.m_Row;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>::TupleVectorSubIter()
	: m_Vec(nullptr),
	  m_Row(0)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>::TupleVectorSubIter(const TupleVectorSubIter& copy)
	: m_Vec(copy.m_Vec),
	  m_Row(copy.m_Row)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>::TupleVectorSubIter(Vec* vec, size_t row)
	: m_Vec(vec),
	  m_Row(row)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>::ref_tuple_type TupleVectorSubIter<Const, Vec, Columns...>::operator*()
{
	return m_Vec->sub_row<Columns...>(m_Row);
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>& TupleVectorSubIter<Const, Vec, Columns...>::operator++()
{
	if (++m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...> TupleVectorSubIter<Const, Vec, Columns...>::operator++(int)
{
	auto copy = *this;
	++(*this);
	return copy;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>& TupleVectorSubIter<Const, Vec, Columns...>::operator--()
{
	if (--m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...> TupleVectorSubIter<Const, Vec, Columns...>::operator--(int)
{
	auto copy = *this;
	--(*this);
	return copy;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>& TupleVectorSubIter<Const, Vec, Columns...>::operator+=(ptrdiff_t n)
{
	if ((m_Row += n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...>& TupleVectorSubIter<Const, Vec, Columns...>::operator-=(ptrdiff_t n)
{
	if ((m_Row -= n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...> operator+(TupleVectorSubIter<Const, Vec, Columns...> iter, ptrdiff_t n)
{
	return iter += n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...> operator+(ptrdiff_t n, TupleVectorSubIter<Const, Vec, Columns...> iter)
{
	return iter += n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubIter<Const, Vec, Columns...> operator-(TupleVectorSubIter<Const, Vec, Columns...> iter, ptrdiff_t n)
{
	return iter -= n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
ptrdiff_t operator-(TupleVectorSubIter<Const, Vec, Columns...> lhs, TupleVectorSubIter<Const, Vec, Columns...> rhs)
{
	if (lhs.m_Vec != rhs.m_Vec)
		throw std::runtime_error("Incompatible iterators");
	return lhs.m_Row - rhs.m_Row;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>::TupleVectorSubReverseIter()
	: m_Vec(nullptr),
	  m_Row(0)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>::TupleVectorSubReverseIter(const TupleVectorSubReverseIter& copy)
	: m_Vec(copy.m_Vec),
	  m_Row(copy.m_Row)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>::TupleVectorSubReverseIter(Vec* vec, size_t row)
	: m_Vec(vec),
	  m_Row(row)
{
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>::ref_tuple_type TupleVectorSubReverseIter<Const, Vec, Columns...>::operator*()
{
	return m_Vec->sub_row<Columns...>(m_Vec->size() - m_Row - 1);
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>& TupleVectorSubReverseIter<Const, Vec, Columns...>::operator++()
{
	if (++m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...> TupleVectorSubReverseIter<Const, Vec, Columns...>::operator++(int)
{
	auto copy = *this;
	++(*this);
	return copy;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>& TupleVectorSubReverseIter<Const, Vec, Columns...>::operator--()
{
	if (--m_Row > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...> TupleVectorSubReverseIter<Const, Vec, Columns...>::operator--(int)
{
	auto copy = *this;
	--(*this);
	return copy;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>& TupleVectorSubReverseIter<Const, Vec, Columns...>::operator+=(ptrdiff_t n)
{
	if ((m_Row += n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...>& TupleVectorSubReverseIter<Const, Vec, Columns...>::operator-=(ptrdiff_t n)
{
	if ((m_Row -= n) > m_Vec->size())
		m_Row = m_Vec->size(); // This is out of bounds
	return *this;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...> operator+(TupleVectorSubReverseIter<Const, Vec, Columns...> iter, ptrdiff_t n)
{
	return iter += n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...> operator+(ptrdiff_t n, TupleVectorSubReverseIter<Const, Vec, Columns...> iter)
{
	return iter += n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
TupleVectorSubReverseIter<Const, Vec, Columns...> operator-(TupleVectorSubReverseIter<Const, Vec, Columns...> iter, ptrdiff_t n)
{
	return iter -= n;
}

template <bool Const, class Vec, size_t... Columns>
requires(sizeof...(Columns) > 0)
ptrdiff_t operator-(TupleVectorSubReverseIter<Const, Vec, Columns...> lhs, TupleVectorSubReverseIter<Const, Vec, Columns...> rhs)
{
	if (lhs.m_Vec != rhs.m_Vec)
		throw std::runtime_error("Incompatible iterators");
	return lhs.m_Row - rhs.m_Row;
}