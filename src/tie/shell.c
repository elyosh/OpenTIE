// FLAGS: TIE95 -od
#include "tie/shell.h"
#include "tie/armship.h"
#include "tie/blueprnt.h"
#include "tie/brief.h"
#include "tie/combat.h"
#include "tie/credits.h"
#include "tie/debrief.h"
#include "tie/edition.h"
#include "tie/filmview.h"
#include "tie/frontend_display_tie98.h"
#include "tie/mainmenu.h"
#include "tie/map.h"
#include "tie/play1.h"
#include "tie/register.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/talk.h"
#include "tie/tie.h"
#include "tie/tielogo.h"
#include "tie/title.h"
#include "tie/tourdesk.h"
#include "tie/train.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/imuse_session.h"
#ifdef TIE_MODERN
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/runtime/armship_task.h"
#include "tie_runtime/runtime/blueprint_task.h"
#include "tie_runtime/runtime/brief_task.h"
#include "tie_runtime/runtime/combat_task.h"
#include "tie_runtime/runtime/credits_task.h"
#include "tie_runtime/runtime/debrief_task.h"
#include "tie_runtime/runtime/film_task.h"
#include "tie_runtime/runtime/filmview_task.h"
#include "tie_runtime/runtime/flight_task.h"
#include "tie_runtime/runtime/mainmenu_task.h"
#include "tie_runtime/runtime/map_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/register_task.h"
#include "tie_runtime/runtime/shell_task.h"
#include "tie_runtime/runtime/talk_task.h"
#include "tie_runtime/runtime/tielogo_task.h"
#include "tie_runtime/runtime/title_task.h"
#include "tie_runtime/runtime/tourdesk_task.h"
#include "tie_runtime/runtime/train_task.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "landru/canvas.h"
#include "landru/error.h"
#include "landru/pal.h"
#include "landru/stream.h"

#include "tie/gamesnd.h"

#ifdef TIE_MODERN
#include "tie_runtime/diagnostics/diagnostics.h"
#endif

#ifdef TIE_MODERN
#include <landru/task.h>
#endif
#include <imuse/lolevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLOBAL: TIE98 0x5F3464
SceneHeadStruct* sHead_gbl;
// GLOBAL: TIE95 0xF56FC
int16_t digital_exists;

#ifndef TIE_MODERN
// GLOBAL: TIE95 0xF5700
// GLOBAL: TIE98 0x5F3460
uint8_t install_cfg_mode;
#endif

// FUNCTION: TIE95 0x67EFA
void shell_programexit(const char* str) {
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
#ifdef TIE_MODERN
	TieDiagnostics_Log(TIE_LOG_ERROR, "%s", str);
	TieDiagnostics_Fatal(str);
	exit(EXIT_FAILURE);
#else
	printf(str);
	exit(0);
#endif
}

// FUNCTION: TIE95 0x672D5
// FUNCTION: TIE98 0x47EF60
int32_t shell_Shell(int32_t scene, int32_t script) {
	int16_t cur_scene;
	int16_t next_scene = 0;
	int16_t exit_flag;
	bool diff_disabled = false;
	bool owns_music = false;
#ifdef TIE_MODERN
	ShellTask* continuation = landru_task_top();
	cur_scene = continuation->cur_scene;
	next_scene = continuation->next_scene;
	exit_flag = continuation->exit_flag;
	diff_disabled = continuation->diff_disabled;
	owns_music = continuation->owns_tie98_music;
	if (!continuation->started)
#else
	SceneHeadStruct the_head;
	cur_scene = (int16_t)scene;
	exit_flag = 0;
#endif
	{
#ifdef TIE_MODERN
		sHead_gbl = &continuation->the_head;
		if (TieProfile_UsesDx5())
			g_frontendDisplayWndProcMode = TieProfile_UsesTie98Frontend() ? 1 : -1;
		frontResolution = (int16_t)TieProfile_Frontend()->vesa_mode;
#else
		sHead_gbl = &the_head;
#ifdef TIE98
		memset(&the_head, 0, sizeof the_head);
		g_frontendDisplayWndProcMode = 1;
#endif
#endif
		sHead_gbl->last_scene = 1;
		sHead_gbl->cur_scene = (int16_t)scene;
		xerror_Clear_Landru_Error();
		gamesnd_Open_Pre_iMuse();
		gamesnd_game_Set_Front_Sound();
		shellext_Open_Landru(NULL, 0, (int16_t)script);
		imuse_pause(im);
		xstream_Set_Stream_Tick_Counts();
		imuse_resume(im);
		xstream_Init_Stream_Engine(0, 2 * 1024 * 1024, 3 * 1024 * 1024 / 2);
		soundext_Prep_Sound_Scene((int16_t)scene);
#ifdef TIE_MODERN
		continuation->started = true;
		continuation->phase = SHELL_PHASE_DISPATCH;
		return 0;
#endif
	}
#ifdef TIE_MODERN
	for (;;) {
#else
	while (!exit_flag && !xerror_Is_Landru_Error()) {
#endif
		bool flight_scene = cur_scene == SCENE_FLIGHT_TRAIN || cur_scene == SCENE_FLIGHT_COMBAT ||
							cur_scene == SCENE_FLIGHT_BATTLE || cur_scene == SCENE_FILM_REPLAY;
#ifdef TIE_MODERN
		if (continuation->phase == SHELL_PHASE_DISPATCH) {
			const char* music_path;
			if (exit_flag || xerror_Is_Landru_Error())
				break;
			if (!TieShell_PrepareScene(cur_scene)) {
				continuation->finished = true;
				return exit_flag;
			}
			owns_music = false;
			if (TieMusicPolicy_UsesTie98()) {

				switch (cur_scene) {
					case SCENE_MAIN_MENU:
						music_path = "music/concourse.wav";
						break;
					case SCENE_BLUEPRINT:
						music_path = "music/tech.wav";
						break;
					case SCENE_TOUR_DESK:
					case SCENE_BRIEF_PRE:
					case SCENE_BRIEF:
					case SCENE_DEBRIEF:
						music_path = "music/battle.wav";
						break;
					case SCENE_TALK_BRIEF_OFFICER:
						music_path = "music/bridge.wav";
						break;
					case SCENE_TALK_BRIEF_PRIEST:
						music_path = "music/secret.wav";
						break;
					case SCENE_TALK_DEBRIEF_OFFICER:
						music_path = shipext_Is_Mission_Success() ? "music/phew.wav" : "music/bummer.wav";
						break;
					case SCENE_TALK_DEBRIEF_PRIEST:
						music_path = mission.secondary_global == 1 ? "music/awe.wav" : "music/evilmonk.wav";
						break;
					default:
						music_path = NULL;
						break;
				}
				if (music_path) {
					FrontendWaveStream_PlayWaveFile(music_path, 1);
					owns_music = true;
				}
			}
			continuation->owns_tie98_music = owns_music;
			if (flight_scene) {
				TieDiagnostics_Log(TIE_LOG_INFO, "[SHELL] scene %d\n", cur_scene);
				shellext_Open_Landru_Scene(cur_scene);
				continuation->phase = SHELL_PHASE_FLIGHT_BEGIN;
				return 0;
			}
		}
#else
		shellext_Open_Landru_Scene(cur_scene);
#endif
		if (flight_scene) {
#ifdef TIE_MODERN
			if (continuation->phase == SHELL_PHASE_FLIGHT_BEGIN)
#endif
			{
#ifdef TIE_MODERN
				if (cur_scene != SCENE_FILM_REPLAY && !TieProfile_ApplyPendingFlight()) {
					TieDiagnostics_Fatal("Could not prepare the selected flight engine.");
					continuation->phase = SHELL_PHASE_AWAITING;
					return 0;
				}
				if (TieProfile_UsesDx5())
					g_frontendDisplayWndProcMode = 0;
#endif
				if (cur_scene == SCENE_FILM_REPLAY) {
					gamesnd_game_Set_Flight_Sound();
					flightResolution = TIE_FLIGHT_EDITION(TIE_FLIGHT_RES_VGA, TIE_FLIGHT_RES_SVGA);
					xstream_Exit_Stream_Engine(0);
#ifdef TIE_MODERN
					TieFlightTask_Begin(1);
#else
					tie_simulator(1);
#endif
				} else {
					shipext_Mission_Enter(cur_scene);
					gamesnd_game_Set_Flight_Sound();
					xstream_Exit_Stream_Engine(0);
					transitions_on = (uint8_t)options_gbl.transition_active;
#ifdef TIE_MODERN
					TieFlightTask_Begin(0);
#else
					tie_simulator(0);
#endif
				}
#ifdef TIE_MODERN
				continuation->phase = SHELL_PHASE_AFTER_FLIGHT;
				return 0;
#endif
			}
#ifdef TIE_MODERN
			if (continuation->phase == SHELL_PHASE_AFTER_FLIGHT)
#endif
			{
#ifdef TIE_MODERN
				TieSnapshotBuilder_SetSceneKind(TIE_SCENE_FRONTEND);
				if (TieProfile_UsesDx5())
					g_frontendDisplayWndProcMode = TieProfile_UsesTie98Frontend() ? 1 : -1;
#endif
				shipext_Reset_Battle_Results();
				xstream_Init_Stream_Engine(0, 2 * 1024 * 1024, 3 * 1024 * 1024 / 2);
				xpal_Set_Screen_RGB(0, 255, 0, 0, 0);
				gamesnd_game_Set_Front_Sound();
				if (cur_scene == SCENE_FLIGHT_BATTLE) {
					shipext_Mission_Exit(cur_scene, mission.player_status);
					if (shipext_Is_Mission_Success() || !shipext_Is_Player_OK())
						shipext_Link_Pilot();
					else if (shipext_Read_Temp_Pilot())
						shipext_Update_Pilot();
					shipext_Set_Mission_Cutscenes();
					next_scene = shipext_Next_Battle_Cutscene();
				} else {
					next_scene = shipext_Mission_Exit(cur_scene, mission.player_status);
				}
				next_scene = shellext_Convert_Transition(next_scene, 0);
				soundext_Prep_Sound_Scene(next_scene);
				shellext_Set_Prefs_Sound();
#ifdef TIE_MODERN
				xerror_Set_Landru_Exit(next_scene);
				continuation->phase = SHELL_PHASE_AWAITING;
				return 0;
#endif
			}
		} else {
#ifdef TIE_MODERN
			if (continuation->phase == SHELL_PHASE_DISPATCH)
#endif
			{
				diff_disabled = false;
				switch (cur_scene) {
					case SCENE_CREDITS:
					case SCENE_CREDITS_ALT:
#ifdef TIE_MODERN
						TieCredits_Begin(sHead_gbl);
#else
						next_scene = credits_Credits(sHead_gbl);
#endif
						break;
					case SCENE_TIELOGO:
#ifdef TIE_MODERN
						TieLogo_Begin(sHead_gbl);
#else
						next_scene = tielogo_TieLogo(sHead_gbl);
#endif
						break;
					case SCENE_MAIN_MENU:
#ifdef TIE_MODERN
						TieMainMenu_Begin(sHead_gbl);
#else
						next_scene = mainmenu_Main_Menu(sHead_gbl);
#endif
						break;
					case SCENE_TITLE:
#ifdef TIE_MODERN
						TieTitle_Begin(sHead_gbl);
#else
						next_scene = title_Title(sHead_gbl);
#endif
						break;
					case SCENE_ARM_SHIP:
#ifdef TIE_MODERN
						TieArmShip_Begin(sHead_gbl);
#else
						next_scene = armship_ArmShip(sHead_gbl);
#endif
						break;
					case SCENE_BRIEF_PRE:
					case SCENE_BRIEF:
#ifdef TIE_MODERN
						TieBrief_Begin(sHead_gbl);
#else
						next_scene = brief_Brief(sHead_gbl);
#endif
						break;
					case SCENE_DEBRIEF:
#ifdef TIE_MODERN
						TieDebrief_Begin(sHead_gbl);
#else
						next_scene = debrief_Debrief(sHead_gbl);
#endif
						break;
					case SCENE_BLUEPRINT:
#ifdef TIE_MODERN
						TieBlueprint_Begin(sHead_gbl);
#else
						next_scene = blueprnt_Blueprint(sHead_gbl);
#endif
						break;
					case SCENE_TOUR_DESK:
#ifdef TIE_MODERN
						TieTourDesk_Begin(sHead_gbl);
#else
						next_scene = tourdesk_TourDesk(sHead_gbl);
#endif
						break;
					case SCENE_FILM_VIEWER:
#ifdef TIE_MODERN
						TieFilmView_Begin(sHead_gbl);
#else
						next_scene = filmview_FilmView(sHead_gbl);
#endif
						break;
					case SCENE_TRAIN_A:
					case SCENE_TRAIN_B:
#ifdef TIE_MODERN
						TieTrain_Begin(sHead_gbl);
#else
						next_scene = train_Train(sHead_gbl);
#endif
						break;
					case SCENE_COMBAT_A:
					case SCENE_COMBAT_B:
#ifdef TIE_MODERN
						TieCombat_Begin(sHead_gbl);
#else
						next_scene = combat_Combat(sHead_gbl);
#endif
						break;
					case SCENE_REGISTER:
					case SCENE_EXIT:
#ifdef TIE_MODERN
						TieRegister_Begin(sHead_gbl);
#else
						next_scene = register_Register(sHead_gbl);
#endif
						break;
					case SCENE_TALK_BRIEF_OFFICER:
					case SCENE_TALK_BRIEF_PRIEST:
					case SCENE_TALK_DEBRIEF_OFFICER:
					case SCENE_TALK_DEBRIEF_PRIEST:
#ifdef TIE_MODERN
						TieTalk_Begin(sHead_gbl);
#else
						next_scene = talk_Talk(sHead_gbl);
#endif
						break;
					case SCENE_TRAIN_MAP:
					case SCENE_COMBAT_MAP_A:
					case SCENE_COMBAT_MAP_B:
					case SCENE_COMBAT_MAP_C:
					case SCENE_COMBAT_MAP_D:
					case SCENE_COMBAT_MAP_E:
					case SCENE_BRIEF_MAP:
#ifdef TIE_MODERN
						TieMap_Begin(sHead_gbl);
#else
						next_scene = map_Map(sHead_gbl);
#endif
						break;
					case 10:
					case 20:
					case 30:
					case 31:
					case 32:
					case 40:
					case 61:
					case 70:
					case 71:
					case 72:
					case 600:
					case 601:
					case 602:
					case 603:
					case 610:
					case 620:
					case 621:
					case 622:
					case 623:
						xcanvas_Disable_Screen_Diff();
						diff_disabled = true;
#ifdef TIE_MODERN
						TieFilm_Begin(sHead_gbl);
#else
						next_scene = play1_Play1(sHead_gbl);
#endif
						break;
#ifdef TIE_MODERN
					case 5:
#endif
					case 6:
					case 7:
#ifdef TIE_MODERN
					case 19:
#endif
					case 25:
					case 50:
					case 60:
					case 120:
					case 130:
					case 170:
					case 210:
#ifdef TIE_MODERN
					case 211:
#endif
#ifdef TIE_MODERN
					case 212:
#endif
#ifdef TIE_MODERN
					case 213:
#endif
					case 231:
					case 240:
					case 250:
					case 251:
					case 252:
					case 253:
					case 254:
					case 255:
					case 256:
					case 257:
					case 258:
					case 259:
					case 260:
					case 261:
					case 262:
					case 263:
					case 270:
					case 280:
					case 281:
					case 282:
					case 283:
					case 284:
					case 285:
					case 390:
					case 400:
					case 401:
					case 402:
					case 403:
					case 404:
					case 405:
					case 406:
					case 407:
					case 408:
					case 409:
					case 410:
					case 411:
					case 420:
					case 500:
					case 510:
					case 520:
					case 530:
					case 531:
					case 540:
					case 550:
					case 560:
					case 570:
					case 571:
					case 572:
					case 573:
					case 580:
					case 581:
					case 590:
					case 591:
					case 700:
					case 710:
					case 720:
					case 730:
					case 740:
					case 900:
#ifdef TIE_MODERN
						TieFilm_Begin(sHead_gbl);
#else
						next_scene = play1_Play1(sHead_gbl);
#endif
						break;
					default:
						exit_flag = 1;
						break;
				}
#ifdef TIE_MODERN
				continuation->exit_flag = exit_flag;
				continuation->diff_disabled = diff_disabled;
				if (exit_flag)
					break;
				TieDiagnostics_Log(TIE_LOG_INFO, "[SHELL] scene %d\n", cur_scene);
				shellext_Open_Landru_Scene(cur_scene);
				continuation->phase = SHELL_PHASE_AWAITING;
				return 0;
#endif
			}
		}
#ifdef TIE_MODERN
		if (continuation->phase == SHELL_PHASE_AWAITING) {
			next_scene = xerror_Get_Landru_Exit();
			if (owns_music) {
				FrontendWaveStream_Shutdown();
				continuation->owns_tie98_music = false;
			}
			if (diff_disabled) {
				xcanvas_Enable_Screen_Diff();
				continuation->diff_disabled = false;
			}
			shellext_Close_Landru_Scene(cur_scene);
			continuation->next_scene = next_scene;
			continuation->phase = SHELL_PHASE_AFTER_SUDDEN_FADE;
			return 0;
		}
		if (continuation->phase == SHELL_PHASE_AFTER_SUDDEN_FADE) {
			xcanvas_Copy_Screen_To_Diff();
			continuation->cur_scene = shellext_Convert_Transition(next_scene, 1);
			continuation->phase = SHELL_PHASE_DISPATCH;
			return 0;
		}
#else
		if (diff_disabled)
			xcanvas_Enable_Screen_Diff();
		shellext_Close_Landru_Scene(cur_scene);
		cur_scene = shellext_Convert_Transition(next_scene, 1);
#endif
	}
#ifdef TIE_MODERN
	continuation->finished = true;
#else
	shipext_Validate_Tour_Battle();
	shipext_Delete_Temp_Pilot();
	xstream_Exit_Stream_Engine(0);
	shellext_Close_Landru(0);
	gamesnd_Close_Pre_iMuse();
#endif
	return exit_flag;
}
