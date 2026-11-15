#include "../../sys/android/app/src/main/cpp/MeshGeometry.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../../sys/android/app/src/main/cpp/third_party/stb_image.h"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    for(const char *name:{"asphalt","concrete","brick","rust"}) {
        std::string path="sys/android/app/src/main/assets/textures/"+std::string(name)+".png";
        int w=0,h=0,n=0;
        auto *p=stbi_load(path.c_str(),&w,&h,&n,4);
        assert(p && w==512 && h==512);
        for(int y=0;y<h;y++)for(int c=0;c<4;c++)assert(p[(y*w)*4+c]==p[(y*w+w-1)*4+c]);
        for(int x=0;x<w;x++)for(int c=0;c<4;c++)assert(p[x*4+c]==p[((h-1)*w+x)*4+c]);
        stbi_image_free(p);
        std::cout<<name<<": PNG decode, 512x512, seamless edges OK\n";
    }
    int w,h,n;
    const unsigned char corrupt[]={137,80,78,71,0,0,0,0};
    assert(!stbi_load_from_memory(corrupt,sizeof(corrupt),&w,&h,&n,4));
    assert(!stbi_load("/this-file-does-not-exist.png",&w,&h,&n,4));
    std::vector<sandbox::MeshVertex> vertices;
    sandbox::appendCube(vertices,{0,3,0,12,6,.4f,1,1,1,sandbox::Concrete});
    assert(vertices.size()==36);
    for(auto v:vertices) {
        assert(std::abs(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz-1)<.00001f);
        assert(v.layer==sandbox::Concrete);
    }
    // Front wall 12 m wide repeats six times, not once stretched across the facade.
    float uMin=vertices[0].u,uMax=uMin;
    for(int i=0;i<6;i++){uMin=std::min(uMin,vertices[i].u);uMax=std::max(uMax,vertices[i].u);}
    assert(std::abs(uMax-uMin-6)<.00001f);
    assert(vertices[0].nz==1 && vertices[24].ny==1);
    std::cout<<"Mesh normals, material IDs and metre-based UV tiling OK\n";
}
