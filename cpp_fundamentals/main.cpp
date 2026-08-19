#include <iostream>
#include "FixedVector.h"
using namespace std;

int main() {
    const size_t capacity = 10;

    FixedVector<int, capacity> fx;

    for(int i = 1; i <= 10; i++) {
        fx.push_back(i);
    }

    for(size_t i = 0; i < capacity; i++) {
        cout << fx[i] << " ";
    }

    return 0;
}