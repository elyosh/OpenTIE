#include "tie/tie_render_tie98.h"

#include "anim.h"
#include "tie/backdrp2.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/flight_composite_tie98.h"
#include "tie/flight_surface_tie98.h"
#include "tie/fview.h"
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/panel.h"
#include "tie/render_list_tie98.h"
#include "tie/render_scene_tie98.h"
#include "tie/replay.h"
#include "tie/rtsvga2.h"
#include "tie/static.h"
#include "tie/std3d_tie98.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/xtimer.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/flight_timing.h"

#include <landru/vesa.h>

// GLOBAL: TIE98 0x592204
uint16_t g_flightInitialTextureCacheFlushPending;

// FUNCTION: TIE98 0x48EF70 TIE_getobjecteyexyz
static int32_t tie_getobjecteyexyz_tie98(uint16_t object_index) {
	FlightObject* object = &objects[object_index];
	worldx = object->world_x - camera.x;
	worldy = object->world_y - camera.y;
	worldz = object->world_z - camera.z;
	if (object_index < NUM_CRAFTS) {
		craftptr = object->craft_ptr;
		craftptr->eye_x_cache = transfm2_geteyex(worldx, worldy, worldz);
		objecteyex = craftptr->eye_x_cache;
		craftptr->eye_y_cache = transfm2_geteyey(worldx, worldy, worldz);
		objecteyey = craftptr->eye_y_cache;
		craftptr->eye_z_cache = transfm2_geteyez(worldx, worldy, worldz);
		objecteyez = craftptr->eye_z_cache;
	} else {
		objecteyex = transfm2_geteyex(worldx, worldy, worldz);
		objecteyey = transfm2_geteyey(worldx, worldy, worldz);
		objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	}
	return objecteyez;
}

// FUNCTION: TIE98 0x48F0C0 TIE_Get_Static_Object_Eye_Position
static int tie_getstaticobjecteyeposition(uint16_t static_object_index) {
	StaticObject* object = &staticobjects[static_object_index];
	worldx = ((int32_t)object->world_x << 8) - camera.x;
	worldy = ((int32_t)object->world_y << 8) - camera.y;
	worldz = ((int32_t)object->world_z << 8) - camera.z;
	objecteyex = transfm2_geteyex(worldx, worldy, worldz);
	objecteyey = transfm2_geteyey(worldx, worldy, worldz);
	objecteyez = transfm2_geteyez(worldx, worldy, worldz);
	return objecteyez;
}

// FUNCTION: TIE98 0x48DF40 TIE_Update_Screen
void tie_updatescreen_tie98(void) {
	if (replayviewmode) {
		replay_calcreplayview();
	} else if (camera.view_target_obj == 0xFFFFu) {
		if (!hyperspaceflag) {
			trig2_ctop(pstate.player->world_x - camera.x, pstate.player->world_y - camera.y,
					   pstate.player->world_z - camera.z);
			camera.roll = 0;
			camera.cam_heading = trig2_zangle;
			camera.cam_pitch = trig2_xyangle;
		}
		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
	} else if ((camera.view_zoom_flag && camera.view_heading_offset == 0) ||
			   (camera.view_heading_offset != 0 && camera.view_pitch_offset != 0)) {
		TieChaseCamera_Update();
		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, 0, (int16_t)camera.side_angle,
						  (int16_t)camera.up_angle, NULL);
		if (hyperspaceflag == 4 || hyperspaceflag == 6) {
			camera.z = 0;
			camera.y = 0;
			camera.x = 0;
		} else {
			create_getworldposition(camera.view_target_obj, 0);
			camera.x = worldlocx;
			camera.y = worldlocy;
			camera.z = worldlocz;
		}
		camera.x -= (worldeyeA3 * camera.view_zoom) >> 15;
		camera.y -= (worldeyeB3 * camera.view_zoom) >> 15;
		camera.z -= (worldeyeC3 * camera.view_zoom) >> 15;
		uint8_t model_type;
		if (camera.view_target_obj >= OBJ_REF_STATIC_BASE)
			model_type = staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE].species;
		else
			model_type = objects[camera.view_target_obj].ship_idx;
		objectsize = (int16_t)species_table[model_type].bound_hwidth;
		camera.x -= (worldeyeA3 * (uint16_t)objectsize) >> 15;
		camera.y -= (worldeyeB3 * (uint16_t)objectsize) >> 15;
		camera.z -= (worldeyeC3 * (uint16_t)objectsize) >> 15;
	} else if (camera.view_heading_offset != 0) {
		panel_pointcamera_tie98(camera.view_target_obj, 0);
	} else {
		FlightObject* object = &objects[camera.view_target_obj];
		camera.roll = object->roll;
		camera.cam_heading = object->heading;
		camera.cam_pitch = object->pitch;
		fview_newcalcview(camera.roll, camera.cam_heading, camera.cam_pitch, camera.bank,
						  (int16_t)camera.side_angle, (int16_t)camera.up_angle, object);
		camera.x = object->world_x;
		camera.y = object->world_y;
		camera.z = object->world_z;
		if (camera.view_target_obj == pstate.object_idx && hyperspaceflag != 3 && hyperspaceflag != 5) {
			camera.x += pstate.laser_origin_dx;
			camera.y += pstate.laser_origin_dy;
			camera.z += pstate.laser_origin_dz;
		}
	}

	g_flightDrawToOffscreenSurface = 0;
	g_flightSurfaceAlreadyLocked = 0;
	if (g_flightInitialTextureCacheFlushPending) {
		if (g_useHardware3D)
			std3D_FlushTextureCache();
		g_flightInitialTextureCacheFlushPending = 0;
	}
	RenderScene_Initialize_tie98(1);
	numbitmaps = 0;
	if (g_useHardware3D) {
		parentobject = 0x3000;
		backdrp2_backdrop();
		if (hyperspaceflag != 3 && hyperspaceflag != 5) {
			FlightSurface_Lock();
			rtsvga2_drawstars_tie98();
			FlightSurface_Unlock();
		}
	}

	RenderList_Reset();
	for (int16_t object_index = 0; object_index < NUM_OBJECTS; ++object_index) {
		if (object_index == DEBRIS_FIRST_SLOT && !(drawdebrisflag && mission.train_craft_type == 0))
			break;
		if (object_index == (int16_t)camera.view_target_obj && !camera.view_zoom_flag && !replayviewmode)
			continue;
		FlightObject* object = &objects[object_index];
		if (object->ship_idx == 0)
			continue;
		objectsize = (int16_t)species_table[object->ship_idx].bound_hwidth;
		switch (object->genus) {
			case GENUS_FIGHTER:
			case GENUS_TRANSPORT:
			case GENUS_UTILITY:
			case GENUS_FREIGHTER:
			case GENUS_STARSHIP:
			case GENUS_PLATFORM:
			case GENUS_GATE:
				craftptr = object->craft_ptr;
				if (tie_checkobjecteyexyz((uint16_t)object_index, (uint16_t)objectsize))
					RenderList_QueueObject(object_index, objecteyez);
				break;
			case GENUS_PROJECTILE_PLAYER:
			case GENUS_PROJECTILE_NPC:
			case GENUS_DEBRIS:
			case GENUS_EXPLOSION:
				if (tie_checkobjecteyexyz((uint16_t)object_index, (uint16_t)objectsize))
					RenderList_QueueObject(object_index, objecteyez);
				break;
			default:
				break;
		}
	}

	for (int16_t static_index = 0; static_index < NUM_STATIC_OBJECTS; ++static_index) {
		StaticObject* object = &staticobjects[static_index];
		if (hyperspaceflag == 3 || hyperspaceflag == 5) {
			if (static_index < hyperspacedetail) {
				const int16_t original_x = object->world_x;
				const int16_t original_y = object->world_y;
				const int16_t original_z = object->world_z;
				objectsize = -1;
				tie_checkstaticobjecteyexyz(original_x, original_y, original_z, 0xFFFFu);
				draw_drawhyperstar_tie98(static_index);

				object->world_z = (int16_t)-original_z;
				tie_checkstaticobjecteyexyz(original_x, original_y, object->world_z, (uint16_t)objectsize);
				draw_drawhyperstar_tie98(static_index);
				object->world_z = original_z;

				if (static_index < hyperspacedetail / 2) {
					const int16_t mirrored_x = (int16_t)(-(int32_t)original_x >> 1);
					const int16_t mirrored_z = (int16_t)(-(int32_t)original_z >> 1);
					object->world_x = mirrored_x;
					object->world_z = mirrored_z;
					tie_checkstaticobjecteyexyz(mirrored_x, original_y, mirrored_z, (uint16_t)objectsize);
					draw_drawhyperstar_tie98(static_index);

					object->world_x = (int16_t)(mirrored_x >> 1);
					object->world_z = (int16_t)(-(int32_t)mirrored_z >> 1);
					tie_checkstaticobjecteyexyz(object->world_x, original_y, object->world_z,
												(uint16_t)objectsize);
					draw_drawhyperstar_tie98(static_index);
					object->world_x = original_x;
					object->world_z = original_z;
				}
			}
			continue;
		}
		if (object->species == 0 || object->ship_class < 8 || object->ship_class > 11)
			continue;
		objectsize = (int16_t)species_table[object->species].bound_hwidth;
		if (!tie_checkstaticobjecteyexyz(object->world_x, object->world_y, object->world_z,
										 (uint16_t)objectsize))
			continue;
		if (object->species >= 100 && object->species <= 105 &&
			(!TieFlightTiming_IsHighRate() || TieFlightTiming_LegacyDue())) {
			const uint16_t rotation_ticks =
				TieFlightTiming_IsHighRate() ? TieFlightTiming_CompatibilityTicks() : frameticks;
			object->roll_byte += rotation_ticks * (static_index >> 4) / 16;
			object->yaw_byte += rotation_ticks * (static_index >> 3) / 32;
			object->pitch_byte += rotation_ticks * (4 - (static_index >> 4)) / 16;
		}
		fview_newcalcrotate((int16_t)((uint16_t)object->roll_byte << 8),
							(int16_t)((uint16_t)object->yaw_byte << 8),
							(int16_t)((uint16_t)object->pitch_byte << 8), 0, NULL);
		RenderList_QueueObject(static_index + OBJ_REF_STATIC_BASE, objecteyez);
		localLightCnt = 0;
	}

	RenderList_SortDepthAscending();
	for (RenderObjectListEntryTIE98* entry = g_renderListHead; entry != NULL; entry = entry->next) {
		if (entry->objectIdx < NUM_OBJECTS) {
			const uint16_t object_index = (uint16_t)entry->objectIdx;
			FlightObject* object = &objects[object_index];
			switch (object->genus) {
				case GENUS_FIGHTER:
				case GENUS_TRANSPORT:
				case GENUS_UTILITY:
				case GENUS_FREIGHTER:
				case GENUS_STARSHIP:
				case GENUS_PLATFORM:
				case GENUS_GATE:
					craftptr = object->craft_ptr;
					tie_getobjecteyexyz_tie98(object_index);
					if (object->genus == GENUS_GATE)
						lightflag = 0;
					fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
					if (object->genus == GENUS_GATE) {
						gate_drawtraininggate_tie98(object_index);
					} else {
						tie_makelocallights_tie98(object);
						draw_process_object_components_tie98(object_index);
						FlightModel_Draw_Object(object);
						localLightCnt = 0;
					}
					lightflag = 1;
					break;
				case GENUS_PROJECTILE_PLAYER:
				case GENUS_PROJECTILE_NPC:
					tie_getobjecteyexyz_tie98(object_index);
					fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
					draw_drawlaser_tie98(object_index);
					break;
				case GENUS_DEBRIS:
				case GENUS_EXPLOSION:
					tie_getobjecteyexyz_tie98(object_index);
					fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
					anim_drawverysimpleobject_tie98(object_index);
					break;
				default:
					break;
			}
		} else {
			const uint16_t static_index = (uint16_t)(entry->objectIdx - OBJ_REF_STATIC_BASE);
			StaticObject* object = &staticobjects[static_index];
			if (object->ship_class >= 8 && object->ship_class <= 11) {
				tie_getstaticobjecteyeposition(static_index);
				fview_newcalcrotate((int16_t)((uint16_t)object->roll_byte << 8),
									(int16_t)((uint16_t)object->yaw_byte << 8),
									(int16_t)((uint16_t)object->pitch_byte << 8), 0, NULL);
				static_drawstaticobject_tie98(static_index);
				localLightCnt = 0;
			}
		}
	}

	if (!g_useHardware3D) {
		parentobject = 0x3000;
		backdrp2_backdrop();
		FlightSurface_Lock();
		if (hyperspaceflag != 3 && hyperspaceflag != 5)
			rtsvga2_drawstars_tie98();
		FlightSurface_Unlock();
	}
	g_drawSceneEffects = 1;
	RenderScene_DrawVisibleFaces();
	g_drawSceneEffects = 0;
	if (!g_useHardware3D) {
		anim_sort_and_draw_bitmaps_tie98(1);
		numbitmaps = 0;
	}

	dxtticks = 0;
	oxtticks = 0;
	tickcounter += (uint16_t)xtimer_time_elapsed();
	dxtticks = tickcounter;
	RenderScene_UnlockSceneBuffers_tie98();
	if (g_useHardware3D)
		Renderer_CopyDirtyRectsToHardwareSurface();
	if (!replayviewmode)
		PANEL_Update3DCrtIfVisible();
	deepspacecolor = 0;
	const uint16_t final_draw_ticks = (uint16_t)xtimer_time_elapsed();
	g_flightDrawToOffscreenSurface = 1;
	tickcounter += final_draw_ticks;
	deepspacecolor = (uint8_t)-5;
	dxtticks = (uint16_t)(tickcounter - dxtticks);
	vesa_dirty_gbl = true;
}
