# libfxdsp: o motor DSP original do FxSound (dsp/) compilado sem alterações,
# sobre a camada de compatibilidade em linux/compat.

python = find_program('python3')
shim_dir = meson.current_build_dir() / 'include-shims'
run_command(python, '-I', files('../gen_include_shims.py'),
  meson.project_source_root(), shim_dir, check : true)

compat_dir = meson.current_source_dir() / '../compat'

dsp_args = [
  '-w',                          # código legado: warnings não acionáveis aqui
  '-fno-strict-aliasing', '-fwrapv',
  '-DWIN32', '-DPT_NON_MFC', '-DDSPSOFT_TARGET', '-DPT_DSP_BUILD=PT_DSP_DFX',
  '-I' + compat_dir, '-I' + shim_dir,
  '-include', compat_dir / 'fx_compat.h',
]

dsp_inc = include_directories(
  '../../dsp/ptutil/include', '../../dsp/include', '../../audiopassthru/include')

dsp_src = files(
      '../../dsp/DfxDsp.cpp',
      '../../dsp/DfxDspEq.cpp',
      '../../dsp/DfxDspPreset.cpp',
      '../../dsp/DfxDspPrivate.cpp',
      '../../dsp/DfxDspRegistry.cpp',
      '../../dsp/ptComSftDfx/Comsftwr.c',
      '../../dsp/ptComSftDfx/ComsftwrCPP.cpp',
      '../../dsp/ptechDsp/Aural/Aural032/Auralp32.c',
      '../../dsp/ptechDsp/Aural/Aural0/Auralp.c',
      '../../dsp/ptechDsp/Dly32/Dly832/dly8p32.c',
      '../../dsp/ptechDsp/Dly8/Dly8p.c',
      '../../dsp/ptechDsp/Lex/Lex16/Lex16.c',
      '../../dsp/ptechDsp/Lex/Lex32/Lex32.c',
      '../../dsp/ptechDsp/Maximizer/Maxi16/Maxi16.c',
      '../../dsp/ptechDsp/Maximizer/Maxi32/Maxi32.c',
      '../../dsp/ptechDsp/Peq/Peq832/peq8p32.c',
      '../../dsp/ptechDsp/Peq/Peq8/peq8p.c',
      '../../dsp/ptechDsp/Play/Play16/Play16.c',
      '../../dsp/ptechDsp/Play/Play32/Play32.c',
      '../../dsp/ptechDsp/wide/Wide16/Wide16.c',
      '../../dsp/ptechDsp/wide/Wide32/Wide32.c',
      '../../dsp/ptutil/COM/Com.cpp',
      '../../dsp/ptutil/COM/Comeprom.cpp',
      '../../dsp/ptutil/COM/Comget.cpp',
      '../../dsp/ptutil/COM/ComMem.cpp',
      '../../dsp/ptutil/COM/Compass.cpp',
      '../../dsp/ptutil/COM/Comread.cpp',
      '../../dsp/ptutil/COM/Comwave.cpp',
      '../../dsp/ptutil/COM/Comwrite.cpp',
      '../../dsp/ptutil/dfxp/dfxpComm.cpp',
      '../../dsp/ptutil/dfxp/dfxpEq.cpp',
      '../../dsp/ptutil/dfxp/dfxpGet.cpp',
      '../../dsp/ptutil/dfxp/dfxpInit.cpp',
      '../../dsp/ptutil/dfxp/dfxpProcess.cpp',
      '../../dsp/ptutil/dfxp/dfxpProcessClear.cpp',
      '../../dsp/ptutil/dfxp/dfxpProcessInt.cpp',
      '../../dsp/ptutil/dfxp/dfxpProcessReal.cpp',
      '../../dsp/ptutil/dfxp/dfxpQnt.cpp',
      '../../dsp/ptutil/dfxp/dfxpQuit.cpp',
      '../../dsp/ptutil/dfxp/dfxpRegistryStandard.cpp',
      '../../dsp/ptutil/dfxp/dfxpSession.cpp',
      '../../dsp/ptutil/dfxp/dfxpSet.cpp',
      '../../dsp/ptutil/dfxp/dfxpSpectrum.cpp',
      '../../dsp/ptutil/dfxp/dfxpUniversal.cpp',
      '../../dsp/ptutil/dfxSharedUtil/dfxSharedUtil.cpp',
      '../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynGet.cpp',
      '../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynInit.cpp',
      '../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynProcess.cpp',
      '../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynSet.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqGet.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInit.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInitBands.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInitSections.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqProcess.cpp',
      '../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqSet.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumGet.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumInit.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumMessageValues.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumProcess.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumReset.cpp',
      '../../dsp/ptutil/DspUtil/spectrum/spectrumSet.cpp',
      '../../dsp/ptutil/DspUtil/SurroundSyn/SurroundSynInit.cpp',
      '../../dsp/ptutil/DspUtil/SurroundSyn/SurroundSynProcess.cpp',
      '../../dsp/ptutil/Filt/Fil12But.cpp',
      '../../dsp/ptutil/Filt/FiltbiqdSos.cpp',
      '../../dsp/ptutil/Filt/FiltCalcBiqd.cpp',
      '../../dsp/ptutil/Filt/FiltCalcFilterResponse.cpp',
      '../../dsp/ptutil/Filt/Filtpoly.cpp',
      '../../dsp/ptutil/Filt/FiltRun.cpp',
      '../../dsp/ptutil/PRELST/Prelst.cpp',
      '../../dsp/ptutil/PWAV/PwavConvert.cpp',
      '../../dsp/ptutil/PWAV/PwavUtil.cpp',
      '../../dsp/ptutil/Qnt/Qnt.cpp',
      '../../dsp/ptutil/Qnt/Qnt2But.cpp',
      '../../dsp/ptutil/Qnt/QntitoBoostCut.cpp',
      '../../dsp/ptutil/Qnt/Qntitol.cpp',
      '../../dsp/ptutil/Qnt/Qntitor.cpp',
      '../../dsp/ptutil/Qnt/Qntitor2.cpp',
      '../../dsp/ptutil/Qnt/Qntrtoi.cpp',
      '../../dsp/ptutil/Qnt/Qntrtol.cpp',
      '../../dsp/ptutil/Qnt/Qntrtor.cpp',
      '../../dsp/ptutil/realSample/realSampleForceLegalValues.cpp',
      '../../dsp/ptutil/SOS/Sos.cpp',
      '../../dsp/ptutil/SOS/SosGet.cpp',
      '../../dsp/ptutil/SOS/SosProcess.cpp',
      '../../dsp/ptutil/SOS/SosSet.cpp',
      '../../dsp/ptutil/VALS/Vals.cpp',
      '../../dsp/ptutil/VALS/Valscfg.cpp',
      '../../dsp/ptutil/VALS/Valsfile.cpp',
      '../../dsp/ptutil/VALS/Valsget.cpp',
      '../../dsp/ptutil/VALS/Valsset.cpp',
      '../../dsp/ptutil/VALS/Valswarp.cpp',
  # utilitários do audiopassthru que são portáveis
  '../../audiopassthru/src/MTH/MthUtil.cpp',
  '../../audiopassthru/src/MTH/Mthfreq.cpp',
  '../../audiopassthru/src/MTH/MthBuffer.cpp',
  '../../audiopassthru/src/SLOUT/Slout.cpp',
  '../compat/fx_compat.cpp',
)

libfxdsp = static_library('fxdsp', dsp_src,
  include_directories : dsp_inc,
  c_args : dsp_args, cpp_args : dsp_args,
  dependencies : [dependency('threads')],
  pic : true)

fxdsp_dep = declare_dependency(
  link_with : libfxdsp,
  include_directories : include_directories('../../dsp/include'))

fxdsp_test = executable('fxdsp-selftest', 'selftest.cpp',
  dependencies : fxdsp_dep, install : false)
test('dsp-selftest', fxdsp_test,
  args : [meson.project_source_root() / 'bin/x64/Factsoft'])

executable('fxdsp-render', 'render.cpp', dependencies : fxdsp_dep, install : false)
