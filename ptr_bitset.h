#ifndef SHARK_PTR_BITSET_HPP
#define SHARK_PTR_BITSET_HPP
#include <stdlib.h>
#include <assert.h>

namespace shark
{

//////////////////////////////////////////////////////////////////////////
// template class to store bits of information 
// in the least significant bits of a pointer
template<class T> 
struct ptr_bits_traits {
	typedef T& reference;
};
template<> 
struct ptr_bits_traits<void> {
	typedef void reference;
};

template<class T, size_t BITS> 
class ptr_bits {
	enum {BITMASK = (1 << BITS)-1};
	T* mPtr;
public:
	typedef typename ptr_bits_traits<T>::reference reference;
	ptr_bits() : mPtr(0) {}
	ptr_bits(T* ptr) : mPtr(ptr) {
		assert(((size_t)ptr & BITMASK) == 0);
	}
	ptr_bits(T* ptr, size_t bits) : mPtr((T*)((size_t)ptr | bits)) {
		assert(((size_t)ptr & BITMASK) == 0);//ptr must be a null pointer
		assert((bits & ~BITMASK) == 0);
	}
	ptr_bits& operator=(const T* ptr) {
		assert(((size_t)ptr & BITMASK) == 0);
		size_t bits = (size_t)mPtr & BITMASK;
		mPtr = (T*)((size_t)ptr | bits);
		return *this;
	}
	ptr_bits(const ptr_bits& rhs) : mPtr(rhs) {
		assert(((size_t)mPtr & BITMASK) == 0);
	}
	ptr_bits& operator=(const ptr_bits& rhs) {
		const T* ptr = rhs;
		assert(((size_t)ptr & BITMASK) == 0);
		size_t bits = (size_t)mPtr & BITMASK;
		mPtr = (T*)((size_t)ptr | bits);
		return *this;
	}
	size_t get_bits() const {
		return (size_t)mPtr & BITMASK;
	}
	void set_bits(size_t bits = BITMASK) {
		assert((bits & ~BITMASK) == 0);
		mPtr = (T*)(((size_t)mPtr & ~BITMASK) | bits);
	}
	void clear_bits() {
		mPtr = (T*)((size_t)mPtr & ~BITMASK);
	}
	void swap_bits(ptr_bits& rhs) {
		size_t bits = get_bits();
		set_bits(rhs.get_bits());
		rhs.set_bits(bits);
	}
	template<size_t BIT> bool get_bit() const {
		size_t bits = (size_t)mPtr & BITMASK;
		return !!(bits & (1 << BIT));
	}
	template<size_t BIT> void set_bit() {
		mPtr = (T*)((size_t)mPtr | (1 << BIT));
	}
	template<size_t BIT> void clear_bit() {
		mPtr = (T*)((size_t)mPtr & ~(1 << BIT));
	}
	reference operator*() const {
		assert((size_t)mPtr & ~BITMASK);
		return *(T*)((size_t)mPtr & ~BITMASK);
	}
	T* operator->() const {
		assert((size_t)mPtr & ~BITMASK);
		return (T*)((size_t)mPtr & ~BITMASK);
	}
	operator T*() const {
		return (T*)((size_t)mPtr & ~BITMASK);
	}
};

}

#endif