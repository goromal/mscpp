#pragma once
#include <assert.h>
#include <memory>
#include <type_traits>

template<typename... T>
struct __place_holder
{
};

template<typename... Types>
struct __type_list
{
};

template<typename... Types>
struct __append_type_list
{
    using type = __type_list<Types...>;
};

template<typename T, typename... Types>
struct __append_type_list<T, __type_list<Types...>>
{
    using type = __type_list<T, Types...>;
};

template<typename T, typename N, typename... Types>
struct __recurse_to_type
{
    using type = typename __append_type_list<N, typename __recurse_to_type<T, Types...>::type>::type;
};

template<typename T, typename... Types>
struct __recurse_to_type<T, T, Types...>
{
    using type = __type_list<>;
};

template<typename...>
struct __one_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of<T, F, Types...>
{
    static constexpr bool value = std::is_same<T, F>::value || __one_of<T, Types...>::value;
};

template<typename...>
struct __one_of_or_derived_from
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of_or_derived_from<T, F, Types...>
{
    static constexpr bool value =
        std::is_same<T, F>::value || std::is_base_of<F, T>::value || __one_of_or_derived_from<T, Types...>::value;
};

template<typename...>
struct __one_of_or_parent_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of_or_parent_of<T, F, Types...>
{
    static constexpr bool value =
        std::is_same<T, F>::value || std::is_base_of<T, F>::value || __one_of_or_parent_of<T, Types...>::value;
};

template<typename...>
struct __derived_from
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __derived_from<T, F, Types...>
{
    static constexpr bool value = std::is_base_of<F, T>::value || __derived_from<T, Types...>::value;
};

template<typename...>
struct __parent_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __parent_of<T, F, Types...>
{
    static constexpr bool value = std::is_base_of<T, F>::value || __parent_of<T, Types...>::value;
};

template<typename...>
struct __index_of;

template<typename T, typename... R>
struct __index_of<T, T, R...> : std::integral_constant<size_t, 0>
{
};

template<typename T, typename F, typename... R>
struct __index_of<T, F, R...> : std::integral_constant<size_t, 1 + __index_of<T, R...>::value>
{
};

struct __handle_later
{
};

template<typename T>
class __instance_wrapper
{
public:
    __instance_wrapper() {}

private:
    std::shared_ptr<T> mInstance;

protected:
    const std::shared_ptr<T>& getInstance() const
    {
        return mInstance;
    }

    void setInstance(std::shared_ptr<T> instance)
    {
        if (mInstance || !instance)
        {
            return;
        }
        mInstance = std::move(instance);
    }
};

#define container_of(ptr, type, member) ((type*)((char*)(ptr) - (unsigned long)(&((type*)0)->member)))

typedef struct list_ptr_s
{
    struct list_ptr_s* next;
    struct list_ptr_s* prev;
} list_ptr_t;

typedef struct list_s
{
    struct list_ptr_s head;
    size_t            size;
} list_t;

static inline void list_init(struct list_s* list)
{
    list->size      = 0;
    list->head.prev = list->head.next = &list->head;
}

static inline void __list_insert(struct list_ptr_s* new, struct list_ptr_s* prev, struct list_ptr_s* next)
{
    next->prev = new;
    new->next  = next;
    new->prev  = prev;
    prev->next = new;
}

static inline void list_push_back(struct list_s* list, struct list_ptr_s* elem)
{
    assert(list);
    assert(elem);

    __list_insert(elem, list->head.prev, &list->head);
    list->size++;
}

static inline void list_push_front(struct list_s* list, struct list_ptr_s* elem)
{
    assert(list);
    assert(elem);

    __list_insert(elem, &list->head, list->head.next);
    list->size++;
}

static inline void list_remove(struct list_s* list, struct list_ptr_s* elem)
{
    assert(list);
    assert(elem);

    struct list_ptr_s* prev = elem->prev;
    struct list_ptr_s* next = elem->next;

    /* check if this item was never removed/wasn't initialized */
    if (prev == NULL || next == NULL)
    {
        assert(next == NULL);
        assert(prev == NULL);
        return;
    }

    prev->next = next;
    next->prev = prev;

    elem->next = elem->prev = NULL;

    list->size--;
}

static inline struct list_ptr_s* list_peek_front(struct list_s* list)
{
    assert(list);

    return &list->head == list->head.next ? NULL : list->head.next;
}

static inline struct list_ptr_s* list_pop_front(struct list_s* list)
{
    assert(list);

    struct list_ptr_s* elem = list_peek_front(list);
    if (elem)
        list_remove(list, elem);
    return elem;
}

#define list_peek_front_container(list, struct_type, member)                                                           \
    ({                                                                                                                 \
        list_ptr_t* _ptr = list_peek_front(list);                                                                      \
        _ptr == NULL ? NULL : container_of(_ptr, struct_type, member);                                                 \
    })

#define list_pop_front_container(list, struct_type, member)                                                            \
    ({                                                                                                                 \
        list_ptr_t* _ptr = list_pop_front(list);                                                                       \
        _ptr == NULL ? NULL : container_of(_ptr, struct_type, member);                                                 \
    })

#define list_for_each(pos, n, list, member)                                                                            \
    for (pos = container_of((list)->head.next, typeof(*pos), member),                                                  \
        n    = container_of(pos->member.next, typeof(*pos), member);                                                   \
         &pos->member != &(list)->head;                                                                                \
         pos = n, n = container_of(n->member.next, typeof(*n), member))
