#ifndef LANDRU_ERROR_H
#define LANDRU_ERROR_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*LandruBailFunc)(void);
typedef int16_t (*LandruEscapeFunc)(void);

void lerror_Do_Landru_Bail(void);
void lerror_Set_Landru_Bail_Function(LandruBailFunc fn);

void lerror_Clear_Landru_Exit(void);
int16_t lerror_Get_Landru_Exit(void);
void lerror_Set_Landru_Exit(int16_t code);
bool lerror_Is_Landru_Exit(void);
bool lerror_Is_Landru_Running(void);

void lerror_Clear_Landru_Escape(void);
int16_t lerror_Do_Landru_Escape(void);
int16_t lerror_Get_Landru_Escape(void);
void lerror_Set_Landru_Escape(int16_t value);
void lerror_Get_Landru_Escape_Function(LandruEscapeFunc* out);
void lerror_Set_Landru_Escape_Function(LandruEscapeFunc fn);

void lerror_Clear_Landru_Error(void);
int16_t lerror_Get_Landru_Error(void);
void lerror_Set_Landru_Error(int16_t code);
bool lerror_Is_Landru_Error(void);

void lerror_Set_Assertion_Level(int16_t level);
int16_t lerror_Get_Assertion_Level(void);

void lerror_Assert_Error(const char* file, uint16_t line, int16_t group, int16_t expr, int level);
void lerror_Assert_Error_Message(const char* file, uint16_t line, int16_t group, int16_t expr,
								 const char* msg, int level);
void lerror_Display_Assert(const char* file, uint16_t line, int group, const char* msg);

/* Global used by assert macros in other modules */
extern int16_t assertion_level;

#endif
