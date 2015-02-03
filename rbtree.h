#ifndef SHARK_READ_BLACK_TREE_HPP
#define SHARK_READ_BLACK_TREE_HPP

#include "ptr_bitset.h"
#include <assert.h>

namespace shark
{

class intrusive_multi_rbtree_base {
public:
	enum side {LEFT = 0, RIGHT = 1};
	enum colour {BLACK = 0, RED = 1};

	class node_base {
		enum {BIT_RB, BIT_PS, NUM_BITS};
		node_base* mChildren[2];
		node_base* mNeighbours[2];
		ptr_bits<node_base,NUM_BITS> mParent;
	public:
		node_base() {
			mChildren[LEFT] = this;
			mChildren[RIGHT] = this;
			mNeighbours[LEFT] = this;
			mNeighbours[RIGHT] = this;
			mParent = this;
		}
		node_base* parent() const {return mParent;}
		node_base* child(side s) const {return mChildren[s];}
		node_base* left() const {return mChildren[LEFT];}
		node_base* right() const {return mChildren[RIGHT];}
		node_base* neighbour(side s) const {return mNeighbours[s];}
		node_base* prev() const {return mNeighbours[LEFT];}
		node_base* next() const {return mNeighbours[RIGHT];}
		bool chained() const {return mNeighbours[LEFT] != this;}
		bool head() const {return !!mParent;}
		side parent_side() const {return (side)(mParent.get_bits() >> BIT_PS);}
		void set_parent_side(side s) {mParent.set_bits((mParent.get_bits() & ~(1 << BIT_PS)) | (s << BIT_PS));}
		bool red() const {return mParent.get_bit<BIT_RB>();}
		bool black() const {return !mParent.get_bit<BIT_RB>();}
		colour red_black() const {return (colour)(mParent.get_bits() & (1 << BIT_RB));}
		void make_red() {mParent.set_bit<BIT_RB>();}
		void make_black() {mParent.clear_bit<BIT_RB>();}
		void make_red_black(colour rb) {mParent.set_bits((mParent.get_bits() & ~(1 << BIT_RB)) | (rb << BIT_RB));}
		bool nil() const {return this == mChildren[RIGHT];}
		void rotate(side s) {
			//旋转中心点是this节点
			assert(mParent->mChildren[parent_side()] == this);
			side o = (side)(1 - s);//计算对侧
			side ps = parent_side();
			node_base* top = mChildren[o];
			mChildren[o] = top->mChildren[s];
			mChildren[o]->mParent = this;
			mChildren[o]->set_parent_side(o);
			top->mParent = mParent;   
			top->set_parent_side(ps);
			mParent->mChildren[ps] = top;
			top->mChildren[s] = this;
			mParent = top;
			set_parent_side(s);
		}

		//计算前驱和后继
		node_base* pred_or_succ(side s) const {
			node_base* cur = const_cast<node_base*>(this);
			cur = cur->mNeighbours[s];
			if (!cur->mParent)
				return cur;
			node_base* xessor = cur->mChildren[s];
			if (!xessor->nil()) {
				// find the left(right) subtree's maximum(minimum) node
				side o = (side)(1 - s);
				while (!xessor->mChildren[o]->nil())
					xessor = xessor->mChildren[o];
			} else {
				assert(!cur->nil());
				xessor = cur->mParent;
				while (cur->parent_side() == s) {
					assert(!xessor->nil());
					cur = xessor;
					xessor = xessor->mParent;
				}
			}
			return xessor;
		}
		node_base* pred() const {return pred_or_succ(LEFT);}
		node_base* succ() const {return pred_or_succ(RIGHT);}
		node_base* min_or_max(side s) const {
			node_base* cur = const_cast<node_base*>(this);
			node_base* minmax = cur;
			while (!cur->nil()) {
				minmax = cur;
				cur = cur->mChildren[s];
			}
			return minmax;
		}
		node_base* minimum() const {return min_or_max(LEFT);}
		node_base* maximum() const {return min_or_max(RIGHT);}
	protected:
		template<class T> friend class intrusive_multi_rbtree;
		void attach_to(node_base* parent, side s) {
			mNeighbours[LEFT] = this;
			mNeighbours[RIGHT] = this;
			mChildren[LEFT] = parent->child(s);
			mChildren[RIGHT] = parent->child(s);
			mParent = parent;
			set_parent_side(s);
			parent->mChildren[s] = this;
			make_red();
		}

		// Use child to substitute this itself.
		void substitute_with(node_base* child) {
			side ps = parent_side();
			child->mParent = mParent;
			child->set_parent_side(ps);
			mParent->mChildren[ps] = child;
		}

		// Use this to replace node
		void switch_with(node_base* node) {
			assert(this != node);
			assert(node->head());
			side nps = node->parent_side();
			mChildren[LEFT] = node->child(LEFT);
			mChildren[RIGHT] = node->child(RIGHT);
			mParent = node->mParent;
			set_parent_side(nps);
			node->child(LEFT)->mParent = this;
			node->child(LEFT)->set_parent_side(LEFT);
			node->child(RIGHT)->mParent = this;
			node->child(RIGHT)->set_parent_side(RIGHT);
			node->mParent->mChildren[nps] = this; 
			make_red_black(node->red_black());
		}
		void unlink() {
			mNeighbours[RIGHT]->mNeighbours[LEFT] = mNeighbours[LEFT];
			mNeighbours[LEFT]->mNeighbours[RIGHT] = mNeighbours[RIGHT];
		}

		// two nodes with the same value are linked together by updating their neighbors.
		void link(node_base* node) {
			mNeighbours[LEFT] = node->mNeighbours[LEFT];
			mNeighbours[RIGHT] = node;
			mNeighbours[RIGHT]->mNeighbours[LEFT] = this;
			mNeighbours[LEFT]->mNeighbours[RIGHT] = this;
			mChildren[LEFT] = NULL;
			mChildren[RIGHT] = NULL;
			mParent = NULL;
			set_parent_side(LEFT);
			make_red();
		}
	};

	intrusive_multi_rbtree_base() {}
	intrusive_multi_rbtree_base(const intrusive_multi_rbtree_base&) {}
	bool empty() const {return mHead.child(LEFT) == &mHead;}
	#ifdef DEBUG_MULTI_RBTREE
	void check() const;
	#endif

protected:
	node_base mHead;
	void insert_fixup(node_base* node);
	void erase_fixup(node_base* node);
private:
	#ifdef DEBUG_MULTI_RBTREE
	unsigned check_height(node_base* node) const;
	#endif
};

template<class T> class intrusive_multi_rbtree : public intrusive_multi_rbtree_base {
	intrusive_multi_rbtree(const intrusive_multi_rbtree& rhs);
	intrusive_multi_rbtree& operator=(const intrusive_multi_rbtree& rhs);
public:
	class node : public node_base {
	public:
		const T& data() const {return *(T*)this;}
		T& data() {return *(T*)this;}
		T* parent() const {return (T*)node_base::parent();}
		T* child(side s) const {return (T*)node_base::child(s);}
		T* left() const {return (T*)node_base::left();}
		T* right() const {return (T*)node_base::right();}
		T* neighbour(side s) const {return (T*)node_base::neighbour(s);}
		T* next() const {return (T*)node_base::next();}
		T* prev() const {return (T*)node_base::prev();}
		T* pred_or_succ(side s) const {return (T*)node_base::pred_or_succ(s);}
		T* pred() const {return (T*)node_base::pred();}
		T* succ() const {return (T*)node_base::succ();}
		T* min_or_max(side s) const {return (T*)node_base::min_or_max(s);}
		T* minimum() const {return (T*)node_base::minimum();}
		T* maximum() const {return (T*)node_base::maximum();}
	};

	class const_iterator;
	class iterator {
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
		iterator& operator++() {
			mPtr = mPtr->succ();
			return *this;
		}
		iterator& operator--() {
			mPtr = mPtr->pred();
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
			mPtr = mPtr->succ();
			return *this;
		}
		const_iterator& operator--() {
			mPtr = mPtr->pred();
			return *this;
		}
		bool operator==(const const_iterator& rhs) const {return mPtr == rhs.mPtr;}
		bool operator!=(const const_iterator& rhs) const {return mPtr != rhs.mPtr;}
		const T* ptr() const {return mPtr;}
	};

	template<class K> iterator lower_bound(const K& key) {return iterator(do_lower_bound(key));}
	template<class K> const_iterator lower_bound(const K& key) const {return const_iterator(do_lower_bound(key));}
	template<class K> iterator upper_bound(const K& key) {return iterator(do_upper_bound(key));}
	template<class K> const_iterator upper_bound(const K& key) const {return const_iterator(do_upper_bound(key));}
	template<class K> iterator find(const K& key) { 
		T* found = do_lower_bound(key); 
		if (found == nil_multi_rbnode() || found->data() > key)
			return end();
		return iterator(found);
	}
	template<class K> const_iterator find(const K& key) const {
		T* found = do_lower_bound(key);
		if (found == nil_multi_rbnode() || found->data() > key)
			return end();
		return const_iterator(found);
	}

	const_iterator begin() const {return minimum();}
	iterator begin() {return minimum();}
	const_iterator end() const {return const_iterator(nil_multi_rbnode());}
	iterator end() {return iterator(nil_multi_rbnode());}
	const_iterator root() const {return const_iterator(root_multi_rbnode());}
	iterator root() {return iterator(root_multi_rbnode());}
	const_iterator minimum() const {return const_iterator(root_multi_rbnode()->minimum());}
	iterator minimum() {return iterator(root_multi_rbnode()->minimum());}
	const_iterator maximum() const {return const_iterator(root_multi_rbnode()->maximum());}
	iterator maximum() {return iterator(root_multi_rbnode()->maximum());}

	intrusive_multi_rbtree() : intrusive_multi_rbtree_base() {}
	~intrusive_multi_rbtree() {clear();}
	iterator insert(T* node) {
		this->do_insert(node);
		return iterator(node);
	}
	iterator erase(iterator where) {
		T* node = where.ptr();
		++where;
		this->do_erase(node);
		return where;
	}
	void erase(T* node) {
		this->do_erase(node);
	}
	void clear() {
		for (iterator it = this->begin(); it != this->end();)
			it = this->erase(it);
	}
protected:
	T* nil_multi_rbnode() const {return (T*)&mHead;}
	T* root_multi_rbnode() const {return (T*)mHead.child(LEFT);}
	template<class K> T* do_lower_bound(const K& key) const {
		T* endNode = nil_multi_rbnode();
		T* bestNode = nil_multi_rbnode();
		T* curNode = root_multi_rbnode();
		while (curNode != endNode) {
			if (curNode->data() < key)
				curNode = curNode->child(RIGHT);
			else {
				bestNode = curNode;
				curNode = curNode->child(LEFT);
			}
		}
		return bestNode;
	}
	template<class K> T* do_upper_bound(const K& key) const {
		T* endNode = nil_multi_rbnode();
		T* bestNode = nil_multi_rbnode();
		T* curNode = root_multi_rbnode();
		while (curNode != endNode) {
			if (curNode->data() > key) {
				bestNode = curNode;
				curNode = curNode->child(LEFT);
			} else
				curNode = curNode->child(RIGHT);
		}
		return bestNode;
	}
	void do_insert(T* node) {
		T* endNode = nil_multi_rbnode();
		T* lastNode = nil_multi_rbnode();
		T* curNode = root_multi_rbnode();
		side s = LEFT;
		while (curNode != endNode) {
			lastNode = curNode;
			s = RIGHT;
			if (node->data() < curNode->data()) {
				s = LEFT;
			} else if (!(node->data() > curNode->data())) {
				// the only way to get here is node->data() == curNode->data()
				node->link(curNode);
				return;
			}
			curNode = curNode->child(s);
		}
		node->attach_to(lastNode, s);
		insert_fixup(node);
		#ifdef DEBUG_MULTI_RBTREE
		check();
		#endif
	}
	void do_erase(T* node) {
		if (node->chained()) {
			if (!node->head()) {
				assert(node->child(LEFT) == NULL);
				assert(node->child(RIGHT) == NULL);
				node->unlink();
				return;
			}
			T* repl = node->next();
			assert(repl != nil_multi_rbnode());
			assert(!repl->parent());
			assert(repl->child(LEFT) == NULL);
			assert(repl->child(RIGHT) == NULL);
			repl->switch_with(node);
			node->unlink();
			return;
		}
		T* endNode = nil_multi_rbnode();
		T* repl = node;
		side s = LEFT;
		if (node->child(RIGHT) != endNode) {
			if (node->child(LEFT) != endNode) {
				repl = node->child(RIGHT);
				while (repl->child(LEFT) != endNode)
					repl = repl->child(LEFT);
			}
			s = RIGHT;
		}
		assert(repl->child((side)(1-s)) == endNode);
		bool red = repl->red();
		T* replChild = repl->child(s);
		repl->substitute_with(replChild);
		if (repl != node)
			repl->switch_with(node);
		if (!red) 
			erase_fixup(replChild);
		#ifdef DEBUG_MULTI_RBTREE
		check();
		#endif
	}
};

}

#endif