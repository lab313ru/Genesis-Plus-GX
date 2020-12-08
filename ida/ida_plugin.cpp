// Copyright (C) 2015 Dr. MefistO
//
// This program is free software : you can redistribute it and / or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License 2.0 for more details.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http ://www.gnu.org/licenses/

//#include <Windows.h>

#include <ida.hpp>
#include <dbg.hpp>
#include <idd.hpp>
#include <loader.hpp>
#include <idp.hpp>
#include <offset.hpp>
#include <kernwin.hpp>

#include "ida_plugin.h"

#include "ida_debmod.h"

extern debugger_t debugger;

static bool plugin_inited;
static bool my_dbg;

static int idaapi idp_to_dbg_reg(int idp_reg)
{
    int reg_idx = idp_reg;
    if (idp_reg >= 0 && idp_reg <= 7)
        reg_idx = 0 + idp_reg;
    else if (idp_reg >= 8 && idp_reg <= 39)
        reg_idx = 8 + (idp_reg % 8);
    else if (idp_reg == 91)
        reg_idx = 16;
    else if (idp_reg == 92 || idp_reg == 93)
        reg_idx = 17;
    else if (idp_reg == 94)
        reg_idx = 15;
    else
    {
        char buf[MAXSTR];
        ::qsnprintf(buf, MAXSTR, "reg: %d\n", idp_reg);
        warning("SEND THIS MESSAGE TO meffi@lab313.ru:\n%s\n", buf);
        return 0;
    }
    return reg_idx;
}

enum m68k_insn_type_t
{
    M68K_linea = CUSTOM_INSN_ITYPE,
    M68K_linef,
};

#ifdef _DEBUG
static const char* const optype_names[] =
{
    "o_void",
    "o_reg",
    "o_mem",
    "o_phrase",
    "o_displ",
    "o_imm",
    "o_far",
    "o_near",
    "o_idpspec0",
    "o_idpspec1",
    "o_idpspec2",
    "o_idpspec3",
    "o_idpspec4",
    "o_idpspec5",
};

static const char* const dtyp_names[] =
{
    "dt_byte",
    "dt_word",
    "dt_dword",
    "dt_float",
    "dt_double",
    "dt_tbyte",
    "dt_packreal",
    "dt_qword",
    "dt_byte16",
    "dt_code",
    "dt_void",
    "dt_fword",
    "dt_bitfild",
    "dt_string",
    "dt_unicode",
    "dt_3byte",
    "dt_ldbl",
    "dt_byte32",
    "dt_byte64",
};

static void print_insn(insn_t *insn)
{
    if (my_dbg)
    {
        msg("cs=%x, ", insn->cs);
        msg("ip=%x, ", insn->ip);
        msg("ea=%x, ", insn->ea);
        msg("itype=%x, ", insn->itype);
        msg("size=%x, ", insn->size);
        msg("auxpref=%x, ", insn->auxpref);
        msg("segpref=%x, ", insn->segpref);
        msg("insnpref=%x, ", insn->insnpref);
        msg("insnpref=%x, ", insn->insnpref);

        msg("flags[");
        if (insn->flags & INSN_MACRO)
            msg("INSN_MACRO|");
        if (insn->flags & INSN_MODMAC)
            msg("OF_OUTER_DISP");
        msg("]\n");
    }
}

static void print_op(ea_t ea, op_t *op)
{
    if (my_dbg)
    {
        msg("type[%s], ", optype_names[op->type]);

        msg("flags[");
        if (op->flags & OF_NO_BASE_DISP)
            msg("OF_NO_BASE_DISP|");
        if (op->flags & OF_OUTER_DISP)
            msg("OF_OUTER_DISP|");
        if (op->flags & PACK_FORM_DEF)
            msg("PACK_FORM_DEF|");
        if (op->flags & OF_NUMBER)
            msg("OF_NUMBER|");
        if (op->flags & OF_SHOW)
            msg("OF_SHOW");
        msg("], ");

        msg("dtyp[%s], ", dtyp_names[op->dtype]);

        if (op->type == o_reg)
            msg("reg=%x, ", op->reg);
        else if (op->type == o_displ || op->type == o_phrase)
            msg("phrase=%x, ", op->phrase);
        else
            msg("reg_phrase=%x, ", op->phrase);

        msg("addr=%x, ", op->addr);

        msg("value=%x, ", op->value);

        msg("specval=%x, ", op->specval);

        msg("specflag1=%x, ", op->specflag1);
        msg("specflag2=%x, ", op->specflag2);
        msg("specflag3=%x, ", op->specflag3);
        msg("specflag4=%x, ", op->specflag4);

        msg("refinfo[");

        opinfo_t buf;

        if (get_opinfo(&buf, ea, op->n, op->flags))
        {
            msg("target=%x, ", buf.ri.target);
            msg("base=%x, ", buf.ri.base);
            msg("tdelta=%x, ", buf.ri.tdelta);

            msg("flags[");
            if (buf.ri.flags & REFINFO_TYPE)
                msg("REFINFO_TYPE|");
            if (buf.ri.flags & REFINFO_RVAOFF)
                msg("REFINFO_RVAOFF|");
            if (buf.ri.flags & REFINFO_PASTEND)
                msg("REFINFO_PASTEND|");
            if (buf.ri.flags & REFINFO_CUSTOM)
                msg("REFINFO_CUSTOM|");
            if (buf.ri.flags & REFINFO_NOBASE)
                msg("REFINFO_NOBASE|");
            if (buf.ri.flags & REFINFO_SUBTRACT)
                msg("REFINFO_SUBTRACT|");
            if (buf.ri.flags & REFINFO_SIGNEDOP)
                msg("REFINFO_SIGNEDOP");
            msg("]");
        }
        msg("]\n");
    }
}
#endif

typedef const regval_t &(idaapi *getreg_func_t)(const char *name, const regval_t *regvalues);

static bool insn_analyzed = false;

static ssize_t idaapi hook_idp(void *user_data, int notification_code, va_list va)
{
    switch (notification_code)
    {
    case processor_t::ev_ana_insn:
    {
        insn_t *out = va_arg(va, insn_t *);

        if (insn_analyzed)
            return out->size;

        uint16 itype = 0;
        ea_t value = out->ea;
        uchar b = get_byte(out->ea);

        if (b == 0xA0 || b == 0xF0)
        {
            switch (b)
            {
            case 0xA0:
                itype = M68K_linea;
                value = get_dword(0x0A * sizeof(uint32));
                break;
            case 0xF0:
                itype = M68K_linef;
                value = get_dword(0x0B * sizeof(uint32));
                break;
            }

            out->itype = itype;
            out->size = 2;

            out->Op1.type = o_near;
            out->Op1.offb = 1;
            out->Op1.dtype = dt_dword;
            out->Op1.addr = value;
            out->Op1.phrase = 0x0A;
            out->Op1.specflag1 = 2;

            out->Op2.type = o_imm;
            out->Op2.offb = 1;
            out->Op2.dtype = dt_byte;
            out->Op2.value = get_byte(out->ea + 1);
        }
        else
        {
            insn_analyzed = true;

            if (ph.ana_insn(out) <= 0)
            {
                insn_analyzed = false;
                break;
            }

            insn_analyzed = false;
        }

#ifdef _DEBUG
        print_insn(out);
#endif

        for (int i = 0; i < UA_MAXOP; ++i)
        {
            op_t &op = out->ops[i];

#ifdef _DEBUG
            print_op(out->ea, &op);
#endif

            switch (op.type)
            {
            case o_near:
            case o_mem:
            {
                op.addr &= 0xFFFFFF; // for any mirrors

                if ((op.addr & 0xE00000) == 0xE00000) // RAM mirrors
                    op.addr |= 0x1F0000;

                if ((op.addr >= 0xC00000 && op.addr <= 0xC0001F) ||
                    (op.addr >= 0xC00020 && op.addr <= 0xC0003F)) // VDP mirrors
                    op.addr &= 0xC000FF;

                if (out->itype == 0x75 && op.n == 0 && op.phrase == 9 && (op.addr & 0xFFFF0000) == 0xFF0000)
                {
                    op.type = o_mem;
                    //op.specflag1 = 1;
                }
                else if ((out->itype == 0x76 || out->itype == 0x75 || out->itype == 0x74) && op.n == 0 &&
                    (op.phrase == 0x09 || op.phrase == 0x0A) &&
                    (op.addr != 0 && op.addr <= 0xA00000) &&
                    op.specflag1 == 2) // lea table(pc),Ax; jsr func(pc); jmp label(pc)
                {
                    short diff = op.addr - value;
                    if (diff >= SHRT_MIN && diff <= SHRT_MAX)
                    {
                        out->Op1.type = o_displ;
                        out->Op1.offb = 2;
                        out->Op1.dtype = dt_dword;
                        out->Op1.phrase = 0x5B;
                        out->Op1.specflag1 = 0x10;
                    }
                }
            } break;
            case o_imm:
            {
                if (out->itype != 0x7F || op.n != 0) // movea
                    break;

                if (op.value & 0xFF0000 && op.dtype == dt_word) {
                    op.value &= 0xFFFF;
                    op_offset(out->ea, op.n, REF_OFF32, BADADDR, 0xFF0000);
                }
            } break;
            }
        }

        return out->size;
    } break;
    case processor_t::ev_emu_insn:
    {
        insn_t *insn = va_arg(va, insn_t *);
        if (insn->itype == 0xB6) // trap #X
        {
            qstring name;
            ea_t trap_addr = get_dword((0x20 + (insn->Op1.value & 0xF)) * sizeof(uint32));
            get_func_name(&name, trap_addr);
            set_cmt(insn->ea, name.c_str(), false);
            insn->add_cref(trap_addr, insn->Op1.offb, fl_CN);

            if (func_does_return(trap_addr)) {
                func_t* trap_func = get_func(trap_addr);
                int argsize = (trap_func != nullptr) ? trap_func->argsize : 0;
                insn->add_cref(insn->ea + 2 + argsize, 0, fl_F); // calc next insn
            }

            return 1;
        }

        if ((insn->itype == 0x76 || insn->itype == 0x75 || insn->itype == 0x74) &&
            insn->Op1.phrase == 0x5B && insn->Op1.specflag1 == 0x10) // lea table(pc),Ax; jsr func(pc); jmp label(pc)
        {
            short diff = insn->Op1.addr - insn->ea;
            if (diff >= SHRT_MIN && diff <= SHRT_MAX)
            {
                insn->add_dref(insn->Op1.addr, insn->Op1.offb, dr_O);

                if (insn->itype != 0x74)
                    insn->add_cref(insn->ea + insn->size, 0, fl_F);

                return 1;
            }
        }

        if (insn->itype == M68K_linea || insn->itype == M68K_linef)
        {
            insn->add_cref(insn->Op1.addr, 0, fl_CN);
            insn->add_cref(insn->ea + insn->size, insn->Op1.offb, fl_F);
            return 1;
        }
    } break;
    case processor_t::ev_out_mnem:
    {
        outctx_t *outctx = va_arg(va, outctx_t *);
        if (outctx->insn.itype != M68K_linea && outctx->insn.itype != M68K_linef)
            break;

        const char *mnem = (outctx->insn.itype == M68K_linef) ? "line_f" : "line_a";

        outctx->out_custom_mnem(mnem);
        return 1;
    } break;
    case processor_t::ev_get_idd_opinfo:
    {
        idd_opinfo_t * opinf = va_arg(va, idd_opinfo_t *);
        ea_t ea = va_arg(va, ea_t);
        int n = va_arg(va, int);
        int thread_id = va_arg(va, int);
        getreg_func_t getreg = va_arg(va, getreg_func_t);
        const regval_t *regvalues = va_arg(va, const regval_t *);

        opinf->ea = BADADDR;
        opinf->debregidx = 0;
        opinf->modified = false;
        opinf->value.ival = 0;
        opinf->value_size = 4;

        insn_t out;
        if (decode_insn(&out, ea))
        {
            op_t op = out.ops[n];

#ifdef _DEBUG
            print_insn(&out);
#endif

            int size = 0;
            switch (op.dtype)
            {
            case dt_byte:
                size = 1;
                break;
            case dt_word:
                size = 2;
                break;
            default:
                size = 4;
                break;
            }

            opinf->value_size = size;

            switch (op.type)
            {
            case o_mem:
            case o_near:
            case o_imm:
            {
                flags_t flags;

                switch (n)
                {
                case 0: flags = get_optype_flags0(get_flags(ea)); break;
                case 1: flags = get_optype_flags1(get_flags(ea)); break;
                default: flags = 0; break;
                }

                switch (op.type)
                {
                case o_mem:
                case o_near: opinf->ea = op.addr; break;
                case o_imm: opinf->ea = op.value; break;
                }

                opinfo_t info;
                if (get_opinfo(&info, ea, n, flags) != NULL)
                {
                    opinf->ea += info.ri.base;
                }
            } break;
            case o_phrase:
            case o_reg:
            {
                int reg_idx = idp_to_dbg_reg(op.reg);
                regval_t reg = getreg(dbg->regs(reg_idx).name, regvalues);

                if (op.phrase >= 0x10 && op.phrase <= 0x1F || // (A0)..(A7), (A0)+..(A7)+
                    op.phrase >= 0x20 && op.phrase <= 0x27) // -(A0)..-(A7)
                {
                    if (op.phrase >= 0x20 && op.phrase <= 0x27)
                        reg.ival -= size;

                    opinf->ea = (ea_t)reg.ival;
                    size_t read_size = 0;

                    switch (size)
                    {
                    case 1:
                    {
                        uint8_t b = 0;
                        dbg->read_memory(&read_size, (ea_t)reg.ival, &b, 1);
                        opinf->value.ival = b;
                    } break;
                    case 2:
                    {
                        uint16_t w = 0;
                        dbg->read_memory(&read_size, (ea_t)reg.ival, &w, 2);
                        w = swap16(w);
                        opinf->value.ival = w;
                    } break;
                    default:
                    {
                        uint32_t l = 0;
                        dbg->read_memory(&read_size, (ea_t)reg.ival, &l, 4);
                        l = swap32(l);
                        opinf->value.ival = l;
                    } break;
                    }
                }
                else
                    opinf->value = reg;

                opinf->debregidx = reg_idx;
            } break;
            case o_displ:
            {
                regval_t main_reg, add_reg;
                int main_reg_idx = idp_to_dbg_reg(op.reg);
                int add_reg_idx = idp_to_dbg_reg(op.specflag1 & 0xF);

                main_reg.ival = 0;
                add_reg.ival = 0;
                if (op.specflag2 & 0x10)
                {
                    add_reg = getreg(dbg->regs(add_reg_idx).name, regvalues);
                    if (op.specflag1 & 0x10)
                    {
                        add_reg.ival &= 0xFFFF;
                        add_reg.ival = (uint64)((int16_t)add_reg.ival);
                    }
                }

                if (main_reg_idx != 16)
                    main_reg = getreg(dbg->regs(main_reg_idx).name, regvalues);

                ea_t addr = (ea_t)main_reg.ival + op.addr + (ea_t)add_reg.ival;
                opinf->ea = addr;
                size_t read_size = 0;

                switch (size)
                {
                case 1:
                {
                    uint8_t b = 0;
                    dbg->read_memory(&read_size, addr, &b, 1);
                    opinf->value.ival = b;
                } break;
                case 2:
                {
                    uint16_t w = 0;
                    dbg->read_memory(&read_size, addr, &w, 2);
                    w = swap16(w);
                    opinf->value.ival = w;
                } break;
                default:
                {
                    uint32_t l = 0;
                    dbg->read_memory(&read_size, addr, &l, 4);
                    l = swap32(l);
                    opinf->value.ival = l;
                } break;
                }
            } break;
            }

            opinf->ea &= 0xFFFFFF;

            return 1;
        }
    } break;
    default:
    {
#ifdef _DEBUG
        if (my_dbg)
        {
            msg("msg = %d\n", notification_code);
        }
#endif
    } break;
    }
    return 0;
}

//--------------------------------------------------------------------------
static unsigned int mask(unsigned char bit_idx, unsigned char bits_cnt = 1)
{
    return (((1 << bits_cnt) - 1) << bit_idx);
}

//--------------------------------------------------------------------------
static bool is_vdp_send_cmd(uint32 val)
{
    if (val & 0xFFFF0000)
    {
        return ((val & 0x9F000000) >= 0x80000000) && ((val & 0x9F000000) <= 0x97000000);
    }
    else
    {
        return ((val & 0x9F00) >= 0x8000) && ((val & 0x9F00) <= 0x9700);
    }
}

//--------------------------------------------------------------------------
static bool is_vdp_rw_cmd(uint32 val)
{
    if (val & 0xFFFF0000) // command was sended by one dword
    {
        switch ((val >> 24) & mask(6, 2))
        {
        case 0 /*00*/ << 6:
        case 1 /*01*/ << 6:
        case 3 /*11*/ << 6:
        {
            switch ((val & 0xFF) & mask(4, 2))
            {
            case 0 /*00*/ << 4:
            case 1 /*01*/ << 4:
            case 2 /*10*/ << 4:
            {
                return true;
            }
            }
            return false;
        }
        }
        return false;
    }
    else // command was sended by halfs (this is high word of it)
    {
        switch ((val >> 8) & mask(6, 2))
        {
        case 0 /*00*/ << 6:
        case 1 /*01*/ << 6:
        case 3 /*11*/ << 6:
        {
            return true;
        }
        }
        return false;
    }
}

static const char wrong_vdp_cmd[] = "Wrong command to send to VDP_CTRL!\n";

//--------------------------------------------------------------------------
static bool do_cmt_vdp_reg_const(ea_t ea, uint32 val)
{
    if (!val) return false;

    char name[250];
    unsigned int addr = 0;
    switch (val & 0x9F00)
    {
    case 0x8000:
    {
        if (val & mask(0))	append_cmt(ea, "DISPLAY_OFF", false);
        else append_cmt(ea, "DISPLAY_ON", false);

        if (val & mask(1))	append_cmt(ea, "PAUSE_HV_WHEN_EXT_INT", false);
        else append_cmt(ea, "NORMAL_HV_COUNTER", false);

        if (val & mask(2))	append_cmt(ea, "EIGHT_COLORS_MODE", false);
        else append_cmt(ea, "FULL_COLORS_MODE", false);

        if (val & mask(4))	append_cmt(ea, "ENABLE_HBLANK", false);
        else append_cmt(ea, "DISABLE_HBLANK", false);

        return true;
    }
    case 0x8100:
    {
        if (val & mask(2))	append_cmt(ea, "GENESIS_DISPLAY_MODE_BIT2", false);
        else append_cmt(ea, "SMS_DISPLAY_MODE_BIT2", false);

        if (val & mask(3))	append_cmt(ea, "SET_PAL_MODE", false);
        else append_cmt(ea, "SET_NTSC_MODE", false);

        if (val & mask(4))	append_cmt(ea, "ENABLE_DMA", false);
        else append_cmt(ea, "DISABLE_DMA", false);

        if (val & mask(5))	append_cmt(ea, "ENABLE_VBLANK", false);
        else append_cmt(ea, "DISABLE_VBLANK", false);

        if (val & mask(6))	append_cmt(ea, "ENABLE_DISPLAY", false);
        else append_cmt(ea, "DISABLE_DISPLAY", false);

        if (val & mask(7))	append_cmt(ea, "TMS9918_DISPLAY_MODE_BIT7", false);
        else append_cmt(ea, "GENESIS_DISPLAY_MODE_BIT7", false);

        return true;
    }
    case 0x8200:
    {
        addr = (val & mask(3, 3));
        ::qsnprintf(name, sizeof(name), "SET_PLANE_A_ADDR_$%.4X", addr * 0x400);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x8300:
    {
        addr = (val & mask(1, 5));
        ::qsnprintf(name, sizeof(name), "SET_WINDOW_PLANE_ADDR_$%.4X", addr * 0x400);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x8400:
    {
        addr = (val & mask(0, 3));
        ::qsnprintf(name, sizeof(name), "SET_PLANE_B_ADDR_$%.4X", addr * 0x2000);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x8500:
    {
        addr = (val & mask(0, 7));
        ::qsnprintf(name, sizeof(name), "SET_SPRITE_TBL_ADDR_$%.4X", addr * 0x200);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x8600:
    {
        if (val & mask(5))	append_cmt(ea, "ENABLE_SPRITES_REBASE", false);
        else append_cmt(ea, "DISABLE_SPRITES_REBASE", false);

        return true;
    }
    case 0x8700:
    {
        unsigned int xx = (val & mask(4, 2));
        unsigned int yyyy = (val & mask(0, 4));

        ::qsnprintf(name, sizeof(name), "SET_BG_AS_%dPAL_%dTH_COLOR", xx + 1, yyyy + 1);
        append_cmt(ea, name, false);

        return true;
    }
    case 0x8A00:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_HBLANK_COUNTER_VALUE_$%.4X", addr);
        append_cmt(ea, name, false);
        return true;
    } break;
    case 0x8B00:
    {
        switch (val & mask(0, 2))
        {
        case 0 /*00*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_FULLSCREEN", false); break;
        case 1 /*01*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_LINE_SCROLL", false); break;
        case 2 /*10*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_CELL_SCROLL", false); break;
        case 3 /*11*/: append_cmt(ea, "SET_HSCROLL_TYPE_AS_LINE__SCROLL", false); break;
        }

        if (val & mask(2))	append_cmt(ea, "_2CELLS_COLUMN_VSCROLL_MODE", false);
        else append_cmt(ea, "FULLSCREEN_VSCROLL_MODE", false);

        if (val & mask(3))	append_cmt(ea, "ENABLE_EXT_INTERRUPT", false);
        else append_cmt(ea, "DISABLE_EXT_INTERRUPT", false);

        return true;
    }
    case 0x8C00:
    {
        switch (val & 0x81)
        {
        case 0 /*0XXXXXX0*/: append_cmt(ea, "SET_40_TILES_WIDTH_MODE", false); break;
        case 0x81 /*1XXXXXX1*/: append_cmt(ea, "SET_32_TILES_WIDTH_MODE", false); break;
        }

        if (val & mask(3)) append_cmt(ea, "ENABLE_SHADOW_HIGHLIGHT_MODE", false);
        else append_cmt(ea, "DISABLE_SHADOW_HIGHLIGHT_MODE", false);

        switch (val & mask(1, 2))
        {
        case 0 /*00*/ << 1: append_cmt(ea, "NO_INTERLACE_MODE", false); break;
        case 1 /*01*/ << 1: append_cmt(ea, "ENABLE_SIMPLE_INTERLACE_MODE", false); break;
        case 3 /*11*/ << 1: append_cmt(ea, "ENABLE_DOUBLE_INTERLACE_MODE", false); break;
        }

        if (val & mask(4)) append_cmt(ea, "ENABLE_EXTERNAL_PIXEL_BUS", false);
        else append_cmt(ea, "DISABLE_EXTERNAL_PIXEL_BUS", false);

        if (val & mask(6)) append_cmt(ea, "DO_PIXEL_CLOCK_INSTEAD_OF_VSYNC", false);
        else append_cmt(ea, "DO_VSYNC_INSTEAD_OF_PIXEL_CLOCK", false);

        return true;
    }
    case 0x8D00:
    {
        addr = (val & mask(0, 6));
        ::qsnprintf(name, sizeof(name), "SET_HSCROLL_DATA_ADDR_$%.4X", addr * 0x400);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x8E00:
    {
        if (val & mask(0))	append_cmt(ea, "ENABLE_PLANE_A_REBASE", false);
        else append_cmt(ea, "DISABLE_PLANE_A_REBASE", false);

        if (val & mask(4))	append_cmt(ea, "ENABLE_PLANE_B_REBASE", false);
        else append_cmt(ea, "DISABLE_PLANE_B_REBASE", false);

        return true;
    }
    case 0x8F00:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_VDP_AUTO_INC_VALUE_$%.4X", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9000:
    {
        switch (val & mask(0, 2))
        {
        case 0 /*00*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_32_TILES", false); break;
        case 1 /*01*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_64_TILES", false); break;
        case 3 /*11*/: append_cmt(ea, "SET_PLANEA_PLANEB_WIDTH_TO_128_TILES", false); break;
        }

        switch (val & mask(4, 2))
        {
        case 0 /*00*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_32_TILES", false); break;
        case 1 /*01*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_64_TILES", false); break;
        case 3 /*11*/ << 4: append_cmt(ea, "SET_PLANEA_PLANEB_HEIGHT_TO_128_TILES", false); break;
        }

        return true;
    }
    case 0x9100:
    {
        if (val & mask(7)) append_cmt(ea, "MOVE_WINDOW_HORZ_RIGHT", false);
        else append_cmt(ea, "MOVE_WINDOW_HORZ_LEFT", false);

        addr = (val & mask(0, 5));
        ::qsnprintf(name, sizeof(name), "MOVE_BY_%d_CELLS", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9200:
    {
        if (val & mask(7)) append_cmt(ea, "MOVE_WINDOW_VERT_RIGHT", false);
        else append_cmt(ea, "MOVE_WINDOW_VERT_LEFT", false);

        addr = (val & mask(0, 5));
        ::qsnprintf(name, sizeof(name), "MOVE_BY_%d_CELLS", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9300:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_LOWER_BYTE_OF_DMA_LEN_TO_$%.2X", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9400:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_HIGHER_BYTE_OF_DMA_LEN_TO_$%.2X", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9500:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_LOWER_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9600:
    {
        addr = (val & mask(0, 8));
        ::qsnprintf(name, sizeof(name), "SET_MIDDLE_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
        append_cmt(ea, name, false);
        return true;
    }
    case 0x9700:
    {
        addr = (val & mask(0, 6));
        ::qsnprintf(name, sizeof(name), "SET_HIGH_BYTE_OF_DMA_SRC_TO_$%.2X", addr);
        append_cmt(ea, name, false);

        if (val & mask(7)) append_cmt(ea, "ADD_$800000_TO_DMA_SRC_ADDR", false);
        else append_cmt(ea, "SET_COPY_M68K_TO_VRAM_DMA_MODE", false);

        switch (val & mask(6, 2))
        {
        case 2 /*10*/ << 6: append_cmt(ea, "SET_VRAM_FILL_DMA_MODE", false); break;
        case 3 /*11*/ << 6: append_cmt(ea, "SET_VRAM_COPY_DMA_MODE", false); break;
        }

        return true;
    }
    default:
    {
        msg(wrong_vdp_cmd);
        return false;
    }
    }
}

//--------------------------------------------------------------------------
static void do_cmt_sr_ccr_reg_const(ea_t ea, uint32 val)
{
    if (val & mask(4)) append_cmt(ea, "SET_X", false);
    else append_cmt(ea, "CLR_X", false);

    if (val & mask(3)) append_cmt(ea, "SET_N", false);
    else append_cmt(ea, "CLR_N", false);

    if (val & mask(2)) append_cmt(ea, "SET_Z", false);
    else append_cmt(ea, "CLR_Z", false);

    if (val & mask(1)) append_cmt(ea, "SET_V", false);
    else append_cmt(ea, "CLR_V", false);

    if (val & mask(0)) append_cmt(ea, "SET_C", false);
    else append_cmt(ea, "CLR_C", false);

    if (val & mask(15)) append_cmt(ea, "SET_T1", false);
    else append_cmt(ea, "CLR_T1", false);

    if (val & mask(14)) append_cmt(ea, "SET_T0", false);
    else append_cmt(ea, "CLR_T0", false);

    if (val & mask(13)) append_cmt(ea, "SET_SF", false);
    else append_cmt(ea, "CLR_SF", false);

    if (val & mask(12)) append_cmt(ea, "SET_MF", false);
    else append_cmt(ea, "CLR_MF", false);

    switch ((val & mask(8, 3)))
    {
    case 0x7 /*111*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS", false); break;
    case 0x6 /*110*/ << 8: append_cmt(ea, "ENABLE_NO_INTERRUPTS", false); break;

    case 0x5 /*101*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS_EXCEPT_VBLANK", false); break;
    case 0x4 /*100*/ << 8: append_cmt(ea, "ENABLE_ONLY_VBLANK_INTERRUPT", false); break;

    case 0x3 /*011*/ << 8: append_cmt(ea, "DISABLE_ALL_INTERRUPTS_EXCEPT_VBLANK_HBLANK", false); break;
    case 0x2 /*010*/ << 8: append_cmt(ea, "ENABLE_ONLY_VBLANK_HBLANK_INTERRUPTS", false); break;

    case 0x1 /*001*/ << 8: append_cmt(ea, "DISABLE_NO_INTERRUPTS", false); break;
    case 0x0 /*000*/ << 8: append_cmt(ea, "ENABLE_ALL_INTERRUPTS", false); break;
    }
}

//--------------------------------------------------------------------------
static void do_cmt_vdp_rw_command(ea_t ea, uint32 val)
{
    char name[250];

    if (val & 0xFFFF0000) // command was sended by one dword
    {
        unsigned int addr = ((val & mask(0, 2)) << 14) | ((val & mask(16, 14)) >> 16);

        switch ((val >> 24) & mask(6))
        {
        case 0 << 6: // read operation
        {
            switch (val & ((1 << 31) | (1 << 5) | (1 << 4)))
            {
            case ((0 << 31) | (0 << 5) | (0 << 4)) /*000*/: // VRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_READ_VRAM_FROM_$%.4X", addr);
                append_cmt(ea, name, false);
            } break;
            case ((0 << 31) | (0 << 5) | (1 << 4)) /*001*/: // VSRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_READ_VSRAM_FROM_$%.4X", addr);
                append_cmt(ea, name, false);
            } break;
            case ((0 << 31) | (1 << 5) | (0 << 4)) /*010*/: // CRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_READ_CRAM_FROM_$%.4X", addr);
                append_cmt(ea, name, false);
            } break;
            default:
            {
                msg(wrong_vdp_cmd);
            } break;
            }
        } break;
        case 1 << 6: // write operation
        {
            switch (val & ((1 << 31) | (1 << 5) | (1 << 4)))
            {
            case ((0 << 31) | (0 << 5) | (0 << 4)) /*000*/: // VRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_VRAM_AT_$%.4X_ADDR", addr);
                append_cmt(ea, name, false);
            } break;
            case ((0 << 31) | (0 << 5) | (1 << 4)) /*001*/: // VSRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_VSRAM_AT_$%.4X_ADDR", addr);
                append_cmt(ea, name, false);
            } break;
            case ((1 << 31) | (0 << 5) | (0 << 4)) /*100*/: // CRAM
            {
                ::qsnprintf(name, sizeof(name), "DO_WRITE_TO_CRAM_AT_$%.4X_ADDR", addr);
                append_cmt(ea, name, false);
            } break;
            default:
            {
                msg(wrong_vdp_cmd);
            } break;
            }
        } break;
        default:
        {
            msg(wrong_vdp_cmd);
        } break;
        }
    }
    else // command was sended by halfs (this is high word of it)
    {
        switch ((val >> 8) & mask(6, 2))
        {
        case 0 /*00*/ << 6: append_cmt(ea, "VRAM_OR_VSRAM_OR_CRAM_READ_MODE", false); break;
        case 1 /*01*/ << 6: append_cmt(ea, "VRAM_OR_VSRAM_WRITE_MODE", false); break;
        case 3 /*11*/ << 6: append_cmt(ea, "CRAM_WRITE_MODE", false); break;
        }
    }

    if (val & mask(6)) append_cmt(ea, "VRAM_COPY_DMA_MODE", false);

    if (val & mask(7)) append_cmt(ea, "DO_OPERATION_USING_DMA", false);
    else append_cmt(ea, "DO_OPERATION_WITHOUT_DMA", false);
}

//--------------------------------------------------------------------------
static void do_cmt_z80_bus_command(ea_t ea, ea_t addr, uint32 val)
{
    switch (addr)
    {
    case 0xA11100: // IO_Z80BUS
    {
        switch (val)
        {
        case 0x0: append_cmt(ea, "Give the Z80 the bus back", false); break;
        case 0x100: append_cmt(ea, "Send the Z80 a bus request", false); break;
        }
    } break;
    case 0xA11200: // IO_Z80RES
    {
        switch (val)
        {
        case 0x0: append_cmt(ea, "Disable the Z80 reset", false); break;
        case 0x100: append_cmt(ea, "Reset the Z80", false); break;
        }
    } break;
    }
}

//--------------------------------------------------------------------------
static void print_version()
{
    static const char format[] = NAME " debugger plugin v%s;\nAuthor: Dr. MefistO [Lab 313] <meffi@lab313.ru>.";
    info(format, VERSION);
    msg(format, VERSION);
}

//--------------------------------------------------------------------------
// Initialize debugger plugin
static bool init_plugin(void)
{
    if (ph.id != PLFM_68K)
        return false;

    return true;
}

struct smd_constant_action_t : public action_handler_t
{
    virtual int idaapi activate(action_activation_ctx_t * ctx)
    {
        qstring name;
        ea_t ea = get_screen_ea();
        if (is_mapped(ea)) // address belongs to disassembly
        {
            if (get_cmt(&name, ea, false) != -1) // remove previous comment and exit
            {
                set_cmt(ea, "", false);
                return 1;
            }

            insn_t out;
            decode_insn(&out, ea);
            print_operand(&name, ea, 1);
            tag_remove(&name, name);

            uval_t val = 0;
            get_immvals(&val, ea, 0, get_flags(ea));
            uint32 value = (uint32)val;
            if (out.Op1.type == o_imm && out.Op2.type == o_reg && !::qstrcmp(name.c_str(), "sr"))
            {
                do_cmt_sr_ccr_reg_const(ea, value);
            }
            else if (out.Op1.type == o_imm && out.Op2.type == o_mem &&
                (out.Op2.addr == 0xA11200 || out.Op2.addr == 0xA11100))
            {
                do_cmt_z80_bus_command(ea, out.Op2.addr, value);
            }
            else if (is_vdp_rw_cmd(value))
            {
                do_cmt_vdp_rw_command(ea, value);
            }
            else if (is_vdp_send_cmd(value)) // comment set vdp reg cmd
            {
                do_cmt_vdp_reg_const(ea, value);
                do_cmt_vdp_reg_const(ea, value >> 16);
            }
        }
        return 1;
    }

    virtual action_state_t idaapi update(action_update_ctx_t *ctx)
    {
        return AST_ENABLE_ALWAYS;
    }
};

static const char smd_constant_name[] = "gensida:smd_constant";
static smd_constant_action_t smd_constant;
static action_desc_t smd_constant_action = ACTION_DESC_LITERAL(smd_constant_name, "Identify SMD constant", &smd_constant, "J", NULL, -1);

//--------------------------------------------------------------------------
static ssize_t idaapi hook_ui(void *user_data, int notification_code, va_list va)
{
    if (notification_code == ui_populating_widget_popup)
    {
        TWidget *widget = va_arg(va, TWidget *);
        if (get_widget_type(widget) == BWN_DISASM)
        {
            TPopupMenu *p = va_arg(va, TPopupMenu *);
            attach_action_to_popup(widget, p, smd_constant_name);
        }
    }

    return 0;
}

//--------------------------------------------------------------------------
// Initialize debugger plugin
static plugmod_t * idaapi init(void)
{
    if (init_plugin())
    {
        dbg = &debugger;
        plugin_inited = true;
        my_dbg = false;

        bool res = register_action(smd_constant_action);

        hook_to_notification_point(HT_UI, hook_ui, NULL);
        hook_to_notification_point(HT_IDP, hook_idp, NULL);

        print_version();
        return PLUGIN_KEEP;
    }
    return PLUGIN_SKIP;
}

//--------------------------------------------------------------------------
// Terminate debugger plugin
static void idaapi term(void)
{
    if (plugin_inited)
    {
        unhook_from_notification_point(HT_UI, hook_ui);
        unhook_from_notification_point(HT_IDP, hook_idp);

        unregister_action(smd_constant_name);

        plugin_inited = false;
    }
}

//--------------------------------------------------------------------------
// The plugin method - usually is not used for debugger plugins
static bool idaapi run(size_t arg)
{
    return false;
}

//--------------------------------------------------------------------------
char comment[] = NAME " debugger plugin by Dr. MefistO.";

char help[] =
NAME " debugger plugin by Dr. MefistO.\n"
"\n"
"This module lets you debug Genesis roms in IDA.\n";

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
    IDP_INTERFACE_VERSION,
    PLUGIN_PROC | PLUGIN_DBG | PLUGIN_MOD, // plugin flags
    init, // initialize

    term, // terminate. this pointer may be NULL.

    run, // invoke plugin

    comment, // long comment about the plugin
             // it could appear in the status line
             // or as a hint

    help, // multiline help about the plugin

    NAME " debugger plugin", // the preferred short name of the plugin

    "" // the preferred hotkey to run the plugin
};