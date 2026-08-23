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
static const float FOV = 60;


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
struct screenVertex{
float x,y,z;
RGB color;
};

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

void drawTriangle(const screenVertex& A, const screenVertex& B, const screenVertex& C) { 

  int min_x, min_y, max_x, max_y;
  bounding_box(A.x, A.y, B.x, B.y, C.x, C.y, min_x, min_y, max_x, max_y);
  float area = edge_function(A.x, A.y, B.x, B.y, C.x, C.y);

   for (int y = min_y; y <= max_y; ++y)
      {
        for (int x = min_x; x <= max_x; ++x)
        {
          float w0 = edge_function(B.x, B.y, C.x, C.y, x+0.5f, y+0.5f); //+0.5f to sample at pixel center
          float w1 = edge_function(C.x, C.y, A.x, A.y, x+0.5f, y+0.5f);
          float w2 = edge_function(A.x, A.y, B.x, B.y, x+0.5f, y+0.5f);

          bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) ||
              (w0 <= 0 && w1 <= 0 && w2 <= 0);

          if (inside) { // cross product order matters
             w0 = w0 / area; //wo,w1,w2 are also x2 areas of the subtriangles respectively
              w1 = w1 / area;
              w2 = w2 / area;
            float z = w0 * A.z + w1 * B.z + w2 * C.z; //Interpolate the depth value for the pixel (x,y) using barycentric coordinates
            if(z<zbuffer[y * VIEWPORT_WIDTH + x]){ //Depth test
              zbuffer[y * VIEWPORT_WIDTH + x] = z; //Update the z-buffer
             

              //wo,w1,w2 now form the barycentric coordinates of the pixel (x,y) with respect to the triangle ABC   
            
            //Barycentric interpolation of the color of the pixel (x,y) using the barycentric coordinates and the colors of the vertices              
              framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(w0 * A.color.r + w1 * B.color.r + w2 * C.color.r),
                                                      uint8_t(w0 * A.color.g + w1 * B.color.g + w2 * C.color.g),
                                                      uint8_t(w0 * A.color.b + w1 * B.color.b + w2 * C.color.b)}; //interpolated color for triangle

              }
                                }
        }
      }


}

int main() {

  /*Cube vertices*/
  Vec4 cube_verts[8] = {
    {-1,-1,-1,1}, { 1,-1,-1,1}, { 1, 1,-1,1}, {-1, 1,-1,1},  // 0-3: back  z=-1
    {-1,-1, 1,1}, { 1,-1, 1,1}, { 1, 1, 1,1}, {-1, 1, 1,1}   // 4-7: front z=+1
};

/*indices of the vertex that make up the faces of the cube as triangles. The indices represent the vertex number in cube_verts
  Direction of vertices chosen such that it follows outward normal nomenclature for a closed solid 
*/
int cube_indices[36] = 
{

    2,1,0, 0,3,2, //z = -1 face
    4,5,6, 6,7,4, //z = +1 face
    7,6,2, 2,3,7, //y = +1 face
    1,5,4, 4,0,1, //y = -1 face
    1,2,6, 6,5,1, //x = +1 face
    7,3,0, 0,4,7  //x = -1 face

};

RGB cube_colors[8] = {
    {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0},
    {255,0,255}, {0,255,255}, {255,128,0}, {128,0,255}
};
 Mat4 perspectiveMatrix;
buildPerspectiveMatrix(perspectiveMatrix, FOV * (PI / 180.0f), float(VIEWPORT_WIDTH) / float(VIEWPORT_HEIGHT), 0.1f, 100.0f);
Mat4 view = { {{1,0,0,0},{0,1,0,0},{0,0,1,-4},{0,0,0,1}} }; //pushes cube 4 unit down into -z
Mat4 rotate_y = { {{cos(PI/4),0,sin(PI/4),0},{0,1,0,0},{-sin(PI/4),0,cos(PI/4),0},{0,0,0,1}} }; //rotate cube 45 degrees about y-axis
Mat4 rotate_x = { {{1,0,0,0},{0,cos(PI/4),-sin(PI/4),0},{0,sin(PI/4),cos(PI/4),0},{0,0,0,1}} }; //rotate cube 45 degrees about x-axis
Mat4 mvp = multiply(perspectiveMatrix, multiply(view, multiply(rotate_y, rotate_x))); //Model-View-Projection matrix
screenVertex screen_verts[8];

for(int i = 0; i < 8; ++i) {
    Vec3 transformedVertex = perspectiveTransform(cube_verts[i],mvp);
    screen_verts[i].x = (transformedVertex.x * 0.5f + 0.5f) * VIEWPORT_WIDTH;
    screen_verts[i].y = (0.5f - transformedVertex.y * 0.5f) * VIEWPORT_HEIGHT;
    screen_verts[i].z = transformedVertex.z;
    screen_verts[i].color = cube_colors[i];
}

/*Rest of the lines at the end of int main*/



  
   Vec4 vertex1 = {1.0f,0.0f,-5.0f,1.0f};
   Vec4 vertex2 = {1.0f,0.0f,-10.0f,1.0f};
  //  Vec3 vertex1_ndc = perspectiveTransform(vertex1, perspectiveMatrix);
  //  Vec3 vertex2_ndc = perspectiveTransform(vertex2, perspectiveMatrix);

  //  printVector_Vec3(vertex1_ndc);
  //   printVector_Vec3(vertex2_ndc);
  

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

      screenVertex t1a = {150.0f, 100.0f, 0.3f, {255,0,0}};
      screenVertex t1b = {620.0f, 220.0f, 0.3f, {255,0,0}};
      screenVertex t1c = {300.0f, 500.0f, 0.3f, {255,0,0}};
      drawTriangle(t1a, t1b, t1c);
      

      screenVertex t2a = {150.0f, 100.0f, 0.6f, {0,255,0}};
      screenVertex t2b = {470.0f, 370.0f, 0.6f, {0,255,0}};
      screenVertex t2c = {150.0f, 450.0f, 0.6f, {0,255,0}};
      drawTriangle(t2a, t2b, t2c);
    

          FILE* h = std::fopen("Overlappingtriangle.ppm", "wb");
    if (h == nullptr) {
        cout << "Error: Could not open triangle.ppm for writing." <<endl;
        return 1;
    }

    

      fprintf(h, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
      fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, h);
      fclose(h);


        for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
          for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)}; //grey color map generation
          }
      }

      for(int i = 0; i<no_of_pixels; i++)
      {
        zbuffer[i] = std::numeric_limits<float>::infinity(); //Reset z-buffer to infinity
      }

/*Reset framebuffer and z-buffer */
      for(int i = 0; i<12; i++)
      {

        screenVertex A = screen_verts[cube_indices[i*3]];
        screenVertex B = screen_verts[cube_indices[i*3+1]];
        screenVertex C = screen_verts[cube_indices[i*3+2]];
        drawTriangle(A,B,C);
      }

      FILE* k = std::fopen("Cube.ppm", "wb");
    if (k == nullptr) {
        cout << "Error: Could not open Cube.ppm for writing." <<endl;
        return 1;
    }

      fprintf(k, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
      fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, k);
      fclose(k);
      return 0;
}


