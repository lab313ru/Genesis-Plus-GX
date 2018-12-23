#include <Windows.h>
#include <process.h>

#include "debug_wrap.h"

static HANDLE hMapFile = NULL, hStartFunc = NULL;

dbg_request_t *open_shared_mem()
{
    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);

    if (hMapFile == NULL)
    {
        return NULL;
    }

    dbg_request_t *request = (dbg_request_t *)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dbg_request_t));

    if (request == NULL)
    {
        CloseHandle(hMapFile);
        return NULL;
    }

    return request;
}

void close_shared_mem(dbg_request_t **request)
{
    UnmapViewOfFile(*request);
    CloseHandle(hMapFile);
    hMapFile = NULL;
    *request = NULL;
}

int recv_dbg_event(dbg_request_t *request, int wait)
{
    while (1)
    {
        for (int i = 0; i < ((request->dbg_events_count > 0) ? MAX_BREAKPOINTS : 0); ++i)
        {
            if (request->dbg_events[i].type != DBG_EVT_NO_EVENT)
            {
                request->dbg_events_count -= 1;
                return i;
            }
        }

        if (!wait)
            return -1;
        Sleep(1);
    }
}

void send_dbg_request(dbg_request_t *request, request_type_t type)
{
    request->req_type = type;

    while (request->dbg_active && request->req_type != REQ_NO_REQUEST)
    {
        Sleep(1);
    }
}
