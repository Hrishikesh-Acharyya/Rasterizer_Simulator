#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <iostream>
#include <vector>
using namespace std;

static const int VIEWPORT_WIDTH  = 800;
static const int VIEWPORT_HEIGHT = 600;
static const int no_of_pixels =   VIEWPORT_HEIGHT*VIEWPORT_WIDTH; 


struct RGB{

uint8_t r;
uint8_t g;
uint8_t b;
};
static_assert(sizeof(RGB) == 3, "RGB must be tightly packed");

vector<RGB> framebuffer(no_of_pixels);//Each RGB struct is one pixel

int main() {

  
    for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
        for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(x % 256), uint8_t(y % 256), 0};

            cout<<"Pixel at (" << x << ", " << y << "): "
                << "R=" << int(framebuffer[y * VIEWPORT_WIDTH + x].r) << ", "
                << "G=" << int(framebuffer[y * VIEWPORT_WIDTH + x].g) << ", "
                << "B=" << int(framebuffer[y * VIEWPORT_WIDTH + x].b) << endl;
        }
    }

    return 0;
}
