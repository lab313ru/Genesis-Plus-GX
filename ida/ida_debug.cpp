#include <ida.hpp>
#include <idd.hpp>
#include <auto.hpp>
#include <funcs.hpp>
#include <idp.hpp>
#include <dbg.hpp>

#include "ida_debmod.h"

#include "debug_wrap.h"

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

#define RC_GENERAL (1 << 0)
#define RC_VDP (1 << 1)
#define RC_Z80 (1 << 2)


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
    { "A7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

    { "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_dword, NULL, 0 },
    { "SR", NULL, RC_GENERAL, dt_word, SRReg, 0xFFFF },

    { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },
    { "USP", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
    { "ISP", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

    { "PPC", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
    { "IR", NULL, RC_GENERAL, dt_dword, NULL, 0 },

    // VDP Registers
    { "00", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "01", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "02", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "03", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "04", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "05", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "06", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "07", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "08", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "09", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0A", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0B", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0C", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0D", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0E", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "0F", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "10", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "11", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "12", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "13", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "14", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "15", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "16", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "17", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "18", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "19", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1A", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1B", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1C", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1D", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1E", NULL, RC_VDP, dt_byte, NULL, 0 },
    { "1F", NULL, RC_VDP, dt_byte, NULL, 0 },

    { "DMA_LEN", REGISTER_READONLY, RC_VDP, dt_word, NULL, 0 },
    { "DMA_SRC", REGISTER_ADDRESS | REGISTER_READONLY, RC_VDP, dt_dword, NULL, 0 },
    { "VDP_DST", REGISTER_ADDRESS | REGISTER_READONLY, RC_VDP, dt_dword, NULL, 0 },

    // Z80 regs
    { "PC", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "SP", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "AF", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "BC", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "DE", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "HL", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "IX", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "IY", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "WZ", NULL, RC_Z80, dt_dword, NULL, 0 },

    { "AF2", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "BC2", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "DE2", NULL, RC_Z80, dt_dword, NULL, 0 },
    { "HL2", NULL, RC_Z80, dt_dword, NULL, 0 },

    { "R", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "R2", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "IFFI1", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "IFFI2", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "HALT", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "IM", NULL, RC_Z80, dt_byte, NULL, 0 },
    { "I", NULL, RC_Z80, dt_byte, NULL, 0 },
};

static const char *register_classes[] =
{
    "General Registers",
    "VDP Registers",
    "Z80 Registers",
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
            plan_ea((ea_t)i);
        }
        show_addr((ea_t)i);
    }
    plan_range(0, MAXROMSIZE);

    for (size_t i = 0; i < MAXROMSIZE; ++i)
    {
        if (g_codemap[i].second && g_codemap[i].first && !get_func((ea_t)i))
        {
            if (add_func(i, BADADDR))
                add_cref(g_codemap[i].first, i, fl_CN);
            plan_ea((ea_t)i);
        }
        show_addr((ea_t)i);
    }
    plan_range(0, MAXROMSIZE);
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
static bool idaapi init_debugger(const char *hostname, int portnum, const char *password)
{
    set_processor_type(ph.psnames[0], SETPROC_LOADER); // reset proc to "M68000"
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
static int idaapi process_get_info(procinfo_vec_t *procs)
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

        regs_68k_data_t *reg_vals = &dbg_req->regs_data.regs_68k;

        values[REG_68K_D0].ival = reg_vals->d0;
        values[REG_68K_D1].ival = reg_vals->d1;
        values[REG_68K_D2].ival = reg_vals->d2;
        values[REG_68K_D3].ival = reg_vals->d3;
        values[REG_68K_D4].ival = reg_vals->d4;
        values[REG_68K_D5].ival = reg_vals->d5;
        values[REG_68K_D6].ival = reg_vals->d6;
        values[REG_68K_D7].ival = reg_vals->d7;

		values[REG_68K_A0].ival = reg_vals->a0;
		values[REG_68K_A1].ival = reg_vals->a1;
		values[REG_68K_A2].ival = reg_vals->a2;
		values[REG_68K_A3].ival = reg_vals->a3;
		values[REG_68K_A4].ival = reg_vals->a4;
		values[REG_68K_A5].ival = reg_vals->a5;
		values[REG_68K_A6].ival = reg_vals->a6;
		values[REG_68K_A7].ival = reg_vals->a7;

        values[REG_68K_PC].ival = reg_vals->pc & 0xFFFFFF;
        values[REG_68K_SR].ival = reg_vals->sr;
        values[REG_68K_SP].ival = reg_vals->sp & 0xFFFFFF;
        values[REG_68K_PPC].ival = reg_vals->ppc & 0xFFFFFF;
        values[REG_68K_IR].ival = reg_vals->ir;
    }

    if (clsmask & RC_VDP)
    {
        dbg_req->regs_data.type = REG_TYPE_VDP;
        send_dbg_request(dbg_req, REQ_GET_REGS);

        vdp_regs_t *vdp_regs = &dbg_req->regs_data.vdp_regs;

        for (int i = 0; i < sizeof(vdp_regs->regs_vdp) / sizeof(vdp_regs->regs_vdp[0]); ++i)
        {
            values[REG_VDP_00 + i].ival = vdp_regs->regs_vdp[i];
        }

        values[REG_VDP_DMA_LEN].ival = vdp_regs->dma_len;
        values[REG_VDP_DMA_SRC].ival = vdp_regs->dma_src;
        values[REG_VDP_DMA_DST].ival = vdp_regs->dma_dst;
    }

    if (clsmask & RC_Z80)
    {
        dbg_req->regs_data.type = REG_TYPE_Z80;
        send_dbg_request(dbg_req, REQ_GET_REGS);

        regs_z80_data_t *z80_regs = &dbg_req->regs_data.regs_z80;

        for (int i = 0; i < (REG_Z80_I - REG_Z80_PC + 1); ++i)
        {
            if (i >= 0 && i <= 12) // PC <-> HL2
            {
                values[REG_Z80_PC + i].ival = ((unsigned int *)&z80_regs->pc)[i];
            }
            else if (i >= 13 && i <= 19) // R <-> I
            {
                values[REG_Z80_PC + i].ival = ((unsigned char *)&z80_regs->r)[i - 13];
            }
        }
    }

    return 1;
}

static void set_reg(register_type_t type, int reg_index, unsigned int value)
{
    dbg_req->regs_data.type = type;
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
    if (regidx >= REG_68K_D0 && regidx <= REG_68K_D7)
    {
        set_reg(REG_TYPE_M68K, regidx - REG_68K_D0, (uint32)value->ival);
    }
    else if (regidx >= REG_68K_A0 && regidx <= REG_68K_A7)
    {
        set_reg(REG_TYPE_M68K, regidx - REG_68K_A0, (uint32)value->ival);
    }
    else if (regidx == REG_68K_PC)
    {
        set_reg(REG_TYPE_M68K, REG_68K_PC, (uint32)value->ival & 0xFFFFFF);
    }
    else if (regidx == REG_68K_SR)
    {
        set_reg(REG_TYPE_M68K, REG_68K_SR, (uint16)value->ival);
    }
    else if (regidx == REG_68K_SP)
    {
        set_reg(REG_TYPE_M68K, REG_68K_SP, (uint32)value->ival & 0xFFFFFF);
    }
    else if (regidx == REG_68K_USP)
    {
        set_reg(REG_TYPE_M68K, REG_68K_USP, (uint32)value->ival & 0xFFFFFF);
    }
    else if (regidx == REG_68K_ISP)
    {
        set_reg(REG_TYPE_M68K, REG_68K_ISP, (uint32)value->ival & 0xFFFFFF);
    }
    else if (regidx >= REG_VDP_00 && regidx <= REG_VDP_1F)
    {
        set_reg(REG_TYPE_VDP, regidx - REG_VDP_00, value->ival & 0xFF);
    }
    else if (regidx >= REG_Z80_PC && regidx <= REG_Z80_I)
    {
        set_reg(REG_TYPE_Z80, regidx - REG_Z80_PC, value->ival);
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
        segment_t *segm = getnseg(i);

        info.start_ea = segm->start_ea;
        info.end_ea = segm->end_ea;

        qstring buf;
        get_segm_name(&buf, segm);
        info.name = buf;

        get_segm_class(&buf, segm);
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