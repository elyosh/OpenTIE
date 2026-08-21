#include <stdint.h>

void* lmemptr_Create_System_Pointer(uint16_t size);
void* lmemptr_Alloc_System_Pointer(uint16_t size);
void* lmemptr_Alloc_Clear_System_Pointer(uint16_t size);
void lmemptr_Free_System_Pointer(void* ptr);
char* lmemptr_Duplicate_String(const char* value);
