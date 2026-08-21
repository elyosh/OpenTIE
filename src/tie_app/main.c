#include "tie_app/application.h"

#include "aeron/main.h"

#include <stdio.h>
#include <string.h>

static int TieMain_Usage(const char* message, const char* argument) {
	if (message && argument)
		fprintf(stderr, "[tie-aeron] "), fprintf(stderr, message, argument), fputc('\n', stderr);
	else if (message)
		fprintf(stderr, "[tie-aeron] %s\n", message);
	fprintf(stderr, "Usage: tie [--tie95-data <dir>] [--tie98-data <dir>] [--resource-root <dir>]\n");
	return 2;
}

static int TieMain_ParseOptions(int argc, char* argv[], TieLaunchOptions* out) {
	static const char* const names[] = { "--tie95-data", "--tie98-data", "--resource-root" };
	const char** destinations[] = { &out->tie95_data, &out->tie98_data, &out->resource_root };
	memset(out, 0, sizeof *out);
	for (int index = 1; index < argc; ++index) {
		const char* argument = argv[index];
		int option = -1;
		const char* value = NULL;
		for (int candidate = 0; candidate < 3; ++candidate) {
			const size_t length = strlen(names[candidate]);
			if (strcmp(argument, names[candidate]) == 0 ||
				(strncmp(argument, names[candidate], length) == 0 && argument[length] == '=')) {
				option = candidate;
				value = argument[length] == '=' ? argument + length + 1 : NULL;
				break;
			}
		}
		if (option < 0)
			return TieMain_Usage(argument[0] == '-' ? "unknown option: %s"
													: "positional argument is not accepted: %s",
								 argument);
		if (*destinations[option])
			return TieMain_Usage("duplicate option: %s", names[option]);
		if (!value && ++index < argc && strncmp(argv[index], "--", 2) != 0)
			value = argv[index];
		if (!value || !value[0])
			return TieMain_Usage("option requires a directory: %s", names[option]);
		*destinations[option] = value;
	}
	return 0;
}

int main(int argc, char* argv[]) {
	TieLaunchOptions launch;
	const int status = TieMain_ParseOptions(argc, argv, &launch);
	return status ? status : TieApplication_Run(&launch);
}
