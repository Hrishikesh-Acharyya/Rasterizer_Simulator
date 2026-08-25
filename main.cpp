#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>

#include "vectors.h"
#include "matrices.h"
#include "framebuffer.h"
#include "types.h"
#include "raster.h"
#include "model.h"

 
static const float FOV = 60;


int main() {

std::vector<Vec4> obj_verts;
std::vector<int> obj_indices;
if (!loadOBJ("Media/Obj_files/torus.obj", obj_verts, obj_indices)) {
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
    clear_frameBuffer();
    clear_zBuffer();
    
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