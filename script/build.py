#! /usr/bin/env python3
import argparse, build_utils, common, glob, os, platform, subprocess, sys

def _configure_and_build(module_dir):
  os.chdir(common.basedir + "/" + module_dir)
  subprocess.check_call(["cmake",
    "-DCMAKE_BUILD_TYPE=Release",
    "-B", "build",
    "-G", "Ninja",
    "-DJWM_ARCH=" + build_utils.arch,
    *(["-DCMAKE_OSX_ARCHITECTURES=" + {"x64": "x86_64", "arm64": "arm64"}[build_utils.arch]] if build_utils.system == "macos" else [])])
  build_utils.ninja("build")

def build_native():
  if build_utils.system == "linux":
    _configure_and_build("linux-x11")
    _configure_and_build("linux-wayland")
  else:
    _configure_and_build(build_utils.platform_dir)
  
  os.chdir(common.basedir + "/" + build_utils.platform_dir)

  if os.path.exists('build/libjwm_x64.dylib'):
    build_utils.copy_newer('build/libjwm_x64.dylib', '../target/classes/libjwm_x64.dylib')
  
  if os.path.exists('build/libjwm_arm64.dylib'):
    build_utils.copy_newer('build/libjwm_arm64.dylib', '../target/classes/libjwm_arm64.dylib')
  
  if os.path.exists('build/libjwm_x11_x64.so'):
    build_utils.copy_newer('build/libjwm_x11_x64.so', '../target/classes/libjwm_x11_x64.so')

  if os.path.exists('../linux-wayland/build/libjwm_wayland_x64.so'):
    build_utils.copy_newer('../linux-wayland/build/libjwm_wayland_x64.so', '../target/classes/libjwm_wayland_x64.so')
  
  if os.path.exists('build/jwm_x64.dll'):
    build_utils.copy_newer('build/jwm_x64.dll', '../target/classes/jwm_x64.dll')

  return 0

def build_java():
  os.chdir(common.basedir)
  sources = build_utils.files("linux-wayland/java/**/*.java", "linux-x11/java/**/*.java", "macos/java/**/*.java", "shared/java/**/*.java",  "windows/java/**/*.java",)
  build_utils.javac(sources, "target/classes", classpath=common.deps_compile())
  return 0

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--only', choices = ['native', 'java'])
  (args, _) = parser.parse_known_args()
  res = 0
  if args.only is None or 'native' == args.only:
    res += build_native()
  if args.only is None or 'java' == args.only:
    res += build_java()
  return res

if __name__ == '__main__':
  sys.exit(main())
