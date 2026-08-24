#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
using namespace std;

#define PI 3.14159265358979f


static const int VIEWPORT_WIDTH  = 1920;
static const int VIEWPORT_HEIGHT = 1080;
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

void buildRotationMatrix_y(Mat4& rotationMatrix, float angle) {
    float c = cos(angle);
    float s = sin(angle);

    rotationMatrix.m[0][0] = c;  rotationMatrix.m[0][1] = 0;  rotationMatrix.m[0][2] = s;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = 0;  rotationMatrix.m[1][1] = 1;  rotationMatrix.m[1][2] = 0;  rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = -s; rotationMatrix.m[2][1] = 0;  rotationMatrix.m[2][2] = c;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

void buildRotationMatrix_x(Mat4& rotationMatrix, float angle) {
    float c = cos(angle);
    float s = sin(angle);

    rotationMatrix.m[0][0] = 1;  rotationMatrix.m[0][1] = 0;  rotationMatrix.m[0][2] = 0;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = 0;  rotationMatrix.m[1][1] = c;  rotationMatrix.m[1][2] = -s; rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = 0;  rotationMatrix.m[2][1] = s;  rotationMatrix.m[2][2] = c;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

void buildRotationMatrix_z(Mat4& rotationMatrix, float angle) {
    float c = cos(angle);
    float s = sin(angle);

    rotationMatrix.m[0][0] = c;  rotationMatrix.m[0][1] = -s; rotationMatrix.m[0][2] = 0;  rotationMatrix.m[0][3] = 0;
    rotationMatrix.m[1][0] = s;  rotationMatrix.m[1][1] = c;  rotationMatrix.m[1][2] = 0;  rotationMatrix.m[1][3] = 0;
    rotationMatrix.m[2][0] = 0;  rotationMatrix.m[2][1] = 0;  rotationMatrix.m[2][2] = 1;  rotationMatrix.m[2][3] = 0;
    rotationMatrix.m[3][0] = 0;  rotationMatrix.m[3][1] = 0;  rotationMatrix.m[3][2] = 0;  rotationMatrix.m[3][3] = 1;
}

void buildTranslationMatrix(Mat4& m, float tx, float ty, float tz)
{
    
    m.m[0][0] = 1; m.m[0][1] = 0; m.m[0][2] = 0; m.m[0][3] = tx;
    m.m[1][0] = 0; m.m[1][1] = 1; m.m[1][2] = 0; m.m[1][3] = ty;
    m.m[2][0] = 0; m.m[2][1] = 0; m.m[2][2] = 1; m.m[2][3] = tz;
    m.m[3][0] = 0; m.m[3][1] = 0; m.m[3][2] = 0; m.m[3][3] = 1;

}

void buildScalingMatrix(Mat4& m, float sx, float sy, float sz)
{
    m.m[0][0] = sx; m.m[0][1] = 0;  m.m[0][2] = 0;  m.m[0][3] = 0;
    m.m[1][0] = 0;  m.m[1][1] = sy; m.m[1][2] = 0;  m.m[1][3] = 0;
    m.m[2][0] = 0;  m.m[2][1] = 0;  m.m[2][2] = sz; m.m[2][3] = 0;
    m.m[3][0] = 0;  m.m[3][1] = 0;  m.m[3][2] = 0;  m.m[3][3] = 1;
}
    

void clearframeBuffer() {
    for (int y = 0; y < VIEWPORT_HEIGHT; ++y) {
        for (int x = 0; x < VIEWPORT_WIDTH; ++x) {
            framebuffer[y * VIEWPORT_WIDTH + x] = {uint8_t(125), uint8_t(125), uint8_t(125)}; //grey color map generation
        }
    }
}

void clearZBuffer() {
    for(int i = 0; i<no_of_pixels; i++)
    {
        zbuffer[i] = std::numeric_limits<float>::infinity(); //Reset z-buffer to infinity
    }
}

void writeFramebufferToPPM(const std::string& filename) {
    FILE* f = std::fopen(filename.c_str(), "wb");
    if (f == nullptr) {
        cout << "Error: Could not open " << filename << " for writing." << endl;
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    fwrite(reinterpret_cast<const char*>(framebuffer.data()), sizeof(RGB), no_of_pixels, f);
    fclose(f);
}

/*
Loads an OBJ file into a vertex list and a flat index list.
Only 'v' and 'f' lines are used; vt/vn/usemtl/o/g/s/# are skipped.
OBJ indices are 1-based, so 1 is subtracted on the way in.
Face tokens may be "5", "5/2", "5//3" or "5/2/3" -- only the part
before the first '/' is the vertex index.
Faces with more than 3 vertices are fan-triangulated.
*/
bool loadOBJ(const std::string& path,
             std::vector<Vec4>& out_verts,
             std::vector<int>&  out_indices)
{
    std::ifstream file(path);
    if (!file) {
        cout << "Error: could not open " << path << endl;
        return false;
    }

    out_verts.clear();
    out_indices.clear();

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            out_verts.push_back({x, y, z, 1.0f});
        }
        else if (tag == "f") {
            std::vector<int> face;          // indices for this one face
            std::string token;
            while (ss >> token) {
                size_t slash = token.find('/');
                if (slash != std::string::npos)
                    token = token.substr(0, slash);   // keep only "5" from "5/2/3"
                face.push_back(std::stoi(token) - 1); // OBJ is 1-based
            }
            // fan-triangulate: (0,1,2), (0,2,3), (0,3,4) ...
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                out_indices.push_back(face[0]);
                out_indices.push_back(face[i]);
                out_indices.push_back(face[i + 1]);
            }
        }
        // everything else ignored
    }

    cout << "Loaded " << out_verts.size() << " vertices, "
         << out_indices.size() / 3 << " triangles" << endl;
    return true;
}



vector<float> normalizationPass(const std::vector<Vec4>& obj_verts)
{
    vector<float> data;
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (int i = 0; i<obj_verts.size(); ++i) {
        min_x = std::min(min_x, static_cast<float>(obj_verts[i].x));
        min_y = std::min(min_y, static_cast<float>(obj_verts[i].y));
        min_z = std::min(min_z, static_cast<float>(obj_verts[i].z));

        max_x = std::max(max_x, static_cast<float>(obj_verts[i].x));
        max_y = std::max(max_y, static_cast<float>(obj_verts[i].y));
        max_z = std::max(max_z, static_cast<float>(obj_verts[i].z));
    }

    data.push_back(min_x);
    data.push_back(min_y);
    data.push_back(min_z);
    data.push_back(max_x);
    data.push_back(max_y);
    data.push_back(max_z);

    return data;
}
int main() {

//   /*Cube vertices*/
//   vector<Vec4> cube_verts = {
//     {-1,-1,-1,1}, { 1,-1,-1,1}, { 1, 1,-1,1}, {-1, 1,-1,1},  // 0-3: back  z=-1
//     {-1,-1, 1,1}, { 1,-1, 1,1}, { 1, 1, 1,1}, {-1, 1, 1,1}   // 4-7: front z=+1
// };

//  vector<screenVertex> screen_verts(cube_verts.size()); //To store the transformed vertices in screen space

// /*indices of the vertex that make up the faces of the cube as triangles. The indices represent the vertex number in cube_verts
//   Direction of vertices chosen such that it follows outward normal nomenclature for a closed solid 
// */
// vector<int> cube_indices = 
// {

//     2,1,0, 0,3,2, //z = -1 face
//     4,5,6, 6,7,4, //z = +1 face
//     7,6,2, 2,3,7, //y = +1 face
//     1,5,4, 4,0,1, //y = -1 face
//     1,2,6, 6,5,1, //x = +1 face
//     7,3,0, 0,4,7  //x = -1 face

// };

// vector<RGB> cube_colors = {
//     {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0},
//     {255,0,255}, {0,255,255}, {255,128,0}, {128,0,255}
// };

vector<Vec4> obj_verts;
vector<int> obj_indices;
if (!loadOBJ("test.obj", obj_verts, obj_indices)) {
    return 1; // Exit if the OBJ file could not be loaded
}
vector<screenVertex> screen_verts(obj_verts.size()); //To store the transformed vertices in screen space
vector<float> normalization_data = normalizationPass(obj_verts);

float centre_x = (normalization_data[0] + normalization_data[3]) / 2.0f;
float centre_y = (normalization_data[1] + normalization_data[4]) / 2.0f;
float centre_z = (normalization_data[2] + normalization_data[5]) / 2.0f;
float extent = std::max({normalization_data[3] - normalization_data[0], normalization_data[4] - normalization_data[1], normalization_data[5] - normalization_data[2]});


vector<RGB> obj_colors(obj_verts.size());
{
    float min_x = normalization_data[0], max_x = normalization_data[3];
    float min_y = normalization_data[1], max_y = normalization_data[4];
    float min_z = normalization_data[2], max_z = normalization_data[5];

    float range_x = max_x - min_x;
    float range_y = max_y - min_y;
    float range_z = max_z - min_z;

    for (size_t i = 0; i < obj_verts.size(); ++i) {
        // map each axis onto 0..1 across the model's bounding box,
        // falling back to 0.5 when the model is flat on that axis
        float fx = (range_x > 1e-8f) ? (obj_verts[i].x - min_x) / range_x : 0.5f;
        float fy = (range_y > 1e-8f) ? (obj_verts[i].y - min_y) / range_y : 0.5f;
        float fz = (range_z > 1e-8f) ? (obj_verts[i].z - min_z) / range_z : 0.5f;

        obj_colors[i] = { uint8_t(fx * 255.0f),
                          uint8_t(fy * 255.0f),
                          uint8_t(fz * 255.0f) };
    }
}

Mat4 perspectiveMatrix;

buildPerspectiveMatrix(perspectiveMatrix, FOV * (PI / 180.0f), float(VIEWPORT_WIDTH) / float(VIEWPORT_HEIGHT), 0.1f, 100.0f);

Mat4 centreM, scaleM;
buildTranslationMatrix(centreM, -centre_x, -centre_y, -centre_z);
float s = 2.0f/extent;
buildScalingMatrix(scaleM, s, s, s);
Mat4 normalise = multiply(scaleM, centreM);

Mat4 view = { {{1,0,0,0},{0,1,0,0},{0,0,1,-4},{0,0,0,1}} };

for(int frame = 0; frame < 120; ++frame) {
    clearframeBuffer();
    clearZBuffer();

    float angle = frame * (PI / 60.0f); //3 degree rotation per
    Mat4 rotationxMatrix, rotationyMatrix, rotationzMatrix;
    buildRotationMatrix_x(rotationxMatrix, angle);
    buildRotationMatrix_y(rotationyMatrix, angle);
    buildRotationMatrix_z(rotationzMatrix, angle);
    Mat4 rotation = multiply(rotationzMatrix, multiply(rotationyMatrix, rotationxMatrix));

    Mat4 model = multiply(rotation, normalise);
    Mat4 mvp = multiply(perspectiveMatrix, multiply(view, model));

    for (size_t i = 0; i < obj_verts.size(); ++i) {
        Vec3 transformed = perspectiveTransform(obj_verts[i], mvp);
        screen_verts[i] = { (transformed.x + 1.0f) * 0.5f * VIEWPORT_WIDTH,
                            (1.0f - (transformed.y + 1.0f) * 0.5f) * VIEWPORT_HEIGHT,
                            transformed.z,
                            obj_colors[i] };
    }

    for (size_t i = 0; i < obj_indices.size(); i += 3) {
        const screenVertex& A = screen_verts[obj_indices[i]];
        const screenVertex& B = screen_verts[obj_indices[i + 1]];
        const screenVertex& C = screen_verts[obj_indices[i + 2]];
        drawTriangle(A, B, C);
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "frames/%03d.ppm", frame);
    writeFramebufferToPPM(filename);

}

return 0;
}