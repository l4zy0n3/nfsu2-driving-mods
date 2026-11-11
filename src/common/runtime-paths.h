// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Yash Tripathi and contributors
#ifndef U2_RUNTIME_PATHS_H
#define U2_RUNTIME_PATHS_H
/* The Linux launcher supplies paths into an owner-only runtime directory. */
static int runtime_path(const char *variable,char path[MAX_PATH])
{
    DWORD n=GetEnvironmentVariableA(variable,path,MAX_PATH);
    if(!n||n>=MAX_PATH){path[0]=0;SetLastError(ERROR_BAD_PATHNAME);return 0;}
    return 1;
}
#endif
