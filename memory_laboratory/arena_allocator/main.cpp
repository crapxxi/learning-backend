#include <iostream>
#include "ArenaAllocator.h"

using namespace std;

struct BadLayout {
    char c1;

    double d;
    char c2;

    int i;
};

struct GoodLayout {
    double d;
    int i;
    char c1;
    char c2;
};

int main() {

    cout << "Bad layout size: " << sizeof(BadLayout) << endl;
    cout << "Good layout size: " << sizeof(GoodLayout) << endl;

    ArenaAllocator arena(16);

    cout << "We gave only 16KB of memory for allocator" << endl;

    void* ptr1 = arena.allocate(sizeof(GoodLayout), alignof(GoodLayout));

    if(ptr1) cout << "Allocator can store GoodLayout" << endl;

    arena.reset();
 
    cout << "allocator reseted" << endl;

    void* ptr2 = arena.allocate(sizeof(BadLayout), alignof(BadLayout));

    if(!ptr2) cout << "Allocator cannot store BadLayout" << endl;


    return 0;
}