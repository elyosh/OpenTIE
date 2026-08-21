/* Auto-generated from STRINGS.DAT — pointer-table indexing enums.
 * See docs/strings-dat-fulldump.md for the full string content. */

#ifndef TIE_STRING_TABLE_IDS_H
#define TIE_STRING_TABLE_IDS_H

/* DAMAGE_outputsystem index (10 entries; cells 0..9) */
typedef enum SystemStringId {
	SYS_AUTO_EJECTION_SYSTEM = 0,   /* Auto Ejection System */
	SYS_HYPER_DRIVE_SYSTEM = 1,     /* Hyper Drive System */
	SYS_LASERS_AND_ION_CANNONS = 2, /* Lasers and Ion Cannons */
	SYS_FLIGHT_CONTROL_SYSTEM = 3,  /* Flight Control System */
	SYS_ENGINES = 4,                /* Engines */
	SYS_TARGETING_COMPUTER = 5,     /* Targeting Computer */
	SYS_SHIELD_SYSTEM = 6,          /* Shield System */
	SYS_WARHEAD_LAUNCH_SYSTEM = 7,  /* Warhead Launch System */
	SYS_TRACTOR_BEAM_SYSTEM = 8,    /* Tractor Beam System */
	SYS_COMMUNICATIONS_SYSTEM = 9,  /* Communications System */
} SystemStringId;

/* FEDISKIO_fatalerror index (11 entries; cells 10..20) */
typedef enum FatalErrId {
	FATAL_ERROR_NOT_ENOUGH_MEMORY_X0A = 0, /* Error! Not Enough Memory!<\x0a> */
	FATAL_ERROR_THE_FOLLOWING_FILE_IS_MISSING_ =
		1,                                       /* Error! The following file is missing or inaccessible:  */
	FATAL_CENTER_JOYSTICK_THEN_CLICK_BUTTON = 2, /* Center joystick, then click button. */
	FATAL_MOVE_JOYSTICK_TO_UPPER_LEFT_THEN_CLI = 3,  /* Move joystick to upper left, then click button. */
	FATAL_MOVE_JOYSTICK_TO_LOWER_RIGHT_THEN_CL = 4,  /* Move joystick to lower right, then click button. */
	FATAL_LEVEL = 5,                                 /* Level: */
	FATAL_SEGMENTS_LEFT = 6,                         /* Segments Left: */
	FATAL_SEGMENTS_DONE = 7,                         /* Segments Done: */
	FATAL_TARGETS_HIT = 8,                           /* Targets Hit: */
	FATAL_SCORE = 9,                                 /* Score: */
	FATAL_PRESS_SPACEBAR_OR_BUTTON_TO_CONTINUE = 10, /* Press SPACEBAR or button to continue */
} FatalErrId;

/* GOALS condition strings (21 entries; cells 21..41) */
typedef enum CondStringId {
	COND_X_ = 0,                 /* --- */
	COND_ARRIVED = 1,            /* arrived. */
	COND_DESTROYED = 2,          /* destroyed. */
	COND_ATTACKED = 3,           /* attacked. */
	COND_CAPTURED = 4,           /* captured. */
	COND_INSPECTED = 5,          /* inspected. */
	COND_BOARDED = 6,            /* boarded. */
	COND_FINISHED_DOCKING = 7,   /* finished docking. */
	COND_DISABLED = 8,           /* disabled. */
	COND_SURVIVED = 9,           /* survived. */
	COND_X__2 = 10,              /* --- */
	COND_X__3 = 11,              /* --- */
	COND_COMPLETED_MISSION = 12, /* completed mission. */
	COND_X__4 = 13,              /* --- */
	COND_X__5 = 14,              /* --- */
	COND_X__6 = 15,              /* --- */
	COND_X__7 = 16,              /* --- */
	COND_X__8 = 17,              /* --- */
	COND_X__9 = 18,              /* --- */
	COND_DROPPED_OFF = 19,       /* dropped off. */
	COND_REINFORCED = 20,        /* reinforced. */
} CondStringId;

/* GOALS condition verb strings (20 entries; cells 42..61) */
typedef enum CondVerbId {
	CONDV_X_ = 0,           /* --- */
	CONDV_WAS = 1,          /* was  */
	CONDV_WAS_NOT = 2,      /* was not  */
	CONDV_X__2 = 3,         /* --- */
	CONDV_MUST_BE = 4,      /* must be  */
	CONDV_X__3 = 5,         /* --- */
	CONDV_HAS = 6,          /* has  */
	CONDV_HAS_NOT = 7,      /* has not  */
	CONDV_X__4 = 8,         /* --- */
	CONDV_MUST_HAVE = 9,    /* must have  */
	CONDV_X__5 = 10,        /* --- */
	CONDV_WERE = 11,        /* were  */
	CONDV_WERE_NOT = 12,    /* were not  */
	CONDV_X__6 = 13,        /* --- */
	CONDV_MUST_BE_2 = 14,   /* must be  */
	CONDV_X__7 = 15,        /* --- */
	CONDV_HAVE = 16,        /* have  */
	CONDV_HAVE_NOT = 17,    /* have not  */
	CONDV_X__8 = 18,        /* --- */
	CONDV_MUST_HAVE_2 = 19, /* must have  */
} CondVerbId;

/* GOALS percentage labels (16 entries; cells 62..77) */
typedef enum PctStringId {
	PCT_X_100 = 0,           /* 100% */
	PCT_X_75 = 1,            /* 75% */
	PCT_X_50 = 2,            /* 50% */
	PCT_X_25 = 3,            /* 25% */
	PCT_AT_LEAST_ONE = 4,    /* At least one */
	PCT_ALL_BUT_ONE = 5,     /* All but one */
	PCT_X_ = 6,              /* --- */
	PCT_X__2 = 7,            /* --- */
	PCT_ALL_BUT_YOU = 8,     /* All but you */
	PCT_YOU = 9,             /* You */
	PCT_ALL = 10,            /* All */
	PCT_X_75_2 = 11,         /* 75% */
	PCT_X_50_2 = 12,         /* 50% */
	PCT_X_25_2 = 13,         /* 25% */
	PCT_AT_LEAST_ONE_2 = 14, /* At least one */
	PCT_ALL_BUT_ONE_2 = 15,  /* All but one */
} PctStringId;

/* GOALS goal operator labels (2 entries; cells 78..79) */
typedef enum GoalOpId {
	GOP_AND = 0, /* AND */
	GOP_OR = 1,  /* OR */
} GoalOpId;

/* GOALS goal title labels (9 entries; cells 80..88) */
typedef enum GoalTitleId {
	GTITLE_PRIMARY_GOALS_FAILED = 0,       /* Primary Goals Failed */
	GTITLE_PRIMARY_GOALS_INCOMPLETE = 1,   /* Primary Goals Incomplete */
	GTITLE_PRIMARY_GOALS_COMPLETED = 2,    /* Primary Goals Completed */
	GTITLE_SECONDARY_GOALS_FAILED = 3,     /* Secondary Goals Failed */
	GTITLE_SECONDARY_GOALS_INCOMPLETE = 4, /* Secondary Goals Incomplete */
	GTITLE_SECONDARY_GOALS_COMPLETED = 5,  /* Secondary Goals Completed */
	GTITLE_BONUS_GOALS_FAILED = 6,         /* Bonus Goals Failed */
	GTITLE_BONUS_GOALS_INCOMPLETE = 7,     /* Bonus Goals Incomplete */
	GTITLE_BONUS_GOALS_COMPLETED = 8,      /* Bonus Goals Completed */
} GoalTitleId;

/* GOALS pilot skill labels (6 entries; cells 96..101) */
typedef enum GoalSkillId {
	GSKILL_ROOKIE_PILOTS = 0,  /* rookie pilots */
	GSKILL_NOVICE_PILOTS = 1,  /* novice pilots */
	GSKILL_VETERAN_PILOTS = 2, /* veteran pilots */
	GSKILL_OFFICER_PILOTS = 3, /* officer pilots */
	GSKILL_ACE_PILOTS = 4,     /* ace pilots */
	GSKILL_TOP_ACE_PILOTS = 5, /* top-ace pilots */
} GoalSkillId;

/* GOALS AI-order labels (30 entries; cells 102..131) */
typedef enum GoalAiId {
	AI_STATIONARY_CRAFT = 0,                        /* stationary craft */
	AI_CRAFT_RETURNING_TO_BASE = 1,                 /* craft returning to base */
	AI_NON_EVADING_CRAFT = 2,                       /* non-evading craft */
	AI_CRAFT_FLYING_IN_FORMATION = 3,               /* craft flying in formation */
	AI_RENDEZVOUS_CRAFT = 4,                        /* rendezvous craft */
	AI_DISABLED_CRAFT = 5,                          /* disabled craft */
	AI_CRAFT_AWAITING_BOARDING = 6,                 /* craft awaiting boarding */
	AI_CRAFT_ON_FREE_PATROL = 7,                    /* craft on free patrol */
	AI_CRAFT_ATTACKING_ESCORTS = 8,                 /* craft attacking escorts */
	AI_CRAFT_ATTACKING_IN_RESPONSE = 9,             /* craft attacking in response */
	AI_ESCORTING_FIGHTERS = 10,                     /* escorting fighters */
	AI_CRAFT_ATTEMPTING_TO_DISABLE = 11,            /* craft attempting to disable */
	AI_CRAFT_DELIVERING_CARGO = 12,                 /* craft delivering cargo */
	AI_CRAFT_SEIZING_CARGO = 13,                    /* craft seizing cargo */
	AI_CRAFT_EXCHANGING_CARGO = 14,                 /* craft exchanging cargo */
	AI_CAPTURING_CRAFT = 15,                        /* capturing craft */
	AI_CRAFT_DESTROYING_CARGO = 16,                 /* craft destroying cargo */
	AI_CRAFT_PICKING_UP_CARGO = 17,                 /* craft picking-up cargo */
	AI_CRAFT_SEIZING_CARGO_2 = 18,                  /* craft seizing cargo */
	AI_WAITING_FIGHTERS = 19,                       /* waiting fighters */
	AI_WAITING_STARSHIPS = 20,                      /* waiting starships */
	AI_PATROLLING_STARSHIPS = 21,                   /* patrolling starships */
	AI_STARSHIPS_WAITING_FOR_ALL_RETURNING_ = 22,   /* starships waiting for all returning craft */
	AI_STARSHIPS_WAITING_TO_DISPATCH_ALL_CR = 23,   /* starships waiting to dispatch all craft */
	AI_STARSHIPS_WAITING_FOR_BOARDING_CRAFT = 24,   /* starships waiting for boarding craft */
	AI_STARSHIPS_WAITING_FOR_BOARDING_CRAFT_2 = 25, /* starships waiting for boarding craft to appear */
	AI_CRAFT_ON_FREE_PATROL_2 = 26,                 /* craft on free patrol */
	AI_CRAFT_ATTEMPTING_TO_DISABLE_2 = 27,          /* craft attempting to disable */
	AI_CRAFT_ATTEMPTING_TO_DISABLE_3 = 28,          /* craft attempting to disable */
	AI_STARSHIPS_RETURNING_TO_BASE = 29,            /* starships returning to base */
} GoalAiId;

/* GOALS side (Rebel/Imperial) labels (3 entries; cells 132..134) */
typedef enum GoalSideId {
	SIDE_REBEL_CRAFT = 0,    /* Rebel craft */
	SIDE_IMPERIAL_CRAFT = 1, /* Imperial craft */
	SIDE_CRAFT = 2,          /*  craft */
} GoalSideId;

/* GOALS family labels (7 entries; cells 135..141) */
typedef enum GoalFamilyId {
	GFAM_SPACE_CRAFT = 0, /* space craft */
	GFAM_WEAPONS = 1,     /* weapons */
	GFAM_SATELLITES = 2,  /* satellites */
	GFAM_X_ = 3,          /* --- */
	GFAM_X__2 = 4,        /* --- */
	GFAM_X__3 = 5,        /* --- */
	GFAM_X__4 = 6,        /* --- */
} GoalFamilyId;

/* GOALS genus labels (16 entries; cells 142..157) */
typedef enum GoalGenusId {
	GENUS_STARFIGHTERS = 0,    /* starfighters */
	GENUS_TRANSPORT_CRAFT = 1, /* transport craft */
	GENUS_UTILITY_CRAFT = 2,   /* utility craft */
	GENUS_FREIGHTER_CRAFT = 3, /* freighter craft */
	GENUS_STARSHIPS = 4,       /* starships */
	GENUS_PLATFORMS = 5,       /* platforms */
	GENUS_X_ = 6,              /* --- */
	GENUS_X__2 = 7,            /* --- */
	GENUS_MINES = 8,           /* mines */
	GENUS_X__3 = 9,            /* --- */
	GENUS_X__4 = 10,           /* --- */
	GENUS_X__5 = 11,           /* --- */
	GENUS_X__6 = 12,           /* --- */
	GENUS_X__7 = 13,           /* --- */
	GENUS_X__8 = 14,           /* --- */
	GENUS_X__9 = 15,           /* --- */
} GoalGenusId;

/* HELP key-name labels (48 entries; cells 158..205) */
typedef enum HelpKeyId {
	HKEY_X_ = 0,           /*  +/- */
	HKEY_X__2 = 1,         /*  [ */
	HKEY_X__3 = 2,         /*  ] */
	HKEY_BACKSPACE = 3,    /*  BACKSPACE */
	HKEY_X__4 = 4,         /*  \ */
	HKEY_RETURN = 5,       /*  RETURN */
	HKEY_H = 6,            /*  H */
	HKEY_ALT_E = 7,        /*  ALT-E */
	HKEY_Q = 8,            /*  Q */
	HKEY_C = 9,            /*  C */
	HKEY_V = 10,           /*  V */
	HKEY_F8 = 11,          /*  F8 */
	HKEY_F9 = 12,          /*  F9 */
	HKEY_OR_SHIFTF9 = 13,  /*  ; or SHIFTF9 */
	HKEY_F10 = 14,         /*  F10 */
	HKEY_OR_SHIFTF10 = 15, /*  ' or SHIFTF10 */
	HKEY_S = 16,           /*  S */
	HKEY_X_0_9 = 17,       /*  0-9 */
	HKEY_X__5 = 18,        /*  . */
	HKEY_F1 = 19,          /*  F1 */
	HKEY_F2 = 20,          /*  F2 */
	HKEY_OR_F3 = 21,       /*  / or F3 */
	HKEY_OR_F4 = 22,       /*  * or F4 */
	HKEY_Z = 23,           /*  Z */
	HKEY_T_Y = 24,         /*  T/Y */
	HKEY_U = 25,           /*  U */
	HKEY_R = 26,           /*  R */
	HKEY_E = 27,           /*  E */
	HKEY_A = 28,           /*  A */
	HKEY_I = 29,           /*  I */
	HKEY_X__6 = 30,        /*  ,/< */
	HKEY_SHIFTF5_F7 = 31,  /*  SHIFTF5-F7 */
	HKEY_F5_F7 = 32,       /*  F5-F7 */
	HKEY_W = 33,           /*  W */
	HKEY_X = 34,           /*  X */
	HKEY_B = 35,           /*  B */
	HKEY_ALT_C = 36,       /*  ALT-C */
	HKEY_ALT_P_OR_P = 37,  /*  ALT-P or P */
	HKEY_ALT_T = 38,       /*  ALT-T */
	HKEY_ALT_D = 39,       /*  ALT-D */
	HKEY_ALT_M_S = 40,     /*  ALT-M/S */
	HKEY_M = 41,           /*  M */
	HKEY_L = 42,           /*  L */
	HKEY_G = 43,           /*  G */
	HKEY_D = 44,           /*  D */
	HKEY_SHIFT_Z = 45,     /*  SHIFT-Z */
	HKEY_K = 46,           /*  K */
	HKEY_ESCAPE = 47,      /*  ESCAPE */
} HelpKeyId;

/* HELP screen-text labels (48 entries; cells 206..253) */
typedef enum HelpScreenId {
	HSCR_INCREASE_DECREASE_THROTTLE = 0,         /* Increase/Decrease Throttle */
	HSCR_SET_THROTTLE_TO_1_3 = 1,                /* Set Throttle to 1/3 */
	HSCR_SET_THROTTLE_TO_2_3 = 2,                /* Set Throttle to 2/3 */
	HSCR_SET_FULL_THROTTLE = 3,                  /* Set Full Throttle */
	HSCR_SET_ZERO_THROTTLE = 4,                  /* Set Zero Throttle */
	HSCR_MATCH_SPEED_OF_TARGET = 5,              /* Match Speed of Target */
	HSCR_ENTER_ABORT_HYPERSPACE = 6,             /* Enter/Abort Hyperspace */
	HSCR_EJECT = 7,                              /* Eject */
	HSCR_QUIT_ABORT_MISSION = 8,                 /* Quit; Abort Mission */
	HSCR_START_STOP_CAMERA = 9,                  /* Start/Stop Camera */
	HSCR_VIEW_FILM = 10,                         /* View Film */
	HSCR_SET_BEAM_WEAPON_RECHARGE_LVL = 11,      /* Set Beam Weapon Recharge Lvl */
	HSCR_SET_LASER_RECHARGE_LEVEL = 12,          /* Set Laser Recharge Level */
	HSCR_BOOST_LASERS_FROM_SHIELDS = 13,         /* Boost Lasers from Shields */
	HSCR_SET_SHIELD_RECHARGE_LEVEL = 14,         /* Set Shield Recharge Level */
	HSCR_BOOST_SHIELDS_FROM_LASERS = 15,         /* Boost Shields from Lasers */
	HSCR_TOGGLE_SHIELDS_FWD_BACK_EVEN = 16,      /* Toggle Shields Fwd/Back/Even */
	HSCR_SELECT_VIEW_DIRECTION = 17,             /* Select View Direction */
	HSCR_TOGGLE_COCKPIT = 18,                    /* Toggle Cockpit */
	HSCR_RESET_TO_FORWARD_VIEW = 19,             /* Reset to Forward View */
	HSCR_FOLLOW_MISSILE_VIEW = 20,               /* Follow Missile View */
	HSCR_EXTERNAL_VIEW = 21,                     /* External View */
	HSCR_TOGGLE_CAMERA_PAN_MODE = 22,            /* Toggle Camera Pan Mode */
	HSCR_TOGGLE_THREAT_DISPLAY = 23,             /* Toggle Threat Display */
	HSCR_SELECT_NEXT_PREV_TARGET = 24,           /* Select Next/Prev Target */
	HSCR_SELECT_NEWEST_BOGIE = 25,               /* Select Newest Bogie */
	HSCR_SELECT_CLOSEST_BOGIE = 26,              /* Select Closest Bogie */
	HSCR_SELECT_NEAREST_ATTACKER = 27,           /* Select Nearest Attacker */
	HSCR_SELECT_ATTACKER_OF_CURRENT_TARGET = 28, /* Select Attacker of Current Target */
	HSCR_TOGGLE_TARGET_BOXES = 29,               /* Toggle Target Boxes */
	HSCR_SELECT_NEXT_PREV_COMPONENT = 30,        /* Select Next/Prev Component */
	HSCR_STORE_CURRENT_TARGET = 31,              /* Store current target */
	HSCR_RECALL_STORED_TARGET = 32,              /* Recall stored target */
	HSCR_SELECT_WEAPON = 33,                     /* Select Weapon */
	HSCR_CROSS_LINK_WEAPON = 34,                 /* Cross-link Weapon */
	HSCR_ACTIVATE_BEAM_WEAPON = 35,              /* Activate Beam Weapon */
	HSCR_CALIBRATE_JOYSTICK = 36,                /* Calibrate Joystick */
	HSCR_PAUSE_GAME = 37,                        /* Pause game */
	HSCR_SELECT_2X_4X_1X_TIME_ACCELERATION = 38, /* Select 2X/4X/1X Time Acceleration */
	HSCR_SET_GRAPHICS_DETAIL = 39,               /* Set Graphics Detail */
	HSCR_TOGGLE_MUSIC_SOUND_FX = 40,             /* Toggle Music/Sound FX */
	HSCR_TOGGLE_INFLIGHT_MAP = 41,               /* Toggle Inflight Map */
	HSCR_TOGGLE_MESSAGE_LOG = 42,                /* Toggle Message Log */
	HSCR_TOGGLE_MISSION_GOALS = 43,              /* Toggle Mission Goals */
	HSCR_TOGGLE_DAMAGE_ASSESSMENT = 44,          /* Toggle Damage Assessment */
	HSCR_TOGGLE_WINGMAN_ORDERS = 45,             /* Toggle Wingman Orders */
	HSCR_TOGGLE_KEY_REFERENCE = 46,              /* Toggle Key Reference */
	HSCR_TOGGLE_INFLIGHT_OPTIONS = 47,           /* Toggle Inflight Options */
} HelpScreenId;

/* MAPROOM NHI status labels (6 entries; cells 254..259) */
typedef enum NHIStatusId {
	NHI_SHOWN = 0,        /* Shown */
	NHI_ICONS = 1,        /* Icons */
	NHI_HIDDEN = 2,       /* Hidden */
	NHI_OH_ROSTILES = 3,  /* þOHþRostiles: */
	NHI_OI_JMPERIALS = 4, /* þOIþJmperials: */
	NHI_ON_FEUTRALS = 5,  /* þONþFeutrals: */
} NHIStatusId;

/* MAPROOM help labels (3 entries; cells 260..262) */
typedef enum MapRoomHelpId {
	MAPHELP_OSPACEBAR_V_TOGGLES_W2_D_V_3_D_MODE = 0, /* þOSPACEBARþV: Toggles þW2-DþV/3-D mode */
	MAPHELP_OSPACEBAR_V_TOGGLES_2_D_W3_D_V_MODE = 1, /* þOSPACEBARþV: Toggles 2-D/þW3-DþV mode */
	MAPHELP_OC_V_CENTERS_MAP_ON_OT_VARGET = 2,       /* þOCþV: Centers map on þOtþVarget */
} MapRoomHelpId;

/* OPTION option-name labels (14 entries; cells 477..490) */
typedef enum OptionStringId {
	OPT_GOURAUD_SHADING_AND_LIGHTING_IS = 0,       /*  Gouraud Shading and Lighting is: */
	OPT_STARFIGHTER_POLYGON_DETAIL_SETTING_I = 1,  /*  Starfighter Polygon Detail Setting is: */
	OPT_STARSHIP_POLYGON_DETAIL_SETTING_IS = 2,    /*  Starship Polygon Detail Setting is: */
	OPT_MARKING_AND_TEXTURE_POLYGONS_ARE = 3,      /*  Marking and Texture Polygons are: */
	OPT_BACKDROP_PLANETS_GALAXIES_AND_NEBULA = 4,  /*  Backdrop Planets, Galaxies and Nebulae are: */
	OPT_SPACE_DEBRIS_IS = 5,                       /*  Space Debris is: */
	OPT_ENGINE_GLOW_COLOR_CYCLING_IS = 6,          /*  Engine Glow Color Cycling is: */
	OPT_STARFIELD_AND_HYPERSPACE_DETAIL_SETT = 7,  /*  Starfield and Hyperspace Detail Setting is: */
	OPT_PLAYER_VS_STARFIGHTER_COLLISION_DAMA = 8,  /*  Player vs. Starfighter Collision Damage is: */
	OPT_PLAYER_S_SPACECRAFT_IS = 9,                /*  Player's Spacecraft is: */
	OPT_PLAYER_S_AMMUNITION_IS = 10,               /*  Player's Ammunition is: */
	OPT_INFLIGHT_SOUND_EFFECTS_VOLUME_SETTIN = 11, /*  Inflight Sound Effects Volume Setting is: */
	OPT_INFLIGHT_MUSIC_VOLUME_SETTING_IS = 12,     /*  Inflight Music Volume Setting is: */
	OPT_INFLIGHT_SPEECH_VOLUME_SETTING_IS = 13,    /*  Inflight Speech Volume Setting is: */
} OptionStringId;

/* OPTION setting-value labels (16 entries; cells 491..506) */
typedef enum SettingStringId {
	SET_OFF = 0,           /* Off */
	SET_ON = 1,            /* On */
	SET_LOW = 2,           /* Low */
	SET_HIGH = 3,          /* High */
	SET_LOW_2 = 4,         /* Low */
	SET_MEDIUM = 5,        /* Medium */
	SET_HIGH_2 = 6,        /* High */
	SET_LOWEST = 7,        /* Lowest */
	SET_LOW_3 = 8,         /* Low */
	SET_HIGH_3 = 9,        /* High */
	SET_HIGHEST = 10,      /* Highest */
	SET_VULNERABLE = 11,   /* Vulnerable */
	SET_INVULNERABLE = 12, /* Invulnerable */
	SET_LIMITED = 13,      /* Limited */
	SET_UNLIMITED = 14,    /* Unlimited */
	SET_DIST = 15,         /* DIST */
} SettingStringId;

/* PANEL waypoint labels (14 entries; cells 522..535) */
typedef enum WaypointStringId {
	WPT_START_POSITION_1 = 0,  /* START POSITION 1 */
	WPT_START_POSITION_2 = 1,  /* START POSITION 2 */
	WPT_START_POSITION_3 = 2,  /* START POSITION 3 */
	WPT_START_POSITION_4 = 3,  /* START POSITION 4 */
	WPT_WAYPOINT_1 = 4,        /* WAYPOINT 1 */
	WPT_WAYPOINT_2 = 5,        /* WAYPOINT 2 */
	WPT_WAYPOINT_3 = 6,        /* WAYPOINT 3 */
	WPT_WAYPOINT_4 = 7,        /* WAYPOINT 4 */
	WPT_WAYPOINT_5 = 8,        /* WAYPOINT 5 */
	WPT_WAYPOINT_6 = 9,        /* WAYPOINT 6 */
	WPT_WAYPOINT_7 = 10,       /* WAYPOINT 7 */
	WPT_WAYPOINT_8 = 11,       /* WAYPOINT 8 */
	WPT_RENDEZVOUS_POINT = 12, /* RENDEZVOUS POINT */
	WPT_HYPERSPACE_POINT = 13, /* HYPERSPACE POINT */
} WaypointStringId;

/* PANEL component-type labels (indexed by MeshType) (33 entries; cells 536..568) */
typedef enum ComponentNameId {
	COMP_HULL = 0,        /* HULL */
	COMP_HULL_2 = 1,      /* HULL */
	COMP_WING = 2,        /* WING */
	COMP_FUSELAGE = 3,    /* FUSELAGE */
	COMP_LASR_TUR = 4,    /* LASR TUR */
	COMP_LASR_GUN = 5,    /* LASR GUN */
	COMP_ENGINE = 6,      /* ENGINE */
	COMP_BRIDGE = 7,      /* BRIDGE */
	COMP_SHLD_GEN = 8,    /* SHLD GEN */
	COMP_ENRG_GEN = 9,    /* ENRG GEN */
	COMP_WHEAD_LN = 10,   /* WHEAD LN */
	COMP_COMM_SYS = 11,   /* COMM SYS */
	COMP_BEAM_SYS = 12,   /* BEAM SYS */
	COMP_COMM_SYS_2 = 13, /* COMM SYS */
	COMP_DOCK_PLT = 14,   /* DOCK PLT */
	COMP_LAND_PLT = 15,   /* LAND PLT */
	COMP_HANGAR = 16,     /* HANGAR */
	COMP_CARGO = 17,      /* CARGO */
	COMP_HULL_3 = 18,     /* HULL */
	COMP_ANTENNA = 19,    /* ANTENNA */
	COMP_WING_2 = 20,     /* WING */
	COMP_LASR_TUR_2 = 21, /* LASR TUR */
	COMP_WHEAD_LN_2 = 22, /* WHEAD LN */
	COMP_COMM_SYS_3 = 23, /* COMM SYS */
	COMP_BEAM_SYS_2 = 24, /* BEAM SYS */
	COMP_COMM_SYS_4 = 25, /* COMM SYS */
	COMP_COCKPIT = 26,    /* COCKPIT */
	COMP_HULL_4 = 27,     /* HULL */
	COMP_HULL_5 = 28,     /* HULL */
	COMP_HULL_6 = 29,     /* HULL */
	COMP_HULL_7 = 30,     /* HULL */
	COMP_HULL_8 = 31,     /* HULL */
	COMP_X_ = 32,         /* -------- */
} ComponentNameId;

/* REPLAY craft-status labels (9 entries; cells 569..577) */
typedef enum StatusStringId {
	STATUS_OK = 0,       /*       OK */
	STATUS_STOPPED = 1,  /*  STOPPED */
	STATUS_DISABLED = 2, /* DISABLED */
	STATUS_CAPTURED = 3, /* CAPTURED */
	STATUS_X_ = 4,       /*          */
	STATUS_HOMING = 5,   /*   HOMING */
	STATUS_SHLDS_DN = 6, /* SHLDS DN */
	STATUS_HULL_DMG = 7, /* HULL DMG */
	STATUS_WAITING = 8,  /*  WAITING */
} StatusStringId;

/* PANEL/MSG warhead-type labels (12 entries; cells 578..589) */
typedef enum WarheadStringId {
	WHEAD_TORPEDO = 0,      /* TORPEDO */
	WHEAD_MISSILE = 1,      /* MISSILE */
	WHEAD_LASER = 2,        /* LASER */
	WHEAD_LASER_2 = 3,      /* LASER */
	WHEAD_ION = 4,          /* ION */
	WHEAD_ADV_TORPEDO = 5,  /* ADV TORPEDO */
	WHEAD_ADV_MISSILE = 6,  /* ADV MISSILE */
	WHEAD_SPACE_BOMB = 7,   /* SPACE BOMB */
	WHEAD_ROCKET = 8,       /* ROCKET */
	WHEAD_MAG_PULSE = 9,    /* MAG PULSE */
	WHEAD_MAG_PULSE_2 = 10, /* MAG PULSE */
	WHEAD_MAG_PULSE_3 = 11, /* MAG PULSE */
} WarheadStringId;

/* PANEL/MSG buoy/static-object labels (15 entries; cells 591..605) */
typedef enum BuoyStringId {
	BUOY_COMM_SAT = 0,    /* Comm Sat */
	BUOY_COMM_SAT_2 = 1,  /* Comm Sat */
	BUOY_COMM_SAT_3 = 2,  /* Comm Sat */
	BUOY_COMM_SAT_4 = 3,  /* Comm Sat */
	BUOY_COMM_SAT_5 = 4,  /* Comm Sat */
	BUOY_MINE = 5,        /* Mine */
	BUOY_MINE_2 = 6,      /* Mine */
	BUOY_MINE_3 = 7,      /* Mine */
	BUOY_MINE_4 = 8,      /* Mine */
	BUOY_MINE_5 = 9,      /* Mine */
	BUOY_PROBE = 10,      /* Probe */
	BUOY_PROBE_2 = 11,    /* Probe */
	BUOY_PROBE_3 = 12,    /* Probe */
	BUOY_NAV_BUOY = 13,   /* Nav Buoy */
	BUOY_NAV_BUOY_2 = 14, /* Nav Buoy */
} BuoyStringId;

/* spec_data[].name_ptr (ship species labels) (69 entries; cells 606..674) */
typedef enum SpeciesNameId {
	SPECIES_PILOT = 0,                /* Pilot */
	SPECIES_X_WING = 1,               /* X-wing */
	SPECIES_Y_WING = 2,               /* Y-wing */
	SPECIES_A_WING = 3,               /* A-wing */
	SPECIES_B_WING = 4,               /* B-wing */
	SPECIES_TIE_FIGHTER = 5,          /* TIE Fighter */
	SPECIES_TIE_INTERCEPTOR = 6,      /* TIE Interceptor */
	SPECIES_TIE_BOMBER = 7,           /* TIE Bomber */
	SPECIES_TIE_ADVANCED = 8,         /* TIE Advanced */
	SPECIES_TIE_DEFENDER = 9,         /* TIE Defender */
	SPECIES_EMPTY = 10,               /* (empty) */
	SPECIES_EMPTY_2 = 11,             /* (empty) */
	SPECIES_MISSILE_BOAT = 12,        /* Missile Boat */
	SPECIES_T_WING = 13,              /* T-wing */
	SPECIES_Z_95_HEADHUNTER = 14,     /* Z-95 Headhunter */
	SPECIES_R_41_STARCHASER = 15,     /* R-41 Starchaser */
	SPECIES_ASSAULT_GUNBOAT = 16,     /* Assault Gunboat */
	SPECIES_SHUTTLE = 17,             /* Shuttle */
	SPECIES_ESCORT_SHUTTLE = 18,      /* Escort Shuttle */
	SPECIES_PATROL_CRAFT = 19,        /* Patrol Craft */
	SPECIES_SCOUT_CRAFT = 20,         /* Scout Craft */
	SPECIES_TRANSPORT = 21,           /* Transport */
	SPECIES_ASSAULT_TRANSPORT = 22,   /* Assault Transport */
	SPECIES_ESCORT_TRANSPORT = 23,    /* Escort Transport */
	SPECIES_TUG = 24,                 /* Tug */
	SPECIES_UTILITY_TUG = 25,         /* Utility Tug */
	SPECIES_CONTAINER_A = 26,         /* Container A */
	SPECIES_CONTAINER_B = 27,         /* Container B */
	SPECIES_CONTAINER_C = 28,         /* Container C */
	SPECIES_CONTAINER_D = 29,         /* Container D */
	SPECIES_HEAVY_LIFTER = 30,        /* Heavy Lifter */
	SPECIES_EMPTY_3 = 31,             /* (empty) */
	SPECIES_FREIGHTER = 32,           /* Freighter */
	SPECIES_CARGO_FERRY = 33,         /* Cargo Ferry */
	SPECIES_MODULAR_CONVEYOR = 34,    /* Modular Conveyor */
	SPECIES_CONTAINER_TRANSPORT = 35, /* Container Transport */
	SPECIES_MUURIAN_TRANSPORT = 36,   /* Muurian Transport */
	SPECIES_EMPTY_4 = 37,             /* (empty) */
	SPECIES_CORELLIAN_TRANSPORT = 38, /* Corellian Transport */
	SPECIES_EMPTY_5 = 39,             /* (empty) */
	SPECIES_CORELLIAN_CORVETTE = 40,  /* Corellian Corvette */
	SPECIES_MOD_CORVETTE = 41,        /* Mod. Corvette */
	SPECIES_NEBULON_B_FRIGATE = 42,   /* Nebulon B Frigate */
	SPECIES_NEBULON_B_2_FRIGATE = 43, /* Nebulon B-2 Frigate */
	SPECIES_C_3_PASSENGER_LINER = 44, /* C-3 Passenger Liner */
	SPECIES_CARRACK_CRUISER = 45,     /* Carrack Cruiser */
	SPECIES_STRIKE_CRUISER = 46,      /* Strike Cruiser */
	SPECIES_ESCORT_CARRIER = 47,      /* Escort Carrier */
	SPECIES_DREADNAUGHT = 48,         /* Dreadnaught */
	SPECIES_CALAMARI_CRUISER = 49,    /* Calamari Cruiser */
	SPECIES_LT_CALAMARI_CRUISER = 50, /* Lt Calamari Cruiser */
	SPECIES_INTERDICTOR = 51,         /* Interdictor */
	SPECIES_VICTORY_STAR_DEST = 52,   /* Victory Star Dest */
	SPECIES_IMPERIAL_STAR_DEST = 53,  /* Imperial Star Dest */
	SPECIES_SUPERSTAR = 54,           /* Superstar */
	SPECIES_CONTAINER_E = 55,         /* Container E */
	SPECIES_CONTAINER_F = 56,         /* Container F */
	SPECIES_CONTAINER_G = 57,         /* Container G */
	SPECIES_CONTAINER_H = 58,         /* Container H */
	SPECIES_CONTAINER_I = 59,         /* Container I */
	SPECIES_PLATFORM_XQ1 = 60,        /* Platform XQ1 */
	SPECIES_PLATFORM_XQ2 = 61,        /* Platform XQ2 */
	SPECIES_PLATFORM_XQ3 = 62,        /* Platform XQ3 */
	SPECIES_PLATFORM_XQ4 = 63,        /* Platform XQ4 */
	SPECIES_PLATFORM_XQ5 = 64,        /* Platform XQ5 */
	SPECIES_PLATFORM_XQ6 = 65,        /* Platform XQ6 */
	SPECIES_PLATFORM = 66,            /* Platform */
	SPECIES_PLATFORM_2 = 67,          /* Platform */
	SPECIES_PLATFORM_3 = 68,          /* Platform */
} SpeciesNameId;

/* WINGMAN command labels (10 entries; cells 677..686) */
typedef enum WingmanStringId {
	WMAN_SHIFT_A_ASSIGN_CURRENT_TARGET_TO_WIN = 0, /*  SHIFT-A    Assign current target to wingman. */
	WMAN_SHIFT_B_BOARD_ME_TO_RELOAD_AND_REPAI = 1, /*  SHIFT-B    Board me to reload and repair. */
	WMAN_SHIFT_C_COVER_ME = 2,                     /*  SHIFT-C    Cover me. */
	WMAN_SHIFT_E_EVASIVE_ACTION = 3,               /*  SHIFT-E    Evasive action! */
	WMAN_SHIFT_G_GO_AHEAD_CONTINUE_WITH_MISSI = 4, /*  SHIFT-G    Go ahead; continue with mission. */
	WMAN_SHIFT_H_HEAD_HOME = 5,                    /*  SHIFT-H    Head home. */
	WMAN_SHIFT_I_IGNORE_CURRENT_TARGET = 6,        /*  SHIFT-I    Ignore current target. */
	WMAN_SHIFT_R_REPORT_IN = 7,                    /*  SHIFT-R    Report in. */
	WMAN_SHIFT_S_SEND_REINFORCEMENTS = 8,          /*  SHIFT-S    Send reinforcements. */
	WMAN_SHIFT_W_WAIT_FOR_FURTHER_ORDERS = 9,      /*  SHIFT-W    Wait for further orders. */
} WingmanStringId;

#endif /* TIE_STRING_TABLE_IDS_H */
