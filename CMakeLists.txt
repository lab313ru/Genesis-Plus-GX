cmake_minimum_required(VERSION 3.13)
project(gpgx_debugger C)

set(CMAKE_C_STANDARD 11)

add_compile_definitions(
        USE_LIBTREMOR
        USE_LIBCHDR
        PACKAGE_VERSION="1.3.2"
        FLAC_API_EXPORTS
        FLAC__HAS_OGG=0
        HAVE_LROUND
        HAVE_STDINT_H
        _7ZIP_ST
        HAVE_FSEEKO
        DEBUG
        USE_16BPP_RENDERING
        FRONTEND_SUPPORTS_RGB565
        LSB_FIRST
        BYTE_ORDER=LITTLE_ENDIAN
        HAVE_ZLIB
        __LIBRETRO__
        M68K_OVERCLOCK_SHIFT=20
        Z80_OVERCLOCK_SHIFT=20
        HAVE_YM3438_CORE
)

add_definitions("-D INLINE=\"static inline\"")

SET(BASEPATH "${CMAKE_SOURCE_DIR}")

include_directories(gpgx_debugger PRIVATE
        ${BASEPATH}/libretro
        ${BASEPATH}/libretro/libretro-common/include
        ${BASEPATH}/core
        ${BASEPATH}/core/cart_hw
        ${BASEPATH}/core/cart_hw/svp
        ${BASEPATH}/core/cd_hw
        ${BASEPATH}/core/cd_hw/libchdr/deps/libFLAC/include
        ${BASEPATH}/core/cd_hw/libchdr/deps/lzma
        ${BASEPATH}/core/cd_hw/libchdr/deps/zlib
        ${BASEPATH}/core/debug
        ${BASEPATH}/core/input_hw
        ${BASEPATH}/core/m68k
        ${BASEPATH}/core/ntsc
        ${BASEPATH}/core/sound
        ${BASEPATH}/core/z80
        ${BASEPATH}/gui
        )

add_library(gpgx_debugger
        SHARED

        core/cart_hw/svp/ssp16.c
        core/cart_hw/svp/svp.c
        core/cart_hw/areplay.c
        core/cart_hw/eeprom_93c.c
        core/cart_hw/eeprom_i2c.c
        core/cart_hw/eeprom_spi.c
        core/cart_hw/ggenie.c
        core/cart_hw/md_cart.c
        core/cart_hw/sms_cart.c
        core/cart_hw/sram.c

        core/cd_hw/libchdr/deps/libFLAC/bitmath.c
        core/cd_hw/libchdr/deps/libFLAC/bitreader.c
        core/cd_hw/libchdr/deps/libFLAC/cpu.c
        core/cd_hw/libchdr/deps/libFLAC/crc.c
        core/cd_hw/libchdr/deps/libFLAC/fixed.c
        core/cd_hw/libchdr/deps/libFLAC/float.c
        core/cd_hw/libchdr/deps/libFLAC/format.c
        core/cd_hw/libchdr/deps/libFLAC/lpc.c
        core/cd_hw/libchdr/deps/libFLAC/lpc_intrin_avx2.c
        core/cd_hw/libchdr/deps/libFLAC/lpc_intrin_sse.c
        core/cd_hw/libchdr/deps/libFLAC/lpc_intrin_sse2.c
        core/cd_hw/libchdr/deps/libFLAC/lpc_intrin_sse41.c
        core/cd_hw/libchdr/deps/libFLAC/md5.c
        core/cd_hw/libchdr/deps/libFLAC/memory.c
        core/cd_hw/libchdr/deps/libFLAC/stream_decoder.c

        core/cd_hw/libchdr/deps/lzma/LzFind.c
        core/cd_hw/libchdr/deps/lzma/LzmaDec.c
        core/cd_hw/libchdr/deps/lzma/LzmaEnc.c

        core/cd_hw/libchdr/deps/zlib/adler32.c
        core/cd_hw/libchdr/deps/zlib/inffast.c
        core/cd_hw/libchdr/deps/zlib/inflate.c
        core/cd_hw/libchdr/deps/zlib/inftrees.c
        core/cd_hw/libchdr/deps/zlib/zutil.c

        core/cd_hw/libchdr/src/bitstream.c
        core/cd_hw/libchdr/src/cdrom.c
        core/cd_hw/libchdr/src/chd.c
        core/cd_hw/libchdr/src/flac.c
        core/cd_hw/libchdr/src/huffman.c

        core/cd_hw/cd_cart.c
        core/cd_hw/cdc.c
        core/cd_hw/cdd.c
        core/cd_hw/gfx.c
        core/cd_hw/pcm.c
        core/cd_hw/scd.c

        core/debug/debug.c
        core/debug/debug_wrap.c

        core/input_hw/activator.c
        core/input_hw/gamepad.c
        core/input_hw/graphic_board.c
        core/input_hw/input.c
        core/input_hw/lightgun.c
        core/input_hw/mouse.c
        core/input_hw/paddle.c
        core/input_hw/sportspad.c
        core/input_hw/teamplayer.c
        core/input_hw/terebi_oekaki.c
        core/input_hw/xe_1ap.c

        core/m68k/m68kcpu.c
        core/m68k/s68kcpu.c

        core/ntsc/md_ntsc.c
        core/ntsc/sms_ntsc.c

        core/sound/blip_buf.c
        core/sound/eq.c
        core/sound/psg.c
        core/sound/sound.c
        core/sound/ym2413.c
        core/sound/ym2612.c
        core/sound/ym3438.c

        core/tremor/bitwise.c
        core/tremor/block.c
        core/tremor/codebook.c
        core/tremor/floor0.c
        core/tremor/floor1.c
        core/tremor/framing.c
        core/tremor/info.c
        core/tremor/mapping0.c
        core/tremor/mdct.c
        core/tremor/registry.c
        core/tremor/res012.c
        core/tremor/sharedbook.c
        core/tremor/synthesis.c
        core/tremor/vorbisfile.c
        core/tremor/window.c

        core/z80/z80.c

        core/genesis.c
        core/io_ctrl.c
        core/loadrom.c
        core/mem68k.c
        core/membnk.c
        core/memz80.c
        core/state.c
        core/system.c
        core/vdp_ctrl.c
        core/vdp_render.c

        gui/gui.c

        libretro/libretro-common/compat/compat_strl.c
        libretro/libretro-common/compat/fopen_utf8.c

        libretro/libretro-common/encodings/encoding_utf.c

        libretro/libretro-common/streams/file_stream.c
        libretro/libretro-common/streams/file_stream_transforms.c

        libretro/libretro-common/vfs/vfs_implementation.c

        libretro/libretro.c
        libretro/scrc32.c
        )

target_link_libraries(gpgx_debugger PRIVATE m rt pthread)