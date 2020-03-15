import scripts.utils as UT
import shutil as SH
import os as OS

# Global Triton information
TRITON_Z3, TRITON_Z3_DIR, TRITON_Z3_URL='','',''
# TRITON_COMMIT='714d63aede9aa6beb24a16479ef76c0b4105ac4a' <- OK
TRITON_COMMIT='fb3241e94a3e1d0be9831bfc7a865246ee4c9e30' # <- commit e6122cc5fadef602fdda9ec99c4581a439c796b2 must be reverted
TRITON_BOOST_DIR='boost_1_72_0'
TRITON_CAPSTONE_DIR='capstone-4.0.1-win64'
TRITON_BOOST='%s.7z' % TRITON_BOOST_DIR
TRITON_CAPSTONE='%s.zip' % TRITON_CAPSTONE_DIR
TRITON_BOOST_URL='https://dl.bintray.com/boostorg/release/1.72.0/source/%s' % TRITON_BOOST
TRITON_CAPSTONE_URL='https://github.com/aquynh/capstone/releases/download/4.0.1/%s' % TRITON_CAPSTONE

def select_Z3(osn):
  global TRITON_Z3, TRITON_Z3_DIR, TRITON_Z3_URL
  if osn.startswith('Windows'):
    TRITON_Z3_DIR='z3-4.8.7-x64-win'
  elif osn.startswith('Linux'):
    TRITON_Z3_DIR='z3-4.8.7-x64-ubuntu-16.04'
  elif osn.startswith('Darwin'):
    TRITON_Z3_DIR='z3-4.8.7-x64-osx-10.14.6'
  # Initialize directory name and URL
  TRITON_Z3='%s.zip' % TRITON_Z3_DIR
  TRITON_Z3_URL='https://github.com/Z3Prover/z3/releases/download/z3-4.8.7/%s' % TRITON_Z3

def execute(mode="Release"):
  # Detect the current path
  curpath = OS.getcwd()
  UT.info('Building Triton (curpath = %s)' % curpath)
  # Detect the operating system name
  osn = UT.os_name()
  # Determine the cores number
  ncpu = UT.cpu_count()
  # Determine the cmake generator
  generator = UT.cmake_generator()
  # Detect the proper Z3 version
  select_Z3(osn)
  # Prepare the 'CACHE' and 'deps' folders
  if not OS.path.isdir('deps'):
    OS.mkdir('deps')
  if not OS.path.isdir('CACHE'):
    OS.mkdir('CACHE')
  # Create directory and configure
  if not OS.path.isdir('build_triton'):
    OS.mkdir('build_triton')
  OS.chdir('build_triton')
  # Clone the Triton repository
  if not OS.path.isdir('Triton'):
    UT.execute('git clone https://github.com/JonathanSalwan/Triton Triton')
  OS.chdir('Triton')
  # Always checkout the master branch first
  UT.execute('git checkout master')
  # Pull the latest changes
  UT.execute('git pull')
  # But stick to a specific commit for now
  UT.execute('git fetch origin %s' % TRITON_COMMIT)
  UT.execute('git checkout %s' % TRITON_COMMIT)
  # Apply our own patches
  UT.execute('git am --signoff < %s/patches/0001-Fixed-reserved-flag.patch' % curpath, False)
  # Create a 'build' directory
  if not OS.path.isdir('build'):
    OS.mkdir('build')
  OS.chdir('build')
  # Create the 'triton' and 'includes' folders
  if not OS.path.isdir('%s/deps/triton' % curpath):
    OS.mkdir('%s/deps/triton' % curpath)
  # Find and copy the new 'includes' folder
  UT.find_and_copy('includes', '../src', '%s/deps/triton/includes' % curpath)
  # Download and unpack the precompiled Z3 and Boost
  if not OS.path.isfile('%s/CACHE/%s' % (curpath, TRITON_Z3)):
    UT.info('Downloading prebuilt Z3: %s' % TRITON_Z3)
    UT.execute('curl -L %s -o %s/CACHE/%s' % (TRITON_Z3_URL, curpath, TRITON_Z3))
  if not OS.path.isdir('%s/deps/z3' % curpath):
    UT.execute('7z x -y -o%s/deps %s/CACHE/%s' % (curpath, curpath, TRITON_Z3))
    OS.rename('%s/deps/%s' % (curpath, TRITON_Z3_DIR), '%s/deps/z3' % curpath)
  if not OS.path.isfile('%s/CACHE/%s' % (curpath, TRITON_BOOST)):
    UT.info('Downloading prebuilt Boost: %s' % TRITON_BOOST)
    UT.execute('curl -L %s -o %s/CACHE/%s' % (TRITON_BOOST_URL, curpath, TRITON_BOOST))
  if not OS.path.isdir('%s/deps/%s' % (curpath, TRITON_BOOST_DIR)):
    UT.execute('7z x -y -o%s/deps %s/CACHE/%s' % (curpath, curpath, TRITON_BOOST))
  # Compile for the proper OS
  if osn == 'Windows':
    # Download the dependencies
    if not OS.path.isfile('%s/CACHE/%s' % (curpath, TRITON_CAPSTONE)):
      UT.info('Downloading prebuilt Capstone: %s' % TRITON_CAPSTONE)
      UT.execute('curl -L %s -o %s/CACHE/%s' % (TRITON_CAPSTONE_URL, curpath, TRITON_CAPSTONE))
    # Unpack the dependencies
    if not OS.path.isdir('%s/deps/%s' % (curpath, TRITON_CAPSTONE_DIR)):
      UT.execute('7z x -y -o%s/deps %s/CACHE/%s' % (curpath, curpath, TRITON_CAPSTONE))
    # Detect the python includes directory and library
    PYTHON_DIR = UT.python_path()
    # Generate the cmake configuration command
    cmd = ('cmake -G "%s" ' +
      '-DKERNEL4=OFF ' +
      '-DPYTHON_BINDINGS=OFF ' +
      '-DBOOST_ROOT=%s\\deps\\%s ' +
      '-DPYTHON_INCLUDE_DIRS=%s\\include ' +
      '-DPYTHON_LIBRARIES=%s\\libs\\python27.lib ' +
      '-DZ3_INCLUDE_DIRS=%s\\deps\\z3\\include ' +
      '-DZ3_LIBRARIES=%s\\deps\\z3\\bin\\libz3.lib ' +
      '-DCAPSTONE_INCLUDE_DIRS=%s\\deps\\%s\\include ' +
      '-DCAPSTONE_LIBRARIES=%s\\deps\\%s\\capstone.lib ' +
      '-DCMAKE_CXX_STANDARD_LIBRARIES="legacy_stdio_definitions.lib msvcrt.lib msvcmrt.lib" ' +
      '..') % (generator, curpath, TRITON_BOOST_DIR, PYTHON_DIR, PYTHON_DIR, curpath,
      curpath, curpath, TRITON_CAPSTONE_DIR, curpath,
      TRITON_CAPSTONE_DIR)
    print("CMake command: %s" % cmd)
    UT.execute(cmd)
    # Compile Triton
    UT.execute('cmake --build . --config Release -- /maxcpucount:%d' % ncpu)
    # Find and copy the 'triton.lib' file
    if not UT.find_and_copy('triton.lib', '.', '%s/deps/triton/triton.lib' % curpath):
      UT.fail('Failed to find and copy: triton.lib')
  else:
    # Generate the cmake configuration (Linux/Darwin)
    cmd = ('cmake -G "%s" ' +
      '-DCMAKE_BUILD_TYPE=%s '
      '-DCMAKE_CXX_FLAGS="-march=native" ' +
      '-DBOOST_ROOT=%s/deps/%s ' +
      '-DZ3_INCLUDE_DIRS=%s/deps/z3/include ' +
      '-DZ3_LIBRARIES=%s/deps/z3/bin/libz3.a ' +
      '-DCAPSTONE_INCLUDE_DIRS=%s/deps/tob_libraries/capstone/include ' +
      '-DCAPSTONE_LIBRARIES=%s/deps/tob_libraries/capstone/libcapstone_static.a ' +
      '-DSTATICLIB=ON ' +
      '-DKERNEL4=OFF ' +
      '-DPYTHON_BINDINGS=OFF ' +
      '..') % (generator, mode, curpath, TRITON_BOOST_DIR, curpath, curpath, curpath, curpath)
    # Execute cmake
    UT.execute(cmd)
    # Compile and install Triton
    UT.execute('make -j%d' % ncpu, False)
    # Find and copy the 'libtriton.a' file
    if not UT.find_and_copy('libtriton.a', '.', '%s/deps/triton/libtriton.a' % curpath):
      UT.fail('Failed to find and copy: libtriton.a')
  # Copy the 'FindTriton.cmake' file
  SH.copyfile('%s/scripts/FindTriton.cmake' % curpath, '%s/deps/tob_libraries/cmake_modules/FindTRITON.cmake' % curpath)
  # Copy the 'FindBoost.cmake' file
  SH.copyfile('%s/scripts/FindBoost.cmake' % curpath, '%s/deps/tob_libraries/cmake_modules/FindBOOST.cmake' % curpath)
  # Copy the 'FindZ3.cmake' file
  SH.copyfile('%s/scripts/FindZ3.cmake' % curpath, '%s/deps/tob_libraries/cmake_modules/FindZ3.cmake' % curpath)
  # Get back to the main directory
  OS.chdir(curpath)
  # Notify we successfully compiled Triton
  UT.succ('Triton has been successfully compiled!')
