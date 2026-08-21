/*
 * Live snapshot inspector for the flight overlay. Shows the main
 * camera state (pos, quaternion, decoded basis, FOV, viewport),
 * skybox sample directions for the screen corners, motion tracking,
 * PIP camera, and the player craft + first 8 flight craft.
 *
 * "Dump to stderr" emits the same report as a flat-text block so it
 * can be pasted into a log. "Reset motion reference" snapshots the
 * current camera position so cumulative-Δ becomes a measurement from
 * a known zero (useful for thrust-direction tests).
 */

#include <imgui.h>

#include <cmath>
#include <cstdio>

extern "C" {
#include "aeron/asset/opt_model.h"
#include "tie_runtime/hooks/orientation.h"
#include "tie_runtime/snapshot/snapshot.h"
}

namespace {

/* ---- math helpers ---- */

void TieRenderMath_QuaternionToMat3(const float q[4], float m[9])
{
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    const float xx = x*x, yy = y*y, zz = z*z;
    const float xy = x*y, xz = x*z, yz = y*z;
    const float wx = w*x, wy = w*y, wz = w*z;
    m[0] = 1.0f - 2.0f*(yy + zz);  m[1] = 2.0f*(xy - wz);         m[2] = 2.0f*(xz + wy);
    m[3] = 2.0f*(xy + wz);         m[4] = 1.0f - 2.0f*(xx + zz);  m[5] = 2.0f*(yz - wx);
    m[6] = 2.0f*(xz - wy);         m[7] = 2.0f*(yz + wx);         m[8] = 1.0f - 2.0f*(xx + yy);
}

float TieCameraInspector_Determinant3(const float m[9])
{
    return m[0]*(m[4]*m[8] - m[5]*m[7])
         - m[1]*(m[3]*m[8] - m[5]*m[6])
         + m[2]*(m[3]*m[7] - m[4]*m[6]);
}

void TieCameraInspector_Normalize3(float v[3])
{
    float n = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (n > 1e-9f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}

/* R^T × eye  →  world. Given W (= R, world→eye) as a row-major 3×3,
 * R^T columns = R rows, so we apply W transposed by indexing. */
void TieCameraInspector_EyeToWorld(const float W[9], const float eye[3], float world[3])
{
    world[0] = W[0]*eye[0] + W[3]*eye[1] + W[6]*eye[2];
    world[1] = W[1]*eye[0] + W[4]*eye[1] + W[7]*eye[2];
    world[2] = W[2]*eye[0] + W[5]*eye[1] + W[8]*eye[2];
}

/* S × world → cube using the renderer's basis swap:
 *     | 1  0  0 |    cube.x =  world.x
 *     | 0  0  1 |    cube.y =  world.z
 *     | 0 -1  0 |    cube.z = -world.y                                */
void TieCameraInspector_ApplyS(const float w[3], float c[3])
{
    c[0] =  w[0];
    c[1] =  w[2];
    c[2] = -w[1];
}

const char *TieCameraInspector_CubeFaceName(const float d[3])
{
    float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
    if (ax >= ay && ax >= az) return d[0] > 0 ? "+X" : "-X";
    if (ay >= az)             return d[1] > 0 ? "+Y" : "-Y";
    return d[2] > 0 ? "+Z" : "-Z";
}

const char *TieCameraInspector_PilotViewName(uint8_t v)
{
    switch (v) {
    case  0: return "full cockpit";
    case 17: return "title strip";
    case 18: return "panel hidden";
    case 19: return "forward";
    case 20: return "threat";
    default: return "padlock/other";
    }
}

const char *TieCameraInspector_GenusName(uint8_t g)
{
    switch (g) {
    case TIE_GENUS_FIGHTER:      return "FIGHTER";
    case TIE_GENUS_TRANSPORT:    return "TRANSPORT";
    case TIE_GENUS_UTILITY:      return "UTILITY";
    case TIE_GENUS_FREIGHTER:    return "FREIGHTER";
    case TIE_GENUS_STARSHIP:     return "STARSHIP";
    case TIE_GENUS_PLATFORM:     return "PLATFORM";
    case TIE_GENUS_PROJECTILE_NPC:        return "LASER";
    case TIE_GENUS_PROJECTILE_PLAYER:      return "MISSILE";
    case TIE_GENUS_MINE:         return "MINE";
    case TIE_GENUS_DEBRIS:       return "DEBRIS";
    case TIE_GENUS_EXPLOSION:    return "EXPLOSION";
    case TIE_GENUS_GATE:         return "GATE";
    default:                     return "?";
    }
}

/* ---- motion-reference state ----
 *
 * Camera position over time gives the player an unambiguous "this is
 * which world direction I'm facing" reference: in cockpit view the
 * camera position equals the craft position, so a forward thrust moves
 * the camera along the craft's forward axis in world coords. We keep
 * a small history so the displayed velocity is averaged over ~half a
 * second and isn't dominated by per-tick jitter.
 *
 * All state is file-static — the tool lives across frames and is
 * cheap enough that we update unconditionally while the window is
 * open. Reset on first sample, on tick wrap, or on user request. */

constexpr int    MOTION_HISTORY = 16;
constexpr int    MOTION_HISTORY_MASK = MOTION_HISTORY - 1;
static_assert((MOTION_HISTORY & MOTION_HISTORY_MASK) == 0,
              "MOTION_HISTORY must be a power of two");

struct MotionSample {
    uint64_t tick;
    int32_t  world_pos[3];
};

struct MotionState {
    MotionSample history[MOTION_HISTORY] = {};
    int          head            = 0;       /* next write index */
    int          count           = 0;       /* valid samples (≤ MOTION_HISTORY) */
    uint64_t     last_tick       = UINT64_MAX;
    bool         have_reference  = false;
    int32_t      ref_world_pos[3] = {};
};

MotionState g_motion;

void TieCameraInspector_ResetMotion(const TieSnapshot *snap)
{
    g_motion.count          = 0;
    g_motion.head           = 0;
    g_motion.last_tick      = UINT64_MAX;
    g_motion.have_reference = true;
    g_motion.ref_world_pos[0] = snap->camera.world_pos[0];
    g_motion.ref_world_pos[1] = snap->camera.world_pos[1];
    g_motion.ref_world_pos[2] = snap->camera.world_pos[2];
}

void TieCameraInspector_UpdateMotion(const TieSnapshot *snap)
{
    if (snap->tick == g_motion.last_tick) return;     /* same-tick re-draw */

    if (!g_motion.have_reference) {
        g_motion.have_reference = true;
        g_motion.ref_world_pos[0] = snap->camera.world_pos[0];
        g_motion.ref_world_pos[1] = snap->camera.world_pos[1];
        g_motion.ref_world_pos[2] = snap->camera.world_pos[2];
    }

    MotionSample &s = g_motion.history[g_motion.head];
    s.tick = snap->tick;
    s.world_pos[0] = snap->camera.world_pos[0];
    s.world_pos[1] = snap->camera.world_pos[1];
    s.world_pos[2] = snap->camera.world_pos[2];

    g_motion.head = (g_motion.head + 1) & MOTION_HISTORY_MASK;
    if (g_motion.count < MOTION_HISTORY) ++g_motion.count;
    g_motion.last_tick = snap->tick;
}

/* Oldest sample currently in the ring, or nullptr if empty. */
const MotionSample *TieCameraInspector_OldestMotion()
{
    if (g_motion.count == 0) return nullptr;
    int idx = (g_motion.head - g_motion.count) & MOTION_HISTORY_MASK;
    return &g_motion.history[idx];
}

const MotionSample *TieCameraInspector_NewestMotion()
{
    if (g_motion.count == 0) return nullptr;
    int idx = (g_motion.head - 1) & MOTION_HISTORY_MASK;
    return &g_motion.history[idx];
}

/* ---- shared report builder ----
 *
 * Both the ImGui window and the stderr dump call into TieCameraInspector_EmitReport() with
 * a `printer` callback that knows where the formatted line should go.
 * Keeps the layout identical and avoids drift between the two outputs. */

struct ProbeRow {
    const char *label;
    float eye[3];
};

using Printer = void (*)(void *, const char *);

void TieCameraInspector_Emit(Printer p, void *u, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    p(u, buf);
}

void TieCameraInspector_EmitReport(Printer p, void *u, const TieSnapshot *snap)
{
    constexpr float RAD2DEG = 57.29577951f;

    TieCameraInspector_Emit(p, u, "── snapshot ─────────────────────────────────────────");
    TieCameraInspector_Emit(p, u,
         "tick=%llu scene_kind=%d replay_mode=%u classic=%ux%u",
         (unsigned long long)snap->tick, (int)snap->scene_kind,
         snap->replay_mode, snap->classic_w, snap->classic_h);

    const TieCameraState &cam = snap->camera;
    float W[9];
    TieRenderMath_QuaternionToMat3(cam.ori, W);

    TieCameraInspector_Emit(p, u, "── main camera ──────────────────────────────────────");
    TieCameraInspector_Emit(p, u, "absolute native = [%d, %d, %d]",
         cam.world_pos[0], cam.world_pos[1], cam.world_pos[2]);
    TieCameraInspector_Emit(p, u, "scene-local native = [0, 0, 0]");
    TieCameraInspector_Emit(p, u, "quat (wxyz) = [% .6f, % .6f, % .6f, % .6f]",
         cam.ori[0], cam.ori[1], cam.ori[2], cam.ori[3]);
    TieCameraInspector_Emit(p, u, "pilotview=%u (%s)  zoom=%s  zoom_raw=%d  target_obj_slot=0x%04X",
         cam.pilotview, TieCameraInspector_PilotViewName(cam.pilotview),
         cam.zoom_active ? "on" : "off",
         cam.view_zoom_raw, cam.target_obj_slot);
    TieCameraInspector_Emit(p, u, "FOV  h_half=%.5f rad (h_full=%.2f°)  v_half=%.5f rad (v_full=%.2f°)",
         cam.fov_h_half_rad, cam.fov_h_half_rad * 2 * RAD2DEG,
         cam.fov_v_half_rad, cam.fov_v_half_rad * 2 * RAD2DEG);
    TieCameraInspector_Emit(p, u, "near=1 native (%.6f m), reversed-Z infinite far; screen_y_offset_ndc=%+.5f",
         AERON_OPT_METERS_PER_UNIT, cam.screen_y_offset_ndc);
    TieCameraInspector_Emit(p, u, "Viewport frac  x=%.4f y=%.4f w=%.4f h=%.4f",
         cam.viewport_frac_x, cam.viewport_frac_y,
         cam.viewport_frac_w, cam.viewport_frac_h);
    TieCameraInspector_Emit(p, u, "W = world→eye rotation 3×3 (rows = eye axes in world coords); det(W)=%+.4f",
         TieCameraInspector_Determinant3(W));
    TieCameraInspector_Emit(p, u, "  W row 0 (eye-X in world): [% 7.4f, % 7.4f, % 7.4f]", W[0], W[1], W[2]);
    TieCameraInspector_Emit(p, u, "  W row 1 (eye-Y in world): [% 7.4f, % 7.4f, % 7.4f]", W[3], W[4], W[5]);
    TieCameraInspector_Emit(p, u, "  W row 2 (eye-Z in world): [% 7.4f, % 7.4f, % 7.4f]", W[6], W[7], W[8]);

    TieCameraInspector_Emit(p, u, "── skybox sample probes ─────────────────────────────");
    TieCameraInspector_Emit(p, u,
         "cube_dir = S · R^T · eye_ray   (S is the engine→cube swap; see flight/passes.c)");

    static const ProbeRow probes[] = {
        { "screen-center", {  0.0f,  0.0f, 1.0f } },
        { "screen-top",    {  0.0f, -1.0f, 1.0f } },
        { "screen-bottom", {  0.0f, +1.0f, 1.0f } },
        { "screen-left",   { -1.0f,  0.0f, 1.0f } },
        { "screen-right",  { +1.0f,  0.0f, 1.0f } },
    };
    for (const ProbeRow &row : probes) {
        float w[3], wn[3], c[3], cn[3];
        TieCameraInspector_EyeToWorld(W, row.eye, w);
        wn[0] = w[0]; wn[1] = w[1]; wn[2] = w[2]; TieCameraInspector_Normalize3(wn);
        TieCameraInspector_ApplyS(wn, c);
        cn[0] = c[0]; cn[1] = c[1]; cn[2] = c[2]; TieCameraInspector_Normalize3(cn);
        TieCameraInspector_Emit(p, u,
             "  %-13s eye=[% .2f,% .2f,% .2f] world=[% .3f,% .3f,% .3f] cube=[% .3f,% .3f,% .3f] → face %s",
             row.label,
             row.eye[0], row.eye[1], row.eye[2],
             wn[0], wn[1], wn[2],
             cn[0], cn[1], cn[2],
             TieCameraInspector_CubeFaceName(cn));
    }
    TieCameraInspector_Emit(p, u, "Static world-axis probes (no R^T applied — direct S input):");
    static const struct { const char *name; float dir[3]; } world_probes[] = {
        { "world +X", { +1, 0, 0 } }, { "world -X", { -1, 0, 0 } },
        { "world +Y", { 0, +1, 0 } }, { "world -Y", { 0, -1, 0 } },
        { "world +Z", { 0, 0, +1 } }, { "world -Z", { 0, 0, -1 } },
    };
    for (auto &wp : world_probes) {
        float c[3];
        TieCameraInspector_ApplyS(wp.dir, c);
        TieCameraInspector_Emit(p, u, "  %-9s → cube=[% .0f, % .0f, % .0f] → face %s",
             wp.name, c[0], c[1], c[2], TieCameraInspector_CubeFaceName(c));
    }

    const TieCockpitState &ck = snap->cockpit;
    TieCameraInspector_Emit(p, u, "── motion reference (camera world position over time) ");
    const MotionSample *oldest = TieCameraInspector_OldestMotion();
    const MotionSample *newest = TieCameraInspector_NewestMotion();
    if (oldest && newest && oldest->tick != newest->tick) {
        float dx = (float)((int64_t)newest->world_pos[0] - oldest->world_pos[0]) * AERON_OPT_METERS_PER_UNIT;
        float dy = (float)((int64_t)newest->world_pos[1] - oldest->world_pos[1]) * AERON_OPT_METERS_PER_UNIT;
        float dz = (float)((int64_t)newest->world_pos[2] - oldest->world_pos[2]) * AERON_OPT_METERS_PER_UNIT;
        uint64_t ticks = newest->tick - oldest->tick;
        float vx = dx / (float)ticks;
        float vy = dy / (float)ticks;
        float vz = dz / (float)ticks;
        float speed = sqrtf(vx*vx + vy*vy + vz*vz);
        float nx = 0, ny = 0, nz = 0;
        if (speed > 1e-6f) { nx = vx / speed; ny = vy / speed; nz = vz / speed; }
        TieCameraInspector_Emit(p, u,
             "history span: %llu ticks   recent Δpos: [% .4f, % .4f, % .4f] m",
             (unsigned long long)ticks, dx, dy, dz);
        TieCameraInspector_Emit(p, u,
             "avg velocity: [% .5f, % .5f, % .5f] m/tick   |v| = %.5f",
             vx, vy, vz, speed);
        TieCameraInspector_Emit(p, u,
             "motion direction (unit, world): [% 6.3f, % 6.3f, % 6.3f]",
             nx, ny, nz);
        if (speed > 1e-6f) {
            float n_world[3] = { nx, ny, nz };
            float c[3];
            TieCameraInspector_ApplyS(n_world, c);
            TieCameraInspector_Emit(p, u,
                 "motion direction → S × world = cube [% 6.3f, % 6.3f, % 6.3f] → face %s",
                 c[0], c[1], c[2], TieCameraInspector_CubeFaceName(c));
        } else {
            TieCameraInspector_Emit(p, u, "(stationary — apply forward thrust to populate motion.)");
        }
    } else {
        TieCameraInspector_Emit(p, u, "(not enough samples yet — re-open this window after a few ticks)");
    }
    if (g_motion.have_reference) {
        float rdx = (float)((int64_t)snap->camera.world_pos[0] - g_motion.ref_world_pos[0]) * AERON_OPT_METERS_PER_UNIT;
        float rdy = (float)((int64_t)snap->camera.world_pos[1] - g_motion.ref_world_pos[1]) * AERON_OPT_METERS_PER_UNIT;
        float rdz = (float)((int64_t)snap->camera.world_pos[2] - g_motion.ref_world_pos[2]) * AERON_OPT_METERS_PER_UNIT;
        TieCameraInspector_Emit(p, u,
             "cumulative Δ from reference: [% .4f, % .4f, % .4f] m  |Δ|=%.4f",
             rdx, rdy, rdz, sqrtf(rdx*rdx + rdy*rdy + rdz*rdz));
    }

    TieCameraInspector_Emit(p, u, "── PIP camera ───────────────────────────────────────");
    TieCameraInspector_Emit(p, u, "present=%s target_slot=0x%04X subsys=0x%02X mask=%u  rect x=%u y=%u w=%u h=%u",
         ck.pip_target_present ? "yes" : "no",
         ck.pip_target_slot, ck.pip_subsys_idx, ck.mask_variant,
         ck.pip_x, ck.pip_y, ck.pip_w, ck.pip_h);
    if (ck.pip_target_present) {
        double bx = ck.pip_back_step[0], by = ck.pip_back_step[1], bz = ck.pip_back_step[2];
        double bm = sqrt(bx*bx + by*by + bz*bz);
        TieCameraInspector_Emit(p, u, "quat (wxyz) = [% .6f, % .6f, % .6f, % .6f]",
             ck.pip_cam_ori[0], ck.pip_cam_ori[1],
             ck.pip_cam_ori[2], ck.pip_cam_ori[3]);
        TieCameraInspector_Emit(p, u, "back_step native = [%.0f, %.0f, %.0f]  |back_step|=%.1f (%.3f m)",
             bx, by, bz, bm, bm * AERON_OPT_METERS_PER_UNIT);
        TieCameraInspector_Emit(p, u, "FOV  h_half=%.5f rad (%.2f°)  v_half=%.5f rad (%.2f°)",
             ck.pip_fov_h_half_rad, ck.pip_fov_h_half_rad * 2 * RAD2DEG,
             ck.pip_fov_v_half_rad, ck.pip_fov_v_half_rad * 2 * RAD2DEG);
    }

    /* Player craft. Identified by id == 0: create_createmission()
     * starts idnumber counting at 0 and assigns the player first, so
     * the player's TieFlightObjectState.id is 0. Snapshot TieCameraInspector_Emit walks
     * objects[0..N] in slot order, and since the player typically
     * occupies slot 0 this lands in flights[0]; we walk the array
     * just in case the mission spawned a non-player craft into slot 0
     * first. See tie.c:3275-3279 for the rationale. */
    const TieFlightObjectState *player = nullptr;
    for (uint16_t i = 0; i < snap->flight_count; ++i) {
        if (snap->flights[i].id == 0) { player = &snap->flights[i]; break; }
    }
    TieCameraInspector_Emit(p, u, "── player craft (id == 0) ───────────────────────────");
    if (!player) {
        TieCameraInspector_Emit(p, u, "(no craft with id==0 in snapshot — front-end / pre-mission?)");
    } else {
        float Rp[9];
        TieRenderMath_QuaternionToMat3(player->ori, Rp);
        /* Stored basis columns are (side, fwd_stored, up). Engine
         * vertex math negates column 1; the player's PHYSICAL forward
         * direction in world is `fwd_stored` itself (= -calcf at
         * fview_calcrotateorient time, which is the +craft-fwd the
         * engine moves the craft along — see craftmove = -calcf in
         * fview.c:125-127). */
        float side[3] = { Rp[0], Rp[3], Rp[6] };
        float fwd[3]  = { Rp[1], Rp[4], Rp[7] };
        float up[3]   = { Rp[2], Rp[5], Rp[8] };
        TieCameraInspector_Emit(p, u, "slot=0x%04X genus=%s ship=%u side=%u flags=0x%04X speed=%d",
             player->slot, TieCameraInspector_GenusName(player->genus),
             player->ship_idx, player->side, player->flags, player->current_speed);
        TieCameraInspector_Emit(p, u, "absolute native = [%d, %d, %d]",
             player->world_pos[0], player->world_pos[1], player->world_pos[2]);
        TieCameraInspector_Emit(p, u, "facing direction in world (= craft-fwd; engine moves the craft along this):");
        TieCameraInspector_Emit(p, u, "  fwd  = [% 6.3f, % 6.3f, % 6.3f]", fwd[0],  fwd[1],  fwd[2]);
        TieCameraInspector_Emit(p, u, "  side = [% 6.3f, % 6.3f, % 6.3f]", side[0], side[1], side[2]);
        TieCameraInspector_Emit(p, u, "  up   = [% 6.3f, % 6.3f, % 6.3f]", up[0],   up[1],   up[2]);
        /* In cockpit view this matches the screen-center skybox probe;
         * in padlock / chase / replay views it diverges from the
         * camera's eye-Z axis. The dot product line below quantifies
         * the divergence. */
        {
            float c[3];
            TieCameraInspector_ApplyS(fwd, c);
            TieCameraInspector_Emit(p, u,
                 "facing → S × world = cube [% 6.3f, % 6.3f, % 6.3f] → face %s",
                 c[0], c[1], c[2], TieCameraInspector_CubeFaceName(c));
        }
        /* Cross-check vs camera: in cockpit view these should be very
         * close. If they diverge, the camera is in padlock / chase /
         * replay (or the player's facing has slewed mid-frame). */
        {
            float W2[3] = { W[6], W[7], W[8] };       /* eye-Z axis in world */
            float dot = fwd[0]*W2[0] + fwd[1]*W2[1] + fwd[2]*W2[2];
            TieCameraInspector_Emit(p, u,
                 "dot(craft-fwd, camera-eyeZ) = %+.4f   (1.00 = cockpit view of self)",
                 dot);
        }
    }

    TieCameraInspector_Emit(p, u, "── flight craft (count=%u, showing first 8) ─────────",
         snap->flight_count);
    uint16_t n = snap->flight_count < 8 ? snap->flight_count : (uint16_t)8;
    for (uint16_t i = 0; i < n; ++i) {
        const TieFlightObjectState &fl = snap->flights[i];
        float R[9];
        TieRenderMath_QuaternionToMat3(fl.ori, R);
        TieCameraInspector_Emit(p, u, "[%u] slot=0x%04X id=%u genus=%s ship=%u side=%u flags=0x%04X",
             i, fl.slot, fl.id, TieCameraInspector_GenusName(fl.genus),
             fl.ship_idx, fl.side, fl.flags);
        TieCameraInspector_Emit(p, u, "    absolute native=[%d, %d, %d]  speed=%d  death=%d  hl=%u",
             fl.world_pos[0], fl.world_pos[1], fl.world_pos[2],
             fl.current_speed, fl.death_timer, fl.highlight);
        TieCameraInspector_Emit(p, u, "    quat=[% .4f, % .4f, % .4f, % .4f]  det(R)=%+.3f",
             fl.ori[0], fl.ori[1], fl.ori[2], fl.ori[3], TieCameraInspector_Determinant3(R));
        TieCameraInspector_Emit(p, u, "    side(col0)=[% 6.3f, % 6.3f, % 6.3f]", R[0], R[3], R[6]);
        TieCameraInspector_Emit(p, u, "    fwd (col1)=[% 6.3f, % 6.3f, % 6.3f]", R[1], R[4], R[7]);
        TieCameraInspector_Emit(p, u, "    up  (col2)=[% 6.3f, % 6.3f, % 6.3f]", R[2], R[5], R[8]);
    }
}

/* Printer that pushes one ImGui::TextUnformatted line per call. */
void TieCameraInspector_ImGuiPrinter(void *, const char *line)
{
    ImGui::TextUnformatted(line);
}

/* Printer that writes to stderr with a trailing newline. */
void TieCameraInspector_StderrPrinter(void *, const char *line)
{
    fprintf(stderr, "%s\n", line);
}

}  // namespace

extern "C" void TieCameraInspector_Init(void)
{
}
extern "C" void TieCameraInspector_Shutdown(void)              {}

extern "C" void TieCameraInspector_Draw(bool *p_open)
{
    ImGui::SetNextWindowSize(ImVec2(720, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Camera / World Inspector", p_open)) {
        ImGui::End();
        return;
    }

    bool gimbal_fix = TieOrientationHook_Enabled();
    if (ImGui::Checkbox("Gimbal-lock orientation hook", &gimbal_fix))
        TieOrientationHook_SetEnabled(gimbal_fix);
    ImGui::Separator();

    const TieSnapshot *snap = TieSnapshot_Current();
    if (!snap) {
        ImGui::TextDisabled("No snapshot available yet.");
        ImGui::End();
        return;
    }

    TieCameraInspector_UpdateMotion(snap);

    if (ImGui::Button("Dump to stderr")) {
        fprintf(stderr, "==== camera_inspector dump ====\n");
        TieCameraInspector_EmitReport(TieCameraInspector_StderrPrinter, nullptr, snap);
        fprintf(stderr, "==== end dump ====\n");
        fflush(stderr);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset motion reference")) {
        TieCameraInspector_ResetMotion(snap);
    }
    ImGui::Separator();

    TieCameraInspector_EmitReport(TieCameraInspector_ImGuiPrinter, nullptr, snap);

    ImGui::End();
}
