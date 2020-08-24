#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#endif

#include "debug_wrap.h"

#ifdef _WIN32
static HANDLE hMapFile = NULL;
#else
static int shm;
#endif

dbg_request_t *open_shared_mem()
{
    dbg_request_t *request;

#ifdef _WIN32
    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);

    if (hMapFile == NULL)
        return NULL;

    request = (dbg_request_t *)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dbg_request_t));

    if (request == NULL)
    {
        CloseHandle(hMapFile);
        return NULL;
    }

#else
    shm = shm_open(SHARED_MEM_NAME, O_RDWR, 0777);

    if (shm == -1)
        return NULL;

    request = mmap(NULL, sizeof(dbg_request_t), PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0);

    if (request == MAP_FAILED) {
        close(shm);
        shm_unlink(SHARED_MEM_NAME);
        return NULL;
    }
#endif

    return request;
}

void close_shared_mem(dbg_request_t **request)
{
#ifdef _WIN32
    UnmapViewOfFile(*request);
    CloseHandle(hMapFile);
    hMapFile = NULL;
    *request = NULL;
#else
    munmap(*request, sizeof(dbg_request_t));
    close(shm);
    shm_unlink(SHARED_MEM_NAME);
#endif
}

int recv_dbg_event(dbg_request_t *request, int wait, int pop)
{
    while (request->dbg_active || request->dbg_events_count)
    {
        for (int i = 0; i < MAX_DBG_EVENTS; ++i)
        {
            if (request->dbg_events[i].type != DBG_EVT_NO_EVENT)
            {
                if (pop) {
                    request->dbg_events_count -= 1;
                }
                return i;
            }
        }

        if (!wait)
            return -1;
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10 * 1000);
#endif
    }

    return -1;
}

void send_dbg_request(dbg_request_t *request, request_type_t type)
{
    if (!request)
        return;

    request->req_type = type;

    while (request->dbg_active && request->req_type != REQ_NO_REQUEST)
    {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10 * 1000);
#endif
    }
}
