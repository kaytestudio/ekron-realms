// Quake 2 qcommon/pmove.c, GPL (c) 1997-2001 id Software, ported to C++ on
// realms/q2game.h. Function names and the order of every operation follow
// the original so the movement matches Quake 2 step for step.
#include "realms/Q2Pmove.h"

#include <cmath>
#include <cstring>

namespace realms::q2 {
namespace {

constexpr int PITCH = 0, YAW = 1, ROLL = 2;
constexpr float STEPSIZE = 18.0f;
constexpr float STOP_EPSILON = 0.1f;
constexpr float MIN_STEP_NORMAL = 0.7f;
constexpr int MAX_CLIP_PLANES = 5;

constexpr int CONTENTS_SOLID = 1, CONTENTS_LAVA = 8, CONTENTS_SLIME = 16, CONTENTS_WATER = 32;
constexpr int CONTENTS_CURRENT_0 = 0x40000, CONTENTS_CURRENT_90 = 0x80000, CONTENTS_CURRENT_180 = 0x100000,
              CONTENTS_CURRENT_270 = 0x200000, CONTENTS_CURRENT_UP = 0x400000, CONTENTS_CURRENT_DOWN = 0x800000;
constexpr int CONTENTS_LADDER = 0x20000000;
constexpr int MASK_WATER = CONTENTS_WATER | CONTENTS_LAVA | CONTENTS_SLIME;
constexpr int MASK_CURRENT = CONTENTS_CURRENT_0 | CONTENTS_CURRENT_90 | CONTENTS_CURRENT_180 | CONTENTS_CURRENT_270 |
                             CONTENTS_CURRENT_UP | CONTENTS_CURRENT_DOWN;
constexpr int SURF_SLICK = 0x2;

inline float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void copy(const float *in, float *out) { out[0] = in[0], out[1] = in[1], out[2] = in[2]; }
inline void clear(float *v) { v[0] = v[1] = v[2] = 0; }
inline void scale(const float *in, float s, float *out) { out[0] = in[0] * s, out[1] = in[1] * s, out[2] = in[2] * s; }
inline void ma(const float *a, float s, const float *b, float *out) {
    out[0] = a[0] + s * b[0], out[1] = a[1] + s * b[1], out[2] = a[2] + s * b[2];
}
inline void cross(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
inline float length(const float *v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
inline float normalize(float *v) {
    const float len = length(v);
    if (len) {
        const float il = 1 / len;
        v[0] *= il, v[1] *= il, v[2] *= il;
    }
    return len;
}

// all of the locals will be zeroed before each pmove
struct pml_t {
    float origin[3];   // full float precision
    float velocity[3]; // full float precision
    float forward[3], right[3], up[3];
    float frametime;
    q2csurface_t *groundsurface;
    q2cplane_t groundplane;
    int groundcontents;
    short previous_origin[3];
    bool ladder;
};

struct Mover {
    q2pmove_t *pm;
    pml_t pml{};
    PmoveParams p;

    q2trace_t trace(float *start, float *mins, float *maxs, float *end) { return pm->trace(start, mins, maxs, end); }

    static void clipVelocity(const float *in, const float *normal, float *out, float overbounce) {
        const float backoff = dot(in, normal) * overbounce;
        for (int i = 0; i < 3; i++) {
            const float change = normal[i] * backoff;
            out[i] = in[i] - change;
            if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0;
        }
    }

    void stepSlideMove_() {
        float dir[3], planes[MAX_CLIP_PLANES][3], primal_velocity[3], end[3];
        const int numbumps = 4;
        copy(pml.velocity, primal_velocity);
        int numplanes = 0;
        float time_left = pml.frametime;
        for (int bumpcount = 0; bumpcount < numbumps; bumpcount++) {
            for (int i = 0; i < 3; i++) end[i] = pml.origin[i] + time_left * pml.velocity[i];
            q2trace_t tr = trace(pml.origin, pm->mins, pm->maxs, end);
            if (tr.allsolid) { // entity is trapped in another solid
                pml.velocity[2] = 0; // don't build up falling damage
                return;
            }
            if (tr.fraction > 0) { // actually covered some distance
                copy(tr.endpos, pml.origin);
                numplanes = 0;
            }
            if (tr.fraction == 1) break; // moved the entire distance
            // save entity for contact
            if (pm->numtouch < Q2_MAXTOUCH && tr.ent) pm->touchents[pm->numtouch++] = tr.ent;
            time_left -= time_left * tr.fraction;
            // slide along this plane
            if (numplanes >= MAX_CLIP_PLANES) { // this shouldn't really happen
                clear(pml.velocity);
                break;
            }
            copy(tr.plane.normal, planes[numplanes]);
            numplanes++;
            // modify original_velocity so it parallels all of the clip planes
            int i, j;
            for (i = 0; i < numplanes; i++) {
                clipVelocity(pml.velocity, planes[i], pml.velocity, 1.01f);
                for (j = 0; j < numplanes; j++)
                    if (j != i && dot(pml.velocity, planes[j]) < 0) break; // not ok
                if (j == numplanes) break;
            }
            if (i == numplanes) { // go along the crease
                if (numplanes != 2) {
                    clear(pml.velocity);
                    break;
                }
                cross(planes[0], planes[1], dir);
                const float d = dot(dir, pml.velocity);
                scale(dir, d, pml.velocity);
            }
            // if velocity is against the original velocity, stop dead
            // to avoid tiny occilations in sloping corners
            if (dot(pml.velocity, primal_velocity) <= 0) {
                clear(pml.velocity);
                break;
            }
        }
        if (pm->s.pm_time) copy(primal_velocity, pml.velocity);
    }

    void stepSlideMove() {
        float start_o[3], start_v[3], down_o[3], down_v[3], up[3], down[3];
        copy(pml.origin, start_o);
        copy(pml.velocity, start_v);
        stepSlideMove_();
        copy(pml.origin, down_o);
        copy(pml.velocity, down_v);
        copy(start_o, up);
        up[2] += STEPSIZE;
        q2trace_t tr = trace(up, pm->mins, pm->maxs, up);
        if (tr.allsolid) return; // can't step up
        // try sliding above
        copy(up, pml.origin);
        copy(start_v, pml.velocity);
        stepSlideMove_();
        // push down the final amount
        copy(pml.origin, down);
        down[2] -= STEPSIZE;
        tr = trace(pml.origin, pm->mins, pm->maxs, down);
        if (!tr.allsolid) copy(tr.endpos, pml.origin);
        copy(pml.origin, up);
        // decide which one went farther
        const float down_dist =
            (down_o[0] - start_o[0]) * (down_o[0] - start_o[0]) + (down_o[1] - start_o[1]) * (down_o[1] - start_o[1]);
        const float up_dist = (up[0] - start_o[0]) * (up[0] - start_o[0]) + (up[1] - start_o[1]) * (up[1] - start_o[1]);
        if (down_dist > up_dist || tr.plane.normal[2] < MIN_STEP_NORMAL) {
            copy(down_o, pml.origin);
            copy(down_v, pml.velocity);
            return;
        }
        //!! Special case
        // if we were walking along a plane, then we need to copy the Z over
        pml.velocity[2] = down_v[2];
    }

    void friction() {
        float *vel = pml.velocity;
        const float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        if (speed < 1) {
            vel[0] = 0;
            vel[1] = 0;
            return;
        }
        float drop = 0;
        // apply ground friction
        if ((pm->groundentity && pml.groundsurface && !(pml.groundsurface->flags & SURF_SLICK)) || pml.ladder) {
            const float fr = p.friction;
            const float control = speed < p.stopSpeed ? p.stopSpeed : speed;
            drop += control * fr * pml.frametime;
        }
        // apply water friction
        if (pm->waterlevel && !pml.ladder) drop += speed * p.waterFriction * pm->waterlevel * pml.frametime;
        // scale the velocity
        float newspeed = speed - drop;
        if (newspeed < 0) newspeed = 0;
        newspeed /= speed;
        vel[0] = vel[0] * newspeed;
        vel[1] = vel[1] * newspeed;
        vel[2] = vel[2] * newspeed;
    }

    void accelerate(const float *wishdir, float wishspeed, float accel) {
        const float currentspeed = dot(pml.velocity, wishdir);
        const float addspeed = wishspeed - currentspeed;
        if (addspeed <= 0) return;
        float accelspeed = accel * pml.frametime * wishspeed;
        if (accelspeed > addspeed) accelspeed = addspeed;
        for (int i = 0; i < 3; i++) pml.velocity[i] += accelspeed * wishdir[i];
    }

    void airAccelerate(const float *wishdir, float wishspeed, float accel) {
        float wishspd = wishspeed;
        if (wishspd > 30) wishspd = 30;
        const float currentspeed = dot(pml.velocity, wishdir);
        const float addspeed = wishspd - currentspeed;
        if (addspeed <= 0) return;
        float accelspeed = accel * wishspeed * pml.frametime;
        if (accelspeed > addspeed) accelspeed = addspeed;
        for (int i = 0; i < 3; i++) pml.velocity[i] += accelspeed * wishdir[i];
    }

    void addCurrents(float *wishvel) {
        float v[3];
        // account for ladders
        if (pml.ladder && std::fabs(pml.velocity[2]) <= 200) {
            if ((pm->viewangles[PITCH] <= -15) && (pm->cmd.forwardmove > 0)) wishvel[2] = 200;
            else if ((pm->viewangles[PITCH] >= 15) && (pm->cmd.forwardmove > 0)) wishvel[2] = -200;
            else if (pm->cmd.upmove > 0) wishvel[2] = 200;
            else if (pm->cmd.upmove < 0) wishvel[2] = -200;
            else wishvel[2] = 0;
            // limit horizontal speed when on a ladder
            if (wishvel[0] < -25) wishvel[0] = -25;
            else if (wishvel[0] > 25) wishvel[0] = 25;
            if (wishvel[1] < -25) wishvel[1] = -25;
            else if (wishvel[1] > 25) wishvel[1] = 25;
        }
        // add water currents
        if (pm->watertype & MASK_CURRENT) {
            clear(v);
            if (pm->watertype & CONTENTS_CURRENT_0) v[0] += 1;
            if (pm->watertype & CONTENTS_CURRENT_90) v[1] += 1;
            if (pm->watertype & CONTENTS_CURRENT_180) v[0] -= 1;
            if (pm->watertype & CONTENTS_CURRENT_270) v[1] -= 1;
            if (pm->watertype & CONTENTS_CURRENT_UP) v[2] += 1;
            if (pm->watertype & CONTENTS_CURRENT_DOWN) v[2] -= 1;
            float s = p.waterSpeed;
            if ((pm->waterlevel == 1) && (pm->groundentity)) s /= 2;
            ma(wishvel, s, v, wishvel);
        }
        // add conveyor belt velocities
        if (pm->groundentity) {
            clear(v);
            if (pml.groundcontents & CONTENTS_CURRENT_0) v[0] += 1;
            if (pml.groundcontents & CONTENTS_CURRENT_90) v[1] += 1;
            if (pml.groundcontents & CONTENTS_CURRENT_180) v[0] -= 1;
            if (pml.groundcontents & CONTENTS_CURRENT_270) v[1] -= 1;
            if (pml.groundcontents & CONTENTS_CURRENT_UP) v[2] += 1;
            if (pml.groundcontents & CONTENTS_CURRENT_DOWN) v[2] -= 1;
            ma(wishvel, 100 /* pm->groundentity->speed */, v, wishvel);
        }
    }

    void waterMove() {
        float wishvel[3], wishdir[3];
        // user intentions
        for (int i = 0; i < 3; i++) wishvel[i] = pml.forward[i] * pm->cmd.forwardmove + pml.right[i] * pm->cmd.sidemove;
        if (!pm->cmd.forwardmove && !pm->cmd.sidemove && !pm->cmd.upmove) wishvel[2] -= 60; // drift towards bottom
        else wishvel[2] += pm->cmd.upmove;
        addCurrents(wishvel);
        copy(wishvel, wishdir);
        float wishspeed = normalize(wishdir);
        if (wishspeed > p.maxSpeed) {
            scale(wishvel, p.maxSpeed / wishspeed, wishvel);
            wishspeed = p.maxSpeed;
        }
        wishspeed *= 0.5f;
        accelerate(wishdir, wishspeed, p.waterAccelerate);
        stepSlideMove();
    }

    void airMove() {
        float wishvel[3], wishdir[3];
        const float fmove = pm->cmd.forwardmove;
        const float smove = pm->cmd.sidemove;
        for (int i = 0; i < 2; i++) wishvel[i] = pml.forward[i] * fmove + pml.right[i] * smove;
        wishvel[2] = 0;
        addCurrents(wishvel);
        copy(wishvel, wishdir);
        float wishspeed = normalize(wishdir);
        // clamp to server defined max speed
        const float maxspeed = (pm->s.pm_flags & Q2_PMF_DUCKED) ? p.duckSpeed : p.maxSpeed;
        if (wishspeed > maxspeed) {
            scale(wishvel, maxspeed / wishspeed, wishvel);
            wishspeed = maxspeed;
        }
        if (pml.ladder) {
            accelerate(wishdir, wishspeed, p.accelerate);
            if (!wishvel[2]) {
                if (pml.velocity[2] > 0) {
                    pml.velocity[2] -= pm->s.gravity * pml.frametime;
                    if (pml.velocity[2] < 0) pml.velocity[2] = 0;
                } else {
                    pml.velocity[2] += pm->s.gravity * pml.frametime;
                    if (pml.velocity[2] > 0) pml.velocity[2] = 0;
                }
            }
            stepSlideMove();
        } else if (pm->groundentity) { // walking on ground
            pml.velocity[2] = 0; //!!! this is before the accel
            accelerate(wishdir, wishspeed, p.accelerate);
            // PGM -- fix for negative trigger_gravity fields
            if (pm->s.gravity > 0) pml.velocity[2] = 0;
            else pml.velocity[2] -= pm->s.gravity * pml.frametime;
            if (!pml.velocity[0] && !pml.velocity[1]) return;
            stepSlideMove();
        } else { // not on ground, so little effect on velocity
            if (p.airAccelerate) airAccelerate(wishdir, wishspeed, p.accelerate);
            else accelerate(wishdir, wishspeed, 1);
            // add gravity
            pml.velocity[2] -= pm->s.gravity * pml.frametime;
            stepSlideMove();
        }
    }

    void catagorizePosition() {
        float point[3];
        // if the player hull point one unit down is solid, the player is on ground
        // see if standing on something solid
        point[0] = pml.origin[0];
        point[1] = pml.origin[1];
        point[2] = pml.origin[2] - 0.25f;
        if (pml.velocity[2] > 180) { //!!ZOID changed from 100 to 180 (ramp accel)
            pm->s.pm_flags &= uint8_t(~Q2_PMF_ON_GROUND);
            pm->groundentity = nullptr;
        } else {
            q2trace_t tr = trace(pml.origin, pm->mins, pm->maxs, point);
            pml.groundplane = tr.plane;
            pml.groundsurface = tr.surface;
            pml.groundcontents = tr.contents;
            if (!tr.ent || (tr.plane.normal[2] < 0.7f && !tr.startsolid)) {
                pm->groundentity = nullptr;
                pm->s.pm_flags &= uint8_t(~Q2_PMF_ON_GROUND);
            } else {
                pm->groundentity = tr.ent;
                // hitting solid ground will end a waterjump
                if (pm->s.pm_flags & Q2_PMF_TIME_WATERJUMP) {
                    pm->s.pm_flags &= uint8_t(~(Q2_PMF_TIME_WATERJUMP | Q2_PMF_TIME_LAND | Q2_PMF_TIME_TELEPORT));
                    pm->s.pm_time = 0;
                }
                if (!(pm->s.pm_flags & Q2_PMF_ON_GROUND)) { // just hit the ground
                    pm->s.pm_flags |= Q2_PMF_ON_GROUND;
                    // don't do landing time if we were just going down a slope
                    if (pml.velocity[2] < -200) {
                        pm->s.pm_flags |= Q2_PMF_TIME_LAND;
                        // don't allow another jump for a little while
                        pm->s.pm_time = pml.velocity[2] < -400 ? 25 : 18;
                    }
                }
            }
            if (pm->numtouch < Q2_MAXTOUCH && tr.ent) pm->touchents[pm->numtouch++] = tr.ent;
        }
        // get waterlevel, accounting for ducking
        pm->waterlevel = 0;
        pm->watertype = 0;
        const int sample2 = int(pm->viewheight - pm->mins[2]);
        const int sample1 = sample2 / 2;
        point[2] = pml.origin[2] + pm->mins[2] + 1;
        int cont = pm->pointcontents(point);
        if (cont & MASK_WATER) {
            pm->watertype = cont;
            pm->waterlevel = 1;
            point[2] = pml.origin[2] + pm->mins[2] + sample1;
            cont = pm->pointcontents(point);
            if (cont & MASK_WATER) {
                pm->waterlevel = 2;
                point[2] = pml.origin[2] + pm->mins[2] + sample2;
                cont = pm->pointcontents(point);
                if (cont & MASK_WATER) pm->waterlevel = 3;
            }
        }
    }

    void checkJump() {
        if (pm->s.pm_flags & Q2_PMF_TIME_LAND) return; // hasn't been long enough since landing to jump again
        if (pm->cmd.upmove < 10) { // not holding jump
            pm->s.pm_flags &= uint8_t(~Q2_PMF_JUMP_HELD);
            return;
        }
        // must wait for jump to be released
        if (pm->s.pm_flags & Q2_PMF_JUMP_HELD) return;
        if (pm->s.pm_type == Q2_PM_DEAD) return;
        if (pm->waterlevel >= 2) { // swimming, not jumping
            pm->groundentity = nullptr;
            if (pml.velocity[2] <= -300) return;
            if (pm->watertype == CONTENTS_WATER) pml.velocity[2] = 100;
            else if (pm->watertype == CONTENTS_SLIME) pml.velocity[2] = 80;
            else pml.velocity[2] = 50;
            return;
        }
        if (pm->groundentity == nullptr) return; // in air, so no effect
        pm->s.pm_flags |= Q2_PMF_JUMP_HELD;
        pm->groundentity = nullptr;
        pml.velocity[2] += 270;
        if (pml.velocity[2] < 270) pml.velocity[2] = 270;
    }

    void checkSpecialMovement() {
        float spot[3], flatforward[3];
        if (pm->s.pm_time) return;
        pml.ladder = false;
        // check for ladder
        flatforward[0] = pml.forward[0];
        flatforward[1] = pml.forward[1];
        flatforward[2] = 0;
        normalize(flatforward);
        ma(pml.origin, 1, flatforward, spot);
        q2trace_t tr = trace(pml.origin, pm->mins, pm->maxs, spot);
        if ((tr.fraction < 1) && (tr.contents & CONTENTS_LADDER)) pml.ladder = true;
        // check for water jump
        if (pm->waterlevel != 2) return;
        ma(pml.origin, 30, flatforward, spot);
        spot[2] += 4;
        int cont = pm->pointcontents(spot);
        if (!(cont & CONTENTS_SOLID)) return;
        spot[2] += 16;
        cont = pm->pointcontents(spot);
        if (cont) return;
        // jump out of water
        scale(flatforward, 50, pml.velocity);
        pml.velocity[2] = 350;
        pm->s.pm_flags |= Q2_PMF_TIME_WATERJUMP;
        pm->s.pm_time = 255;
    }

    void flyMove(bool doclip) {
        float wishvel[3], wishdir[3], end[3];
        pm->viewheight = 22;
        // friction
        const float speed = length(pml.velocity);
        if (speed < 1) {
            clear(pml.velocity);
        } else {
            float drop = 0;
            const float fr = p.friction * 1.5f; // extra friction
            const float control = speed < p.stopSpeed ? p.stopSpeed : speed;
            drop += control * fr * pml.frametime;
            // scale the velocity
            float newspeed = speed - drop;
            if (newspeed < 0) newspeed = 0;
            newspeed /= speed;
            scale(pml.velocity, newspeed, pml.velocity);
        }
        // accelerate
        const float fmove = pm->cmd.forwardmove;
        const float smove = pm->cmd.sidemove;
        normalize(pml.forward);
        normalize(pml.right);
        for (int i = 0; i < 3; i++) wishvel[i] = pml.forward[i] * fmove + pml.right[i] * smove;
        wishvel[2] += pm->cmd.upmove;
        copy(wishvel, wishdir);
        float wishspeed = normalize(wishdir);
        // clamp to server defined max speed
        if (wishspeed > p.maxSpeed) {
            scale(wishvel, p.maxSpeed / wishspeed, wishvel);
            wishspeed = p.maxSpeed;
        }
        const float currentspeed = dot(pml.velocity, wishdir);
        const float addspeed = wishspeed - currentspeed;
        if (addspeed <= 0) return;
        float accelspeed = p.accelerate * pml.frametime * wishspeed;
        if (accelspeed > addspeed) accelspeed = addspeed;
        for (int i = 0; i < 3; i++) pml.velocity[i] += accelspeed * wishdir[i];
        if (doclip) {
            for (int i = 0; i < 3; i++) end[i] = pml.origin[i] + pml.frametime * pml.velocity[i];
            q2trace_t tr = trace(pml.origin, pm->mins, pm->maxs, end);
            copy(tr.endpos, pml.origin);
        } else {
            // move
            ma(pml.origin, pml.frametime, pml.velocity, pml.origin);
        }
    }

    void checkDuck() {
        pm->mins[0] = -16;
        pm->mins[1] = -16;
        pm->maxs[0] = 16;
        pm->maxs[1] = 16;
        if (pm->s.pm_type == Q2_PM_GIB) {
            pm->mins[2] = 0;
            pm->maxs[2] = 16;
            pm->viewheight = 8;
            return;
        }
        pm->mins[2] = -24;
        if (pm->s.pm_type == Q2_PM_DEAD) {
            pm->s.pm_flags |= Q2_PMF_DUCKED;
        } else if (pm->cmd.upmove < 0 && (pm->s.pm_flags & Q2_PMF_ON_GROUND)) { // duck
            pm->s.pm_flags |= Q2_PMF_DUCKED;
        } else { // stand up if possible
            if (pm->s.pm_flags & Q2_PMF_DUCKED) {
                // try to stand up
                pm->maxs[2] = 32;
                q2trace_t tr = trace(pml.origin, pm->mins, pm->maxs, pml.origin);
                if (!tr.allsolid) pm->s.pm_flags &= uint8_t(~Q2_PMF_DUCKED);
            }
        }
        if (pm->s.pm_flags & Q2_PMF_DUCKED) {
            pm->maxs[2] = 4;
            pm->viewheight = -2;
        } else {
            pm->maxs[2] = 32;
            pm->viewheight = 22;
        }
    }

    void deadMove() {
        if (!pm->groundentity) return;
        // extra friction
        float forward = length(pml.velocity);
        forward -= 20;
        if (forward <= 0) {
            clear(pml.velocity);
        } else {
            normalize(pml.velocity);
            scale(pml.velocity, forward, pml.velocity);
        }
    }

    bool goodPosition() {
        float origin[3], end[3];
        if (pm->s.pm_type == Q2_PM_SPECTATOR) return true;
        for (int i = 0; i < 3; i++) origin[i] = end[i] = pm->s.origin[i] * 0.125f;
        q2trace_t tr = trace(origin, pm->mins, pm->maxs, end);
        return !tr.allsolid;
    }

    void snapPosition() {
        int sign[3];
        short base[3];
        // try all single bits first
        static constexpr int jitterbits[8] = {0, 4, 1, 2, 3, 5, 6, 7};
        // snap velocity to eigths
        for (int i = 0; i < 3; i++) pm->s.velocity[i] = int16_t(int(pml.velocity[i] * 8));
        for (int i = 0; i < 3; i++) {
            sign[i] = pml.origin[i] >= 0 ? 1 : -1;
            pm->s.origin[i] = int16_t(int(pml.origin[i] * 8));
            if (pm->s.origin[i] * 0.125f == pml.origin[i]) sign[i] = 0;
        }
        for (int i = 0; i < 3; i++) base[i] = pm->s.origin[i];
        // try all combinations
        for (int j = 0; j < 8; j++) {
            const int bits = jitterbits[j];
            for (int i = 0; i < 3; i++) pm->s.origin[i] = base[i];
            for (int i = 0; i < 3; i++)
                if (bits & (1 << i)) pm->s.origin[i] = int16_t(pm->s.origin[i] + sign[i]);
            if (goodPosition()) return;
        }
        // go back to the last position
        for (int i = 0; i < 3; i++) pm->s.origin[i] = pml.previous_origin[i];
    }

    void initialSnapPosition() {
        short base[3];
        static constexpr int offset[3] = {0, -1, 1};
        for (int i = 0; i < 3; i++) base[i] = pm->s.origin[i];
        for (int z = 0; z < 3; z++) {
            pm->s.origin[2] = int16_t(base[2] + offset[z]);
            for (int y = 0; y < 3; y++) {
                pm->s.origin[1] = int16_t(base[1] + offset[y]);
                for (int x = 0; x < 3; x++) {
                    pm->s.origin[0] = int16_t(base[0] + offset[x]);
                    if (goodPosition()) {
                        for (int i = 0; i < 3; i++) {
                            pml.origin[i] = pm->s.origin[i] * 0.125f;
                            pml.previous_origin[i] = pm->s.origin[i];
                        }
                        return;
                    }
                }
            }
        }
    }

    void clampAngles() {
        if (pm->s.pm_flags & Q2_PMF_TIME_TELEPORT) {
            pm->viewangles[YAW] = shortToAngle(int16_t(pm->cmd.angles[YAW] + pm->s.delta_angles[YAW]));
            pm->viewangles[PITCH] = 0;
            pm->viewangles[ROLL] = 0;
        } else {
            // circularly clamp the angles with deltas
            for (int i = 0; i < 3; i++) {
                const short temp = short(pm->cmd.angles[i] + pm->s.delta_angles[i]);
                pm->viewangles[i] = shortToAngle(temp);
            }
            // don't let the player look up or down more than 90 degrees
            if (pm->viewangles[PITCH] > 89 && pm->viewangles[PITCH] < 180) pm->viewangles[PITCH] = 89;
            else if (pm->viewangles[PITCH] < 271 && pm->viewangles[PITCH] >= 180) pm->viewangles[PITCH] = 271;
        }
        angleVectors(pm->viewangles, pml.forward, pml.right, pml.up);
    }

    void run() {
        // clear results
        pm->numtouch = 0;
        clear(pm->viewangles);
        pm->viewheight = 0;
        pm->groundentity = nullptr;
        pm->watertype = 0;
        pm->waterlevel = 0;
        // clear all pmove local vars
        std::memset(&pml, 0, sizeof(pml));
        // convert origin and velocity to float values
        for (int i = 0; i < 3; i++) {
            pml.origin[i] = pm->s.origin[i] * 0.125f;
            pml.velocity[i] = pm->s.velocity[i] * 0.125f;
            // save old org in case we get stuck
            pml.previous_origin[i] = pm->s.origin[i];
        }
        pml.frametime = pm->cmd.msec * 0.001f;
        clampAngles();
        if (pm->s.pm_type == Q2_PM_SPECTATOR) {
            flyMove(false);
            snapPosition();
            return;
        }
        if (pm->s.pm_type >= Q2_PM_DEAD) {
            pm->cmd.forwardmove = 0;
            pm->cmd.sidemove = 0;
            pm->cmd.upmove = 0;
        }
        if (pm->s.pm_type == Q2_PM_FREEZE) return; // no movement at all
        // set mins, maxs, and viewheight
        checkDuck();
        if (pm->snapinitial) initialSnapPosition();
        // set groundentity, watertype, and waterlevel
        catagorizePosition();
        if (pm->s.pm_type == Q2_PM_DEAD) deadMove();
        checkSpecialMovement();
        // drop timing counter
        if (pm->s.pm_time) {
            int msec = pm->cmd.msec >> 3;
            if (!msec) msec = 1;
            if (msec >= pm->s.pm_time) {
                pm->s.pm_flags &= uint8_t(~(Q2_PMF_TIME_WATERJUMP | Q2_PMF_TIME_LAND | Q2_PMF_TIME_TELEPORT));
                pm->s.pm_time = 0;
            } else {
                pm->s.pm_time = uint8_t(pm->s.pm_time - msec);
            }
        }
        if (pm->s.pm_flags & Q2_PMF_TIME_TELEPORT) {
            // teleport pause stays exactly in place
        } else if (pm->s.pm_flags & Q2_PMF_TIME_WATERJUMP) {
            // waterjump has no control, but falls
            pml.velocity[2] -= pm->s.gravity * pml.frametime;
            if (pml.velocity[2] < 0) { // cancel as soon as we are falling down again
                pm->s.pm_flags &= uint8_t(~(Q2_PMF_TIME_WATERJUMP | Q2_PMF_TIME_LAND | Q2_PMF_TIME_TELEPORT));
                pm->s.pm_time = 0;
            }
            stepSlideMove();
        } else {
            checkJump();
            friction();
            if (pm->waterlevel >= 2) {
                waterMove();
            } else {
                float angles[3];
                copy(pm->viewangles, angles);
                if (angles[PITCH] > 180) angles[PITCH] = angles[PITCH] - 360;
                angles[PITCH] /= 3;
                angleVectors(angles, pml.forward, pml.right, pml.up);
                airMove();
            }
        }
        // set groundentity, watertype, and waterlevel for final spot
        catagorizePosition();
        snapPosition();
    }
};

} // namespace

void angleVectors(const float angles[3], float forward[3], float right[3], float up[3]) {
    constexpr double kDeg = 3.14159265358979323846 * 2 / 360;
    float angle = float(angles[YAW] * kDeg);
    const float sy = std::sin(angle), cy = std::cos(angle);
    angle = float(angles[PITCH] * kDeg);
    const float sp = std::sin(angle), cp = std::cos(angle);
    angle = float(angles[ROLL] * kDeg);
    const float sr = std::sin(angle), cr = std::cos(angle);
    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (right) {
        right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
        right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
        right[2] = -1 * sr * cp;
    }
    if (up) {
        up[0] = (cr * sp * cy + -sr * -sy);
        up[1] = (cr * sp * sy + -sr * cy);
        up[2] = cr * cp;
    }
}

void Pmove(q2pmove_t *pm, const PmoveParams &params) {
    if (!pm || !pm->trace || !pm->pointcontents) return;
    Mover m;
    m.pm = pm;
    m.p = params;
    m.run();
}

} // namespace realms::q2
