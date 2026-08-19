#include <iostream>
#include "PoolAllocator.h"
#include <new>

using namespace std;

struct Point {
    int x, y;
};

int main() {
    PoolAllocator pool(sizeof(Point), 2);

    void* mem1 = pool.allocate();

    Point* p1 = new (mem1) Point{10, 20};

    void* mem2 = pool.allocate();

    Point* p2 = new (mem2) Point{2, 3};

    cout << "Point1----x: " << (*p1).x << " y: " << (*p2).y << endl;

    p1->~Point();

    pool.deallocate(p1);

    void* mem3 = pool.allocate();

    if(mem1==mem3) cout << "Mem1 and mem2 are identical" << endl;

    return 0;
}