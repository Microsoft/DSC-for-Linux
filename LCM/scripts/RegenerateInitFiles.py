#!/usr/bin/python2
import glob
import os.path

omi_libdir = "<DSC_SCRIPT_PATH>"

script_dirs=[omi_libdir + "/2.4x-2.5x/Scripts",
             omi_libdir + "/2.6x-2.7x/Scripts",
             omi_libdir + "/3.x/Scripts"]


for current_dir in script_dirs:
    out_init = "__all__="
    py_files = glob.glob(current_dir + "/*.py")
    py_files_basename = []
    for current_file in py_files:
        current_basename = os.path.basename(current_file)

        if len(current_basename) < 3:
            print("Found odd file name at: " + current_file + ", ignoring it.")
            continue
        if current_basename == "__init__.py":
            continue

        py_files_basename.append(current_basename[:-3])

    out_init = out_init + str(py_files_basename)
    open(current_dir + "/__init__.py", "w").write(out_init)