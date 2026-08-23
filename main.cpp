#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>
using namespace std;

static const int VIEWPORT_WIDTH  = 800;
static const int VIEWPORT_HEIGHT = 600;
static const int no_of_pixels =   VIEWPORT_HEIGHT*VIEWPORT_WIDTH; 

/*
Returns the edge function given the triangle 
*/
float edge_function(float ax, float ay, float bx, float by, float cx, float cy) {
     return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/*
Creates the bounding box, given a triangle
*/

void bounding_box(float ax, float ay, float bx, float by, float cx, float cy,
                  int& min_x, int& min_y, int& max_x, int& max_y) {
    // min/max on the FLOATS first, then floor/ceil, then cast.
    min_x = static_cast<int>(std::floor(std::min({ax, bx, cx})));
    max_x = static_cast<int>(std::ceil (std::max({ax, bx, cx})));
    min_y = static_cast<int>(std::floor(std::min({ay, by, cy})));
    max_y = static_cast<int>(std::ceil (std::max({ay, by, cy})));

    // Clamp to the screen. Without this, an off-screen vertex indexes
    // outside the vector and corrupts memory.
    min_x = std::max(min_x, 0);
    min_y = std::max(min_y, 0);
    max_x = std::min(max_x, VIEWPORT_WIDTH  - 1);
    max_y = std::min(max_y, VIEWPORT_HEIGHT - 1);
}
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
  
      ///Wrote the framebuffer to a PPM file

      for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
          for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)}; //blue color map generation
          }
      }

      float a_x = 150.0f, a_y = 100.0f;
      float b_x = 620.0f, b_y = 220.0f;
      float c_x = 300.0f, c_y = 500.0f;

      //Assign a color to each vertex
      RGB colorA = {uint8_t(255), uint8_t(0), uint8_t(0)}; //red
      RGB colorB = {uint8_t(0), uint8_t(255), uint8_t(0)}; //green
      RGB colorC = {uint8_t(0), uint8_t(0), uint8_t(255)}; //blue

      int min_x, min_y, max_x, max_y;
      
      bounding_box(a_x, a_y, b_x, b_y, c_x, c_y, min_x, min_y, max_x, max_y);
      float area = edge_function(a_x, a_y, b_x, b_y, c_x, c_y); //Area of the parallelogram (x2 area of ABC)

      for (int y = min_y; y <= max_y; ++y)
      {
        for (int x = min_x; x <= max_x; ++x)
        {
          float w0 = edge_function(b_x, b_y, c_x, c_y, x+0.5f, y+0.5f); //+0.5f to sample at pixel center
          float w1 = edge_function(c_x, c_y, a_x, a_y, x+0.5f, y+0.5f);
          float w2 = edge_function(a_x, a_y, b_x, b_y, x+0.5f, y+0.5f);

          bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);

          if (inside) { // cross product order matters
              w0 = w0 / area; //wo,w1,w2 are also x2 areas of the subtriangles respectively
              w1 = w1 / area;
              w2 = w2 / area;

              //wo,w1,w2 now form the barycentric coordinates of the pixel (x,y) with respect to the triangle ABC   

            //Barycentric interpolation of the color of the pixel (x,y) using the barycentric coordinates and the colors of the vertices              
              framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(w0 * colorA.r + w1 * colorB.r + w2 * colorC.r),
                                                      uint8_t(w0 * colorA.g + w1 * colorB.g + w2 * colorC.g),
                                                      uint8_t(w0 * colorA.b + w1 * colorB.b + w2 * colorC.b)}; //interpolated color for triangle
          }
        }
      }

      FILE* g = std::fopen("triangle.ppm", "wb");
    if (g == nullptr) {
        cout << "Error: Could not open triangle.ppm for writing." <<endl;
        return 1;
    }

    

      fprintf(g, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
      fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, g);
      fclose(g);

      return 0;
}


