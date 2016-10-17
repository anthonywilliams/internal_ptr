// Copyright (c) 2016, Just Software Solutions Ltd
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or
// without modification, are permitted provided that the
// following conditions are met:
//
// 1. Redistributions of source code must retain the above
// copyright notice, this list of conditions and the following
// disclaimer.
//
// 2. Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following
// disclaimer in the documentation and/or other materials
// provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of
// its contributors may be used to endorse or promote products
// derived from this software without specific prior written
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#ifndef _JSS_INTERNAL_PTR_HPP
#define _JSS_INTERNAL_PTR_HPP

#include <algorithm>
#include <memory>
#include <type_traits>
#include <vector>

namespace jss {

template <class T> class root_ptr;
template <class T> class internal_ptr;
class internal_base;

template <typename U, typename... Args> root_ptr<U> make_root(Args &&... args);

namespace detail {
struct root_ptr_data_block_base {};

struct internal_ptr_base;

template <class D> struct root_ptr_deleter_base {
    D d;

    root_ptr_deleter_base(D &d_) : d(d_) {}

    template <typename P> void do_delete(P p) {
        d(p);
    }
};

template <> struct root_ptr_deleter_base<void> {
    template <typename T> void do_delete(T *p) {
        delete p;
    }
};

class root_ptr_header_block_base;

template <typename T> struct pointer_set {
    std::vector<T *> vec;

    template <typename V> static auto find_bp_pos(V &v, T *p) {
        return std::lower_bound(v.begin(), v.end(), p, std::less<T *>());
    }

    bool contains(T *p) const {
        auto pos = find_bp_pos(vec, p);
        return (pos != vec.end()) && (*pos == p);
    }

    void add(T *p) {
        auto pos = find_bp_pos(vec, p);
        vec.insert(pos, p);
    }

    bool add_unique(T *p) {
        auto pos = find_bp_pos(vec, p);
        if ((pos != vec.end()) && (*pos == p))
            return false;
        vec.insert(pos, p);
        return true;
    }

    void remove(T *p) {
        auto pos = find_bp_pos(vec, p);
        if ((pos != vec.end()) && (*pos == p))
            vec.erase(pos);
    }

    auto begin() const {
        return vec.begin();
    }
    auto end() const {
        return vec.end();
    }

    auto size() const {
        return vec.size();
    }

    void clear() {
        vec.clear();
    }
};

class root_ptr_header_block_base {
    unsigned owner_count;
    unsigned internal_count;
    pointer_set<root_ptr_header_block_base> back_pointers;
    bool unreachable;
    bool deleted;

    void check_reachable();
    static bool check_reachable(
        pointer_set<root_ptr_header_block_base> &seen_parents,
        std::vector<root_ptr_header_block_base *> &pending,
        pointer_set<root_ptr_header_block_base> *unreachable_nodes = nullptr,
        pointer_set<root_ptr_header_block_base> *owned_nodes = nullptr);
    void mark_unreachable();
    static void cleanup_unreachable_nodes(
        pointer_set<root_ptr_header_block_base> const &seen);
    static void find_unreachable_children(
        pointer_set<root_ptr_header_block_base> &seen,
        std::vector<root_ptr_header_block_base *> &pending);

    virtual void do_delete() = 0;
    virtual internal_base *get_internal_base() = 0;

    bool is_owned() const {
        if (owner_count) {
            return true;
        }
        if (internal_count > back_pointers.size()) {
            return true;
        }
        return false;
    }

    void dec_internal_count() {
        if (!--internal_count) {
            free_self();
        } else if (!unreachable && !owner_count) {
            check_reachable();
        }
    }

    void delete_object() {
        if (!deleted) {
            deleted = true;
            do_delete();
        }
    }

    void free_self() {
        if (unreachable)
            return;
        pointer_set<root_ptr_header_block_base> seen;
        std::vector<root_ptr_header_block_base *> pending;
        seen.add(this);
        find_unreachable_children(seen, pending);
        cleanup_unreachable_nodes(seen);
    }

  public:
    void add_back_pointer(root_ptr_header_block_base *p) {
        back_pointers.add(p);
    }
    void reachable_from(internal_base *p);
    void not_reachable_from(internal_base *p);
    unsigned use_count() {
        return unreachable ? 0 : internal_count;
    }

    virtual ~root_ptr_header_block_base() {}

    root_ptr_header_block_base()
        : owner_count(1), internal_count(1), unreachable(false),
          deleted(false) {}

    bool is_unreachable() {
        return unreachable;
    }

    void remove_owner() {
        --owner_count;
        dec_internal_count();
    }

    bool owner_from_internal() {
        if (unreachable)
            return false;
        ++owner_count;
        ++internal_count;
        return true;
    }

    void set_owner();

    void add_owner() {
        ++owner_count;
        ++internal_count;
    }
};

template <class P> struct root_ptr_header_block : root_ptr_header_block_base {};

template <typename T,
          bool = std::is_polymorphic<typename std::remove_cv<T>::type>::value>
struct get_internal_base_helper {
    static internal_base *get_internal_base(T *p) {
        return dynamic_cast<internal_base *>(p);
    }
};

template <typename T> struct get_internal_base_helper<T, false> {
    static internal_base *get_internal_base(T *) {
        return nullptr;
    }
};

template <typename T> internal_base *get_internal_base_impl(T *p) {
    return get_internal_base_helper<T>::get_internal_base(p);
}

template <class P, class D>
struct root_ptr_header_separate : public root_ptr_header_block<P>,
                                  private root_ptr_deleter_base<D> {
    P const ptr;

    internal_base *get_internal_base() {
        return get_internal_base_impl(ptr);
    }

    root_ptr_header_separate(P p) : ptr(p) {}

    template <typename D2>
    root_ptr_header_separate(P p, D2 &d)
        : root_ptr_deleter_base<D>(d), ptr(p) {}

    void do_delete() {
        root_ptr_deleter_base<D>::do_delete(ptr);
    }
};

template <class T>
struct root_ptr_header_combined : public root_ptr_header_block<T *> {
    typedef
        typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_type;
    storage_type storage;

    T *value() {
        return static_cast<T *>(get_base_ptr());
    }

    void *get_base_ptr() {
        return &storage;
    }

    internal_base *get_internal_base() {
        return get_internal_base_impl(value());
    }

    template <typename... Args> root_ptr_header_combined(Args &&... args) {
        new (get_base_ptr()) T(static_cast<Args &&>(args)...);
    }

    void do_delete() {
        value()->~T();
    }
};

struct internal_ptr_base {
    internal_base *base;
    root_ptr_header_block_base *header;
    internal_ptr_base *next;

    internal_ptr_base(internal_base *base_, root_ptr_header_block_base *header_)
        : base(base_), header(header_), next(nullptr) {}
};
}

template <class T> class root_ptr {
  private:
    T *ptr;
    detail::root_ptr_header_block_base *header;

    template <typename U> friend class root_ptr;
    template <typename U> friend class internal_ptr;
    friend class internal_base;
    friend class detail::root_ptr_header_block_base;

    template <typename U, typename... Args>
    friend root_ptr<U> make_root(Args &&... args);

    root_ptr(detail::root_ptr_header_block_base *header_, T *ptr_)
        : ptr(ptr_), header(header_) {
        if (header && !header->owner_from_internal()) {
            ptr = nullptr;
            header = nullptr;
        }
    }

    root_ptr(detail::root_ptr_header_combined<T> *header_)
        : ptr(header_->value()), header(header_) {
        header->set_owner();
    }

    void clear() {
        header = nullptr;
        ptr = nullptr;
    }

  public:
    typedef T element_type;
    constexpr root_ptr() noexcept : ptr(nullptr), header(nullptr) {}

    template <class Y>
    explicit root_ptr(Y *p) try
        : ptr(p),
          header(new detail::root_ptr_header_separate<Y *, void>(p)) {
        header->set_owner();
    } catch (...) {
        delete p;
    }

    template <class Y, class D>
    root_ptr(Y *p, D d) try
        : ptr(p),
          header(new detail::root_ptr_header_separate<Y *, D>(p, d)) {
        header->set_owner();
    } catch (...) {
        d(p);
    }

    template <class D>
    root_ptr(std::nullptr_t p, D d) try
        : ptr(p),
          header(
              new detail::root_ptr_header_separate<std::nullptr_t, D>(p, d)) {
        header->set_owner();
    } catch (...) {
        d(p);
    }

    template <class Y>
    root_ptr(const root_ptr<Y> &r, T *p) noexcept : ptr(p), header(r.header) {
        if (header)
            header->add_owner();
    }

    root_ptr(const root_ptr &r) noexcept : ptr(r.ptr), header(r.header) {
        if (header)
            header->add_owner();
    }

    template <class Y>
    root_ptr(const root_ptr<Y> &r) noexcept : ptr(r.ptr), header(r.header) {
        if (header)
            header->add_owner();
    }

    root_ptr(root_ptr &&r) noexcept : ptr(r.ptr), header(r.header) {
        r.clear();
    }

    template <class Y>
    root_ptr(root_ptr<Y> &&r) noexcept : ptr(r.ptr), header(r.header) {
        r.clear();
    }

    template <class Y> explicit root_ptr(const internal_ptr<Y> &r);

    template <class Y, class D>
    root_ptr(std::unique_ptr<Y, D> &&r)
        : ptr(r.get()),
          header(
              r.get() ? new detail::root_ptr_header_separate<Y, D>(
                            r.get(), r.get_deleter())
                      : nullptr) {
        header->set_owner();
        r.release();
    }
    constexpr root_ptr(std::nullptr_t) : root_ptr() {}
    ~root_ptr() {
        if (header) {
            header->remove_owner();
        }
    }

    root_ptr &operator=(const root_ptr &r) noexcept {
        if (&r != this) {
            root_ptr temp(r);
            swap(temp);
        }
        return *this;
    }
    template <class Y> root_ptr &operator=(const root_ptr<Y> &r) noexcept {
        root_ptr temp(r);
        swap(temp);
        return *this;
    }

    template <class Y> root_ptr &operator=(const internal_ptr<Y> &r) noexcept {
        root_ptr temp(r);
        swap(temp);
        return *this;
    }

    root_ptr &operator=(root_ptr &&r) noexcept {
        swap(r);
        r.reset();
        return *this;
    }

    template <class Y> root_ptr &operator=(root_ptr<Y> &&r) noexcept {
        root_ptr temp(static_cast<root_ptr<Y> &&>(r));
        swap(temp);
        return *this;
    }

    template <class Y, class D> root_ptr &operator=(std::unique_ptr<Y, D> &&r) {
        root_ptr temp(static_cast<std::unique_ptr<Y> &&>(r));
        swap(temp);
        return *this;
    }
    void swap(root_ptr &r) noexcept {
        std::swap(ptr, r.ptr);
        std::swap(header, r.header);
    }
    void reset() noexcept {
        if (header) {
            header->remove_owner();
        }
        clear();
    }

    template <class Y> void reset(Y *p) {
        root_ptr temp(p);
        swap(temp);
    }

    template <class Y, class D> void reset(Y *p, D d) {
        root_ptr temp(p, d);
        swap(temp);
    }

    T *get() const noexcept {
        return ptr;
    }

    T &operator*() const noexcept {
        return *ptr;
    }

    T *operator->() const noexcept {
        return ptr;
    }

    unsigned use_count() const noexcept {
        return header ? header->use_count() : 0;
    }

    bool unique() const noexcept {
        return use_count() == 1;
    }

    explicit operator bool() const noexcept {
        return ptr;
    }
    template <class U> bool owner_before(root_ptr<U> const &b) const;
    template <class U> bool owner_before(internal_ptr<U> const &b) const;
};

class internal_base {
    detail::root_ptr_header_block_base *self_header = nullptr;
    detail::internal_ptr_base *pointers = nullptr;

    template <typename U> friend class internal_ptr;
    template <typename U> friend class root_ptr;
    friend class detail::root_ptr_header_block_base;

    void set_self_header(detail::root_ptr_header_block_base *header) {
        self_header = header;
        for (auto p = pointers; p; p = p->next) {
            if (p->header)
                p->header->add_back_pointer(header);
        }
    }

    void register_ptr(detail::internal_ptr_base *p) {
        p->next = pointers;
        pointers = p;
    }

    void deregister_ptr(detail::internal_ptr_base *p) {
        detail::internal_ptr_base **entry = &pointers;
        while (*entry && (*entry != p))
            entry = &((*entry)->next);
        if (*entry == p)
            *entry = p->next;
        if (p->header)
            p->header->not_reachable_from(this);
    }

  public:
    virtual ~internal_base() {}
};

namespace detail {
void root_ptr_header_block_base::set_owner() {
    if (auto target = get_internal_base()) {
        target->set_self_header(this);
    }
}
void root_ptr_header_block_base::reachable_from(internal_base *p) {
    ++internal_count;
    if (p->self_header) {
        back_pointers.add(p->self_header);
    }
}

void root_ptr_header_block_base::not_reachable_from(internal_base *p) {
    if (p->self_header) {
        back_pointers.remove(p->self_header);
    }
    dec_internal_count();
}

void root_ptr_header_block_base::check_reachable() {
    if (is_owned()) {
        return;
    }

    pointer_set<root_ptr_header_block_base> seen;
    std::vector<root_ptr_header_block_base *> pending(1, this);
    seen.add(this);

    if (check_reachable(seen, pending))
        return;
    find_unreachable_children(seen, pending);
    cleanup_unreachable_nodes(seen);
}

bool root_ptr_header_block_base::check_reachable(
    pointer_set<root_ptr_header_block_base> &seen_parents,
    std::vector<root_ptr_header_block_base *> &pending,
    pointer_set<root_ptr_header_block_base> *unreachable_nodes,
    pointer_set<root_ptr_header_block_base> *owned_nodes) {
    while (!pending.empty()) {
        auto node = pending.back();
        pending.pop_back();
        if (owned_nodes && owned_nodes->contains(node))
            return true;
        if (unreachable_nodes && unreachable_nodes->contains(node))
            continue;

        if (!node->is_owned()) {
            for (auto bp : node->back_pointers) {
                if ((unreachable_nodes && unreachable_nodes->contains(bp)) ||
                    seen_parents.contains(bp))
                    continue;
                if (owned_nodes && owned_nodes->contains(bp)) {
                    owned_nodes->add_unique(node);
                    return true;
                }
                seen_parents.add(bp);
                pending.push_back(bp);
            }
        } else {
            if (owned_nodes)
                owned_nodes->add_unique(node);
            return true;
        }
    }
    return false;
}

void root_ptr_header_block_base::find_unreachable_children(
    pointer_set<root_ptr_header_block_base> &unreachable_nodes,
    std::vector<root_ptr_header_block_base *> &nodes_to_check_children) {

    pointer_set<root_ptr_header_block_base> owned_nodes;
    nodes_to_check_children.assign(
        unreachable_nodes.begin(), unreachable_nodes.end());
    pointer_set<root_ptr_header_block_base> seen_parents;

    std::vector<root_ptr_header_block_base *> pending;

    while (!nodes_to_check_children.empty()) {
        auto next = nodes_to_check_children.back();
        nodes_to_check_children.pop_back();

        if (auto base = next->get_internal_base()) {
            auto child = base->pointers;
            while (child) {
                auto const child_node = child->header;
                child = child->next;
                if (child_node) {
                    if (unreachable_nodes.contains(child_node) ||
                        owned_nodes.contains(child_node)) {
                        continue;
                    }

                    if (child_node->is_owned()) {
                        owned_nodes.add(child_node);
                        continue;
                    }

                    pending.clear();
                    seen_parents.clear();
                    pending.push_back(child_node);

                    if (!check_reachable(
                            seen_parents, pending, &unreachable_nodes,
                            &owned_nodes)) {
                        for (auto p : seen_parents) {
                            if (unreachable_nodes.add_unique(p))
                                nodes_to_check_children.push_back(p);
                        }
                        if (unreachable_nodes.add_unique(child_node))
                            nodes_to_check_children.push_back(child_node);
                    } else {
                        owned_nodes.add_unique(child_node);
                    }
                }
            }
        }
    }
}
void root_ptr_header_block_base::mark_unreachable() {
    unreachable = true;
    if (auto base = get_internal_base()) {
        auto child = base->pointers;
        while (child) {
            if (auto const child_node = child->header) {
                --child_node->internal_count;
                child_node->back_pointers.remove(this);
                child->header = nullptr;
            }
            child = child->next;
        }
    }
}

void root_ptr_header_block_base::cleanup_unreachable_nodes(
    pointer_set<root_ptr_header_block_base> const &seen) {
    for (auto p : seen) {
        p->mark_unreachable();
    }
    for (auto p : seen) {
        p->delete_object();
    }
    for (auto p : seen) {
        delete p;
    }
}
}

template <typename T> class internal_ptr : detail::internal_ptr_base {
    friend class internal_base;
    template <typename U> friend class internal_ptr;
    template <typename U> friend class root_ptr;

    T *ptr;

    void clear() {
        header = nullptr;
        ptr = nullptr;
    }

  public:
    explicit internal_ptr(internal_base *base_, root_ptr<T> const &p)
        : detail::internal_ptr_base(base_, p.header), ptr(p.ptr) {
        base->register_ptr(this);
        if (header) {
            header->reachable_from(base);
        }
    }

    explicit internal_ptr(internal_base *base_, internal_ptr<T> const &p)
        : detail::internal_ptr_base(base_, p.header), ptr(p.ptr) {
        base->register_ptr(this);
        if (header) {
            header->reachable_from(base);
        }
    }

    explicit internal_ptr(internal_base *base_)
        : detail::internal_ptr_base(base_, nullptr), ptr(nullptr) {
        base->register_ptr(this);
    }

    internal_ptr(internal_ptr const &) = delete;
    internal_ptr(internal_ptr &&other)
        : detail::internal_ptr_base(other.base, other.header), ptr(other.ptr) {
        base->register_ptr(this);
        other.ptr = nullptr;
        other.header = nullptr;
    }

    internal_ptr &operator=(root_ptr<T> const &p) {
        reset();
        ptr = p.ptr;
        header = p.header;
        if (header) {
            header->reachable_from(base);
        }
        return *this;
    }

    internal_ptr &operator=(internal_ptr const &p) {
        if ((p.header != header) || (p.ptr != ptr)) {
            auto temp_header = header;
            header = p.header;
            ptr = p.ptr;
            if (header) {
                header->reachable_from(base);
            }

            if (temp_header)
                temp_header->not_reachable_from(base);
        }

        return *this;
    }

    void reset() {
        if (header) {
            header->not_reachable_from(base);
        }
        clear();
    }

    void swap(internal_ptr &other) {
        jss::root_ptr<T> temp(other);
        jss::root_ptr<T> temp2(*this);
        other = temp2;
        *this = temp;
    }

    T *get() const noexcept {
        return (!header || header->is_unreachable()) ? nullptr : ptr;
    }

    T &operator*() const noexcept {
        return *get();
    }

    T *operator->() const noexcept {
        return get();
    }

    long use_count() const noexcept {
        return header ? header->use_count() : 0;
    }

    bool unique() const noexcept {
        return use_count() == 1;
    }

    explicit operator bool() const noexcept {
        return get();
    }

    ~internal_ptr() {
        base->deregister_ptr(this);
    }
};

template <typename T> class local_ptr {
    T *ptr;

  public:
    local_ptr(root_ptr<T> const &other) noexcept : ptr(other.get()) {}
    local_ptr(root_ptr<T> const &&other) = delete;
    local_ptr(internal_ptr<T> const &other) noexcept : ptr(other.get()) {}
    local_ptr(std::nullptr_t) noexcept : ptr(nullptr) {}
    T *operator->() const noexcept {
        return get();
    }
    T *get() const noexcept {
        return ptr;
    }

    T &operator*() const noexcept {
        return *get();
    }

    explicit operator bool() const noexcept {
        return get();
    }

    void reset() noexcept {
        ptr = nullptr;
    }
};

template <typename T>
inline bool operator==(root_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T> inline bool operator==(root_ptr<T> const &lhs, T *rhs) {
    return lhs.get() == rhs;
}

template <typename T> inline bool operator==(T *lhs, root_ptr<T> const &rhs) {
    return lhs == rhs.get();
}

template <typename T>
inline bool operator!=(root_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T> inline bool operator!=(root_ptr<T> const &lhs, T *rhs) {
    return !(lhs == rhs);
}

template <typename T> inline bool operator!=(T *lhs, root_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator==(root_ptr<T> const &lhs, std::nullptr_t) {
    return !lhs;
}

template <typename T>
inline bool operator==(std::nullptr_t, root_ptr<T> const &rhs) {
    return !rhs;
}

template <typename T>
inline bool operator!=(root_ptr<T> const &lhs, std::nullptr_t) {
    return bool(lhs);
}

template <typename T>
inline bool operator!=(std::nullptr_t, root_ptr<T> const &rhs) {
    return bool(rhs);
}

template <typename T>
inline bool operator==(internal_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator==(internal_ptr<T> const &lhs, T *rhs) {
    return lhs.get() == rhs;
}

template <typename T>
inline bool operator==(T *lhs, internal_ptr<T> const &rhs) {
    return lhs == rhs.get();
}

template <typename T>
inline bool operator!=(internal_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator!=(internal_ptr<T> const &lhs, T *rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator!=(T *lhs, internal_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator==(internal_ptr<T> const &lhs, std::nullptr_t) {
    return !lhs;
}

template <typename T>
inline bool operator==(std::nullptr_t, internal_ptr<T> const &rhs) {
    return !rhs;
}

template <typename T>
inline bool operator!=(internal_ptr<T> const &lhs, std::nullptr_t) {
    return bool(lhs);
}

template <typename T>
inline bool operator!=(std::nullptr_t, internal_ptr<T> const &rhs) {
    return bool(rhs);
}

template <typename T>
inline bool operator==(internal_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator==(root_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator!=(internal_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator!=(root_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator==(local_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T> inline bool operator==(local_ptr<T> const &lhs, T *rhs) {
    return lhs.get() == rhs;
}

template <typename T> inline bool operator==(T *lhs, local_ptr<T> const &rhs) {
    return lhs == rhs.get();
}

template <typename T>
inline bool operator!=(local_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T> inline bool operator!=(local_ptr<T> const &lhs, T *rhs) {
    return !(lhs == rhs);
}

template <typename T> inline bool operator!=(T *lhs, local_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator==(local_ptr<T> const &lhs, std::nullptr_t) {
    return !lhs;
}

template <typename T>
inline bool operator==(std::nullptr_t, local_ptr<T> const &rhs) {
    return !rhs;
}

template <typename T>
inline bool operator!=(local_ptr<T> const &lhs, std::nullptr_t) {
    return bool(lhs);
}

template <typename T>
inline bool operator!=(std::nullptr_t, local_ptr<T> const &rhs) {
    return bool(rhs);
}

template <typename T>
inline bool operator==(local_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator==(root_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator!=(local_ptr<T> const &lhs, root_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator!=(root_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator==(local_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator==(internal_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return lhs.get() == rhs.get();
}

template <typename T>
inline bool operator!=(local_ptr<T> const &lhs, internal_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
inline bool operator!=(internal_ptr<T> const &lhs, local_ptr<T> const &rhs) {
    return !(lhs == rhs);
}

template <typename T>
template <typename Y>
root_ptr<T>::root_ptr(internal_ptr<Y> const &other)
    : ptr(other.ptr), header(other.header) {
    if (header && !header->owner_from_internal()) {
        ptr = nullptr;
        header = nullptr;
    }
}

template <typename Target, typename... Args>
root_ptr<Target> make_root(Args &&... args) {
    return root_ptr<Target>(
        new detail::root_ptr_header_combined<Target>(
            static_cast<Args &&>(args)...));
}
}

#endif
