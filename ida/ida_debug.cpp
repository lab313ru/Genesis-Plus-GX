#include <ida.hpp>
#include <idd.hpp>
#include <auto.hpp>
#include <funcs.hpp>
#include <idp.hpp>
#include <dbg.hpp>

#include "ida_debmod.h"

#include "debug_wrap.h"

#define BREAKPOINTS_BASE 0x00D00000

static dbg_request_t *dbg_req = NULL;

static void pause_execution()
{
    send_dbg_request(dbg_req, REQ_PAUSE);
}

static void continue_execution()
{
    send_dbg_request(dbg_req, REQ_RESUME);
}

static void stop_debugging()
{
    send_dbg_request(dbg_req, REQ_STOP);
}

typedef qvector<std::pair<uint32, bool>> codemap_t;

static codemap_t g_codemap;
eventlist_t g_events;
static qthread_t events_thread = NULL;

static const char *const SRReg[] =
{
    "C",
    "V",
    "Z",
    "N",
    "X",
    NULL,
    NULL,
    NULL,
    "I",
    "I",
    "I",
    NULL,
    NULL,
    "S",
    NULL,
    "T"
};

static const char *const ALLOW_FLAGS_DA[] =
{
    "_A07",
    "_A06",
    "_A05",
    "_A04",
    "_A03",
    "_A02",
    "_A01",
    "_A00",

    "_D07",
    "_D06",
    "_D05",
    "_D04",
    "_D03",
    "_D02",
    "_D01",
    "_D00",
};

static const char *const ALLOW_FLAGS_V[] =
{
    "_V23",
    "_V22",
    "_V21",
    "_V20",
    "_V19",
    "_V18",
    "_V17",
    "_V16",
    "_V15",
    "_V14",
    "_V13",
    "_V12",
    "_V11",
    "_V10",
    "_V09",
    "_V08",
    "_V07",
    "_V06",
    "_V05",
    "_V04",
    "_V03",
    "_V02",
    "_V01",
    "_V00",
};

#define RC_GENERAL 1
#define RC_VDP 2
#define RC_BREAK 4

register_info_t registers[] =
{
    { "D0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "D7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

    { "A0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "A7", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },

    { "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_dword, NULL, 0 },

    { "SR", NULL, RC_GENERAL, dt_word, SRReg, 0xFFFF },

    { "DMA_LEN", REGISTER_READONLY, RC_GENERAL, dt_word, NULL, 0 },
    { "DMA_SRC", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
    { "VDP_DST", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },

    // Register Breakpoints
    { "D00", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D01", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D02", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D03", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D04", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D05", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D06", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "D07", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },

    { "A00", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A01", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A02", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A03", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A04", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A05", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A06", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },
    { "A07", REGISTER_ADDRESS, RC_BREAK, dt_dword, NULL, 0 },

    { "V00", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V01", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V02", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V03", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V04", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V05", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V06", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V07", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V08", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V09", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V10", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V11", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V12", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V13", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V14", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V15", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V16", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V17", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V18", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V19", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V20", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V21", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V22", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "V23", NULL, RC_BREAK, dt_byte, NULL, 0 },
    { "ALLOW0", NULL, RC_BREAK, dt_word, ALLOW_FLAGS_DA, 0xFFFF },
    { "ALLOW1", NULL, RC_BREAK, dt_3byte, ALLOW_FLAGS_V, 0xFFFFFF },

    // VDP Registers
    { "Set1", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set2", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "PlaneA", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Window", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "PlaneB", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Sprite", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg6", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "BgClr", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg8", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg9", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "HInt", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set3", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Set4", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "HScrl", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "Reg14", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WrInc", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "ScrSz", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WinX", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "WinY", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "LenLo", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "LenHi", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcLo", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcMid", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "SrcHi", NULL, RC_VDP, dt_byte, NULL, 0 },
};

static const char *register_classes[] =
{
    "General Registers",
    "VDP Registers",
    "Register Breakpoints",
    NULL
};

static void prepare_codemap()
{
    g_codemap.resize(MAXROMSIZE);
    for (size_t i = 0; i < MAXROMSIZE; ++i)
    {
        g_codemap[i] = std::pair<uint32, bool>(BADADDR, false);
    }
}

static void apply_codemap()
{
    if (g_codemap.empty()) return;

    msg("Applying codemap...\n");
    for (size_t i = 0; i < MAXROMSIZE; ++i)
    {
        if (g_codemap[i].second && g_codemap[i].first)
        {
            auto_make_code((ea_t)i);
            noUsed((ea_t)i);
        }
        showAddr((ea_t)i);
    }
    noUsed(0, MAXROMSIZE);

    for (size_t i = 0; i < MAXROMSIZE; ++i)
    {
        if (g_codemap[i].second && g_codemap[i].first && !get_func((ea_t)i))
        {
            if (add_func(i, BADADDR))
                add_cref(g_codemap[i].first, i, fl_CN);
            noUsed((ea_t)i);
        }
        showAddr((ea_t)i);
    }
    noUsed(0, MAXROMSIZE);
    msg("Codemap applied.\n");
}

static void finish_execution()
{
    if (events_thread != NULL)
    {
        qthread_join(events_thread);
        qthread_free(events_thread);
        qthread_kill(events_thread);
        events_thread = NULL;
    }
}

// Initialize debugger
// Returns true-success
// This function is called from the main thread
static bool idaapi init_debugger(const char *hostname,
    int port_num,
    const char *password)
{
    set_processor_type(ph.psnames[0], SETPROC_COMPAT); // reset proc to "M68000"
    return true;
}

// Terminate debugger
// Returns true-success
// This function is called from the main thread
static bool idaapi term_debugger(void)
{
    return true;
}

// Return information about the n-th "compatible" running process.
// If n is 0, the processes list is reinitialized.
// 1-ok, 0-failed, -1-network error
// This function is called from the main thread
static int idaapi process_get_info(int n, process_info_t *info)
{
    return 0;
}

static int idaapi check_debugger_events(void *ud)
{
    while (dbg_req->dbg_active || dbg_req->dbg_events_count)
    {
        int event_index = recv_dbg_event(dbg_req, 0);
        if (event_index == -1)
        {
            qsleep(10);
            continue;
        }

        debugger_event_t *dbg_event = &dbg_req->dbg_events[event_index];

        debug_event_t ev;
        switch (dbg_event->type)
        {
        case DBG_EVT_STARTED:
            ev.eid = PROCESS_START;
            ev.pid = 1;
            ev.tid = 1;
            ev.ea = BADADDR;
            ev.handled = true;

            ev.modinfo.name[0] = 'G';
            ev.modinfo.name[1] = 'P';
            ev.modinfo.name[2] = 'G';
            ev.modinfo.name[3] = 'X';
            ev.modinfo.name[4] = '\0';
            ev.modinfo.base = 0;
            ev.modinfo.size = 0;
            ev.modinfo.rebase_to = BADADDR;

            g_events.enqueue(ev, IN_FRONT);
            break;
        case DBG_EVT_PAUSED:
            ev.pid = 1;
            ev.tid = 1;
            ev.ea = dbg_event->pc;
            ev.handled = true;
            ev.eid = PROCESS_SUSPEND;
            g_events.enqueue(ev, IN_BACK);
            break;
        case DBG_EVT_BREAK:
            ev.pid = 1;
            ev.tid = 1;
            ev.ea = dbg_event->pc;
            ev.handled = true;
            ev.eid = BREAKPOINT;
            ev.bpt.hea = ev.bpt.kea = ev.ea;
            g_events.enqueue(ev, IN_BACK);
            break;
        case DBG_EVT_STEP:
            ev.pid = 1;
            ev.tid = 1;
            ev.ea = dbg_event->pc;
            ev.handled = true;
            ev.eid = STEP;
            g_events.enqueue(ev, IN_BACK);
            break;
        case DBG_EVT_STOPPED:
            ev.eid = PROCESS_EXIT;
            ev.pid = 1;
            ev.handled = true;
            ev.exit_code = 0;

            g_events.enqueue(ev, IN_BACK);
            break;
        default:
            break;
        }

        dbg_event->type = DBG_EVT_NO_EVENT;
        qsleep(10);
    }

    return 0;
}

// Start an executable to debug
// 1 - ok, 0 - failed, -2 - file not found (ask for process options)
// 1|CRC32_MISMATCH - ok, but the input file crc does not match
// -1 - network error
// This function is called from debthread
static int idaapi start_process(const char *path,
    const char *args,
    const char *startdir,
    int dbg_proc_flags,
    const char *input_path,
    uint32 input_file_crc32)
{
    g_events.clear();

    dbg_req = open_shared_mem();
    dbg_req->is_ida = 1;

    if (!dbg_req)
    {
        show_wait_box("HIDECANCEL\nWaiting for connection to plugin...");

        while (!dbg_req)
        {
            dbg_req = open_shared_mem();
        }

        hide_wait_box();
    }

    events_thread = qthread_create(check_debugger_events, NULL);

    send_dbg_request(dbg_req, REQ_ATTACH);

    return 1;
}

// rebase database if the debugged program has been rebased by the system
// This function is called from the main thread
static void idaapi rebase_if_required_to(ea_t new_base)
{
}

// Prepare to pause the process
// This function will prepare to pause the process
// Normally the next get_debug_event() will pause the process
// If the process is sleeping then the pause will not occur
// until the process wakes up. The interface should take care of
// this situation.
// If this function is absent, then it won't be possible to pause the program
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi prepare_to_pause_process(void)
{
    pause_execution();
    return 1;
}

// Stop the process.
// May be called while the process is running or suspended.
// Must terminate the process in any case.
// The kernel will repeatedly call get_debug_event() and until PROCESS_EXIT.
// In this mode, all other events will be automatically handled and process will be resumed.
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi emul_exit_process(void)
{
    stop_debugging();
    finish_execution();
    dbg_req->is_ida = 0;
    close_shared_mem(&dbg_req);

    return 1;
}

// Get a pending debug event and suspend the process
// This function will be called regularly by IDA.
// This function is called from debthread
static gdecode_t idaapi get_debug_event(debug_event_t *event, int timeout_ms)
{
    while (true)
    {
        // are there any pending events?
        if (g_events.retrieve(event))
        {
            return g_events.empty() ? GDE_ONE_EVENT : GDE_MANY_EVENTS;
        }
        if (g_events.empty())
            break;
    }
    return GDE_NO_EVENT;
}

// Continue after handling the event
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi continue_after_event(const debug_event_t *event)
{
    ui_notification_t req = get_running_request();
    switch (event->eid)
    {
    case STEP:
    case BREAKPOINT:
    case PROCESS_SUSPEND:
        if (req == ui_null)
            continue_execution();
    break;
    }

    return 1;
}

// The following function will be called by the kernel each time
// when it has stopped the debugger process for some reason,
// refreshed the database and the screen.
// The debugger module may add information to the database if it wants.
// The reason for introducing this function is that when an event line
// LOAD_DLL happens, the database does not reflect the memory state yet
// and therefore we can't add information about the dll into the database
// in the get_debug_event() function.
// Only when the kernel has adjusted the database we can do it.
// Example: for imported PE DLLs we will add the exported function
// names to the database.
// This function pointer may be absent, i.e. NULL.
// This function is called from the main thread
static void idaapi stopped_at_debug_event(bool dlls_added)
{
}

// The following functions manipulate threads.
// 1-ok, 0-failed, -1-network error
// These functions are called from debthread
static int idaapi thread_suspend(thid_t tid) // Suspend a running thread
{
    return 0;
}

static int idaapi thread_continue(thid_t tid) // Resume a suspended thread
{
    return 0;
}

static int idaapi set_step_mode(thid_t tid, resume_mode_t resmod) // Run one instruction in the thread
{
    switch (resmod)
    {
    case RESMOD_INTO:    ///< step into call (the most typical single stepping)
        send_dbg_request(dbg_req, REQ_STEP_INTO);
        break;
    case RESMOD_OVER:    ///< step over call
        send_dbg_request(dbg_req, REQ_STEP_OVER);
        break;
    }

    return 1;
}

// Read thread registers
//	tid	- thread id
//	clsmask- bitmask of register classes to read
//	regval - pointer to vector of regvals for all registers
//			 regval is assumed to have debugger_t::registers_size elements
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi read_registers(thid_t tid, int clsmask, regval_t *values)
{
    if (!dbg_req)
        return 0;

    if (clsmask & RC_GENERAL)
    {
        dbg_req->regs_data.type = REG_TYPE_M68K;
        send_dbg_request(dbg_req, REQ_GET_REGS);

        regs_68k_data_t *reg_vals = &dbg_req->regs_data.regs_68k.values;

        values[0].ival = reg_vals->d0;
        values[1].ival = reg_vals->d1;
        values[2].ival = reg_vals->d2;
        values[3].ival = reg_vals->d3;
        values[4].ival = reg_vals->d4;
        values[5].ival = reg_vals->d5;
        values[6].ival = reg_vals->d6;
        values[7].ival = reg_vals->d7;

		values[8].ival = reg_vals->a0;
		values[9].ival = reg_vals->a1;
		values[10].ival = reg_vals->a2;
		values[11].ival = reg_vals->a3;
		values[12].ival = reg_vals->a4;
		values[13].ival = reg_vals->a5;
		values[14].ival = reg_vals->a6;
		values[15].ival = reg_vals->a7;

        values[16].ival = reg_vals->pc & 0xFFFFFF;
        values[17].ival = reg_vals->sr;
    }

    if (clsmask & RC_VDP)
    {
    }

    if (clsmask & RC_BREAK)
    {
    }

    return 1;
}

static void set_m68k_reg(int reg_index, unsigned int value)
{
    dbg_req->regs_data.type = REG_TYPE_M68K;
    dbg_req->regs_data.any_reg.index = reg_index;
    dbg_req->regs_data.any_reg.val = value;
    send_dbg_request(dbg_req, REQ_SET_REG);
}

// Write one thread register
//	tid	- thread id
//	regidx - register index
//	regval - new value of the register
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi write_register(thid_t tid, int regidx, const regval_t *value)
{
    if (regidx >= 0 && regidx <= 7)
    {
        set_m68k_reg(regidx - 0, (uint32)value->ival);
    }
    else if (regidx >= 8 && regidx <= 15)
    {
        set_m68k_reg(regidx - 8, (uint32)value->ival);
    }
    else if (regidx == 15)
    {
        set_m68k_reg(16, (uint32)value->ival & 0xFFFFFF);
    }
    else if (regidx == 16)
    {
        set_m68k_reg(17, (uint16)value->ival);
    }

    return 1;
}

//
// The following functions manipulate bytes in the memory.
//
// Get information on the memory areas
// The debugger module fills 'areas'. The returned vector MUST be sorted.
// Returns:
//   -3: use idb segmentation
//   -2: no changes
//   -1: the process does not exist anymore
//	0: failed
//	1: new memory layout is returned
// This function is called from debthread
static int idaapi get_memory_info(meminfo_vec_t &areas)
{
    memory_info_t info;

    // Don't remove this loop
    for (int i = 0; i < get_segm_qty(); ++i)
    {
        char buf[256];

        segment_t *segm = getnseg(i);

        info.startEA = segm->startEA;
        info.endEA = segm->endEA;

        get_segm_name(segm, buf, sizeof(buf));
        info.name = buf;

        get_segm_class(segm, buf, sizeof(buf));
        info.sclass = buf;

        info.sbase = 0;
        info.perm = SEGPERM_READ | SEGPERM_WRITE;
        info.bitness = 1;
        areas.push_back(info);
    }
    // Don't remove this loop

    return 1;
}

// Read process memory
// Returns number of read bytes
// 0 means read error
// -1 means that the process does not exist anymore
// This function is called from debthread
static ssize_t idaapi read_memory(ea_t ea, void *buffer, size_t size)
{
    if ((ea >= 0xA00000 && ea < 0xA0FFFF))
    {
        dbg_req->mem_data.address = ea;
        dbg_req->mem_data.size = size;
        send_dbg_request(dbg_req, REQ_READ_Z80);

        memcpy(buffer, &dbg_req->mem_data.z80_ram[ea & 0x1FFF], size);
        // Z80
    }
    else if (ea < MAXROMSIZE)
    {
        dbg_req->mem_data.address = ea;
        dbg_req->mem_data.size = size;
        send_dbg_request(dbg_req, REQ_READ_68K_ROM);

        memcpy(buffer, &dbg_req->mem_data.m68k_rom[ea], size);
    }
    else if ((ea >= 0xFF0000 && ea < 0x1000000))
    {
        dbg_req->mem_data.address = ea;
        dbg_req->mem_data.size = size;
        send_dbg_request(dbg_req, REQ_READ_68K_RAM);

        memcpy(buffer, &dbg_req->mem_data.m68k_ram[ea & 0xFFFF], size);
        // RAM
    }

    return size;
}
// Write process memory
// Returns number of written bytes, -1-fatal error
// This function is called from debthread
static ssize_t idaapi write_memory(ea_t ea, const void *buffer, size_t size)
{
    return 0;
}

// Is it possible to set breakpoint?
// Returns: BPT_...
// This function is called from debthread or from the main thread if debthread
// is not running yet.
// It is called to verify hardware breakpoints.
static int idaapi is_ok_bpt(bpttype_t type, ea_t ea, int len)
{
    switch (type)
    {
        //case BPT_SOFT:
    case BPT_EXEC:
    case BPT_READ: // there is no such constant in sdk61
    case BPT_WRITE:
    case BPT_RDWR:
        return BPT_OK;
    }

    return BPT_BAD_TYPE;
}

// Add/del breakpoints.
// bpts array contains nadd bpts to add, followed by ndel bpts to del.
// returns number of successfully modified bpts, -1-network error
// This function is called from debthread
static int idaapi update_bpts(update_bpt_info_t *bpts, int nadd, int ndel)
{
    for (int i = 0; i < nadd; ++i)
    {
        ea_t start = bpts[i].ea;
        ea_t end = bpts[i].ea + bpts[i].size - 1;
        
        bpt_data_t *bpt_data = &dbg_req->bpt_data;

        switch (bpts[i].type)
        {
        case BPT_EXEC:
            bpt_data->type = BPT_M68K_E;
            break;
        case BPT_READ:
            bpt_data->type = BPT_M68K_R;
            break;
        case BPT_WRITE:
            bpt_data->type = BPT_M68K_W;
            break;
        case BPT_RDWR:
            bpt_data->type = BPT_M68K_RW;
            break;
        }

        bpt_data->address = start;
        bpt_data->width = bpts[i].size;
        send_dbg_request(dbg_req, REQ_ADD_BREAK);

        bpts[i].code = BPT_OK;
    }

    for (int i = 0; i < ndel; ++i)
    {
        ea_t start = bpts[nadd + i].ea;
        ea_t end = bpts[nadd + i].ea + bpts[nadd + i].size - 1;

        bpt_data_t *bpt_data = &dbg_req->bpt_data;

        switch (bpts[nadd + i].type)
        {
        case BPT_EXEC:
            bpt_data->type = BPT_M68K_E;
            break;
        case BPT_READ:
            bpt_data->type = BPT_M68K_R;
            break;
        case BPT_WRITE:
            bpt_data->type = BPT_M68K_W;
            break;
        case BPT_RDWR:
            bpt_data->type = BPT_M68K_RW;
            break;
        }

        bpt_data->address = start;
        send_dbg_request(dbg_req, REQ_DEL_BREAK);

        bpts[nadd + i].code = BPT_OK;
    }

    return (ndel + nadd);
}

//--------------------------------------------------------------------------
//
//	  DEBUGGER DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------

debugger_t debugger =
{
    IDD_INTERFACE_VERSION,
    "GXIDA", // Short debugger name
    0x8000 + 1, // Debugger API module id
    "m68k", // Required processor name
    DBG_FLAG_NOHOST | DBG_FLAG_CAN_CONT_BPT | DBG_FLAG_FAKE_ATTACH | DBG_FLAG_SAFE | DBG_FLAG_NOPASSWORD | DBG_FLAG_NOSTARTDIR | DBG_FLAG_CONNSTRING | DBG_FLAG_ANYSIZE_HWBPT | DBG_FLAG_DEBTHREAD,

    register_classes, // Array of register class names
    RC_GENERAL, // Mask of default printed register classes
    registers, // Array of registers
    qnumber(registers), // Number of registers

    0x1000, // Size of a memory page

    NULL, // bpt_bytes, // Array of bytes for a breakpoint instruction
    NULL, // bpt_size, // Size of this array
    0, // for miniidbs: use this value for the file type after attaching

    DBG_RESMOD_STEP_INTO | DBG_RESMOD_STEP_OVER, // Resume modes

    init_debugger,
    term_debugger,

    process_get_info,

    start_process,
    NULL, // attach_process,
    NULL, // detach_process,
    rebase_if_required_to,
    prepare_to_pause_process,
    emul_exit_process,

    get_debug_event,
    continue_after_event,

    NULL, // set_exception_info
    stopped_at_debug_event,

    thread_suspend,
    thread_continue,
    set_step_mode,

    read_registers,
    write_register,

    NULL, // thread_get_sreg_base

    get_memory_info,
    read_memory,
    write_memory,

    is_ok_bpt,
    update_bpts,
    NULL,

    NULL, // open_file
    NULL, // close_file
    NULL, // read_file

    NULL, // map_address,

    NULL, // set_dbg_options
    NULL, // get_debmod_extensions
    NULL,

    NULL, // appcall
    NULL, // cleanup_appcall

    NULL, // eval_lowcnd

    NULL, // write_file

    NULL, // send_ioctl
};