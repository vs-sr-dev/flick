import pycdlib, os, sys
# argv[1]: the extracted X-Men disc (XM_V1_0_USE), argv[2]: the ISO to write
src = sys.argv[1]
out = sys.argv[2]
p = pycdlib.PyCdlib()
p.new(interchange_level=3, joliet=3, vol_ident="XM_V1_0_USE")
# data first, HYPER.EXE last (IWL's layout puts it at the end)
p.add_directory("/XMEN_DATA", joliet_path="/XMEN_DATA")
for fn in sorted(os.listdir(os.path.join(src, "XMEN_DATA"))):
    p.add_file(os.path.join(src, "XMEN_DATA", fn), iso_path=f"/XMEN_DATA/{fn.upper()};1", joliet_path=f"/XMEN_DATA/{fn}")
p.add_file(os.path.join(src, "HYPER.EXE"), iso_path="/HYPER.EXE;1", joliet_path="/HYPER.EXE")
p.write(out + "/xm.iso"); p.close()
open(out + "/xm.cue", "w").write('FILE "xm.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
print("ok", os.path.getsize(out + "/xm.iso"))
