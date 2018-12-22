#include <Windows.h>
#include <process.h>

#include "debug.h"

static HANDLE hMapFile;

int activate_shared_mem()
{
    hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(dbg_request_t), SHARED_MEM_NAME);

    if (hMapFile == NULL)
    {
        return -1;
    }

    dbg_req = (dbg_request_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dbg_request_t));

    if (dbg_req == NULL)
    {
        CloseHandle(hMapFile);
        return -1;
    }

    memset(dbg_req, 0, sizeof(dbg_request_t));

    return 0;
}

void deactivate_shared_mem()
{
    UnmapViewOfFile(dbg_req);
    CloseHandle(hMapFile);
    hMapFile = NULL;
    dbg_req = NULL;
}

void wrap_debugger()
{
    dbg_req->start_debugging = start_debugging;
    dbg_req->dbg_no_paused = CreateEvent(NULL, TRUE, FALSE, "GX_DBG_NO_PAUSED");
    dbg_req->dbg_has_event = CreateEvent(NULL, TRUE, FALSE, "GX_DBG_HAS_EVENT");
    dbg_req->dbg_has_no_req = CreateEvent(NULL, TRUE, TRUE, "GX_DBG_HAS_NO_REQ");
}

void unwrap_debugger()
{
    CloseHandle(dbg_req->dbg_no_paused);
    CloseHandle(dbg_req->dbg_has_event);
    CloseHandle(dbg_req->dbg_has_no_req);
}

int recv_dbg_event(int wait)
{
    int state = WaitForSingleObject(dbg_req->dbg_has_event, wait ? INFINITE : 0);
    if (!wait && state == WAIT_TIMEOUT)
        return 0;

    return 1;
}

void send_dbg_request(request_type_t type)
{
    dbg_req->req_type = type;
    ResetEvent(dbg_req->dbg_has_no_req);
    int state = WaitForSingleObject(dbg_req->dbg_has_no_req, INFINITE);
}
