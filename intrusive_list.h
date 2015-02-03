#ifndef SHARK_LIST_HPP
#define SHARK_LIST_HPP
#include <assert.h>

namespace shark
{

class intrusive_list_base {
public:

	/*node_base只是个wrapper，将容器包含的内容进行包装*/
	class node_base 
 	{
		node_base* mPrev;
		node_base* mNext;
	public:
		node_base* next() const {return mNext;}
		node_base* prev() const {return mPrev;}
		void reset() 
		{
			mPrev = this;
			mNext = this;
		}
		void unlink() {
			mNext->mPrev = mPrev;
			mPrev->mNext = mNext;
			reset();
		}
		void link(node_base* node) 
		{
			mPrev = node->mPrev;
			mNext = node;
			node->mPrev = this;
			mPrev->mNext = this;
		}
	};
	intrusive_list_base() {
		mHead.reset();
	}
	intrusive_list_base(const intrusive_list_base&) {
		mHead.reset();
	}
	bool empty() const {return mHead.next() == &mHead;}

	//only switch both lists' head nodes
	void swap(intrusive_list_base& other) {
		node_base* node = &other.mHead;
		if (!empty()) {
			node = mHead.next();
			mHead.unlink();
			mHead.reset();
		}
		node_base* other_node = &mHead;
		if (!other.empty()) {
			other_node = other.mHead.next();
			other.mHead.unlink();
			other.mHead.reset();
		}
		mHead.link(other_node);
		other.mHead.link(node);
	}
protected:
	node_base mHead;
};

//////////////////////////////////////////////////////////////////////////
template<class T> 
class intrusive_list : public intrusive_list_base {
	intrusive_list(const intrusive_list& rhs);
	intrusive_list& operator=(const intrusive_list& rhs);
public:
	class node : public node_base {
	public:
		T* next() const {return static_cast<T*>(node_base::next());}
		T* prev() const {return static_cast<T*>(node_base::prev());}
		const T& data() const {return *static_cast<const T*>(this);}
		T& data() {return *static_cast<T*>(this);}
	};
	
	class const_iterator;
	class iterator 
	{
		typedef T& reference;
		typedef T* pointer;
		friend class const_iterator;
		T* mPtr;
	public:
		iterator() : mPtr(0) {}
		explicit iterator(T* ptr) : mPtr(ptr) {}
		reference operator*() const {return mPtr->data();}
		pointer operator->() const {return &mPtr->data();}
		operator pointer() const {return &mPtr->data();}
		iterator& operator++() 
		{
			mPtr = mPtr->next();
			return *this;
		}
		iterator& operator--() {
			mPtr = mPtr->prev();
			return *this;
		}
		bool operator==(const iterator& rhs) const {return mPtr == rhs.mPtr;}
		bool operator!=(const iterator& rhs) const {return mPtr != rhs.mPtr;}
		T* ptr() const {return mPtr;}
	};

	class const_iterator {
		typedef const T& reference;
		typedef const T* pointer;
		const T* mPtr;
	public:
		const_iterator() : mPtr(0) {}
		explicit const_iterator(const T* ptr) : mPtr(ptr) {}
		const_iterator(const iterator& it) : mPtr(it.mPtr) {}
		reference operator*() const {return mPtr->data();}
		pointer operator->() const {return &mPtr->data();}
		operator pointer() const {return &mPtr->data();}
		const_iterator& operator++() {
			mPtr = mPtr->next();
			return *this;
		}
		const_iterator& operator--() {
			mPtr = mPtr->prev();
			return *this;
		}
		bool operator==(const const_iterator& rhs) const {return mPtr == rhs.mPtr;}
		bool operator!=(const const_iterator& rhs) const {return mPtr != rhs.mPtr;}
		const T* ptr() const {return mPtr;}
	};

	intrusive_list() : intrusive_list_base() {}
	~intrusive_list() {clear();}
	
	const_iterator begin() const {return const_iterator((const T*)mHead.next());}
	iterator begin() {return iterator((T*)mHead.next());}
	const_iterator end() const {return const_iterator((const T*)&mHead);}
	iterator end() {return iterator((T*)&mHead);}

	const T& front() const {
		assert(!empty());
		return *begin();
	}
	T& front() {
		assert(!empty());
		return *begin();
	}
	const T& back() const {
		assert(!empty());
		return *(--end());
	}
	T& back() {
		assert(!empty());
		return *(--end());
	}

	void push_front(T* v) {insert(this->begin(), v);}
	void pop_front() {erase(this->begin());}
	void push_back(T* v) {insert(this->end(), v);}
	void pop_back() {erase(--(this->end()));}

	iterator insert(iterator where, T* node) {
		T* newLink = node;
		newLink->link(where.ptr());
		return iterator(newLink);
	}
	iterator erase(iterator where) {
		T* node = where.ptr();
		++where;
		node->unlink();
		return where;
	}
	void erase(T* node) {
		node->unlink();
	}
	void clear() {
		while (!this->empty()) {
			this->pop_back();
		}
	}
};

}

#endif
