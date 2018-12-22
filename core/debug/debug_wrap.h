#ifndef _DEBUG_WRAP_H_
#define _DEBUG_WRAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <Windows.h>
#undef EXCEPTION_ILLEGAL_INSTRUCTION

#define SHARED_MEM_NAME "GX_PLUS_SHARED_MEM"
#define MAX_BREAKPOINTS 1000

#pragma pack(push, 1)
typedef enum {
    BPT_ANY = (0 << 0),
    // M68K
    BPT_M68K_E = (1 << 0),
    BPT_M68K_R = (1 << 1),
    BPT_M68K_W = (1 << 2),
    BPT_M68K_RW = BPT_M68K_R | BPT_M68K_W,

    // VDP
    BPT_VRAM_R = (1 << 3),
    BPT_VRAM_W = (1 << 4),
    BPT_VRAM_RW = BPT_VRAM_R | BPT_VRAM_W,

    BPT_CRAM_R = (1 << 5),
    BPT_CRAM_W = (1 << 6),
    BPT_CRAM_RW = BPT_CRAM_R | BPT_CRAM_W,

    BPT_VSRAM_R = (1 << 7),
    BPT_VSRAM_W = (1 << 8),
    BPT_VSRAM_RW = BPT_VSRAM_R | BPT_VSRAM_W,

    // Z80
    BPT_Z80_E = (1 << 11),
    BPT_Z80_R = (1 << 12),
    BPT_Z80_W = (1 << 13),
    BPT_Z80_RW = BPT_Z80_R | BPT_Z80_W,

    // REGS
    BPT_VDP_REG = (1 << 9),
    BPT_M68K_REG = (1 << 10),
} bpt_type_t;

typedef enum {
    REQ_NO_REQUEST,

    REQ_GET_REGS,
    REQ_SET_REGS,

    REQ_GET_REG,
    REQ_SET_REG,

    REQ_READ_68K_ROM,
    REQ_READ_68K_RAM,

    REQ_WRITE_68K_ROM,
    REQ_WRITE_68K_RAM,

    REQ_READ_Z80,
    REQ_WRITE_Z80,

    REQ_ADD_BREAK,
    REQ_DEL_BREAK,
    REQ_CLEAR_BREAKS,
    REQ_LIST_BREAKS,

    REQ_PAUSE,
    REQ_RESUME,
    REQ_DETACH,

    REQ_STEP_INTO,
    REQ_STEP_OVER,
} request_type_t;

typedef enum {
    REG_TYPE_M68K = 1,
    REG_TYPE_S80,
    REG_TYPE_Z80,
    REG_TYPE_VDP,
} register_type_t;

typedef enum {
    DBG_EVT_NO_EVENT,
    DBG_EVT_STARTED,
    DBG_EVT_PAUSED,
    DBG_EVT_STOPPED,
} dbg_event_type_t;

typedef struct {
    dbg_event_type_t type;
    unsigned int pc;
    char msg[256];
} debugger_event_t;

typedef struct {
    int index;
    unsigned int val;
} reg_val_t;

typedef struct {
    unsigned int d0, d1, d2, d3, d4, d5, d6, d7;
    unsigned int a0, a1, a2, a3, a4, a5, a6, a7;
    unsigned int pc, sp, ppc, sr;
} regs_68k_data_t;

typedef struct {
    unsigned int pc, sp, af, bc, de, hl, ix, iy, wz;
    unsigned int af2,bc2,de2,hl2;
    unsigned char r, r2, iff1, iff2, halt, im, i;
} regs_z80_data_t;

typedef struct {
    register_type_t type;
    
    union {
        regs_68k_data_t values;
        unsigned int array[20];
    } regs_68k;
    reg_val_t any_reg;
    unsigned char regs_vdp[0x20];
    regs_z80_data_t regs_z80;
} register_data_t;

typedef struct {
    int size;
    unsigned int address;

    unsigned char m68k_rom[0xA00000];
    unsigned char m68k_ram[0x10000];
    unsigned char z80_ram[0x2000];
} memory_data_t;

typedef struct {
    bpt_type_t type;
    unsigned int address;
    int width;
} bpt_data_t;

typedef struct {
    int count;
    bpt_data_t breaks[MAX_BREAKPOINTS];
} bpt_list_t;

typedef struct {
    request_type_t req_type;
    register_data_t regs_data;
    memory_data_t mem_data;
    bpt_data_t bpt_data;
    debugger_event_t dbg_evt;
    bpt_list_t bpt_list;
    int dbg_boot_found;
    int dbg_active, dbg_trace, dbg_dont_check_bp;
    int dbg_paused;
    //HANDLE dbg_no_paused, dbg_has_event, dbg_has_no_req;
    int dbg_step_over;
    unsigned int dbg_step_over_addr;

    // functions
    void (*start_debugging)();
    void (*handle_request)();
    void (*stop_debugging)();
} dbg_request_t;
#pragma pack(pop)

extern dbg_request_t *dbg_req;

void wrap_debugger();
int activate_shared_mem();
void deactivate_shared_mem();
void unwrap_debugger();
int recv_dbg_event(int wait);
void send_dbg_request(request_type_t type);

#ifdef __cplusplus
}
#endif

#endif
