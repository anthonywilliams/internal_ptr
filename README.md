# `internal_ptr<T>` --- reference counting for data structures with cycles

In his CppCon 2016 keynote, Herb Sutter described the ideal of code being "leak free by construction", and then went on to detail the uses of `std::unique_ptr<T>` and `std::shared_ptr<T>` to ensure this. One of the key shortcomings of the standard smart pointers is that they don't work for data structures with cycles: if **A** holds a `std::shared_ptr<T>` to **B** and **B** holds a `std::shared_ptr<T>` to **A** then they will never be freed, even if there are no other references to **A** or **B**.

Herb proposed his [***gcpp*** library](https://github.com/hsutter/gcpp) as an experimental solution to this problem. By allocating your objects from the `deferred_heap`, and using `deferred_ptr<T>` to point to them, then you are guaranteed that they will be properly destroyed by calling their destructors, even if that destruction is "deferred" until later.

This library is an alternative solution to the same problem. Rather than deferring collection of unreachable objects, collection is done immediately, just as with `std::shared_ptr<T>`. The key difference here is that if the only outstanding references to objects are those within a cycle then the whole set of objects is destroyed, rather than the internal references keeping the whole data structure alive.

## Usage

The library provides two smart pointer class templates: `root_ptr<T>` and `internal_ptr<T>`. `root_ptr<T>` is directly equivalent to `std::shared_ptr<T>`: it is a reference-counted smart pointer. For many uses, you could use `root_ptr<T>` as a direct replacement for `std::shared_ptr<T>` and your code will have identical behaviour. `root_ptr<T>` is intended to represent an external **owner** for your data structure. For a tree it could hold the pointer to the root node. For a general graph it could be used to hold each of the external nodes of the graph.

The difference comes with `internal_ptr<T>`. This holds a pointer to another object **within** the data structure. It is an **internal** pointer to another part of the same larger data structure. It is also reference counted, so if there are no `root_ptr<T>` or `internal_ptr<T>` objects pointing to a given object then it is immediately destroyed, but even one `internal_ptr<T>` can be enough to keep an object alive as part of a larger data structure.

The "magic" is that if an object is only pointed to by `internal_ptr<T>` pointers, then it is only kept alive as long as the whole data structure has an accessible root in the form of an `root_ptr<T>` or an object with an `internal_ptr<T>` that is not pointed to by either an `root_ptr<T>` or an `internal_ptr<T>`.

This is made possible by the internal nodes deriving from `internal_base`, much like `std::enable_shared_from_this<T>` enables additional functionality when using `std::shared_ptr<T>`. This base class is then passed to the `internal_ptr<T>` constructor, to identify which object the `internal_ptr<T>` belongs to.

For example, a singly-linked list could be implemented like so:

~~~cpp
class List{
    struct Node: jss::internal_base{
        jss::internal_ptr<Node> next;
        data_type data;
        
        Node(data_type data_):next(this),data(data_){}
    };
    
    jss::root_ptr<Node> head;
public:
    void push_front(data_type new_data){
        auto new_node=jss::make_owner<Node>(new_data);
        new_node->next=head;
        head=new_node;
    }
    
    data_type pop_front(){
        auto old_head=head;
        if(!old_head)
            throw std::runtime_error("Empty list");
        head=old_head->next;
        return old_head->data;
    }
    
    void clear(){
        head.reset();
    }
};
~~~

This actually has an advantage over using `std::shared_ptr<Node>` for the links in the list, due to another feature of the library. When a group of interlinked nodes becomes unreachable, then firstly each node is marked as unreachable, thus making any `internal_ptr<T>`s that point to them become equal to `nullptr`. Then all the unreachable nodes are destroyed in turn. All this is done with iteration rather than recursion, and thus avoids the deep recursive destructor chaining that can occur when using `std::shared_ptr<T>`.

`local_ptr<T>` completes the set: you can use a `local_ptr<T>` when traversing a data structure that uses `internal_ptr<T>`. `local_ptr<T>` does not hold a reference, and is not in any way involved in the lifetime tracking of the nodes. It is intended to be used when you need to keep a local pointer to a node, but you're not updating the data structure, and don't need that pointer to keep the node alive. e.g.

~~~cpp
class List{
// as above
public:
    void for_each(std::function<void(data_type&)> f){
        jss::local_ptr<Node> node=head;
        while(node){
            f(node->data);
            node=node->next;
        }
    }
}
~~~

**Warning:** `root_ptr<T>` and `internal_ptr<T>` are not safe for use if multiple threads may be accessing any of the nodes in the data structure while **any** thread is modifying any part of it. The data structure **as a whole** must be protected with external synchronization in a multi-threaded context.

## How it works

The key to this system is twofold. Firstly the nodes in the data structure derive from `internal_base`, which allows the library to store a back-pointer to the smart pointer control block in the node itself, as long as the head of the list of `internal_ptr<T>`s that belong to that node. Secondly, the control blocks each hold a list of back-pointers to the control blocks of the objects that point to them via `internal_ptr<T>`. When a reference to a node is dropped (either from an `root_ptr<T>` or an `internal_ptr<T>`), if that node has no remaining `root_ptr<T>`s that point to it, the back-pointers are checked. The chain of back-pointers is followed until either a node is found that has an `root_ptr<T>` that points to it, or a node is found that does not have a control block (e.g. because it is allocated on the stack, or owned by `std::shared_ptr<T>`). If either is found, then the data structure is **reachable**, and thus kept alive. If neither is found once all the back-pointers have been followed, then the set of nodes that were checked is unreachable, and thus can be destroyed. Each of the unreachable nodes is then marked as such, which causes `internal_ptr<T>`s that refer to them to become `nullptr`, and thus prevents resurrection of the nodes. Finally, the unreachable nodes are all destroyed in an unspecified order. The scan and destroy is done with iteration rather than recursion to avoid the potential for deep recursive nesting on large interconnected graphs of nodes.

The downside is that the time taken to drop a reference to a node is dependent on the number of nodes in the data structure, in particular the number of nodes that have to be examined in order to find an owned node.

Note: only dropping a reference to a node (destroying a pointer, or reassigning a pointer) incurs this cost. Constructing the data structure is still relatively low overhead.

## Copyright and License

The code is copyright (c) 2016 Just Sofware Solutions Ltd, and is released under the BSD license. See the license text at the top of `internal_ptr.hpp`.
