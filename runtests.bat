@echo off

if "%1"=="386" set MODE=386
if not "%1"=="386" set MODE=8086

echo === Running tests in %MODE% mode ===

DEL *.MAP
DEL *.ASM
DEL TESTS\*.OUT

if exist _PASSED.TMP del _PASSED.TMP
if exist _FAILED.TMP del _FAILED.TMP
if exist _TESTOK del _TESTOK

call runone.bat hello %MODE%
if exist _TESTOK echo hello>>_PASSED.TMP
if not exist _TESTOK echo hello>>_FAILED.TMP

call runone.bat arith %MODE%
if exist _TESTOK echo arith>>_PASSED.TMP
if not exist _TESTOK echo arith>>_FAILED.TMP

call runone.bat cls_bas %MODE%
if exist _TESTOK echo cls_bas>>_PASSED.TMP
if not exist _TESTOK echo cls_bas>>_FAILED.TMP

call runone.bat gen_stk %MODE%
if exist _TESTOK echo gen_stk>>_PASSED.TMP
if not exist _TESTOK echo gen_stk>>_FAILED.TMP

call runone.bat bool_ops %MODE%
if exist _TESTOK echo bool_ops>>_PASSED.TMP
if not exist _TESTOK echo bool_ops>>_FAILED.TMP

call runone.bat str_ops %MODE%
if exist _TESTOK echo str_ops>>_PASSED.TMP
if not exist _TESTOK echo str_ops>>_FAILED.TMP

call runone.bat lst_bas %MODE%
if exist _TESTOK echo lst_bas>>_PASSED.TMP
if not exist _TESTOK echo lst_bas>>_FAILED.TMP

call runone.bat dct_bas %MODE%
if exist _TESTOK echo dct_bas>>_PASSED.TMP
if not exist _TESTOK echo dct_bas>>_FAILED.TMP

call runone.bat for_loop %MODE%
if exist _TESTOK echo for_loop>>_PASSED.TMP
if not exist _TESTOK echo for_loop>>_FAILED.TMP

call runone.bat nst_if %MODE%
if exist _TESTOK echo nst_if>>_PASSED.TMP
if not exist _TESTOK echo nst_if>>_FAILED.TMP

call runone.bat brk_cont %MODE%
if exist _TESTOK echo brk_cont>>_PASSED.TMP
if not exist _TESTOK echo brk_cont>>_FAILED.TMP

call runone.bat nst_func %MODE%
if exist _TESTOK echo nst_func>>_PASSED.TMP
if not exist _TESTOK echo nst_func>>_FAILED.TMP

call runone.bat aug_asgn %MODE%
if exist _TESTOK echo aug_asgn>>_PASSED.TMP
if not exist _TESTOK echo aug_asgn>>_FAILED.TMP

call runone.bat cmp_chn %MODE%
if exist _TESTOK echo cmp_chn>>_PASSED.TMP
if not exist _TESTOK echo cmp_chn>>_FAILED.TMP

call runone.bat bit_ops %MODE%
if exist _TESTOK echo bit_ops>>_PASSED.TMP
if not exist _TESTOK echo bit_ops>>_FAILED.TMP

call runone.bat complex %MODE%
if exist _TESTOK echo complex>>_PASSED.TMP
if not exist _TESTOK echo complex>>_FAILED.TMP

call runone.bat try_exc %MODE%
if exist _TESTOK echo try_exc>>_PASSED.TMP
if not exist _TESTOK echo try_exc>>_FAILED.TMP

call runone.bat ft_exc %MODE%
if exist _TESTOK echo ft_exc>>_PASSED.TMP
if not exist _TESTOK echo ft_exc>>_FAILED.TMP

call runone.bat ft_shape %MODE%
if exist _TESTOK echo ft_shape>>_PASSED.TMP
if not exist _TESTOK echo ft_shape>>_FAILED.TMP

call runone.bat ft_vec %MODE%
if exist _TESTOK echo ft_vec>>_PASSED.TMP
if not exist _TESTOK echo ft_vec>>_FAILED.TMP

call runone.bat ft_ent %MODE%
if exist _TESTOK echo ft_ent>>_PASSED.TMP
if not exist _TESTOK echo ft_ent>>_FAILED.TMP

call runone.bat ft_gen %MODE%
if exist _TESTOK echo ft_gen>>_PASSED.TMP
if not exist _TESTOK echo ft_gen>>_FAILED.TMP

call runone.bat ft_algo %MODE%
if exist _TESTOK echo ft_algo>>_PASSED.TMP
if not exist _TESTOK echo ft_algo>>_FAILED.TMP

call runone.bat ft_pop %MODE%
if exist _TESTOK echo ft_pop>>_PASSED.TMP
if not exist _TESTOK echo ft_pop>>_FAILED.TMP

call runone.bat ft_pop2 %MODE%
if exist _TESTOK echo ft_pop2>>_PASSED.TMP
if not exist _TESTOK echo ft_pop2>>_FAILED.TMP

call runone.bat ft_pop3 %MODE%
if exist _TESTOK echo ft_pop3>>_PASSED.TMP
if not exist _TESTOK echo ft_pop3>>_FAILED.TMP

call runone.bat ft_pop4 %MODE%
if exist _TESTOK echo ft_pop4>>_PASSED.TMP
if not exist _TESTOK echo ft_pop4>>_FAILED.TMP

call runone.bat ft_pop5 %MODE%
if exist _TESTOK echo ft_pop5>>_PASSED.TMP
if not exist _TESTOK echo ft_pop5>>_FAILED.TMP

call runone.bat fact %MODE%
if exist _TESTOK echo fact>>_PASSED.TMP
if not exist _TESTOK echo fact>>_FAILED.TMP

call runone.bat brk_stm %MODE%
if exist _TESTOK echo brk_stm>>_PASSED.TMP
if not exist _TESTOK echo brk_stm>>_FAILED.TMP

call runone.bat pass_st %MODE%
if exist _TESTOK echo pass_st>>_PASSED.TMP
if not exist _TESTOK echo pass_st>>_FAILED.TMP

call runone.bat blt_ops %MODE%
if exist _TESTOK echo blt_ops>>_PASSED.TMP
if not exist _TESTOK echo blt_ops>>_FAILED.TMP

call runone.bat lst_itr %MODE%
if exist _TESTOK echo lst_itr>>_PASSED.TMP
if not exist _TESTOK echo lst_itr>>_FAILED.TMP

call runone.bat exc_adv %MODE%
if exist _TESTOK echo exc_adv>>_PASSED.TMP
if not exist _TESTOK echo exc_adv>>_FAILED.TMP

call runone.bat neg_idx %MODE%
if exist _TESTOK echo neg_idx>>_PASSED.TMP
if not exist _TESTOK echo neg_idx>>_FAILED.TMP

call runone.bat str_idx %MODE%
if exist _TESTOK echo str_idx>>_PASSED.TMP
if not exist _TESTOK echo str_idx>>_FAILED.TMP

call runone.bat lst_ops %MODE%
if exist _TESTOK echo lst_ops>>_PASSED.TMP
if not exist _TESTOK echo lst_ops>>_FAILED.TMP

call runone.bat dct_ops %MODE%
if exist _TESTOK echo dct_ops>>_PASSED.TMP
if not exist _TESTOK echo dct_ops>>_FAILED.TMP

call runone.bat conv_fn %MODE%
if exist _TESTOK echo conv_fn>>_PASSED.TMP
if not exist _TESTOK echo conv_fn>>_FAILED.TMP

call runone.bat dct_in %MODE%
if exist _TESTOK echo dct_in>>_PASSED.TMP
if not exist _TESTOK echo dct_in>>_FAILED.TMP

call runone.bat dct_itr %MODE%
if exist _TESTOK echo dct_itr>>_PASSED.TMP
if not exist _TESTOK echo dct_itr>>_FAILED.TMP

call runone.bat assert %MODE%
if exist _TESTOK echo assert>>_PASSED.TMP
if not exist _TESTOK echo assert>>_FAILED.TMP

call runone.bat slicing %MODE%
if exist _TESTOK echo slicing>>_PASSED.TMP
if not exist _TESTOK echo slicing>>_FAILED.TMP

call runone.bat mul_exc %MODE%
if exist _TESTOK echo mul_exc>>_PASSED.TMP
if not exist _TESTOK echo mul_exc>>_FAILED.TMP

call runone.bat dflt_pm %MODE%
if exist _TESTOK echo dflt_pm>>_PASSED.TMP
if not exist _TESTOK echo dflt_pm>>_FAILED.TMP

call runone.bat dunder %MODE%
if exist _TESTOK echo dunder>>_PASSED.TMP
if not exist _TESTOK echo dunder>>_FAILED.TMP

call runone.bat finally %MODE%
if exist _TESTOK echo finally>>_PASSED.TMP
if not exist _TESTOK echo finally>>_FAILED.TMP

call runone.bat lp_else %MODE%
if exist _TESTOK echo lp_else>>_PASSED.TMP
if not exist _TESTOK echo lp_else>>_FAILED.TMP

call runone.bat is_inst %MODE%
if exist _TESTOK echo is_inst>>_PASSED.TMP
if not exist _TESTOK echo is_inst>>_FAILED.TMP

call runone.bat is_sub %MODE%
if exist _TESTOK echo is_sub>>_PASSED.TMP
if not exist _TESTOK echo is_sub>>_FAILED.TMP

call runone.bat dct_get %MODE%
if exist _TESTOK echo dct_get>>_PASSED.TMP
if not exist _TESTOK echo dct_get>>_FAILED.TMP

call runone.bat gen_2tp %MODE%
if exist _TESTOK echo gen_2tp>>_PASSED.TMP
if not exist _TESTOK echo gen_2tp>>_FAILED.TMP

call runone.bat tuples %MODE%
if exist _TESTOK echo tuples>>_PASSED.TMP
if not exist _TESTOK echo tuples>>_FAILED.TMP

call runone.bat lst_srt %MODE%
if exist _TESTOK echo lst_srt>>_PASSED.TMP
if not exist _TESTOK echo lst_srt>>_FAILED.TMP

call runone.bat lst_call %MODE%
if exist _TESTOK echo lst_call>>_PASSED.TMP
if not exist _TESTOK echo lst_call>>_FAILED.TMP

call runone.bat flt_ops %MODE%
if exist _TESTOK echo flt_ops>>_PASSED.TMP
if not exist _TESTOK echo flt_ops>>_FAILED.TMP

call runone.bat fstring %MODE%
if exist _TESTOK echo fstring>>_PASSED.TMP
if not exist _TESTOK echo fstring>>_FAILED.TMP

call runone.bat listcmp %MODE%
if exist _TESTOK echo listcmp>>_PASSED.TMP
if not exist _TESTOK echo listcmp>>_FAILED.TMP

call runone.bat mul_inh %MODE%
if exist _TESTOK echo mul_inh>>_PASSED.TMP
if not exist _TESTOK echo mul_inh>>_FAILED.TMP

call runone.bat set_ops %MODE%
if exist _TESTOK echo set_ops>>_PASSED.TMP
if not exist _TESTOK echo set_ops>>_FAILED.TMP

call runone.bat nst_fn2 %MODE%
if exist _TESTOK echo nst_fn2>>_PASSED.TMP
if not exist _TESTOK echo nst_fn2>>_FAILED.TMP

call runone.bat lambda %MODE%
if exist _TESTOK echo lambda>>_PASSED.TMP
if not exist _TESTOK echo lambda>>_FAILED.TMP

call runone.bat dct_srt %MODE%
if exist _TESTOK echo dct_srt>>_PASSED.TMP
if not exist _TESTOK echo dct_srt>>_FAILED.TMP

call runone.bat fn_obj %MODE%
if exist _TESTOK echo fn_obj>>_PASSED.TMP
if not exist _TESTOK echo fn_obj>>_FAILED.TMP

call runone.bat gen_bas %MODE%
if exist _TESTOK echo gen_bas>>_PASSED.TMP
if not exist _TESTOK echo gen_bas>>_FAILED.TMP

call runone.bat varargs %MODE%
if exist _TESTOK echo varargs>>_PASSED.TMP
if not exist _TESTOK echo varargs>>_FAILED.TMP

call runone.bat mod_bas %MODE%
if exist _TESTOK echo mod_bas>>_PASSED.TMP
if not exist _TESTOK echo mod_bas>>_FAILED.TMP

call runone.bat mod_ent %MODE% --entry main
if exist _TESTOK echo mod_ent>>_PASSED.TMP
if not exist _TESTOK echo mod_ent>>_FAILED.TMP

call runone.bat nst_dct %MODE%
if exist _TESTOK echo nst_dct>>_PASSED.TMP
if not exist _TESTOK echo nst_dct>>_FAILED.TMP

call runone.bat typ_col %MODE%
if exist _TESTOK echo typ_col>>_PASSED.TMP
if not exist _TESTOK echo typ_col>>_FAILED.TMP

call runone.bat blt_new %MODE%
if exist _TESTOK echo blt_new>>_PASSED.TMP
if not exist _TESTOK echo blt_new>>_FAILED.TMP

call runone.bat str_mtd %MODE%
if exist _TESTOK echo str_mtd>>_PASSED.TMP
if not exist _TESTOK echo str_mtd>>_FAILED.TMP

call runone.bat walrus %MODE%
if exist _TESTOK echo walrus>>_PASSED.TMP
if not exist _TESTOK echo walrus>>_FAILED.TMP

call runone.bat dct_cmp %MODE%
if exist _TESTOK echo dct_cmp>>_PASSED.TMP
if not exist _TESTOK echo dct_cmp>>_FAILED.TMP

call runone.bat set_cmp %MODE%
if exist _TESTOK echo set_cmp>>_PASSED.TMP
if not exist _TESTOK echo set_cmp>>_FAILED.TMP

REM call runone.bat genexpr %MODE%
REM if exist _TESTOK echo genexpr>>_PASSED.TMP
REM if not exist _TESTOK echo genexpr>>_FAILED.TMP

call runone.bat ge_flt %MODE%
if exist _TESTOK echo ge_flt>>_PASSED.TMP
if not exist _TESTOK echo ge_flt>>_FAILED.TMP

call runone.bat ge_man %MODE%
if exist _TESTOK echo ge_man>>_PASSED.TMP
if not exist _TESTOK echo ge_man>>_FAILED.TMP

call runone.bat ge_chk %MODE%
if exist _TESTOK echo ge_chk>>_PASSED.TMP
if not exist _TESTOK echo ge_chk>>_FAILED.TMP

call runone.bat ge_2nf %MODE%
if exist _TESTOK echo ge_2nf>>_PASSED.TMP
if not exist _TESTOK echo ge_2nf>>_FAILED.TMP

call runone.bat with_st %MODE%
if exist _TESTOK echo with_st>>_PASSED.TMP
if not exist _TESTOK echo with_st>>_FAILED.TMP

call runone.bat match %MODE%
if exist _TESTOK echo match>>_PASSED.TMP
if not exist _TESTOK echo match>>_FAILED.TMP

call runone.bat dn_arth %MODE%
if exist _TESTOK echo dn_arth>>_PASSED.TMP
if not exist _TESTOK echo dn_arth>>_FAILED.TMP

call runone.bat dn_cmp %MODE%
if exist _TESTOK echo dn_cmp>>_PASSED.TMP
if not exist _TESTOK echo dn_cmp>>_FAILED.TMP

call runone.bat dn_bool %MODE%
if exist _TESTOK echo dn_bool>>_PASSED.TMP
if not exist _TESTOK echo dn_bool>>_FAILED.TMP

call runone.bat dn_iter %MODE%
if exist _TESTOK echo dn_iter>>_PASSED.TMP
if not exist _TESTOK echo dn_iter>>_FAILED.TMP

call runone.bat dn_cont %MODE%
if exist _TESTOK echo dn_cont>>_PASSED.TMP
if not exist _TESTOK echo dn_cont>>_FAILED.TMP

call runone.bat dn_init %MODE%
if exist _TESTOK echo dn_init>>_PASSED.TMP
if not exist _TESTOK echo dn_init>>_FAILED.TMP

call runone.bat dn_iadd %MODE%
if exist _TESTOK echo dn_iadd>>_PASSED.TMP
if not exist _TESTOK echo dn_iadd>>_FAILED.TMP

call runone.bat dn_radd %MODE%
if exist _TESTOK echo dn_radd>>_PASSED.TMP
if not exist _TESTOK echo dn_radd>>_FAILED.TMP

call runone.bat dn_matm %MODE%
if exist _TESTOK echo dn_matm>>_PASSED.TMP
if not exist _TESTOK echo dn_matm>>_FAILED.TMP

call runone.bat dn_unar %MODE%
if exist _TESTOK echo dn_unar>>_PASSED.TMP
if not exist _TESTOK echo dn_unar>>_FAILED.TMP

call runone.bat dn_ctx %MODE%
if exist _TESTOK echo dn_ctx>>_PASSED.TMP
if not exist _TESTOK echo dn_ctx>>_FAILED.TMP

call runone.bat dn_hash %MODE%
if exist _TESTOK echo dn_hash>>_PASSED.TMP
if not exist _TESTOK echo dn_hash>>_FAILED.TMP

call runone.bat dn_call %MODE%
if exist _TESTOK echo dn_call>>_PASSED.TMP
if not exist _TESTOK echo dn_call>>_FAILED.TMP

call runone.bat dn_attr %MODE%
if exist _TESTOK echo dn_attr>>_PASSED.TMP
if not exist _TESTOK echo dn_attr>>_FAILED.TMP

call runone.bat del_var %MODE%
if exist _TESTOK echo del_var>>_PASSED.TMP
if not exist _TESTOK echo del_var>>_FAILED.TMP

call runone.bat del_idx %MODE%
if exist _TESTOK echo del_idx>>_PASSED.TMP
if not exist _TESTOK echo del_idx>>_FAILED.TMP

call runone.bat del_atr %MODE%
if exist _TESTOK echo del_atr>>_PASSED.TMP
if not exist _TESTOK echo del_atr>>_FAILED.TMP

call runone.bat nl_bas %MODE%
if exist _TESTOK echo nl_bas>>_PASSED.TMP
if not exist _TESTOK echo nl_bas>>_FAILED.TMP

call runone.bat nl_chn %MODE%
if exist _TESTOK echo nl_chn>>_PASSED.TMP
if not exist _TESTOK echo nl_chn>>_FAILED.TMP

call runone.bat cls_repr %MODE%
if exist _TESTOK echo cls_repr>>_PASSED.TMP
if not exist _TESTOK echo cls_repr>>_FAILED.TMP

call runone.bat typ_als %MODE%
if exist _TESTOK echo typ_als>>_PASSED.TMP
if not exist _TESTOK echo typ_als>>_FAILED.TMP

call runone.bat ge_snd %MODE%
if exist _TESTOK echo ge_snd>>_PASSED.TMP
if not exist _TESTOK echo ge_snd>>_FAILED.TMP

call runone.bat ge_thr %MODE%
if exist _TESTOK echo ge_thr>>_PASSED.TMP
if not exist _TESTOK echo ge_thr>>_FAILED.TMP

call runone.bat ge_cls %MODE%
if exist _TESTOK echo ge_cls>>_PASSED.TMP
if not exist _TESTOK echo ge_cls>>_FAILED.TMP

call runone.bat ge_yf %MODE%
if exist _TESTOK echo ge_yf>>_PASSED.TMP
if not exist _TESTOK echo ge_yf>>_FAILED.TMP

call runone.bat ge_nst %MODE%
if exist _TESTOK echo ge_nst>>_PASSED.TMP
if not exist _TESTOK echo ge_nst>>_FAILED.TMP

call runone.bat ge_inf %MODE%
if exist _TESTOK echo ge_inf>>_PASSED.TMP
if not exist _TESTOK echo ge_inf>>_FAILED.TMP

call runone.bat asyncaw %MODE%
if exist _TESTOK echo asyncaw>>_PASSED.TMP
if not exist _TESTOK echo asyncaw>>_FAILED.TMP

call runone.bat asc_tsk %MODE%
if exist _TESTOK echo asc_tsk>>_PASSED.TMP
if not exist _TESTOK echo asc_tsk>>_FAILED.TMP

call runone.bat posonly %MODE%
if exist _TESTOK echo posonly>>_PASSED.TMP
if not exist _TESTOK echo posonly>>_FAILED.TMP

call runone.bat strunp %MODE%
if exist _TESTOK echo strunp>>_PASSED.TMP
if not exist _TESTOK echo strunp>>_FAILED.TMP

call runone.bat mc_map %MODE%
if exist _TESTOK echo mc_map>>_PASSED.TMP
if not exist _TESTOK echo mc_map>>_FAILED.TMP

call runone.bat mc_seq %MODE%
if exist _TESTOK echo mc_seq>>_PASSED.TMP
if not exist _TESTOK echo mc_seq>>_FAILED.TMP

call runone.bat mc_cls %MODE%
if exist _TESTOK echo mc_cls>>_PASSED.TMP
if not exist _TESTOK echo mc_cls>>_FAILED.TMP

call runone.bat mc_grd %MODE%
if exist _TESTOK echo mc_grd>>_PASSED.TMP
if not exist _TESTOK echo mc_grd>>_FAILED.TMP

call runone.bat mc_or %MODE%
if exist _TESTOK echo mc_or>>_PASSED.TMP
if not exist _TESTOK echo mc_or>>_FAILED.TMP

call runone.bat mc_nst %MODE%
if exist _TESTOK echo mc_nst>>_PASSED.TMP
if not exist _TESTOK echo mc_nst>>_FAILED.TMP

call runone.bat excgrp %MODE%
if exist _TESTOK echo excgrp>>_PASSED.TMP
if not exist _TESTOK echo excgrp>>_FAILED.TMP

call runone.bat pair %MODE%
if exist _TESTOK echo pair>>_PASSED.TMP
if not exist _TESTOK echo pair>>_FAILED.TMP

call runone.bat exc_par %MODE%
if exist _TESTOK echo exc_par>>_PASSED.TMP
if not exist _TESTOK echo exc_par>>_FAILED.TMP

call runone.bat tp_fzs %MODE%
if exist _TESTOK echo tp_fzs>>_PASSED.TMP
if not exist _TESTOK echo tp_fzs>>_FAILED.TMP

call runone.bat tp_cpx %MODE%
if exist _TESTOK echo tp_cpx>>_PASSED.TMP
if not exist _TESTOK echo tp_cpx>>_FAILED.TMP

call runone.bat tp_bya %MODE%
if exist _TESTOK echo tp_bya>>_PASSED.TMP
if not exist _TESTOK echo tp_bya>>_FAILED.TMP

call runone.bat tsl_any %MODE%
if exist _TESTOK echo tsl_any>>_PASSED.TMP
if not exist _TESTOK echo tsl_any>>_FAILED.TMP

call runone.bat tsl_all %MODE%
if exist _TESTOK echo tsl_all>>_PASSED.TMP
if not exist _TESTOK echo tsl_all>>_FAILED.TMP

call runone.bat tsl_sum %MODE%
if exist _TESTOK echo tsl_sum>>_PASSED.TMP
if not exist _TESTOK echo tsl_sum>>_FAILED.TMP

call runone.bat tsl_mm %MODE%
if exist _TESTOK echo tsl_mm>>_PASSED.TMP
if not exist _TESTOK echo tsl_mm>>_FAILED.TMP

call runone.bat tsl_enum %MODE%
if exist _TESTOK echo tsl_enum>>_PASSED.TMP
if not exist _TESTOK echo tsl_enum>>_FAILED.TMP

call runone.bat tsl_zip %MODE%
if exist _TESTOK echo tsl_zip>>_PASSED.TMP
if not exist _TESTOK echo tsl_zip>>_FAILED.TMP

call runone.bat tsl_map %MODE%
if exist _TESTOK echo tsl_map>>_PASSED.TMP
if not exist _TESTOK echo tsl_map>>_FAILED.TMP

call runone.bat tsl_fil %MODE%
if exist _TESTOK echo tsl_fil>>_PASSED.TMP
if not exist _TESTOK echo tsl_fil>>_FAILED.TMP

call runone.bat tsl_rev %MODE%
if exist _TESTOK echo tsl_rev>>_PASSED.TMP
if not exist _TESTOK echo tsl_rev>>_FAILED.TMP

call runone.bat tsl_lm %MODE%
if exist _TESTOK echo tsl_lm>>_PASSED.TMP
if not exist _TESTOK echo tsl_lm>>_FAILED.TMP

call runone.bat tsl_tm %MODE%
if exist _TESTOK echo tsl_tm>>_PASSED.TMP
if not exist _TESTOK echo tsl_tm>>_FAILED.TMP

call runone.bat tsl_dm %MODE%
if exist _TESTOK echo tsl_dm>>_PASSED.TMP
if not exist _TESTOK echo tsl_dm>>_FAILED.TMP

call runone.bat tsl_sm %MODE%
if exist _TESTOK echo tsl_sm>>_PASSED.TMP
if not exist _TESTOK echo tsl_sm>>_FAILED.TMP

call runone.bat tsl_srt %MODE%
if exist _TESTOK echo tsl_srt>>_PASSED.TMP
if not exist _TESTOK echo tsl_srt>>_FAILED.TMP

call runone.bat is_typ %MODE%
if exist _TESTOK echo is_typ>>_PASSED.TMP
if not exist _TESTOK echo is_typ>>_FAILED.TMP

call runone.bat dct_kvi %MODE%
if exist _TESTOK echo dct_kvi>>_PASSED.TMP
if not exist _TESTOK echo dct_kvi>>_FAILED.TMP

call runone.bat dct_pop %MODE%
if exist _TESTOK echo dct_pop>>_PASSED.TMP
if not exist _TESTOK echo dct_pop>>_FAILED.TMP

call runone.bat lst_idx %MODE%
if exist _TESTOK echo lst_idx>>_PASSED.TMP
if not exist _TESTOK echo lst_idx>>_FAILED.TMP

call runone.bat lst_popi %MODE%
if exist _TESTOK echo lst_popi>>_PASSED.TMP
if not exist _TESTOK echo lst_popi>>_FAILED.TMP

call runone.bat set_mut %MODE%
if exist _TESTOK echo set_mut>>_PASSED.TMP
if not exist _TESTOK echo set_mut>>_FAILED.TMP

call runone.bat lst_rmcp %MODE%
if exist _TESTOK echo lst_rmcp>>_PASSED.TMP
if not exist _TESTOK echo lst_rmcp>>_FAILED.TMP

REM call runone.bat linq %MODE%
REM if exist _TESTOK echo linq>>_PASSED.TMP
REM if not exist _TESTOK echo linq>>_FAILED.TMP

echo.
echo === Passed ===
if exist _PASSED.TMP type _PASSED.TMP
if not exist _PASSED.TMP echo (none)
echo.
echo === Failed ===
if exist _FAILED.TMP type _FAILED.TMP
if not exist _FAILED.TMP echo (none)
echo.
if exist _PASSED.TMP del _PASSED.TMP
if exist _FAILED.TMP del _FAILED.TMP
if exist _TESTOK del _TESTOK
