import scripts.utils as UT
import shutil as SH
import os as OS

# Global LLVM information
LLVM_VER='9.0.1'
LLVM_URL='https://github.com/llvm/llvm-project/releases/download/llvmorg-%s' % LLVM_VER
LLVM_CFE='clang-%s.src' % LLVM_VER
LLVM_SRC='llvm-%s.src' % LLVM_VER
LLVM_CFE_TAR='%s.tar' % LLVM_CFE
LLVM_SRC_TAR='%s.tar' % LLVM_SRC
LLVM_CFE_TAR_XZ='%s.xz' % LLVM_CFE_TAR
LLVM_SRC_TAR_XZ='%s.xz' % LLVM_SRC_TAR
LLVM_CFE_URL='%s/%s' % (LLVM_URL, LLVM_CFE_TAR_XZ)
LLVM_SRC_URL='%s/%s' % (LLVM_URL, LLVM_SRC_TAR_XZ)

def execute(mode="Release"):
	# Detect the current path
	curpath = OS.getcwd()
	UT.info('Building LLVM (curpath = %s)' % curpath)
	# Detect the OS
	osn = UT.os_name()
	# Clean and create the 'CACHE' and 'deps' folders
	if not OS.path.isdir('CACHE'):
		OS.mkdir('CACHE')
	if not OS.path.isdir('deps'):
		OS.mkdir('deps')
	# Create directory and configure
	if not OS.path.isdir('build_llvm'):
		OS.mkdir('build_llvm')
	OS.chdir('build_llvm')
	# Download LLVM's sources
	if not OS.path.isfile('%s/CACHE/%s' % (curpath, LLVM_SRC_TAR_XZ)):
		print('LLVM_SRC_URL: %s' % LLVM_SRC_URL)
		UT.execute('curl -L -o %s/CACHE/%s %s' % (curpath, LLVM_SRC_TAR_XZ, LLVM_SRC_URL))
	if not OS.path.isfile('%s/CACHE/%s' % (curpath, LLVM_CFE_TAR_XZ)):
		print('LLVM_CFE_URL: %s' % LLVM_CFE_URL)
		UT.execute('curl -L -o %s/CACHE/%s %s' % (curpath, LLVM_CFE_TAR_XZ, LLVM_CFE_URL))
	# Unpack the downloaded 'xz' archives
	UT.execute('7z x -y %s/CACHE/%s -o%s/CACHE' % (curpath, LLVM_SRC_TAR_XZ, curpath))
	UT.execute('7z x -y %s/CACHE/%s -o%s/CACHE' % (curpath, LLVM_CFE_TAR_XZ, curpath))
	# Unpack the extracted 'tar' archives
	UT.execute('7z x -y %s/CACHE/%s' % (curpath, LLVM_SRC_TAR), False)
	UT.execute('7z x -y %s/CACHE/%s' % (curpath, LLVM_CFE_TAR), False)
	# Delete the 'tar' archives
	OS.remove('%s/CACHE/%s' % (curpath, LLVM_SRC_TAR))
	OS.remove('%s/CACHE/%s' % (curpath, LLVM_CFE_TAR))
	# Move the clang sources into the proper LLVM subdirectory
	if OS.path.isdir('%s/build_llvm/%s/tools/clang/%s' % (curpath, LLVM_SRC, LLVM_CFE)):
		UT.rmdir('%s/build_llvm/%s/tools/clang/%s' % (curpath, LLVM_SRC, LLVM_CFE))
	SH.move('%s/build_llvm/%s' % (curpath, LLVM_CFE), '%s/build_llvm/%s/tools/clang' % (curpath, LLVM_SRC))
	# Prepare the 'ninja' makefile with 'cmake'
	cmd = ('cmake -G Ninja ' +
		'-DCMAKE_CXX_FLAGS="-march=native" ' +
		'-DCMAKE_BUILD_TYPE=%s ' +
		'-DCMAKE_INSTALL_PREFIX=%s/deps/tob_libraries/llvm ' +
		'-DLLVM_TARGETS_TO_BUILD="X86;AArch64" ' +
		'-DLLVM_BUILD_TESTS=False ' +
		'-DLLVM_INCLUDE_TESTS=False ' +
		'-DLLVM_INCLUDE_BENCHMARKS=False ' +
		'-DLLVM_ENABLE_ASSERTIONS=True ' +
		'-DLLVM_BUILD_DOCS=False ' +
		'-DLLVM_ENABLE_DOXYGEN=False ' +
		'-DLLVM_ENABLE_DUMP=True ' +
		'-DLLVM_ENABLE_RTTI=True ' +
		LLVM_SRC) % (mode, curpath)
	# Generate the 'ninja' makefiles
	UT.execute(cmd)
	# Compile LLVM with 'ninja'
	UT.execute('ninja')
	# Install LLVM with 'ninja'
	UT.execute('ninja install')
	# Get back to the main directory
	OS.chdir(curpath)
	# Notify we successfully compiled LLVM
	UT.succ('LLVM has been successfully compiled!')
