// SPDX-License-Identifier: Apache-2.0
#include "internal.hpp"
#include "ace_result.h"

namespace 
{ 
    thread_local std::string g_lastError; 
}

void nga_set_last_error(const std::string& m) 
{ 
    g_lastError = m; 
}

void nga_clear_last_error() 
{ 
    g_lastError.clear(); 
}

const char* nga_get_last_error() 
{ 
    return g_lastError.c_str(); 
}

NgaResult nga_translate_result(ACEResult aceSDKResult, const char* context) 
{
    if (aceSDKResult == ACEResultOk) return NGA_OK;

    NgaResult ngaSDKResult;

    switch (aceSDKResult) {
        case ACEResultNullContext: case ACEResultInvalidParameter: case ACEResultContextMismatch:
            ngaSDKResult = NGA_ERR_INVALID_ARG; break;

        case ACEResultInvalidDriverVersion: 
        case ACEResultNvIgiFailedToLoadSDK:

        case ACEResultNvIgiFailedToInitSLM: 
            ngaSDKResult = NGA_ERR_NO_GPU; 
            break;

        case ACEResultBufferTooSmall: 
            ngaSDKResult = NGA_ERR_BUFFER_SMALL; 
            break;

        case ACEResultInvalidFormat: 
            ngaSDKResult = NGA_ERR_INVALID_JSON; 
            break;

        case ACEResultRagDBNotFound: 

        case ACEResultEmbeddingModelNotFound: 
            ngaSDKResult = NGA_ERR_NOT_FOUND; 
            break;

        case ACEResultWaitTimeout: 

        case ACEResultInvalidState: 
            ngaSDKResult = NGA_ERR_NOT_READY; 
            break;

        case ACEResultSLMTokenizeFailed: 
        case ACEResultSLMDecodeFailed:

        case ACEResultSLMTokenToPieceFailed: 
        
        case ACEResultRagQueryFailed: 
            ngaSDKResult = NGA_ERR_INFERENCE; 
            break;

        default: 
            ngaSDKResult = NGA_ERR_INTERNAL; 
            break;
    }

    std::string msg = context ? context : "[ACE_SDK] call";
    msg += " failed (ACEResult=" + std::to_string((unsigned)aceSDKResult) + ")";
    nga_set_last_error(msg);

    return ngaSDKResult;
}