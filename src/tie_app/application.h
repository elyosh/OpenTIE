#ifndef TIE_APP_APPLICATION_H
#define TIE_APP_APPLICATION_H

typedef struct TieLaunchOptions {
	const char* tie95_data;
	const char* tie98_data;
	const char* resource_root;
} TieLaunchOptions;

int TieApplication_Run(const TieLaunchOptions* launch);

#endif
