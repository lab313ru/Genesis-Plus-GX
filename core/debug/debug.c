#include "debug.h"

#include "shared.h"

#define m68ki_cpu m68k
#define MUL (7)

#ifndef BUILD_TABLES
#include "m68ki_cycles.h"
#endif

#include "m68kconf.h"
#include "m68kcpu.h"
#include "m68kops.h"

#include "vdp_ctrl.h"
#include "Z80.h"

dbg_request_t *dbg_req = NULL;

typedef struct breakpoint_s {
    struct breakpoint_s *next, *prev;
    int width;
    bpt_type_t type;
    unsigned int address;
} breakpoint_t;

static breakpoint_t *first_bp = NULL;

static breakpoint_t *add_bpt(bpt_type_t type, unsigned int address, int width) {
    breakpoint_t *bp = (breakpoint_t *)malloc(sizeof(breakpoint_t));

    bp->type = type;
    bp->address = address;
    bp->width = width;

    if (first_bp) {
        bp->next = first_bp;
        bp->prev = first_bp->prev;
        first_bp->prev = bp;
        bp->prev->next = bp;
    }
    else {
        first_bp = bp;
        bp->next = bp;
        bp->prev = bp;
    }

    return bp;
}

static void delete_breakpoint(breakpoint_t * bp) {
    if (bp == first_bp) {
        if (bp->next == bp) {
            first_bp = NULL;
        }
        else {
            first_bp = bp->next;
        }
    }

    bp->next->prev = bp->prev;
    bp->prev->next = bp->next;

    free(bp);
}

static breakpoint_t *next_breakpoint(breakpoint_t *bp) {
    return bp->next != first_bp ? bp->next : 0;
}

static breakpoint_t *find_breakpoint(unsigned int address, bpt_type_t type) {
    breakpoint_t *p;

    for (p = first_bp; p; p = next_breakpoint(p)) {
        if ((p->address == address) && ((p->type == BPT_ANY) || (p->type & type)))
            return p;
    }

    return 0;
}

static void remove_bpt(unsigned int address, bpt_type_t type)
{
    breakpoint_t *bpt;
    if ((bpt = find_breakpoint(address, type)))
        delete_breakpoint(bpt);
}

static int count_bpt_list()
{
    breakpoint_t *p;
    int i = 0;

    for (p = first_bp; p; p = next_breakpoint(p)) {
        ++i;
    }

    return i;
}

static void get_bpt_data(int index, bpt_data_t *data)
{
    breakpoint_t *p;
    int i = 0;

    for (p = first_bp; p; p = next_breakpoint(p)) {
        if (i == index)
        {
            data->address = p->address;
            data->width = p->width;
            data->type = p->type;
            break;
        }
        ++i;
    }
}

static void clear_bpt_list() {
    while (first_bp != NULL) delete_breakpoint(first_bp);
}

static void init_bpt_list()
{
    if (first_bp)
        clear_bpt_list();
}

void check_breakpoint(bpt_type_t type, int width, unsigned int address, unsigned int value)
{
    if (!dbg_req)
        return;

    if (!dbg_req->dbg_active || dbg_req->dbg_dont_check_bp)
        return;

    breakpoint_t *bp;
    for (bp = first_bp; bp; bp = next_breakpoint(bp)) {
        if (!(bp->type & type)) continue;
        if ((address <= (bp->address + bp->width)) && ((address + width) >= bp->address)) {
            ResetEvent(dbg_req->dbg_no_paused);
            break;
        }
    }
}

static void pause_debugger()
{
    if (!dbg_req)
        return;

    dbg_req->dbg_trace = 1;
    ResetEvent(dbg_req->dbg_no_paused);
}

static void resume_debugger()
{
    if (!dbg_req)
        return;

    dbg_req->dbg_trace = 0;
    SetEvent(dbg_req->dbg_no_paused);
}

static void detach_debugger()
{
    clear_bpt_list();
    resume_debugger();
}

static void activate_debugger()
{
    if (!dbg_req)
        return;

    dbg_req->dbg_active = 1;
}

static void deactivate_debugger()
{
    if (!dbg_req)
        return;

    dbg_req->dbg_active = 0;
}

static unsigned int calc_step_over() {
    unsigned int pc = m68k_get_reg(M68K_REG_PC);
    unsigned int sp = m68k_get_reg(M68K_REG_SP);
    unsigned int opc = m68ki_read_imm_16();

    unsigned int dest_pc = (unsigned int)(-1);

    // jsr
    if ((opc & 0xFFF8) == 0x4E90) {
        m68k_op_jsr_32_ai();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFF8) == 0x4EA8) {
        m68k_op_jsr_32_di();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFF8) == 0x4EB0) {
        m68k_op_jsr_32_ix();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFFF) == 0x4EB8) {
        m68k_op_jsr_32_aw();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFFF) == 0x4EB9) {
        m68k_op_jsr_32_al();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFFF) == 0x4EBA) {
        m68k_op_jsr_32_pcdi();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFFF) == 0x4EBB) {
        m68k_op_jsr_32_pcix();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    // bsr
    else if ((opc & 0xFFFF) == 0x6100) {
        m68k_op_bsr_16();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFFFF) == 0x61FF) {
        m68k_op_bsr_32();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    else if ((opc & 0xFF00) == 0x6100) {
        m68k_op_bsr_8();
        m68k_op_rts_32();
        dest_pc = m68k_get_reg(M68K_REG_PC);
    }
    // dbf
    else if ((opc & 0xfff8) == 0x51C8) {
        dest_pc = m68k_get_reg(M68K_REG_PC) + 2;
    }

    m68k_set_reg(M68K_REG_PC, pc);
    m68k_set_reg(M68K_REG_SP, sp);

    return dest_pc;
}

static void process_request()
{
    if (!dbg_req)
        return;

    if (WaitForSingleObject(dbg_req->dbg_has_no_req, 0) == WAIT_OBJECT_0)
        return;

    switch (dbg_req->req_type)
    {
    case REQ_GET_REG:
    {
        register_data_t *regs_data = &dbg_req->data.regs_data;

        switch (regs_data->type)
        {
        case REG_TYPE_M68K: regs_data->data.any_reg.val = m68k_get_reg(regs_data->data.any_reg.index); break;
        case REG_TYPE_VDP: regs_data->data.any_reg.val = reg[regs_data->data.any_reg.index]; break;
        case REG_TYPE_Z80: regs_data->data.any_reg.val = ((unsigned int *)&Z80.pc)[regs_data->data.any_reg.index]; break;
        default:
            break;
        }
        
    } break;
    case REQ_SET_REG:
    {
        register_data_t *regs_data = &dbg_req->data.regs_data;

        switch (regs_data->type)
        {
        case REG_TYPE_M68K: m68k_set_reg(regs_data->data.any_reg.index, regs_data->data.any_reg.val); break;
        case REG_TYPE_VDP: reg[regs_data->data.any_reg.index] = regs_data->data.any_reg.val; break;
        case REG_TYPE_Z80: ((unsigned int *)&Z80.pc)[regs_data->data.any_reg.index] = regs_data->data.any_reg.val; break;
        default:
            break;
        }
    } break;
    case REQ_GET_REGS:
    case REQ_SET_REGS:
    {
        register_data_t *regs_data = &dbg_req->data.regs_data;

        switch (regs_data->type)
        {
        case REG_TYPE_M68K:
        {
            regs_68k_data_t *m68kr = &regs_data->data.regs_68k.values;

            if (dbg_req->req_type == REQ_GET_REGS)
            {
                m68kr->d0 = m68k_get_reg(M68K_REG_D0);
                m68kr->d1 = m68k_get_reg(M68K_REG_D1);
                m68kr->d2 = m68k_get_reg(M68K_REG_D2);
                m68kr->d3 = m68k_get_reg(M68K_REG_D3);
                m68kr->d4 = m68k_get_reg(M68K_REG_D4);
                m68kr->d5 = m68k_get_reg(M68K_REG_D5);
                m68kr->d6 = m68k_get_reg(M68K_REG_D6);
                m68kr->d7 = m68k_get_reg(M68K_REG_D7);
                m68kr->a0 = m68k_get_reg(M68K_REG_A0);
                m68kr->a1 = m68k_get_reg(M68K_REG_A1);
                m68kr->a2 = m68k_get_reg(M68K_REG_A2);
                m68kr->a3 = m68k_get_reg(M68K_REG_A3);
                m68kr->a4 = m68k_get_reg(M68K_REG_A4);
                m68kr->a5 = m68k_get_reg(M68K_REG_A5);
                m68kr->a6 = m68k_get_reg(M68K_REG_A6);
                m68kr->a7 = m68k_get_reg(M68K_REG_A7);
                m68kr->pc = m68k_get_reg(M68K_REG_PC);
                m68kr->sp = m68k_get_reg(M68K_REG_SP);
                m68kr->ppc = m68k_get_reg(M68K_REG_PPC);
                m68kr->sr = m68k_get_reg(M68K_REG_SR);
            }
            else
            {
                m68k_set_reg(M68K_REG_D0, m68kr->d0);
                m68k_set_reg(M68K_REG_D1, m68kr->d1);
                m68k_set_reg(M68K_REG_D2, m68kr->d2);
                m68k_set_reg(M68K_REG_D3, m68kr->d3);
                m68k_set_reg(M68K_REG_D4, m68kr->d4);
                m68k_set_reg(M68K_REG_D5, m68kr->d5);
                m68k_set_reg(M68K_REG_D6, m68kr->d6);
                m68k_set_reg(M68K_REG_D7, m68kr->d7);
                m68k_set_reg(M68K_REG_A0, m68kr->a0);
                m68k_set_reg(M68K_REG_A1, m68kr->a1);
                m68k_set_reg(M68K_REG_A2, m68kr->a2);
                m68k_set_reg(M68K_REG_A3, m68kr->a3);
                m68k_set_reg(M68K_REG_A4, m68kr->a4);
                m68k_set_reg(M68K_REG_A5, m68kr->a5);
                m68k_set_reg(M68K_REG_A6, m68kr->a6);
                m68k_set_reg(M68K_REG_A7, m68kr->a7);
                m68k_set_reg(M68K_REG_PC, m68kr->pc);
                m68k_set_reg(M68K_REG_SP, m68kr->sp);
                m68k_set_reg(M68K_REG_PPC, m68kr->ppc);
                m68k_set_reg(M68K_REG_SR, m68kr->sr);
            }
        } break;
        case REG_TYPE_VDP:
        {
            for (int i = 0; i < (sizeof(regs_data->data.regs_vdp) / sizeof(regs_data->data.regs_vdp[0])); ++i)
            {
                if (dbg_req->req_type == REQ_GET_REGS)
                    regs_data->data.regs_vdp[i] = reg[i];
                else
                    reg[i] = regs_data->data.regs_vdp[i];
            }
        } break;
        case REG_TYPE_Z80:
        {
            regs_z80_data_t *z80r = &regs_data->data.regs_z80;
            if (dbg_req->req_type == REQ_GET_REGS)
            {
                z80r->pc = Z80.pc.d;
                z80r->sp = Z80.sp.d;
                z80r->af = Z80.af.d;
                z80r->bc = Z80.bc.d;
                z80r->de = Z80.de.d;
                z80r->hl = Z80.hl.d;
                z80r->ix = Z80.ix.d;
                z80r->iy = Z80.iy.d;
                z80r->wz = Z80.wz.d;
                z80r->af2 = Z80.af2.d;
                z80r->bc2 = Z80.bc2.d;
                z80r->de2 = Z80.de2.d;
                z80r->hl2 = Z80.hl2.d;
                z80r->r = Z80.r;
                z80r->r2 = Z80.r2;
                z80r->iff1 = Z80.iff1;
                z80r->iff2 = Z80.iff2;
                z80r->halt = Z80.halt;
                z80r->im = Z80.im;
                z80r->i = Z80.i;
            }
            else
            {
                Z80.pc.d = z80r->pc;
                Z80.sp.d = z80r->sp;
                Z80.af.d = z80r->af;
                Z80.bc.d = z80r->bc;
                Z80.de.d = z80r->de;
                Z80.hl.d = z80r->hl;
                Z80.ix.d = z80r->ix;
                Z80.iy.d = z80r->iy;
                Z80.wz.d = z80r->wz;
                Z80.af2.d = z80r->af2;
                Z80.bc2.d = z80r->bc2;
                Z80.de2.d = z80r->de2;
                Z80.hl2.d = z80r->hl2;
                Z80.r = z80r->r;
                Z80.r2 = z80r->r2;
                Z80.iff1 = z80r->iff1;
                Z80.iff2 = z80r->iff2;
                Z80.halt = z80r->halt;
                Z80.im = z80r->im;
                Z80.i = z80r->i;
            }
        } break;
        default:
            break;
        }
    } break;
    case REQ_READ_68K_ROM:
    case REQ_READ_68K_RAM:
    case REQ_READ_Z80:
    {
        dbg_req->dbg_dont_check_bp = 1;

        memory_data_t *mem_data = &dbg_req->data.mem_data;
        for (int i = 0; i < mem_data->size; ++i)
        {
            switch (dbg_req->req_type)
            {
            case REQ_READ_68K_ROM: mem_data->data.m68k_rom[mem_data->address + i] = m68ki_read_8(mem_data->address + i); break;
            case REQ_READ_68K_RAM: mem_data->data.m68k_ram[0xFFFF0000 - mem_data->address + i] = m68ki_read_8(mem_data->address + i); break;
            case REQ_READ_Z80: mem_data->data.z80_ram[mem_data->address + i] = z80_readmem(mem_data->address + i); break;
            default:
                break;
            }
        }

        dbg_req->dbg_dont_check_bp = 0;
    } break;
    case REQ_WRITE_68K_ROM:
    case REQ_WRITE_68K_RAM:
    case REQ_WRITE_Z80:
    {
        dbg_req->dbg_dont_check_bp = 1;

        memory_data_t *mem_data = &dbg_req->data.mem_data;
        for (int i = 0; i < mem_data->size; ++i)
        {
            switch (dbg_req->req_type)
            {
            case REQ_WRITE_68K_ROM: m68ki_write_8(mem_data->address + i, mem_data->data.m68k_rom[mem_data->address + i]); break;
            case REQ_WRITE_68K_RAM: m68ki_write_8(0xFF0000 + mem_data->address + i, mem_data->data.m68k_ram[mem_data->address + i]); break;
            case REQ_WRITE_Z80: z80_writemem(mem_data->address + i, mem_data->data.z80_ram[mem_data->address + i]); break;
            default:
                break;
            }
        }

        dbg_req->dbg_dont_check_bp = 0;
    } break;
    case REQ_ADD_BREAK:
    {
        bpt_data_t *bpt_data = &dbg_req->data.bpt_data;
        if (!find_breakpoint(bpt_data->address, bpt_data->type))
            add_bpt(bpt_data->type, bpt_data->address, bpt_data->width);
    } break;
    case REQ_DEL_BREAK:
    {
        bpt_data_t *bpt_data = &dbg_req->data.bpt_data;
        remove_bpt(bpt_data->address, bpt_data->type);
    } break;
    case REQ_CLEAR_BREAKS:
        clear_bpt_list();
    case REQ_LIST_BREAKS:
    {
        bpt_list_t *bpt_list = &dbg_req->bpt_list;
        bpt_list->count = count_bpt_list();
        for (int i = 0; i < bpt_list->count; ++i)
            get_bpt_data(i, &bpt_list->breaks[i]);
    } break;
    case REQ_PAUSE:
        pause_debugger();
        break;
    case REQ_RESUME:
        resume_debugger();
        break;
    case REQ_DETACH:
        detach_debugger();
        break;
    case REQ_STEP_INTO:
    {
        int state = WaitForSingleObject(dbg_req->dbg_no_paused, 0);
        if (state == WAIT_TIMEOUT)
        {
            dbg_req->dbg_trace = 1;
            SetEvent(dbg_req->dbg_no_paused);
        }
    } break;
    case REQ_STEP_OVER:
    {
        int state = WaitForSingleObject(dbg_req->dbg_no_paused, 0);
        if (state == WAIT_TIMEOUT)
        {
            unsigned int dest_pc = calc_step_over();

            if (dest_pc != (unsigned int)(-1))
            {
                dbg_req->dbg_step_over = 1;
                dbg_req->dbg_step_over_addr = dest_pc;
            }
            else
            {
                dbg_req->dbg_step_over = 0;
                dbg_req->dbg_step_over_addr = 0;
                dbg_req->dbg_trace = 1;
            }

            SetEvent(dbg_req->dbg_no_paused);
        }
    } break;
    default:
        break;
    }

    SetEvent(dbg_req->dbg_has_no_req);
}

void send_dbg_event()
{
    if (!dbg_req)
        return;

    SetEvent(dbg_req->dbg_has_event);
}

static void stop_debugging()
{
    if (!dbg_req)
        return;

    dbg_req->dbg_evt.type = DBG_EVT_STOPPED;
    send_dbg_event();

    detach_debugger();
    deactivate_debugger();

    dbg_req->dbg_boot_found = 0;
}

static void handle_request()
{
    if (!dbg_req)
        return;

    if (dbg_req->dbg_active)
        return;

    process_request();
}

void start_debugging()
{
    if (!dbg_req)
        return;

    if (dbg_req->dbg_active)
        return;

    dbg_req->handle_request = handle_request;
    dbg_req->stop_debugging = stop_debugging;

    init_bpt_list();

    dbg_req->dbg_boot_found = 0;

    activate_debugger();
}

void process_breakpoints() {
    if (!dbg_req)
        return;

    int handled_event = 0;

    if (!dbg_req->dbg_active)
        return;

    unsigned int pc = m68k_get_reg(M68K_REG_PC);

    if ((!dbg_req->dbg_boot_found) && (pc == (unsigned int)(m68k_read_immediate_32(4)))) {
        dbg_req->dbg_boot_found = 1;
        ResetEvent(dbg_req->dbg_no_paused);

        dbg_req->dbg_evt.pc = pc;
        strncpy(dbg_req->dbg_evt.msg, "genplusgx", sizeof(dbg_req->dbg_evt.msg));
        dbg_req->dbg_evt.type = DBG_EVT_STARTED;
        send_dbg_event();
    }

    if (dbg_req->dbg_trace) {
        dbg_req->dbg_trace = 0;
        ResetEvent(dbg_req->dbg_no_paused);

        dbg_req->dbg_evt.pc = pc;
        dbg_req->dbg_evt.type = DBG_EVT_PAUSED;
        send_dbg_event();

        handled_event = 1;
    }

    int state = WaitForSingleObject(dbg_req->dbg_no_paused, 0);
    if (state == WAIT_OBJECT_0) {
        if (dbg_req->dbg_step_over && pc == dbg_req->dbg_step_over_addr) {
            dbg_req->dbg_step_over = 0;
            dbg_req->dbg_step_over_addr = 0;

            ResetEvent(dbg_req->dbg_no_paused);
        }

        check_breakpoint(BPT_M68K_E, 1, pc, pc);

        int state = WaitForSingleObject(dbg_req->dbg_no_paused, 0);
        if (state == WAIT_TIMEOUT) {
            dbg_req->dbg_evt.pc = pc;
            dbg_req->dbg_evt.type = DBG_EVT_PAUSED;
            send_dbg_event();

            handled_event = 1;
        }
    }

    state = WaitForSingleObject(dbg_req->dbg_no_paused, 0);
    if (dbg_req->dbg_boot_found && (!handled_event) && state == WAIT_TIMEOUT) {
        dbg_req->dbg_evt.pc = pc;
        dbg_req->dbg_evt.type = DBG_EVT_PAUSED;
        send_dbg_event();
    }

    while (WaitForSingleObject(dbg_req->dbg_no_paused, 0) == WAIT_TIMEOUT)
    {
        process_request();
    }
}
