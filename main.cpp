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

  
    for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
        for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(x % 256), uint8_t(y % 256), 0}; //red-green color map generation

            // cout<<"Pixel at (" << x << ", " << y << "): "
            //     << "R=" << int(framebuffer[y * VIEWPORT_WIDTH + x].r) << ", " //uint_8 is a char type hence typecasting required
            //     << "G=" << int(framebuffer[y * VIEWPORT_WIDTH + x].g) << ", "
            //     << "B=" << int(framebuffer[y * VIEWPORT_WIDTH + x].b) << endl;
        }
    }

    FILE* f = std::fopen("output.ppm", "wb");
    if (f == nullptr) {
        cout << "Error: Could not open output.ppm for writing." <<endl;
        return 1;
    }

    

      fprintf(f, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
      fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, f);
      fclose(f);
  
    return 0;
}
