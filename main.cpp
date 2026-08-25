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
#include "vectors.h"
#include "matrices.h"

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

struct screenVertex{
float x,y,z;
RGB color;
};

std::vector<RGB> framebuffer(no_of_pixels);//Each RGB struct is one pixel
std::vector<float> zbuffer(no_of_pixels, std::numeric_limits<float>::infinity()); //Initialize z-buffer to infinity










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
        std::cout << "Error: Could not open " << filename << " for writing." << std::endl;
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
        std::cout << "Error: could not open " << path << std::endl;
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

    std::cout << "Loaded " << out_verts.size() << " vertices, "
         << out_indices.size() / 3 << " triangles" << std::endl;
    return true;
}



std::vector<float> normalizationPass(const std::vector<Vec4>& obj_verts)
{
    std::vector<float> data;
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

std::vector<Vec4> obj_verts;
std::vector<int> obj_indices;
if (!loadOBJ("torus.obj", obj_verts, obj_indices)) {
    return 1; // Exit if the OBJ file could not be loaded
}
std::vector<screenVertex> screen_verts(obj_verts.size()); //To store the transformed vertices in screen space
std::vector<Vec4> world_verts(obj_verts.size());
std::vector<float> normalization_data = normalizationPass(obj_verts);
std::vector<Vec3> vertex_normals(obj_verts.size(), {0.0f, 0.0f, 0.0f}); // Initialize vertex normals to zero
std::vector<Vec3> world_normals(obj_verts.size());  

for (size_t i = 0; i < obj_indices.size(); i += 3) {
    Vec3 A = {obj_verts[obj_indices[i]].x, obj_verts[obj_indices[i]].y, obj_verts[obj_indices[i]].z};
    Vec3 B = {obj_verts[obj_indices[i + 1]].x, obj_verts[obj_indices[i + 1]].y, obj_verts[obj_indices[i + 1]].z};
    Vec3 C = {obj_verts[obj_indices[i + 2]].x, obj_verts[obj_indices[i + 2]].y, obj_verts[obj_indices[i + 2]].z};
    Vec3 edge1 = subtract_Vec3(B, A);
    Vec3 edge2 = subtract_Vec3(C, A);
    Vec3 faceNormal = cross_Vec3(edge1, edge2);

    vertex_normals[obj_indices[i]].x += faceNormal.x;
    vertex_normals[obj_indices[i]].y += faceNormal.y;
    vertex_normals[obj_indices[i]].z += faceNormal.z;

    vertex_normals[obj_indices[i + 1]].x += faceNormal.x;
    vertex_normals[obj_indices[i + 1]].y += faceNormal.y;
    vertex_normals[obj_indices[i + 1]].z += faceNormal.z;

    vertex_normals[obj_indices[i + 2]].x += faceNormal.x;
    vertex_normals[obj_indices[i + 2]].y += faceNormal.y;
    vertex_normals[obj_indices[i + 2]].z += faceNormal.z;
}

/*Normalization later to preserve the larger effect of larger triangles. Normalization before 
would destroy that*/
for (Vec3& normal : vertex_normals) {
    normal = normalize_Vec3(normal);
}

float centre_x = (normalization_data[0] + normalization_data[3]) / 2.0f;
float centre_y = (normalization_data[1] + normalization_data[4]) / 2.0f;
float centre_z = (normalization_data[2] + normalization_data[5]) / 2.0f;
float extent = std::max({normalization_data[3] - normalization_data[0], normalization_data[4] - normalization_data[1], normalization_data[5] - normalization_data[2]});


std::vector<RGB> obj_colors(obj_verts.size());
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
Mat4 normalise = multiply_matrices(scaleM, centreM);

Mat4 view = { {{1,0,0,0},{0,1,0,0},{0,0,1,-4},{0,0,0,1}} };
Vec3 lightDir = normalize_Vec3({1.0f, 1.0f, 1.0f}); //pointing towards camera instead of away to make calculations less messy


for(int frame = 0; frame < 120; ++frame) {
    clearframeBuffer();
    clearZBuffer();
    
    float angle = frame * (PI / 60.0f); //3 degree rotation per
    Mat4 rotationxMatrix, rotationyMatrix, rotationzMatrix;
    buildRotationMatrix_x(rotationxMatrix, angle);
    buildRotationMatrix_y(rotationyMatrix, angle);
    buildRotationMatrix_z(rotationzMatrix, angle);
    Mat4 rotation = multiply_matrices(rotationzMatrix, multiply_matrices(rotationyMatrix, rotationxMatrix));

    Mat4 world_space = multiply_matrices(rotation, normalise);
    Mat4 mvp = multiply_matrices(perspectiveMatrix, multiply_matrices(view, world_space));

    for (size_t i = 0; i < obj_verts.size(); ++i) {
        world_verts[i] = transform_Vec4(world_space, obj_verts[i]);
        
        Vec4 vertex_normal_augmented = {vertex_normals[i].x,vertex_normals[i].y,vertex_normals[i].z,0};
        Vec4 vertex_normals_augmented_transformed = transform_Vec4(world_space, vertex_normal_augmented);
        world_normals[i] = {vertex_normals_augmented_transformed.x,vertex_normals_augmented_transformed.y, vertex_normals_augmented_transformed.z};
        world_normals[i] = normalize_Vec3({vertex_normals_augmented_transformed.x, vertex_normals_augmented_transformed.y,vertex_normals_augmented_transformed.z});        
        Vec3 transformed = perspectiveTransform(obj_verts[i], mvp);
        screen_verts[i] = { (transformed.x + 1.0f) * 0.5f * VIEWPORT_WIDTH,
                            (1.0f - (transformed.y + 1.0f) * 0.5f) * VIEWPORT_HEIGHT,
                            transformed.z,
                            obj_colors[i] };
    }

    for (size_t i = 0; i < obj_indices.size(); i += 3) {
        screenVertex A = screen_verts[obj_indices[i]];
        screenVertex B = screen_verts[obj_indices[i + 1]];
        screenVertex C = screen_verts[obj_indices[i + 2]];
        
        
const int ia = obj_indices[i];
const int ib = obj_indices[i + 1];
const int ic = obj_indices[i + 2];

float intensityA = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ia], lightDir));
float intensityB = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ib], lightDir));
float intensityC = 0.4f + 0.6f * std::max(0.0f, dot_Vec3(world_normals[ic], lightDir));

         A.color = { uint8_t(A.color.r * intensityA),
                    uint8_t(A.color.g * intensityA),
                    uint8_t(A.color.b * intensityA) };
                
        B.color = { uint8_t(B.color.r * intensityB),
                    uint8_t(B.color.g * intensityB),
                    uint8_t(B.color.b * intensityB) };
        C.color = { uint8_t(C.color.r * intensityC),
                    uint8_t(C.color.g * intensityC),
                    uint8_t(C.color.b * intensityC) };

        drawTriangle(A, B, C);
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "frames/%03d.ppm", frame);
    writeFramebufferToPPM(filename);

}

return 0;
}