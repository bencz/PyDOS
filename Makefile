# PyDOS wmake Makefile (MS-DOS 5.0)
# Usage: wmake [all|compiler|runtime|test|all32|runtime32|test32|clean]

# ---- Toolchain ----

CC          = wcc
CPP         = wpp386
CC386       = wcc386

CFLAGS      = -0 -ml -ox -zq -fpc
CPPFLAGS    = -3s -mf -ox -zq -fpc
C386FLAGS   = -3s -mf -ox -zq -fpc -dPYDOS_32BIT
CDBGFLAGS   = -0 -ml -od -zq -fpc -d2 -dPYDOS_DEBUG_CMP

BIN         = bin
LIB         = lib

SYSTEM32     = dos4g

# ---- Object lists ----

COMPILER_OBJS = &
    compiler\main.obj &
    compiler\lexer.obj &
    compiler\ast.obj &
    compiler\parser.obj &
    compiler\types.obj &
    compiler\sema.obj &
    compiler\mono.obj &
    compiler\ir.obj &
    compiler\iropt.obj &
    compiler\codegen.obj &
    compiler\cg8086.obj &
    compiler\cg386.obj &
    compiler\regalloc.obj &
    compiler\error.obj &
    compiler\pir.obj &
    compiler\pirbld.obj &
    compiler\pirprt.obj &
    compiler\pirlwr.obj &
    compiler\pirdom.obj &
    compiler\pirutil.obj &
    compiler\piropt.obj &
    compiler\pirtyp.obj &
    compiler\piresc.obj &
    compiler\pirspc.obj &
    compiler\modscan.obj &
    compiler\stdscan.obj &
    compiler\pirsrlz.obj &
    compiler\pirmrg.obj &
    compiler\stdbld.obj

RUNTIME_OBJS = &
    runtime\pdos_obj.obj &
    runtime\pdos_mem.obj &
    runtime\pdos_gc.obj &
    runtime\pdos_io.obj &
    runtime\pdos_str.obj &
    runtime\pdos_int.obj &
    runtime\pdos_blt.obj &
    runtime\pdos_exc.obj &
    runtime\pdos_vtb.obj &
    runtime\pdos_lst.obj &
    runtime\pdos_dic.obj &
    runtime\pdos_itn.obj &
    runtime\pdos_gen.obj &
    runtime\pdos_rt.obj &
    runtime\pdos_sjn.obj &
    runtime\pdos_arn.obj &
    runtime\pdos_cll.obj &
    runtime\pdos_asn.obj &
    runtime\pdos_exg.obj &
    runtime\pdos_fzs.obj &
    runtime\pdos_cpx.obj &
    runtime\pdos_bya.obj

TEST_OBJS = &
    rttests\main.obj &
    rttests\t_obj.obj &
    rttests\t_mem.obj &
    rttests\t_str.obj &
    rttests\t_gc.obj &
    rttests\t_int.obj &
    rttests\t_lst.obj &
    rttests\t_exc.obj &
    rttests\t_io.obj &
    rttests\t_gen.obj &
    rttests\t_rt.obj &
    rttests\t_vtb.obj &
    rttests\t_itn.obj &
    rttests\t_dic.obj &
    rttests\t_blt.obj &
    rttests\t_e2e.obj &
    rttests\t_gcs.obj &
    rttests\t_func.obj &
    rttests\t_sjn.obj &
    rttests\t_arn.obj &
    rttests\t_set.obj &
    rttests\t_cll.obj &
    rttests\t_asn.obj &
    rttests\t_unp.obj &
    rttests\t_excg.obj &
    rttests\t_fzs.obj &
    rttests\t_cpx.obj &
    rttests\t_bya.obj

RUNTIME32_OBJS = &
    runtime\pdos_o32.obj &
    runtime\pdos_m32.obj &
    runtime\pdos_g32.obj &
    runtime\pdos_i32.obj &
    runtime\pdos_s32.obj &
    runtime\pdos_n32.obj &
    runtime\pdos_b32.obj &
    runtime\pdos_e32.obj &
    runtime\pdos_v32.obj &
    runtime\pdos_l32.obj &
    runtime\pdos_d32.obj &
    runtime\pdos_t32.obj &
    runtime\pdos_x32.obj &
    runtime\pdos_r32.obj &
    runtime\pdos_j32.obj &
    runtime\pdos_a32.obj &
    runtime\pdos_c32.obj &
    runtime\pdos_y32.obj &
    runtime\pdos_q32.obj &
    runtime\pdos_f32.obj &
    runtime\pdos_p32.obj &
    runtime\pdos_h32.obj

TEST32_OBJS = &
    rttests\main32.obj &
    rttests\t_obj32.obj &
    rttests\t_mem32.obj &
    rttests\t_str32.obj &
    rttests\t_gc32.obj &
    rttests\t_int32.obj &
    rttests\t_lst32.obj &
    rttests\t_exc32.obj &
    rttests\t_io32.obj &
    rttests\t_gen32.obj &
    rttests\t_rt32.obj &
    rttests\t_vtb32.obj &
    rttests\t_itn32.obj &
    rttests\t_dic32.obj &
    rttests\t_blt32.obj &
    rttests\t_e2e32.obj &
    rttests\t_gcs32.obj &
    rttests\t_fun32.obj &
    rttests\t_sjn32.obj &
    rttests\t_arn32.obj &
    rttests\t_set32.obj &
    rttests\t_cll32.obj &
    rttests\t_asn32.obj &
    rttests\t_unp32.obj &
    rttests\t_exg32.obj &
    rttests\t_fzs32.obj &
    rttests\t_cpx32.obj &
    rttests\t_bya32.obj

# ===========================================================================
# Top-level targets
# ===========================================================================

all:   compiler runtime runtime32 test test32 .SYMBOLIC

.BEFORE
    @if not exist $(BIN) mkdir $(BIN)
    @if not exist $(LIB) mkdir $(LIB)

# ===========================================================================
# Implicit rules
# ===========================================================================

.cpp: compiler
.c:   runtime;rttests

.cpp.obj: .AUTODEPEND
    $(CPP) $(CPPFLAGS) -fo=$@ $<

.c.obj: .AUTODEPEND
    $(CC) $(CFLAGS) -fo=$@ $<

# ===========================================================================
# Compiler (PYDOS.EXE, 386 DOS/4GW)
# ===========================================================================

# Explicit rule: "main" exists in both compiler\ (.cpp) and rttests\ (.c).
# Without this, wmake picks rttests\main.c via the .c search path.
compiler\main.obj: compiler\main.cpp .AUTODEPEND
    $(CPP) $(CPPFLAGS) -fo=$@ compiler\main.cpp

compiler\stdscan.obj: compiler\stdscan.cpp .AUTODEPEND
    $(CPP) $(CPPFLAGS) -fo=$@ compiler\stdscan.cpp

compiler: $(BIN)\PYDOS.EXE $(BIN)\STDLIB.IDX .SYMBOLIC

$(BIN)\PYDOS.EXE: $(COMPILER_OBJS)
    %write  pydos.lnk system $(SYSTEM32)
    %append pydos.lnk name $@
    %append pydos.lnk file compiler\main.obj
    %append pydos.lnk file compiler\lexer.obj
    %append pydos.lnk file compiler\ast.obj
    %append pydos.lnk file compiler\parser.obj
    %append pydos.lnk file compiler\types.obj
    %append pydos.lnk file compiler\sema.obj
    %append pydos.lnk file compiler\mono.obj
    %append pydos.lnk file compiler\ir.obj
    %append pydos.lnk file compiler\iropt.obj
    %append pydos.lnk file compiler\codegen.obj
    %append pydos.lnk file compiler\cg8086.obj
    %append pydos.lnk file compiler\cg386.obj
    %append pydos.lnk file compiler\regalloc.obj
    %append pydos.lnk file compiler\error.obj
    %append pydos.lnk file compiler\pir.obj
    %append pydos.lnk file compiler\pirbld.obj
    %append pydos.lnk file compiler\pirprt.obj
    %append pydos.lnk file compiler\pirlwr.obj
    %append pydos.lnk file compiler\pirdom.obj
    %append pydos.lnk file compiler\pirutil.obj
    %append pydos.lnk file compiler\piropt.obj
    %append pydos.lnk file compiler\pirtyp.obj
    %append pydos.lnk file compiler\piresc.obj
    %append pydos.lnk file compiler\pirspc.obj
    %append pydos.lnk file compiler\modscan.obj
    %append pydos.lnk file compiler\stdscan.obj
    %append pydos.lnk file compiler\pirsrlz.obj
    %append pydos.lnk file compiler\pirmrg.obj
    %append pydos.lnk file compiler\stdbld.obj
    wlink @pydos.lnk

# ===========================================================================
# Stdlibgen tool (STDGEN.EXE, 386 DOS/4GW)
# ===========================================================================

compiler\stdgen.obj: compiler\stdgen.cpp .AUTODEPEND
    $(CPP) $(CPPFLAGS) -fo=$@ compiler\stdgen.cpp

$(BIN)\STDGEN.EXE: compiler\stdgen.obj
    %write  stdgen.lnk system $(SYSTEM32)
    %append stdgen.lnk name $@
    %append stdgen.lnk file compiler\stdgen.obj
    wlink @stdgen.lnk

$(BIN)\STDLIB.IDX: $(BIN)\PYDOS.EXE
    $(BIN)\PYDOS.EXE --build-stdlib stdlib -o $(BIN)\STDLIB.IDX

stdgen: $(BIN)\STDGEN.EXE .SYMBOLIC

# ===========================================================================
# 16-bit Runtime (PYDOSRT.LIB)
# ===========================================================================

runtime: $(LIB)\PYDOSRT.LIB .SYMBOLIC

$(LIB)\PYDOSRT.LIB: $(RUNTIME_OBJS)
    %write  pydosrt.lnk -q -n $@
    %append pydosrt.lnk +runtime\pdos_obj.obj
    %append pydosrt.lnk +runtime\pdos_mem.obj
    %append pydosrt.lnk +runtime\pdos_gc.obj
    %append pydosrt.lnk +runtime\pdos_io.obj
    %append pydosrt.lnk +runtime\pdos_str.obj
    %append pydosrt.lnk +runtime\pdos_int.obj
    %append pydosrt.lnk +runtime\pdos_blt.obj
    %append pydosrt.lnk +runtime\pdos_exc.obj
    %append pydosrt.lnk +runtime\pdos_vtb.obj
    %append pydosrt.lnk +runtime\pdos_lst.obj
    %append pydosrt.lnk +runtime\pdos_dic.obj
    %append pydosrt.lnk +runtime\pdos_itn.obj
    %append pydosrt.lnk +runtime\pdos_gen.obj
    %append pydosrt.lnk +runtime\pdos_rt.obj
    %append pydosrt.lnk +runtime\pdos_sjn.obj
    %append pydosrt.lnk +runtime\pdos_arn.obj
    %append pydosrt.lnk +runtime\pdos_cll.obj
    %append pydosrt.lnk +runtime\pdos_asn.obj
    %append pydosrt.lnk +runtime\pdos_exg.obj
    %append pydosrt.lnk +runtime\pdos_fzs.obj
    %append pydosrt.lnk +runtime\pdos_cpx.obj
    %append pydosrt.lnk +runtime\pdos_bya.obj
    wlib @pydosrt.lnk

# ===========================================================================
# 16-bit Runtime DEBUG build (PYDOSRT.LIB with PYDOS_DEBUG_CMP)
# Usage: wmake runtime_debug
# ===========================================================================

runtime_debug: .SYMBOLIC
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_obj.obj runtime\pdos_obj.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_mem.obj runtime\pdos_mem.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_gc.obj runtime\pdos_gc.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_io.obj runtime\pdos_io.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_str.obj runtime\pdos_str.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_int.obj runtime\pdos_int.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_blt.obj runtime\pdos_blt.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_exc.obj runtime\pdos_exc.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_vtb.obj runtime\pdos_vtb.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_lst.obj runtime\pdos_lst.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_dic.obj runtime\pdos_dic.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_itn.obj runtime\pdos_itn.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_gen.obj runtime\pdos_gen.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_rt.obj runtime\pdos_rt.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_sjn.obj runtime\pdos_sjn.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_arn.obj runtime\pdos_arn.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_cll.obj runtime\pdos_cll.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_asn.obj runtime\pdos_asn.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_exg.obj runtime\pdos_exg.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_fzs.obj runtime\pdos_fzs.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_cpx.obj runtime\pdos_cpx.c
    $(CC) $(CDBGFLAGS) -fo=runtime\pdos_bya.obj runtime\pdos_bya.c
    %write  pydosrt.lnk -q -n $(LIB)\PYDOSRT.LIB
    %append pydosrt.lnk +runtime\pdos_obj.obj
    %append pydosrt.lnk +runtime\pdos_mem.obj
    %append pydosrt.lnk +runtime\pdos_gc.obj
    %append pydosrt.lnk +runtime\pdos_io.obj
    %append pydosrt.lnk +runtime\pdos_str.obj
    %append pydosrt.lnk +runtime\pdos_int.obj
    %append pydosrt.lnk +runtime\pdos_blt.obj
    %append pydosrt.lnk +runtime\pdos_exc.obj
    %append pydosrt.lnk +runtime\pdos_vtb.obj
    %append pydosrt.lnk +runtime\pdos_lst.obj
    %append pydosrt.lnk +runtime\pdos_dic.obj
    %append pydosrt.lnk +runtime\pdos_itn.obj
    %append pydosrt.lnk +runtime\pdos_gen.obj
    %append pydosrt.lnk +runtime\pdos_rt.obj
    %append pydosrt.lnk +runtime\pdos_sjn.obj
    %append pydosrt.lnk +runtime\pdos_arn.obj
    %append pydosrt.lnk +runtime\pdos_cll.obj
    %append pydosrt.lnk +runtime\pdos_asn.obj
    %append pydosrt.lnk +runtime\pdos_exg.obj
    %append pydosrt.lnk +runtime\pdos_fzs.obj
    %append pydosrt.lnk +runtime\pdos_cpx.obj
    %append pydosrt.lnk +runtime\pdos_bya.obj
    wlib @pydosrt.lnk

# ===========================================================================
# 16-bit Tests (RTTEST.EXE)
# ===========================================================================

test: runtime $(BIN)\RTTEST.EXE .SYMBOLIC

$(BIN)\RTTEST.EXE: $(TEST_OBJS) $(LIB)\PYDOSRT.LIB
    %write  rttest.lnk system dos
    %append rttest.lnk name $@
    %append rttest.lnk file rttests\main.obj
    %append rttest.lnk file rttests\t_obj.obj
    %append rttest.lnk file rttests\t_mem.obj
    %append rttest.lnk file rttests\t_str.obj
    %append rttest.lnk file rttests\t_gc.obj
    %append rttest.lnk file rttests\t_int.obj
    %append rttest.lnk file rttests\t_lst.obj
    %append rttest.lnk file rttests\t_exc.obj
    %append rttest.lnk file rttests\t_io.obj
    %append rttest.lnk file rttests\t_gen.obj
    %append rttest.lnk file rttests\t_rt.obj
    %append rttest.lnk file rttests\t_vtb.obj
    %append rttest.lnk file rttests\t_itn.obj
    %append rttest.lnk file rttests\t_dic.obj
    %append rttest.lnk file rttests\t_blt.obj
    %append rttest.lnk file rttests\t_e2e.obj
    %append rttest.lnk file rttests\t_gcs.obj
    %append rttest.lnk file rttests\t_func.obj
    %append rttest.lnk file rttests\t_sjn.obj
    %append rttest.lnk file rttests\t_arn.obj
    %append rttest.lnk file rttests\t_set.obj
    %append rttest.lnk file rttests\t_cll.obj
    %append rttest.lnk file rttests\t_asn.obj
    %append rttest.lnk file rttests\t_unp.obj
    %append rttest.lnk file rttests\t_excg.obj
    %append rttest.lnk file rttests\t_fzs.obj
    %append rttest.lnk file rttests\t_cpx.obj
    %append rttest.lnk file rttests\t_bya.obj
    %append rttest.lnk library $(LIB)\PYDOSRT.LIB
    wlink @rttest.lnk

# ===========================================================================
# 32-bit Runtime (PDOS32RT.LIB, DOS/4GW)
# ===========================================================================

runtime32: $(LIB)\PDOS32RT.LIB .SYMBOLIC

$(LIB)\PDOS32RT.LIB: $(RUNTIME32_OBJS)
    %write  pdos32rt.lnk -q -n $@
    %append pdos32rt.lnk +runtime\pdos_o32.obj
    %append pdos32rt.lnk +runtime\pdos_m32.obj
    %append pdos32rt.lnk +runtime\pdos_g32.obj
    %append pdos32rt.lnk +runtime\pdos_i32.obj
    %append pdos32rt.lnk +runtime\pdos_s32.obj
    %append pdos32rt.lnk +runtime\pdos_n32.obj
    %append pdos32rt.lnk +runtime\pdos_b32.obj
    %append pdos32rt.lnk +runtime\pdos_e32.obj
    %append pdos32rt.lnk +runtime\pdos_v32.obj
    %append pdos32rt.lnk +runtime\pdos_l32.obj
    %append pdos32rt.lnk +runtime\pdos_d32.obj
    %append pdos32rt.lnk +runtime\pdos_t32.obj
    %append pdos32rt.lnk +runtime\pdos_x32.obj
    %append pdos32rt.lnk +runtime\pdos_r32.obj
    %append pdos32rt.lnk +runtime\pdos_j32.obj
    %append pdos32rt.lnk +runtime\pdos_a32.obj
    %append pdos32rt.lnk +runtime\pdos_c32.obj
    %append pdos32rt.lnk +runtime\pdos_y32.obj
    %append pdos32rt.lnk +runtime\pdos_q32.obj
    %append pdos32rt.lnk +runtime\pdos_f32.obj
    %append pdos32rt.lnk +runtime\pdos_p32.obj
    %append pdos32rt.lnk +runtime\pdos_h32.obj
    wlib @pdos32rt.lnk

runtime\pdos_o32.obj: runtime\pdos_obj.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_m32.obj: runtime\pdos_mem.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_g32.obj: runtime\pdos_gc.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_i32.obj: runtime\pdos_io.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_s32.obj: runtime\pdos_str.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_n32.obj: runtime\pdos_int.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_b32.obj: runtime\pdos_blt.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_e32.obj: runtime\pdos_exc.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_v32.obj: runtime\pdos_vtb.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_l32.obj: runtime\pdos_lst.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_d32.obj: runtime\pdos_dic.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_t32.obj: runtime\pdos_itn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_x32.obj: runtime\pdos_gen.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_r32.obj: runtime\pdos_rt.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_j32.obj: runtime\pdos_sjn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_a32.obj: runtime\pdos_arn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_c32.obj: runtime\pdos_cll.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_y32.obj: runtime\pdos_asn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_q32.obj: runtime\pdos_exg.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_f32.obj: runtime\pdos_fzs.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_p32.obj: runtime\pdos_cpx.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

runtime\pdos_h32.obj: runtime\pdos_bya.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

# ===========================================================================
# 32-bit Tests (RTTEST32.EXE, DOS/4GW)
# ===========================================================================

test32: runtime32 $(BIN)\RTTEST32.EXE .SYMBOLIC

$(BIN)\RTTEST32.EXE: $(TEST32_OBJS) $(LIB)\PDOS32RT.LIB
    %write  rtts32.lnk system $(SYSTEM32)
    %append rtts32.lnk name $@
    %append rtts32.lnk file rttests\main32.obj
    %append rtts32.lnk file rttests\t_obj32.obj
    %append rtts32.lnk file rttests\t_mem32.obj
    %append rtts32.lnk file rttests\t_str32.obj
    %append rtts32.lnk file rttests\t_gc32.obj
    %append rtts32.lnk file rttests\t_int32.obj
    %append rtts32.lnk file rttests\t_lst32.obj
    %append rtts32.lnk file rttests\t_exc32.obj
    %append rtts32.lnk file rttests\t_io32.obj
    %append rtts32.lnk file rttests\t_gen32.obj
    %append rtts32.lnk file rttests\t_rt32.obj
    %append rtts32.lnk file rttests\t_vtb32.obj
    %append rtts32.lnk file rttests\t_itn32.obj
    %append rtts32.lnk file rttests\t_dic32.obj
    %append rtts32.lnk file rttests\t_blt32.obj
    %append rtts32.lnk file rttests\t_e2e32.obj
    %append rtts32.lnk file rttests\t_gcs32.obj
    %append rtts32.lnk file rttests\t_fun32.obj
    %append rtts32.lnk file rttests\t_sjn32.obj
    %append rtts32.lnk file rttests\t_arn32.obj
    %append rtts32.lnk file rttests\t_set32.obj
    %append rtts32.lnk file rttests\t_cll32.obj
    %append rtts32.lnk file rttests\t_asn32.obj
    %append rtts32.lnk file rttests\t_unp32.obj
    %append rtts32.lnk file rttests\t_exg32.obj
    %append rtts32.lnk file rttests\t_fzs32.obj
    %append rtts32.lnk file rttests\t_cpx32.obj
    %append rtts32.lnk file rttests\t_bya32.obj
    %append rtts32.lnk library $(LIB)\PDOS32RT.LIB
    wlink @rtts32.lnk

rttests\main32.obj: rttests\main.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_obj32.obj: rttests\t_obj.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_mem32.obj: rttests\t_mem.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_str32.obj: rttests\t_str.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_gc32.obj: rttests\t_gc.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_int32.obj: rttests\t_int.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_lst32.obj: rttests\t_lst.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_exc32.obj: rttests\t_exc.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_io32.obj: rttests\t_io.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_gen32.obj: rttests\t_gen.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_rt32.obj: rttests\t_rt.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_vtb32.obj: rttests\t_vtb.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_itn32.obj: rttests\t_itn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_dic32.obj: rttests\t_dic.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_blt32.obj: rttests\t_blt.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_e2e32.obj: rttests\t_e2e.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_gcs32.obj: rttests\t_gcs.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_fun32.obj: rttests\t_func.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_sjn32.obj: rttests\t_sjn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_arn32.obj: rttests\t_arn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_set32.obj: rttests\t_set.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_cll32.obj: rttests\t_cll.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_asn32.obj: rttests\t_asn.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_unp32.obj: rttests\t_unp.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_exg32.obj: rttests\t_excg.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_fzs32.obj: rttests\t_fzs.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_cpx32.obj: rttests\t_cpx.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

rttests\t_bya32.obj: rttests\t_bya.c .AUTODEPEND
    $(CC386) $(C386FLAGS) -fo=$@ $[@

# ===========================================================================
# Clean
# ===========================================================================

clean: .SYMBOLIC
    -del $(BIN)\*.EXE
    -del $(BIN)\*.IDX
    -del $(LIB)\*.LIB
    -rmdir $(BIN)
    -rmdir $(LIB)
    -del compiler\*.obj
    -del runtime\*.obj
    -del rttests\*.obj
    -del tests\*.out
    -del tests\*.asm
    -del *.lnk
    -del *.err
    -del *.log
    -del *.exe
    -del *.map
    -del *.out
