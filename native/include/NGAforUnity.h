// SPDX-License-Identifier: Apache-2.0
#ifndef NGAFORUNITY_H
    #define NGAFORUNITY_H

#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef NGAFORUNITY_EXPORTS
#define NGA_API __declspec(dllexport)
#else
#define NGA_API __declspec(dllimport)
#endif

#define NGA_ABI_MAJOR 0
#define NGA_ABI_MINOR 3
#define NGA_ABI_VERSION ((NGA_ABI_MAJOR << 16) | NGA_ABI_MINOR)

        typedef enum NgaResult {
            NGA_OK = 0, NGA_ERR_INVALID_ARG = 1, NGA_ERR_NO_GPU = 2, NGA_ERR_MODEL_LOAD = 3,
            NGA_ERR_INFERENCE = 4, NGA_ERR_BUFFER_SMALL = 5, NGA_ERR_NOT_READY = 6,
            NGA_ERR_INVALID_JSON = 7, NGA_ERR_NOT_FOUND = 8, NGA_ERR_INTERNAL = 99
        } NgaResult;

        typedef struct NgaRuntime* NgaRuntimeHandle;
        
        typedef void(__cdecl* NgaTokenCallback)(const char* tokenUtf8, void* userData);

        NGA_API uint32_t    Nga_AbiVersion(void);
        NGA_API const char* Nga_LastError(void);

        NGA_API NgaResult NgaRuntime_Create(const char* configJson, NgaRuntimeHandle* out);
        NGA_API void      NgaRuntime_Destroy(NgaRuntimeHandle rt);
        NGA_API NgaResult NgaRuntime_GetInfo(NgaRuntimeHandle rt, char* outJson, int32_t len, int32_t* written);
        NGA_API NgaResult NgaChat_Generate(NgaRuntimeHandle rt, const char* messagesJson,
            const char* paramsJson, char* outBuf, int32_t len, int32_t* written);

        NGA_API NgaResult NgaChat_GenerateStream(NgaRuntimeHandle rt, const char* messagesJson,
            const char* paramsJson, NgaTokenCallback onToken, void* userData);

#ifdef __cplusplus
    }
#endif
#endif // NGAFORUNITY_H
