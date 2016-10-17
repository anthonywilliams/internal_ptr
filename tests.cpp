#include <assert.h>
#include <iostream>
#include "internal_ptr.hpp"
#include <vector>

struct Counted{
    Counted(){
        ++instances;
    }
    Counted(Counted const&){
        ++instances;
    }
    ~Counted(){
        --instances;
    }

    static unsigned instances;
};

unsigned Counted::instances=0;

void root_ptr_destroys_object_when_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        jss::root_ptr<Counted> p(new Counted);
        assert(Counted::instances==1);
    }
    assert(Counted::instances==0);
}

void internal_ptr_destroys_object_when_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Owner:jss::internal_base{
            jss::internal_ptr<Counted> p;

            Owner():
                p(this,jss::root_ptr<Counted>(new Counted)){}
        };

        Owner x;
        assert(Counted::instances==1);
    }
    assert(Counted::instances==0);
}

void cycle_destroyed_when_owner_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Node:jss::internal_base{
            jss::internal_ptr<Node> next;
            Counted x;

            Node():
                next(this){}
        };

        jss::root_ptr<Node> first(new Node);
        {
            jss::root_ptr<Node> second(new Node);
            first->next=second;
            second->next=first;
        }
        assert(Counted::instances==2);
    }
    assert(Counted::instances==0);
}

void three_node_cycle_destroyed_when_last_owner_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Node:jss::internal_base{
            jss::internal_ptr<Node> next;
            Counted x;

            Node():
                next(this){}
        };

        jss::root_ptr<Node> first(new Node);
        {
            jss::root_ptr<Node> second(new Node);
            jss::root_ptr<Node> third(new Node);
            first->next=second;
            second->next=third;
            third->next=first;
        }
        assert(Counted::instances==3);
    }
    assert(Counted::instances==0);
}

void partial_structure_dropped_when_owner_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Node:jss::internal_base{
            jss::internal_ptr<Node> next;
            Counted x;

            Node():
                next(this){}
        };

        jss::root_ptr<Node> first(new Node);
        {
            jss::root_ptr<Node> second(new Node);
            jss::root_ptr<Node> third(new Node);
            first->next=second;
            second->next=first;
            third->next=first;
        }
        assert(Counted::instances==2);
    }
    assert(Counted::instances==0);
}

void partial_structure_with_backref_dropped_when_owner_destroyed(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Node:jss::internal_base{
            jss::internal_ptr<Node> next;
            Counted x;

            Node():
                next(this){}
        };

        jss::root_ptr<Node> first(new Node);
        {
            jss::root_ptr<Node> second(new Node);
            jss::root_ptr<Node> third(new Node);
            second->next=first;
            third->next=second;
        }
        assert(Counted::instances==1);
        assert(first->next.get()==nullptr);
    }
    assert(Counted::instances==0);
}

void clearing_internal_pointer_to_cycle_destroys_cycle(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        struct Node:jss::internal_base{
            jss::internal_ptr<Node> next;
            Counted x;

            Node():
                next(this){}
        };

        jss::root_ptr<Node> first(new Node);
        {
            jss::root_ptr<Node> second(new Node);
            jss::root_ptr<Node> third(new Node);
            first->next=second;
            second->next=third;
            third->next=second;
        }
        assert(Counted::instances==3);
        first->next.reset();
        assert(Counted::instances==1);
    }
    assert(Counted::instances==0);
}

void use_count(){
    std::cout<<__FUNCTION__<<std::endl;
    jss::root_ptr<Counted> first(new Counted);
    assert(first.use_count()==1);
    jss::root_ptr<Counted> second(first);
    assert(first.use_count()==2);
    assert(second.use_count()==2);
    first.reset();
    assert(second.use_count()==1);
    assert(first.use_count()==0);
    struct X:jss::internal_base{
        jss::internal_ptr<Counted> p;
        
        X():
            p(this){}

        X(X const& other):
            p(this,other.p){}
    };

    X x;
    x.p=second;
    X x2(x);
    assert(second.use_count()==3);
    assert(x.p.use_count()==3);
    assert(x2.p.use_count()==3);
    jss::root_ptr<Counted> third(x.p);
    assert(second.use_count()==4);
    assert(x.p.use_count()==4);
    assert(x2.p.use_count()==4);
    assert(third.use_count()==4);
    second.reset();
    third.reset();
    assert(x.p.use_count()==2);
    assert(x2.p.use_count()==2);
    x.p.reset();
    assert(x2.p.use_count()==1);
    assert(x2.p.get()!=nullptr);
    first=x2.p;
    assert(x2.p.use_count()==2);
    assert(first.use_count()==2);
    x.p=x2.p;
    assert(x2.p.use_count()==3);
    assert(x.p.use_count()==3);
    assert(first.use_count()==3);
}

struct Base{
    virtual ~Base()
    {}
};

class Derived:
    public Base{
    Counted x;
};

void derived_class_destroyed_correctly(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        jss::root_ptr<Base> p(new Derived);
        assert(Counted::instances==1);
    }
    assert(Counted::instances==0);
}

class Middle1:
    public Base{};

class Middle2:
    public Base{};

class Duplicate:
    public Middle1,public Middle2{
    Counted x;
};

void duplicate_inheritance_handled(){
    std::cout<<__FUNCTION__<<std::endl;
    {
        jss::root_ptr<Duplicate> p(new Duplicate);
        jss::root_ptr<Middle1> m1(p);
        jss::root_ptr<Middle2> m2(p);
        jss::root_ptr<Base> b1(m1);
        jss::root_ptr<Base> b2(m2);

        assert(Counted::instances==1);
        assert(b1!=b2);
        assert(dynamic_cast<Derived*>(b1.get())==dynamic_cast<Derived*>(b2.get()));
    }
    assert(Counted::instances==0);
}

void deref_and_comparison(){
    std::cout<<__FUNCTION__<<std::endl;

    struct X:jss::internal_base{
        jss::internal_ptr<Counted> p;
        
        X():
            p(this){}

        X(X const& other):
            p(this,other.p){}
    };

    X x;

    assert(x.p.get()==nullptr);
    assert(x.p.operator->()==nullptr);
    assert(x.p==nullptr);
    assert(nullptr==x.p);

    Counted* p=new Counted;
    jss::root_ptr<Counted> op;
    assert(op.get()==nullptr);
    assert(op.operator->()==nullptr);
    assert(op==nullptr);
    assert(nullptr==op);
    op=jss::root_ptr<Counted>(p);
    x.p=op;
    assert(x.p.get()==p);
    assert(x.p.operator->()==p);
    assert(x.p==p);
    assert(p==x.p);
    assert(!(x.p!=p));
    assert(!(p!=x.p));
    assert(&*x.p==p);
    assert(x.p!=nullptr);
    assert(nullptr!=x.p);
    assert(op.get()==p);
    assert(op.operator->()==p);
    assert(op==p);
    assert(p==op);
    assert(!(op!=p));
    assert(!(p!=op));
    assert(&*op==p);
    assert(op==x.p);
    assert(x.p==op);
    assert(!(op!=x.p));
    assert(!(x.p!=op));
    assert(op!=nullptr);
    assert(nullptr!=op);
}

void make_root_func(){
    std::cout<<__FUNCTION__<<std::endl;

    jss::root_ptr<Counted> p=jss::make_root<Counted>();
    assert(Counted::instances==1);
    p.reset();
    assert(Counted::instances==0);
}

void linked_list() {
    std::cout << __FUNCTION__ << std::endl;

    using data_type = int;

    class List {
        struct Node : jss::internal_base {
            jss::internal_ptr<Node> next;
            data_type data;

            Node(data_type data_) : next(this), data(data_) {}
        };

        jss::root_ptr<Node> head;

      public:
        void push_front(data_type new_data) {
            auto new_node = jss::make_root<Node>(new_data);
            new_node->next = head;
            head = new_node;
        }

        data_type pop_front() {
            auto old_head = head;
            if (!old_head)
                throw std::runtime_error("Empty list");
            head = old_head->next;
            return old_head->data;
        }

        void clear() {
            head.reset();
        }
    };

    List x;
    for(unsigned i=0;i<100;++i)
        x.push_front(i);

    for(unsigned i=100;i;--i){
        auto v=x.pop_front();
        assert(v==(i-1));
    }

    for(unsigned i=0;i<100;++i)
        x.push_front(i);

    x.clear();
}

void swapping(){
    std::cout<<__FUNCTION__<<std::endl;

    struct X:jss::internal_base{
        jss::internal_ptr<Counted> p;
        
        X():
            p(this){}

        X(X const& other):
            p(this,other.p){}
    };

    auto p1=jss::make_root<Counted>();

    auto p2=p1;
    jss::root_ptr<Counted> p3;
    p3.swap(p2);
    assert(!p2);
    assert(p3==p1);
    assert(p1.use_count()==2);
    p2=jss::make_root<Counted>();
    assert(p2!=p3);
    assert(p2!=p1);
    p2.swap(p3);
    assert(p2!=p3);
    assert(p2==p1);
    assert(p3.use_count()==1);
    std::swap(p2,p3);
    assert(p2!=p3);
    assert(p3==p1);
    assert(p3.use_count()==2);
    
    X x,x2;
    
    x.p=p1;
    assert(x.p.use_count()==3);
    x.p.swap(x2.p);
    assert(!x.p);
    assert(x2.p==p1);
    assert(x2.p.use_count()==3);
    assert(x.p.use_count()==0);
    
}

void construct_cycle_before_pointers(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        jss::internal_ptr<X> p1,p2;
        Counted data;

        X():
            p1(this),p2(this){}
    };

    X* x=new X;
    {
        auto x2 = jss::make_root<X>();
        auto x3 = jss::make_root<X>();
        auto x4 = jss::make_root<X>();
        auto x5 = jss::make_root<X>();
        auto x6 = jss::make_root<X>();

        x->p1 = x2;
        x->p2 = x3;
        x2->p1 = x3;
        x2->p2 = x4;
        x3->p1 = x2;
        x3->p2 = x4;
        x4->p1 = x5;
        x5->p1 = x6;
    }

    jss::root_ptr<X> xp(x);
    assert(Counted::instances==6);

    xp->p1->p2->p2=xp;
    xp.reset();
    assert(Counted::instances==0);
}

void assign_within_data_structure(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        jss::internal_ptr<X> p;
        Counted data;

        X():
            p(this){}
    };

    auto root=jss::make_root<X>();
    root->p=jss::make_root<X>();
    root->p->p=jss::make_root<X>();
    root->p->p->p=jss::make_root<X>();
    root->p->p->p->p=jss::make_root<X>();
    assert(Counted::instances==5);
    root->p->p=root->p->p->p->p;
    assert(Counted::instances==3);
}

void two_pointers_within_same_object_to_same_other_object(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        jss::internal_ptr<X> p1,p2;
        Counted data;

        X():
            p1(this),p2(this){}
    };

    auto x=jss::make_root<X>();
    x->p1=jss::make_root<X>();
    x->p2=x->p1;
    assert(Counted::instances==2);
    assert(x->p2.use_count()==2);
    x->p1.reset();
    assert(Counted::instances==2);
    assert(x->p2.get()!=nullptr);
    assert(x->p2.use_count()==1);
    x->p2.reset();
    assert(Counted::instances==1);

    x->p1=jss::make_root<X>();
    x->p1->p1=jss::make_root<X>();
    x->p1->p2=x->p1->p1;
    assert(Counted::instances==3);
    x->p1->p1.reset();
    assert(Counted::instances==3);
    x->p1->p2->p1=x->p1;
    assert(Counted::instances==3);
    x->p1.reset();
    assert(Counted::instances==1);
}

void can_convert_internal_ptr_to_local_ptr(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        jss::internal_ptr<X> p;
        Counted data;

        X():
            p(this){}
    };

    X x;
    x.p=jss::make_root<X>();

    jss::local_ptr<X> lp=x.p;
    assert(x.p.use_count()==1);
    assert(lp);
    assert(!!lp);
    assert(lp==x.p);
    assert(lp==x.p.get());
    assert(x.p.get()==lp);
    assert(x.p==lp);
    assert(!(lp!=x.p));
    assert(!(lp!=x.p.get()));
    assert(!(x.p.get()!=lp));
    assert(!(x.p!=lp));
    assert(lp.get()==x.p.get());
    assert(&*lp==x.p.get());
    assert(lp.operator->()==x.p.get());
    lp.reset();
    assert(x.p.use_count()==1);
    assert(!(bool)lp);
    assert(!lp);
    assert(lp!=x.p);
    assert(lp!=x.p.get());
    assert(x.p!=lp);
    assert(x.p.get()!=lp);
    assert(!(lp==x.p));
    assert(!(lp==x.p.get()));
    assert(!(x.p.get()==lp));
    assert(!(x.p==lp));
    assert(lp.get()==nullptr);
    assert(!lp.operator->());
    lp=x.p;
    assert(x.p.use_count()==1);
    assert(lp);
    assert(!!lp);
    assert(lp==x.p);
    assert(lp==x.p.get());
    assert(x.p.get()==lp);
    assert(x.p==lp);
    assert(!(lp!=x.p));
    assert(!(lp!=x.p.get()));
    assert(!(x.p.get()!=lp));
    assert(!(x.p!=lp));
    assert(lp.get()==x.p.get());
    assert(&*lp==x.p.get());
    assert(lp.operator->()==x.p.get());
}

void can_convert_root_ptr_to_local_ptr(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        Counted data;
    };

    auto x=jss::make_root<X>();

    jss::local_ptr<X> lp=x;
    assert(x.use_count()==1);
    assert(lp);
    assert(!!lp);
    assert(lp==x);
    assert(lp==x.get());
    assert(x.get()==lp);
    assert(x==lp);
    assert(!(lp!=x));
    assert(!(lp!=x.get()));
    assert(!(x.get()!=lp));
    assert(!(x!=lp));
    assert(lp.get()==x.get());
    assert(&*lp==x.get());
    assert(lp.operator->()==x.get());
    lp.reset();
    assert(x.use_count()==1);
    assert(!(bool)lp);
    assert(!lp);
    assert(lp!=x);
    assert(lp!=x.get());
    assert(x!=lp);
    assert(x.get()!=lp);
    assert(!(lp==x));
    assert(!(lp==x.get()));
    assert(!(x.get()==lp));
    assert(!(x==lp));
    assert(lp.get()==nullptr);
    assert(!lp.operator->());
    lp=x;
    assert(x.use_count()==1);
    assert(lp);
    assert(!!lp);
    assert(lp==x);
    assert(lp==x.get());
    assert(x.get()==lp);
    assert(x==lp);
    assert(!(lp!=x));
    assert(!(lp!=x.get()));
    assert(!(x.get()!=lp));
    assert(!(x!=lp));
    assert(lp.get()==x.get());
    assert(&*lp==x.get());
    assert(lp.operator->()==x.get());
}

void vector_of_internal_ptr(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        std::vector<jss::internal_ptr<X>> pointers;
        Counted data;

        void add(jss::root_ptr<X> p){
            pointers.emplace_back(this,p);
        }

        void drop_front(){
            if(!pointers.empty()){
                pointers.erase(pointers.begin());
            }
        }
    };

    auto x=jss::make_root<X>();

    x->add(jss::make_root<X>());
    x->add(jss::make_root<X>());
    x->add(jss::make_root<X>());
    
}

void pointers_are_null_in_destructor(){
    std::cout<<__FUNCTION__<<std::endl;
    struct X:jss::internal_base{
        jss::internal_ptr<X> p;
        Counted data;

        X():
            p(this){}

        ~X(){
            assert(!p);
        }
    };

    X x;
    x.p=jss::make_root<X>();
    x.p->p=jss::make_root<X>();
    x.p.reset();
}

int main(){
    root_ptr_destroys_object_when_destroyed();
    internal_ptr_destroys_object_when_destroyed();
    cycle_destroyed_when_owner_destroyed();
    three_node_cycle_destroyed_when_last_owner_destroyed();
    partial_structure_dropped_when_owner_destroyed();
    partial_structure_with_backref_dropped_when_owner_destroyed();
    clearing_internal_pointer_to_cycle_destroys_cycle();
    use_count();
    derived_class_destroyed_correctly();
    duplicate_inheritance_handled();
    deref_and_comparison();
    make_root_func();
    linked_list();
    swapping();
    construct_cycle_before_pointers();
    assign_within_data_structure();
    two_pointers_within_same_object_to_same_other_object();
    can_convert_internal_ptr_to_local_ptr();
    can_convert_root_ptr_to_local_ptr();
    vector_of_internal_ptr();
    pointers_are_null_in_destructor();
}
