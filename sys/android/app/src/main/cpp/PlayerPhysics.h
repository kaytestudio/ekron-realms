#pragma once
#include "CollisionMesh.h"
#include <memory>

namespace physics {
class PlayerController {
    Vec3 feet_{};
    float velocity_=0,accumulator_=0,height_=1.8f,eye_=1.7f,camera_=1.7f;
    bool initialized_=false,grounded_=false,jump_=false;
    static constexpr float radius=.25f;
    Bounds bodyBounds(Vec3 feet,float height)const{Bounds b;b.add(feet+Vec3{-radius,-.02f,-radius});b.add(feet+Vec3{radius,height+.02f,radius});return b;}
    bool contacts(const CollisionMesh&mesh,Vec3 feet,float height)const{
        bool hit=false;mesh.query(bodyBounds(feet,height),[&](const Triangle&t){Vec3 n;float d=capsuleTriangle(feet+Vec3{0,radius,0},feet+Vec3{0,height-radius,0},t,n);if(d<radius-.002f)hit=true;});return hit;
    }
    void solve(const CollisionMesh&mesh){
        for(int iteration=0;iteration<6;iteration++){
            bool hit=false;auto bounds=bodyBounds(feet_,height_);
            mesh.query(bounds,[&](const Triangle&t){
                Vec3 n;float d=capsuleTriangle(feet_+Vec3{0,radius,0},feet_+Vec3{0,height_-radius,0},t,n);
                if(d>=radius-.0001f)return;
                float depth=radius-d+.0002f;hit=true;
                if(n.y>.64f&&feet_.y>=std::min({t.a.y,t.b.y,t.c.y})-.3f){
                    feet_.y+=depth/n.y;grounded_=true;if(velocity_<0)velocity_=0;
                }else if(n.y<-.64f){feet_.y-=depth/-n.y;if(velocity_>0)velocity_=0;}
                else{float horizontal=std::hypot(n.x,n.z);if(horizontal>.001f){feet_.x+=n.x/horizontal*depth;feet_.z+=n.z/horizontal*depth;}}
            });
            if(!hit)break;
        }
    }
public:
    void reset(float x,float eyeY,float z){feet_={x,eyeY-eye_,z};velocity_=accumulator_=0;camera_=eyeY;grounded_=false;initialized_=true;jump_=false;}
    void jump(){jump_=true;}
    bool grounded()const{return grounded_;}
    Vec3 feet()const{return feet_;}
    float eyeHeight()const{return eye_;}
    bool overlaps(const CollisionMesh&mesh,float x,float eyeY,float z)const{return contacts(mesh,{x,eyeY-1.7f,z},1.8f);}
    void update(const CollisionMesh&mesh,float right,float forward,float yaw,float dt,bool crouched,float &x,float &y,float &z,const CollisionMesh *extra=nullptr){
        if(!initialized_)reset(x,y,z);
        // Crouch keeps the feet still; standing up is refused under a concrete frame/ceiling.
        if(crouched){height_=1.0f;eye_=.9f;}
        else if(height_<1.8f&&!contacts(mesh,feet_,1.8f)){height_=1.8f;eye_=1.7f;}
        accumulator_+=std::clamp(dt,0.f,.05f);
        constexpr float step=1.f/120;
        while(accumulator_>=step){
            accumulator_-=step;bool wasGrounded=grounded_;
            if(jump_&&grounded_){velocity_=6.5f;grounded_=false;wasGrounded=false;}jump_=false;
            float lengthInput=std::max(1.f,std::hypot(right,forward)),speed=crouched?2.2f:4.5f;
            feet_.x+=(std::cos(yaw)*right-std::sin(yaw)*forward)/lengthInput*speed*step;
            feet_.z+=(-std::sin(yaw)*right-std::cos(yaw)*forward)/lengthInput*speed*step;
            velocity_=std::max(-30.f,velocity_-18.f*step);feet_.y+=velocity_*step;grounded_=false;
            solve(mesh);if(extra)solve(*extra);
            // Follow small downhill changes, but NEVER snap over an actual pit or from mid-air.
            if(wasGrounded&&!grounded_&&velocity_<=0){
                float floor=mesh.groundHeight(feet_.x,feet_.z,feet_.y+.08f,feet_.y-.18f);
                if(std::isfinite(floor)&&feet_.y-floor<.18f){feet_.y=floor+.0005f;solve(mesh);grounded_=true;velocity_=0;}
            }
        }
        x=feet_.x;z=feet_.z;
        // Only camera Y is damped. Physics is fixed-step and never depends on camera smoothing.
        float target=feet_.y+eye_;
        camera_+=(target-camera_)*(1-std::exp(-22.f*std::clamp(dt,0.f,.05f)));
        if(std::abs(camera_-target)<.0001f)camera_=target;
        y=camera_;
    }
};
}
