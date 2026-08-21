#include "tie_app/setup/setup.h"

#include "tie_app/midi_resources.h"
#include "tie_app/setup/installation_ui.h"
#include "tie_app/ui.h"

#include "aeron/aeron.h"
#include "aeron/scene/ui_file_picker.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A .tfr stores matching current and backup 1928-byte pilot records. */
enum { TIE_SETUP_PILOT_FILE_SIZE = 3856 };

typedef enum TieSetupPreset {
	TIE_SETUP_PRESET_RECOMMENDED,
	TIE_SETUP_PRESET_TIE95,
	TIE_SETUP_PRESET_TIE98,
	TIE_SETUP_PRESET_NONE,
} TieSetupPreset;

typedef enum TieSetupFrameResult {
	TIE_SETUP_FRAME_PENDING,
	TIE_SETUP_FRAME_SUCCESS,
	TIE_SETUP_FRAME_CANCELLED,
} TieSetupFrameResult;

typedef enum TieSetupPickerTarget {
	TIE_SETUP_PICKER_TIE95,
	TIE_SETUP_PICKER_TIE98,
	TIE_SETUP_PICKER_SC55,
} TieSetupPickerTarget;

typedef struct TieSetupPresetDesc {
	const char* label;
	TieVersionSelection frontend;
	TieVersionSelection flight;
	TieMusicSource music;
	bool require_tie95;
	bool require_tie98;
} TieSetupPresetDesc;

static const TieSetupPresetDesc setup_presets[] = {
	[TIE_SETUP_PRESET_RECOMMENDED] = {
		.label = "Recommended",
		.frontend = TIE_VERSION_SELECTION_TIE95,
		.flight = TIE_VERSION_SELECTION_TIE98,
		.music = TIE_MUSIC_IMUSE,
		.require_tie95 = true,
		.require_tie98 = true,
	},
	[TIE_SETUP_PRESET_TIE95] = {
		.label = "TIE Fighter - Collector's CD-ROM (1995)",
		.frontend = TIE_VERSION_SELECTION_TIE95,
		.flight = TIE_VERSION_SELECTION_TIE95,
		.music = TIE_MUSIC_IMUSE,
		.require_tie95 = true,
	},
	[TIE_SETUP_PRESET_TIE98] = {
		.label = "TIE Fighter 1998",
		.frontend = TIE_VERSION_SELECTION_TIE98,
		.flight = TIE_VERSION_SELECTION_TIE98,
		.music = TIE_MUSIC_TIE98,
		.require_tie98 = true,
	},
};

typedef struct TieSetupDialog {
	AeronVfs* application_vfs;
	TieAppConfigState* config;
	TieInstallationSet* installations;
	AeronUiFilePicker* picker;
	TieInstallation picker_installation;
	TieSetupPickerTarget picker_target;
	TieSetupPreset preset;
	bool preset_touched;
	bool tie95_from_override;
	bool tie98_from_override;
	char tie95_path[TIE_GAME_DATA_PATH_MAX];
	char tie98_path[TIE_GAME_DATA_PATH_MAX];
	char sc55_path[TIE_GAME_DATA_PATH_MAX];
	char message[768];
} TieSetupDialog;

typedef struct TieSetupPilotImport {
	AeronVfs* application_vfs;
	const TieInstallation* installation;
	const char* source_directory;
	size_t imported;
	size_t skipped;
	size_t failed;
} TieSetupPilotImport;

static bool TieSetup_PilotFilename(const char* source, char destination[13]) {
	if (!source || strchr(source, '/') || strchr(source, '\\'))
		return false;
	const char* extension = strrchr(source, '.');
	const size_t base_length = extension ? (size_t)(extension - source) : 0;
	if (base_length == 0 || base_length > 8 || strlen(extension) != 4 ||
		tolower((unsigned char)extension[1]) != 't' || tolower((unsigned char)extension[2]) != 'f' ||
		tolower((unsigned char)extension[3]) != 'r')
		return false;

	const size_t length = strlen(source);
	for (size_t index = 0; index <= length; ++index)
		destination[index] = (char)toupper((unsigned char)source[index]);
	return strcmp(destination, "__TEMP__.TFR") != 0;
}

static int TieSetup_ImportPilot(void* userdata, const AeronVfsEntry* entry) {
	TieSetupPilotImport* import = (TieSetupPilotImport*)userdata;
	char destination[13];
	if (!entry || entry->is_directory || entry->size != TIE_SETUP_PILOT_FILE_SIZE ||
		!TieSetup_PilotFilename(entry->name, destination)) {
		import->skipped++;
		return 1;
	}
	if (AeronVfs_Exists(import->application_vfs, AERON_VFS_ROOT_USER, destination)) {
		import->skipped++;
		return 1;
	}

	char source[32];
	const int source_length =
		strcmp(import->source_directory, ".") == 0
			? snprintf(source, sizeof source, "%s", entry->name)
			: snprintf(source, sizeof source, "%s/%s", import->source_directory, entry->name);
	if (source_length < 0 || (size_t)source_length >= sizeof source) {
		import->failed++;
		return 1;
	}

	uint8_t* data = NULL;
	size_t size = 0;
	if (!AeronVfs_ReadAll(import->installation->vfs, AERON_VFS_ROOT_ASSET, source, TIE_SETUP_PILOT_FILE_SIZE,
						  &data, &size) ||
		size != TIE_SETUP_PILOT_FILE_SIZE) {
		Aeron_LogWarn("tie.setup", "could not read pilot preset %s from %s", source,
					  import->installation->root);
		free(data);
		import->failed++;
		return 1;
	}
	if (!AeronVfs_WriteAllAtomic(import->application_vfs, AERON_VFS_ROOT_USER, destination, data, size)) {
		Aeron_LogWarn("tie.setup", "could not import pilot preset %s from %s", source,
					  import->installation->root);
		free(data);
		import->failed++;
		return 1;
	}
	free(data);
	import->imported++;
	return 1;
}

static void TieSetup_ImportPilotsFrom(TieSetupPilotImport* import, const TieInstallation* installation) {
	static const char* const directories[] = { "SUPPORT", "." };
	import->installation = installation;
	for (size_t index = 0; index < sizeof directories / sizeof directories[0]; ++index) {
		AeronFileInfo info;
		import->source_directory = directories[index];
		if (!AeronVfs_Stat(installation->vfs, AERON_VFS_ROOT_ASSET, import->source_directory, &info) ||
			!info.is_directory)
			continue;
		if (!AeronVfs_Glob(installation->vfs, AERON_VFS_ROOT_ASSET, import->source_directory, "*.tfr",
						   AERON_VFS_GLOB_FILES | AERON_VFS_GLOB_CASE_INSENSITIVE, TieSetup_ImportPilot,
						   import)) {
			Aeron_LogWarn("tie.setup", "could not enumerate pilot presets in %s/%s", installation->root,
						  import->source_directory);
			import->failed++;
		}
	}
}

static void TieSetup_ImportPilotPresets(const TieSetupDialog* setup) {
	const TieGameVersion preferred = setup->config->requested.frontend_version == TIE_VERSION_SELECTION_TIE98
										 ? TIE_GAME_VERSION_TIE98
										 : TIE_GAME_VERSION_TIE95;
	const TieInstallation* first = TieInstallation_Get(setup->installations, preferred);
	const TieInstallation* second = TieInstallation_Get(
		setup->installations,
		preferred == TIE_GAME_VERSION_TIE98 ? TIE_GAME_VERSION_TIE95 : TIE_GAME_VERSION_TIE98);
	TieSetupPilotImport import = { .application_vfs = setup->application_vfs };
	if (first)
		TieSetup_ImportPilotsFrom(&import, first);
	if (second)
		TieSetup_ImportPilotsFrom(&import, second);
	Aeron_LogInfo("tie.setup", "pilot preset import: %zu copied, %zu skipped, %zu failed", import.imported,
				  import.skipped, import.failed);
}

static bool TieSetup_Error(char* error, size_t capacity, const char* format, const char* detail) {
	if (error && capacity)
		snprintf(error, capacity, format, detail ? detail : "");
	return false;
}

static const char* TieSetup_VersionName(TieGameVersion version) {
	return version == TIE_GAME_VERSION_TIE98 ? "TIE98" : "TIE95 Collector's CD-ROM";
}

static bool TieSetup_SelectionRequires(TieVersionSelection frontend, TieVersionSelection flight,
									   TieGameVersion version) {
	const TieVersionSelection required =
		version == TIE_GAME_VERSION_TIE98 ? TIE_VERSION_SELECTION_TIE98 : TIE_VERSION_SELECTION_TIE95;
	return frontend == required || flight == required;
}

static bool TieSetup_IsRequired(const TieAppConfig* config, const TieInstallationSet* installations) {
	const bool require_tie95 =
		TieSetup_SelectionRequires(config->frontend_version, config->flight_version, TIE_GAME_VERSION_TIE95);
	const bool require_tie98 =
		TieSetup_SelectionRequires(config->frontend_version, config->flight_version, TIE_GAME_VERSION_TIE98);
	return (!installations->has_tie95 && !installations->has_tie98) ||
		   (require_tie95 && !installations->has_tie95) || (require_tie98 && !installations->has_tie98);
}

static bool TieSetup_OpenSavedInstallation(TieInstallation* out, TieGameVersion version, const char* path,
										   const char* resource_root, char* warning,
										   size_t warning_capacity) {
	if (!path || !path[0])
		return false;
	if (TieInstallation_Open(out, version, path, resource_root, warning, warning_capacity))
		return true;
	Aeron_LogWarn("tie.config", "saved %s installation is invalid: %s",
				  version == TIE_GAME_VERSION_TIE98 ? "TIE98" : "TIE95", warning);
	return false;
}

static bool TieSetup_PresetAvailable(const TieSetupDialog* setup, TieSetupPreset preset) {
	if (!setup || preset < TIE_SETUP_PRESET_RECOMMENDED || preset > TIE_SETUP_PRESET_TIE98)
		return false;
	const TieSetupPresetDesc* desc = &setup_presets[preset];
	return (!desc->require_tie95 || setup->installations->has_tie95) &&
		   (!desc->require_tie98 || setup->installations->has_tie98);
}

static TieSetupPreset TieSetup_BestPreset(const TieInstallationSet* installations) {
	if (installations->has_tie95 && installations->has_tie98)
		return TIE_SETUP_PRESET_RECOMMENDED;
	if (installations->has_tie95)
		return TIE_SETUP_PRESET_TIE95;
	if (installations->has_tie98)
		return TIE_SETUP_PRESET_TIE98;
	return TIE_SETUP_PRESET_NONE;
}

static void TieSetup_RefreshPreset(TieSetupDialog* setup) {
	if (!setup->preset_touched || !TieSetup_PresetAvailable(setup, setup->preset))
		setup->preset = TieSetup_BestPreset(setup->installations);
}

static int TieSetup_AcceptInstallation(const char* path, void* user, char* error, size_t error_capacity) {
	TieSetupDialog* setup = (TieSetupDialog*)user;
	const TieGameVersion expected_version =
		setup->picker_target == TIE_SETUP_PICKER_TIE98 ? TIE_GAME_VERSION_TIE98 : TIE_GAME_VERSION_TIE95;
	TieGameVersion actual_version;
	if (!TieInstallation_Classify(path, &actual_version, error, error_capacity))
		return 0;
	if (actual_version != expected_version) {
		snprintf(error, error_capacity, "This is a %s installation; a %s installation is required.",
				 TieSetup_VersionName(actual_version), TieSetup_VersionName(expected_version));
		return 0;
	}
	TieInstallation_Close(&setup->picker_installation);
	return TieInstallation_Open(&setup->picker_installation, expected_version, path, Aeron_ResourceRoot(),
								error, error_capacity);
}

static int TieSetup_AcceptSc55(const char* path, void* user, char* error, size_t error_capacity) {
	(void)user;
	return TieMidiResources_Sc55RomDirectoryvalidate(path, error, error_capacity);
}

static void TieSetup_OpenPicker(TieSetupDialog* setup, TieGameVersion version) {
	char title[80];
	char instructions[160];
	char error[512];
	const TieInstallation* current = TieInstallation_Get(setup->installations, version);
	setup->picker_target =
		version == TIE_GAME_VERSION_TIE98 ? TIE_SETUP_PICKER_TIE98 : TIE_SETUP_PICKER_TIE95;
	TieInstallation_Close(&setup->picker_installation);
	snprintf(title, sizeof title, "SELECT %s INSTALLATION",
			 version == TIE_GAME_VERSION_TIE98 ? "TIE98" : "TIE95");
	snprintf(instructions, sizeof instructions, "Select the complete original %s installation folder.",
			 TieSetup_VersionName(version));
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_SELECT_DIRECTORY,
		.title = title,
		.instructions = instructions,
		.accept_label = "Use This Folder",
		.cancel_label = "Cancel",
		.initial_path = current ? current->root : NULL,
		.accept_fn = TieSetup_AcceptInstallation,
		.accept_user = setup,
	};
	if (!AeronUiFilePicker_Open(setup->picker, &desc, error, sizeof error))
		snprintf(setup->message, sizeof setup->message, "%s", error);
}

static void TieSetup_OpenSc55Picker(TieSetupDialog* setup) {
	char error[512];
	setup->picker_target = TIE_SETUP_PICKER_SC55;
	TieInstallation_Close(&setup->picker_installation);
	const AeronUiFilePickerDesc desc = {
		.mode = AERON_UI_FILE_PICKER_SELECT_DIRECTORY,
		.title = "SELECT SC-55 ROM DIRECTORY",
		.instructions = "Select the folder containing the original SC-55 ROM dumps.",
		.accept_label = "Use This Folder",
		.cancel_label = "Cancel",
		.initial_path = setup->sc55_path[0] ? setup->sc55_path : NULL,
		.accept_fn = TieSetup_AcceptSc55,
		.accept_user = setup,
	};
	if (!AeronUiFilePicker_Open(setup->picker, &desc, error, sizeof error))
		snprintf(setup->message, sizeof setup->message, "%s", error);
}

static void TieSetup_TakePickerInstallation(TieSetupDialog* setup) {
	TieInstallation* destination;
	bool* available;
	bool* from_override;
	char* path;
	if (setup->picker_target == TIE_SETUP_PICKER_TIE98) {
		destination = &setup->installations->tie98;
		available = &setup->installations->has_tie98;
		from_override = &setup->tie98_from_override;
		path = setup->tie98_path;
	} else {
		destination = &setup->installations->tie95;
		available = &setup->installations->has_tie95;
		from_override = &setup->tie95_from_override;
		path = setup->tie95_path;
	}
	TieInstallation_Close(destination);
	*destination = setup->picker_installation;
	memset(&setup->picker_installation, 0, sizeof setup->picker_installation);
	*available = true;
	*from_override = false;
	snprintf(path, TIE_GAME_DATA_PATH_MAX, "%s", destination->root);
	setup->message[0] = '\0';
	TieSetup_RefreshPreset(setup);
}

static void TieSetup_TakePickerResult(TieSetupDialog* setup, const char* selected_path) {
	if (setup->picker_target != TIE_SETUP_PICKER_SC55) {
		TieSetup_TakePickerInstallation(setup);
		return;
	}
	snprintf(setup->sc55_path, sizeof setup->sc55_path, "%s", selected_path);
	setup->message[0] = '\0';
}

static bool TieSetup_Commit(TieSetupDialog* setup) {
	if (!TieSetup_PresetAvailable(setup, setup->preset))
		return TieSetup_Error(setup->message, sizeof setup->message, "%s", "Select an available preset.");
	TieAppLaunchOptions launch;
	TieAppConfig_GetLaunchOptions(&setup->config->requested, &launch);
	if (!setup->tie95_from_override)
		snprintf(launch.tie95_data, sizeof launch.tie95_data, "%s",
				 setup->installations->has_tie95 ? setup->installations->tie95.root : "");
	if (!setup->tie98_from_override)
		snprintf(launch.tie98_data, sizeof launch.tie98_data, "%s",
				 setup->installations->has_tie98 ? setup->installations->tie98.root : "");
	const TieSetupPresetDesc* preset = &setup_presets[setup->preset];
	launch.frontend_version = preset->frontend;
	launch.flight_version = preset->flight;
	launch.music_source = preset->music;
	if (setup->sc55_path[0] && TieMidiBackend_Available(TIE_MIDI_BACKEND_SC55)) {
		if (!TieMidiResources_Sc55RomDirectoryvalidate(setup->sc55_path, setup->message,
													   sizeof setup->message))
			return false;
		snprintf(launch.sc55_rom_directory, sizeof launch.sc55_rom_directory, "%s", setup->sc55_path);
		launch.midi_backend = TIE_MIDI_BACKEND_SC55;
	}
	return TieAppConfig_SetLaunchOptions(setup->config, &launch, setup->message, sizeof setup->message) &&
		   TieAppConfig_Save(setup->application_vfs, setup->config, setup->message, sizeof setup->message);
}

static const char* TieSetup_VersionChoice(TieVersionSelection version) {
	return version == TIE_VERSION_SELECTION_TIE98 ? "TIE98" : "TIE95";
}

static bool TieSetup_HasSc55(const TieSetupDialog* setup) {
	return setup->sc55_path[0] && TieMidiBackend_Available(TIE_MIDI_BACKEND_SC55);
}

static void TieSetup_DrawSummary(AeronUiContext* ui, const TieSetupDialog* setup) {
	const TieSetupPreset preset = setup->preset;
	const TieSetupPresetDesc* desc = preset <= TIE_SETUP_PRESET_TIE98 ? &setup_presets[preset] : NULL;
	const char* music = "-";
	if (desc)
		music = desc->music == TIE_MUSIC_TIE98 ? "CD Music"
				: TieSetup_HasSc55(setup)      ? "iMUSE / SC-55"
											   : "iMUSE / OPL3";
	AeronUi_Header(ui, "Preset Settings");
	AeronUi_BeginColumns(ui, 2, (const float[]) { -280.0f, 1.0f });
	AeronUi_Label(ui, "Cutscenes and Menus");
	AeronUi_Label(ui, "Flight Engine");
	AeronUi_Label(ui, "Music");
	AeronUi_NextColumn(ui);
	AeronUi_Label(ui, desc ? TieSetup_VersionChoice(desc->frontend) : "-");
	AeronUi_Label(ui, desc ? TieSetup_VersionChoice(desc->flight) : "-");
	AeronUi_Label(ui, music);
	AeronUi_EndColumns(ui);
}

static TieSetupFrameResult TieSetup_DrawWindow(TieSetupDialog* setup, AeronUiContext* ui) {
	TieSetupFrameResult result = TIE_SETUP_FRAME_PENDING;
	const AeronUiWindowDesc window = { .width_ref = 920.0f, .height_ref = 940.0f, .centered = 1 };
	if (!AeronUi_BeginWindow(ui, "Welcome to OpenTIE", &window))
		return result;
	AeronUi_Help(ui, "Select one or both original installations. For the best experience, select both.");
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Original Installations");
	uint32_t path_result = TieInstallation_PathRow(ui, "TIE Fighter CD 1995", setup->tie95_path,
												   sizeof setup->tie95_path, AERON_UI_INPUT_TEXT_READ_ONLY);
	if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
		TieSetup_OpenPicker(setup, TIE_GAME_VERSION_TIE95);
	path_result = TieInstallation_PathRow(ui, "TIE Fighter 1998", setup->tie98_path, sizeof setup->tie98_path,
										  AERON_UI_INPUT_TEXT_READ_ONLY);
	if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
		TieSetup_OpenPicker(setup, TIE_GAME_VERSION_TIE98);
	if (TieMidiBackend_Available(TIE_MIDI_BACKEND_SC55)) {
		AeronUi_Spacer(ui, 8.0f);
		AeronUi_Header(ui, "MIDI Music");
		AeronUi_Help(ui, "Have Roland SC-55 ROMs? Select them for the recommended music experience.");
		path_result =
			AeronUi_InputTextWithAction(ui, "SC-55 ROM Directory", setup->sc55_path, sizeof setup->sc55_path,
										AERON_UI_INPUT_TEXT_NONE, "Browse...");
		if (path_result & AERON_UI_INPUT_TEXT_ACTION_ACTIVATED)
			TieSetup_OpenSc55Picker(setup);
	}

	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Preset");
	AeronUiListItem items[3];
	for (size_t index = 0; index < 3; ++index) {
		const TieSetupPreset preset = (TieSetupPreset)index;
		items[index] = (AeronUiListItem) {
			.id = index,
			.label = setup_presets[index].label,
			.flags = TieSetup_PresetAvailable(setup, preset) ? AERON_UI_LIST_ITEM_NONE
															 : AERON_UI_LIST_ITEM_DISABLED,
		};
	}
	size_t selected = setup->preset <= TIE_SETUP_PRESET_TIE98 ? (size_t)setup->preset : SIZE_MAX;
	if (AeronUi_ListBox(ui, "First-launch preset", items, 3, &selected, 120.0f) & AERON_UI_LIST_CHANGED) {
		setup->preset = selected < 3 ? (TieSetupPreset)selected : TIE_SETUP_PRESET_NONE;
		setup->preset_touched = selected < 3;
	}
	TieSetup_DrawSummary(ui, setup);
	if (setup->message[0])
		AeronUi_Error(ui, setup->message);
	AeronUi_Separator(ui);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Quit"))
		result = TIE_SETUP_FRAME_CANCELLED;
	AeronUi_NextColumn(ui);
	if (AeronUi_ButtonEnabled(ui, "Continue", TieSetup_PresetAvailable(setup, setup->preset)) &&
		TieSetup_Commit(setup))
		result = TIE_SETUP_FRAME_SUCCESS;
	AeronUi_EndColumns(ui);
	AeronUi_EndWindow(ui);
	return result;
}

static TieSetupResult TieSetup_RunDialog(TieUi* ui, TieSetupDialog* setup, char* error,
										 size_t error_capacity) {
	setup->picker = AeronUiFilePicker_Create();
	if (!setup->picker) {
		TieSetup_Error(error, error_capacity, "Could not create the installation folder picker: %s",
					   "out of memory");
		return TIE_SETUP_ERROR;
	}
	TieSetup_RefreshPreset(setup);
	TieSetupResult outcome = TIE_SETUP_ERROR;
	while (!Aeron_QuitRequested() && !Aeron_FatalErrorRequested()) {
		const int32_t delta_us = Aeron_BeginFrame();
		if (Aeron_FatalErrorRequested())
			break;
		AeronUiContext* context = TieUi_Context(ui);
		AeronUi_BeginFrame(context, &(AeronUiFrameDesc) {
										.input = Aeron_InputSnapshot(),
										.dt_seconds = (float)delta_us * 1e-6f,
									});
		const TieSetupFrameResult frame_result = TieSetup_DrawWindow(setup, context);
		char selected[TIE_GAME_DATA_PATH_MAX];
		char picker_error[512] = { 0 };
		const AeronUiFilePickerResult picker_result = AeronUiFilePicker_Draw(
			setup->picker, context, selected, sizeof selected, picker_error, sizeof picker_error);
		if (picker_result == AERON_UI_FILE_PICKER_SELECTED)
			TieSetup_TakePickerResult(setup, selected);
		else if (picker_result == AERON_UI_FILE_PICKER_ERROR)
			snprintf(setup->message, sizeof setup->message, "%s", picker_error);
		const AeronUiOutput ui_output = AeronUi_EndFrame(context);
		if (!AeronUi_Submit(context) || !Aeron_Present()) {
			TieSetup_Error(error, error_capacity, "Could not render the installation setup dialog: %s",
						   "renderer failure");
			break;
		}
		if (frame_result == TIE_SETUP_FRAME_SUCCESS || frame_result == TIE_SETUP_FRAME_CANCELLED) {
			outcome = frame_result == TIE_SETUP_FRAME_SUCCESS ? TIE_SETUP_SUCCESS : TIE_SETUP_CANCELLED;
			break;
		}
		if (ui_output.cancel_pressed) {
			outcome = TIE_SETUP_CANCELLED;
			break;
		}
	}
	if (Aeron_QuitRequested() && !Aeron_FatalErrorRequested())
		outcome = TIE_SETUP_CANCELLED;
	if (outcome == TIE_SETUP_ERROR) {
		if (setup->message[0])
			snprintf(error, error_capacity, "%s", setup->message);
		else
			TieSetup_Error(error, error_capacity, "Installation setup could not continue: %s",
						   "unknown error");
	}
	TieInstallation_Close(&setup->picker_installation);
	AeronUiFilePicker_Destroy(setup->picker);
	setup->picker = NULL;
	return outcome;
}

TieSetupResult TieSetup_ResolveInstallations(TieUi* ui, AeronVfs* application_vfs, TieAppConfigState* config,
											 const char* tie95_override, const char* tie98_override,
											 TieInstallationSet* out, char* error, size_t error_capacity) {
	if (!ui || !application_vfs || !config || !out) {
		TieSetup_Error(error, error_capacity, "Invalid installation setup state: %s", "unavailable");
		return TIE_SETUP_ERROR;
	}
	memset(out, 0, sizeof *out);
	TieSetupDialog setup = {
		.application_vfs = application_vfs,
		.config = config,
		.installations = out,
		.preset = TIE_SETUP_PRESET_NONE,
	};
	const char* resource_root = Aeron_ResourceRoot();
	if (tie95_override && tie95_override[0]) {
		out->has_tie95 = TieInstallation_Open(&out->tie95, TIE_GAME_VERSION_TIE95, tie95_override,
											  resource_root, error, error_capacity);
		if (!out->has_tie95)
			return TIE_SETUP_ERROR;
		setup.tie95_from_override = true;
	} else {
		out->has_tie95 =
			TieSetup_OpenSavedInstallation(&out->tie95, TIE_GAME_VERSION_TIE95, config->requested.tie95_data,
										   resource_root, setup.message, sizeof setup.message);
	}
	if (tie98_override && tie98_override[0]) {
		out->has_tie98 = TieInstallation_Open(&out->tie98, TIE_GAME_VERSION_TIE98, tie98_override,
											  resource_root, error, error_capacity);
		if (!out->has_tie98) {
			TieInstallation_SetClose(out);
			return TIE_SETUP_ERROR;
		}
		setup.tie98_from_override = true;
	} else {
		out->has_tie98 =
			TieSetup_OpenSavedInstallation(&out->tie98, TIE_GAME_VERSION_TIE98, config->requested.tie98_data,
										   resource_root, setup.message, sizeof setup.message);
	}
	snprintf(setup.tie95_path, sizeof setup.tie95_path, "%s",
			 out->has_tie95 ? out->tie95.root : config->requested.tie95_data);
	snprintf(setup.tie98_path, sizeof setup.tie98_path, "%s",
			 out->has_tie98 ? out->tie98.root : config->requested.tie98_data);
	snprintf(setup.sc55_path, sizeof setup.sc55_path, "%s", config->requested.sc55_rom_directory);
	if (!TieSetup_IsRequired(&config->requested, out))
		return TIE_SETUP_SUCCESS;
	const TieSetupResult result = TieSetup_RunDialog(ui, &setup, error, error_capacity);
	if (result == TIE_SETUP_SUCCESS)
		TieSetup_ImportPilotPresets(&setup);
	else
		TieInstallation_SetClose(out);
	return result;
}
