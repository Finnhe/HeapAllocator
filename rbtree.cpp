#include "rbtree.h"
using namespace shark;

//////////////////////////////////////////////////////////////////////////
void intrusive_multi_rbtree_base::insert_fixup(node_base* node) {
	node_base* cur = node;
	node_base* p = cur->parent();
	while (p->red()) {
		node_base* pp = p->parent();
		assert(pp != &mHead);
		side s = (side)p->parent_side();
		side o = (side)(1 - s);
		node_base* pp_right = pp->child(o);
		if (pp_right->red()) {
			p->make_black();
			pp_right->make_black();
			pp->make_red();
			cur = pp;
			p = cur->parent();
		} else {
			if (cur == p->child(o)) {
				cur = p;
				cur->rotate(s);
				p = cur->parent();
			}
			p->make_black();
			pp->make_red();
			pp->rotate(o);
		} 
	}
	mHead.child(LEFT)->make_black();
}

void intrusive_multi_rbtree_base::erase_fixup(node_base* node) {
	node_base* cur = node;
	while (!cur->red() && cur != mHead.child(LEFT)) {
		node_base* p = cur->parent();
		side s = (side)cur->parent_side();
		side o = (side)(1 - s);
		node_base* w = p->child(o);
		assert(w != &mHead);
		if (w->red()) {
			assert(w->child(LEFT)->black() && w->child(RIGHT)->black());
			w->make_black();
			p->make_red();
			w = w->child(s);
			p->rotate(s);
		}
		assert(w != &mHead);
		if (w->child(LEFT)->black() && w->child(RIGHT)->black()) { 
			w->make_red();
			cur = p;
		} else {
			if (w->child(o)->black()) {
				w->child(s)->make_black();
				w->make_red();
				node_base* c = w->child(s);
				w->rotate(o);
				w = c;
				assert(w != &mHead);
			}
			assert(w->child(o)->red());
			w->make_red_black(p->red_black());
			p->make_black();
			w->child(o)->make_black();
			p->rotate(s);
			cur = mHead.child(LEFT);
		}
	}
	cur->make_black();
}

#ifdef DEBUG_MULTI_RBTREE
unsigned intrusive_multi_rbtree_base::check_height(node_base* node) const {
	if (node == &mHead)
		return 0;
	if (node->black())
		return check_height(node->child(LEFT)) + check_height(node->child(RIGHT)) + 1;
	assert(node->child(LEFT)->black() && node->child(RIGHT)->black());
	unsigned lh = check_height(node->child(LEFT));
	unsigned rh = check_height(node->child(RIGHT));
	assert(lh == rh);
	return lh;
}

void intrusive_multi_rbtree_base::check() const {
	assert(mHead.black());
	assert(mHead.child(RIGHT) == &mHead);
	assert(mHead.child(LEFT) == &mHead || mHead.child(LEFT)->black());
	check_height(mHead.child(LEFT));
}
#endif

