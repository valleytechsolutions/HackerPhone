"""Build portable services with a native C++ compiler; no serial device access."""
import argparse,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cxx');args=p.parse_args()
out=root/'.work/tests';out.mkdir(parents=True,exist_ok=True)
cxx=[args.cxx] if args.cxx else [sys.executable,'-m','ziglang','c++']
cc=[args.cxx.replace('g++','gcc').replace('clang++','clang')] if args.cxx else [sys.executable,'-m','ziglang','cc']
inc=[f'-I{root / d}' for d in ('firmware/include','third_party/dragon','third_party/opendroneid/src')]
obj=out/'opendroneid.o'
subprocess.run(cc+['-std=c11','-O1','-DODID_DISABLE_PRINTF']+inc+['-c',str(root/'third_party/opendroneid/src/opendroneid.c'),'-o',str(obj)],check=True)
exe=out/('core_test.exe' if sys.platform=='win32' else 'core_test')
sources=[str(root/'firmware/src'/s) for s in ('phone_core.cpp','recon_core.cpp','capture_format.cpp','vendor_db.cpp','lab_format.cpp','attack_frames.cpp','ir_format.cpp')]
subprocess.run(cxx+['-std=c++17','-O1','-Wall','-Wextra','-UNDEBUG']+inc+sources+[str(root/'tests/core_test.cpp'),str(obj),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
