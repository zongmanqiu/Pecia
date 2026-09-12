// PipeSecurity.h - shared helper: build a SECURITY_ATTRIBUTES that grants
// named-pipe access to the current user only.
//
// Security context: Pecia's IPC pipes (\\.\pipe\pecia-lua-<pid>,
// \\.\pipe\pecia-ai-push-<pid>, \\.\pipe\pecia-lua-tool) previously passed
// nullptr for CreateNamedPipe's security attributes, i.e. the creator's
// default DACL. Combined with the full Lua library load (os.execute, io,
// package.loadlib - see lua_api.txt: the Lua engine is intentionally NOT a
// sandbox), any process that can reach those pipes can execute arbitrary
// code as the user running Pecia. Restricting the pipe DACL to the current
// user SID removes that entry point for other users and lower-integrity
// processes.
#pragma once

#if defined(_WIN32)

#include <windows.h>
#include <aclapi.h>

#include <vector>

namespace pipeSec {

// Fill `sa`/`sd` and allocate `acl` (caller must LocalFree(acl) once done
// with the attributes, e.g. right after CreateNamedPipe) so that the pipe
// rejects everyone except the current user. Returns false on failure.
inline bool makeUserOnlySa(SECURITY_DESCRIPTOR &sd, SECURITY_ATTRIBUTES &sa, PACL &acl) {
    acl = nullptr;

    // Current user's SID from the process token.
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    TOKEN_USER *tu = reinterpret_cast<TOKEN_USER *>(buf.data());
    BOOL tokOk = GetTokenInformation(hToken, TokenUser, tu, len, &len);
    CloseHandle(hToken);
    if (!tokOk || len == 0) return false;

    // DACL entries, evaluated in order: explicit DENY for Everyone first
    // (so it wins over any later implicit grants), then GRANT for the
    // current user. "S-1-1-0" is the Everyone well-known SID.
    EXPLICIT_ACCESS ea[2] = {};
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = DENY_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName = const_cast<LPWSTR>(L"S-1-1-0");

    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = GRANT_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(tu->User.Sid);

    if (SetEntriesInAcl(2, ea, nullptr, &acl) != ERROR_SUCCESS)
        return false;
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
        LocalFree(acl); acl = nullptr;
        return false;
    }
    if (!SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)) {
        LocalFree(acl); acl = nullptr;
        return false;
    }
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;
    return true;
}

} // namespace pipeSec

#endif // _WIN32