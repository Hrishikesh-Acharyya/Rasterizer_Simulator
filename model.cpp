#include "model.h"
#include <fstream>
#include <sstream>
#include <cstdio>

bool loadOBJ(const std::string& path,
             std::vector<Vec4>& out_verts,
             std::vector<int>&  out_indices)
{
    std::ifstream file(path);
    if (!file) {
        std::printf("Error: could not open %s", path.c_str()); 
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

    std::printf("Loaded %d vertices and %d triangles", out_verts.size(),out_indices.size() / 3);
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

    for (size_t i = 0; i<obj_verts.size(); ++i) {
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