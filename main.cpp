#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>
using namespace std;

#define PI 3.14159265358979f


static const int VIEWPORT_WIDTH  = 800;
static const int VIEWPORT_HEIGHT = 600;
static const int no_of_pixels =   VIEWPORT_HEIGHT*VIEWPORT_WIDTH; 
static const float FOV = 90;


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

struct Vec3 { float x,y,z; };
struct Vec4 { float x,y,z,w; };
struct Mat4 { float m[4][4]; };


vector<RGB> framebuffer(no_of_pixels);//Each RGB struct is one pixel
vector<float> zbuffer(no_of_pixels, std::numeric_limits<float>::infinity()); //Initialize z-buffer to infinity

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.m[i][j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                result.m[i][j] += a.m[i][k] * b.m[k][j];
            }
        }
    }
    return result;
}

Vec4 transform(const Mat4& mat, const Vec4& vec) {
    Vec4 result;
    result.x = mat.m[0][0] * vec.x + mat.m[0][1] * vec.y + mat.m[0][2] * vec.z + mat.m[0][3] * vec.w;
    result.y = mat.m[1][0] * vec.x + mat.m[1][1] * vec.y + mat.m[1][2] * vec.z + mat.m[1][3] * vec.w;
    result.z = mat.m[2][0] * vec.x + mat.m[2][1] * vec.y + mat.m[2][2] * vec.z + mat.m[2][3] * vec.w;
    result.w = mat.m[3][0] * vec.x + mat.m[3][1] * vec.y + mat.m[3][2] * vec.z + mat.m[3][3] * vec.w;
    return result;
}
void printMatrix(const Mat4& mat) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            cout << mat.m[i][j] << " ";
        }
        cout << endl;
    }
}

void printVector_Vec4(const Vec4& vec) {
    cout << "(" << vec.x << ", " << vec.y << ", " << vec.z << ", " << vec.w << ")" << endl;
}

void printVector_Vec3(const Vec3& vec) {
    cout << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")" << endl;
}

void buildPerspectiveMatrix(Mat4& perspectiveMatrix, float fov, float aspectRatio, float nearPlane, float farPlane) {
    float t = 1.0f / tan(fov / 2.0f);

    for (int i = 0; i<4; ++i) {
        for (int j = 0; j<4; ++j) {
            perspectiveMatrix.m[i][j] = 0.0f;
        }
    }

    perspectiveMatrix.m[0][0] = t / aspectRatio;
    perspectiveMatrix.m[1][1] = t;
    perspectiveMatrix.m[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    perspectiveMatrix.m[2][3] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    perspectiveMatrix.m[3][2] = -1.0f;
}
Vec3 perspectiveTransform(const Vec4& vertex, const Mat4& m) {
    Vec4 clip = transform(m, vertex);
    float inv_w = (std::fabs(clip.w) > 1e-8f) ? 1.0f / clip.w : 0.0f;
    return { clip.x * inv_w, clip.y * inv_w, clip.z * inv_w };
}

int main() {

   Mat4 perspectiveMatrix;
   buildPerspectiveMatrix(perspectiveMatrix, FOV * (PI / 180.0f), float(VIEWPORT_WIDTH) / float(VIEWPORT_HEIGHT), 0.1f, 100.0f);
   Vec4 vertex1 = {1.0f,0.0f,-5.0f,1.0f};
   Vec4 vertex2 = {1.0f,0.0f,-10.0f,1.0f};
   Vec3 vertex1_ndc = perspectiveTransform(vertex1, perspectiveMatrix);
   Vec3 vertex2_ndc = perspectiveTransform(vertex2, perspectiveMatrix);

   printVector_Vec3(vertex1_ndc);
    printVector_Vec3(vertex2_ndc);
  

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
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)}; //grey color map generation
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

    for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
          for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)}; //grey color map generation
          }
      }

      float a1_x = 150.0f, a1_y = 100.0f;
      float b1_x = 620.0f, b1_y = 220.0f;
      float c1_x = 300.0f, c1_y = 500.0f;
      float za1 = 0.3f, zb1 = 0.3f, zc1 = 0.3f; //Depth values for the triangle vertices
      
      float a2_x = 300.0f, a2_y = 150.0f;
      float b2_x = 470.0f, b2_y = 370.0f;
      float c2_x = 150.0f, c2_y = 450.0f;
      float za2 = 0.6f, zb2 = 0.6f, zc2 = 0.6f; //Depth values for the second triangle vertices

      RGB colorA1 = {uint8_t(255), uint8_t(0), uint8_t(0)}; //red
      RGB colorB1 = {uint8_t(255), uint8_t(0), uint8_t(0)}; //red
      RGB colorC1 = {uint8_t(255), uint8_t(0), uint8_t(0)}; //red
      
      RGB colorA2 = {uint8_t(0), uint8_t(0), uint8_t(255)}; //blue
      RGB colorB2 = {uint8_t(0), uint8_t(0), uint8_t(255)}; //blue
      RGB colorC2 = {uint8_t(0), uint8_t(0), uint8_t(255)}; //blue

      int min_x1, min_y1, max_x1, max_y1;
      int min_x2, min_y2, max_x2, max_y2;
      bounding_box(a1_x, a1_y, b1_x, b1_y, c1_x, c1_y, min_x1, min_y1, max_x1, max_y1);
      bounding_box(a2_x, a2_y, b2_x, b2_y, c2_x, c2_y, min_x2, min_y2, max_x2, max_y2);
      float area1 = edge_function(a1_x, a1_y, b1_x, b1_y, c1_x, c1_y); //Area of the parallelogram (x2 area of ABC)
      float area2 = edge_function(a2_x, a2_y, b2_x, b2_y, c2_x, c2_y); //Area of the parallelogram (x2 area of ABC)


      for (int y = min_y1; y <= max_y1; ++y)
      {
        for (int x = min_x1; x <= max_x1; ++x)
        {
          float w0 = edge_function(b1_x, b1_y, c1_x, c1_y, x+0.5f, y+0.5f); //+0.5f to sample at pixel center
          float w1 = edge_function(c1_x, c1_y, a1_x, a1_y, x+0.5f, y+0.5f);
          float w2 = edge_function(a1_x, a1_y, b1_x, b1_y, x+0.5f, y+0.5f);

          bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);

          if (inside) { // cross product order matters
             w0 = w0 / area1; //wo,w1,w2 are also x2 areas of the subtriangles respectively
              w1 = w1 / area1;
              w2 = w2 / area1;
            float z = w0 * za1 + w1 * zb1 + w2 * zc1; //Interpolate the depth value for the pixel (x,y) using barycentric coordinates
            if(z<zbuffer[y * VIEWPORT_WIDTH + x]){ //Depth test
              zbuffer[y * VIEWPORT_WIDTH + x] = z; //Update the z-buffer
             

              //wo,w1,w2 now form the barycentric coordinates of the pixel (x,y) with respect to the triangle ABC   
            
            //Barycentric interpolation of the color of the pixel (x,y) using the barycentric coordinates and the colors of the vertices              
              framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(w0 * colorA1.r + w1 * colorB1.r + w2 * colorC1.r),
                                                      uint8_t(w0 * colorA1.g + w1 * colorB1.g + w2 * colorC1.g),
                                                      uint8_t(w0 * colorA1.b + w1 * colorB1.b + w2 * colorC1   .b)}; //interpolated color for triangle

              }
                                }
        }
      }
      


      for (int y = min_y2; y <= max_y2; ++y)
      {
        for (int x = min_x2; x <= max_x2; ++x)
        {
          float w0 = edge_function(b2_x, b2_y, c2_x, c2_y, x+0.5f, y+0.5f); //+0.5f to sample at pixel center
          float w1 = edge_function(c2_x, c2_y, a2_x, a2_y, x+0.5f, y+0.5f);
          float w2 = edge_function(a2_x, a2_y, b2_x, b2_y, x+0.5f, y+0.5f);

          bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);

          if (inside) { // cross product order matters

            w0 = w0 / area2; //wo,w1,w2 are also x2 areas of the subtriangles respectively
              w1 = w1 / area2;
              w2 = w2 / area2;
            float z = w0 * za2 + w1 * zb2 + w2 * zc2; //Interpolate the depth value for the pixel (x,y) using barycentric coordinates
            if(z<zbuffer[y * VIEWPORT_WIDTH + x]){ //Depth test
              zbuffer[y * VIEWPORT_WIDTH + x] = z; //Update the z-buffer
              

              //wo,w1,w2 now form the barycentric coordinates of the pixel (x,y) with respect to the triangle ABC   
            
            //Barycentric interpolation of the color of the pixel (x,y) using the barycentric coordinates and the colors of the vertices              
              framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(w0 * colorA2.r + w1 * colorB2.r + w2 * colorC2.r),
                                                      uint8_t(w0 * colorA2.g + w1 * colorB2.g + w2 * colorC2.g),
                                                      uint8_t(w0 * colorA2.b + w1 * colorB2.b + w2 * colorC2   .b)}; //interpolated color for triangle

              }
                                }
        }
      }

          FILE* h = std::fopen("Overlappingtriangle.ppm", "wb");
    if (h == nullptr) {
        cout << "Error: Could not open triangle.ppm for writing." <<endl;
        return 1;
    }

    

      fprintf(h, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
      fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, h);
      fclose(h);


      return 0;
}


