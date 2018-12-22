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
}

void unwrap_debugger()
{
}

int recv_dbg_event(int wait)
{
    while (dbg_req->dbg_evt.type == DBG_EVT_NO_EVENT)
    {
        if (!wait)
            return 0;
        Sleep(10);
    }

    return 1;
}

void send_dbg_request(request_type_t type)
{
    dbg_req->req_type = type;

    while (dbg_req->req_type != REQ_NO_REQUEST)
    {
        Sleep(10);
    }
}
