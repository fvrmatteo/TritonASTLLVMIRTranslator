import scripts.utils as UT
import shutil as SH
import os as OS

# Global Capstone information
CAPSTONE_URL='https://github.com/aquynh/capstone'

def execute(branch='master', mode="Release"):
	# Detect the current path
	curpath = OS.getcwd()
	UT.info('Building Capstone (curpath = %s)' % curpath)
	# Detect the operating system name
	osn = UT.os_name()
	# Determine the cpu core count
	ncpu = UT.cpu_count()
	# Determine cmake's 'generator' parameter
	generator = UT.cmake_generator()
	# Create directory and configure
	if not OS.path.isdir('build_capstone'):
		OS.mkdir('build_capstone')
	OS.chdir('build_capstone')
	# Clone the Capstone repository
	if not OS.path.isdir('capstone'):
		UT.execute('git clone %s capstone' % CAPSTONE_URL)
	OS.chdir('capstone')
	# Checkout the proper branch
	UT.execute('git checkout %s' % branch)
	# Pull the latest changes
	UT.execute('git pull')
	# Create the 'tob_libraries' capstone directory
	if not OS.path.isdir('%s/deps/tob_libraries/capstone' % curpath):
		OS.mkdir('%s/deps/tob_libraries/capstone' % curpath)
	else:
		UT.rmdir('%s/deps/tob_libraries/capstone' % curpath)
		OS.mkdir('%s/deps/tob_libraries/capstone' % curpath)
	# Copy the 'include' folder
	if branch == 'v3':
		UT.find_and_copy('include', '.', '%s/deps/tob_libraries/capstone/include/capstone' % curpath)
	else:
		UT.find_and_copy('include', '.', '%s/deps/tob_libraries/capstone/include' % curpath)
	# Create the build directory
	if not OS.path.isdir('build'):
		OS.mkdir('build')
	OS.chdir('build')
	# Generate the cmake configuration command
	cmd = ('cmake -G "%s" ' +
		'-DCMAKE_CXX_FLAGS="-march=native" ' +
		'-DCMAKE_BUILD_TYPE=%s ' +
		'-DCAPSTONE_BUILD_CSTOOL=no ' +
		'-DCAPSTONE_BUILD_TESTS=no ' +
		'-DCAPSTONE_TMS320C64X_SUPPORT=0 ' +
		'-DCAPSTONE_MOS65XX_SUPPORT=0 ' +
		'-DCAPSTONE_SPARC_SUPPORT=0 ' +
		'-DCAPSTONE_ARM64_SUPPORT=0 ' +
		'-DCAPSTONE_M680X_SUPPORT=0 ' +
		'-DCAPSTONE_XCORE_SUPPORT=0 ' +
		'-DCAPSTONE_M68K_SUPPORT=0 ' +
		'-DCAPSTONE_MIPS_SUPPORT=0 ' +
		'-DCAPSTONE_SYSZ_SUPPORT=0 ' +
		'-DCAPSTONE_PPC_SUPPORT=0 ' +
		'-DCAPSTONE_ARM_SUPPORT=0 ' +
		'-DCAPSTONE_EVM_SUPPORT=0 ' +
		'-DCAPSTONE_X86_M680X=0 ' +
		'..') % (generator, mode)
	# Execute cmake
	UT.execute(cmd)
	# Compile for the proper OS
	if osn.startswith('Windows'):
		# Compile Capstone (x86 only)
		UT.execute('cmake --build . --config Release -- /maxcpucount:%d' % ncpu)
		# Copy the 'capstone.dll' and 'capstone.lib' files
		if not UT.find_and_copy('capstone.dll', '.', '%s/deps/tob_libraries/capstone/capstone.dll' % curpath):
			UT.fail('Failed to find and copy: capstone.dll')
		if not UT.find_and_copy('capstone.lib', '.', '%s/deps/tob_libraries/capstone/capstone_static.lib' % curpath):
			UT.fail('Failed to find and copy: capstone.lib')
	elif osn.startswith('Linux'):
		# Compile Capstone (x86 only)
		UT.execute('make -j%d' % ncpu)
		# Copy the 'libcapstone.so' and 'libcapstone.a' files
		if not UT.find_and_copy('libcapstone.so', '.', '%s/deps/tob_libraries/capstone/libcapstone.so' % curpath):
			UT.fail('Failed to find and copy: libcapstone.so')
		if not UT.find_and_copy('libcapstone.a', '.', '%s/deps/tob_libraries/capstone/libcapstone_static.a' % curpath):
			UT.fail('Failed to find and copy: libcapstone.a')
	elif osn.startswith('Darwin'):
		# Compile Capstone (x86 only)
		UT.execute('make -j%d' % ncpu)
		# Copy the 'capstone.dll' and 'capstone.lib' files
		if not UT.find_and_copy('libcapstone.dylib', '.', '%s/deps/tob_libraries/capstone/libcapstone.dylib' % curpath):
			UT.fail('Failed to find and copy: libcapstone.dylib')
		if not UT.find_and_copy('libcapstone.a', '.', '%s/deps/tob_libraries/capstone/libcapstone_static.a' % curpath):
			UT.fail('Failed to find and copy: libcapstone.a')
	# Copy the 'FindCapstone.cmake' file
	SH.copyfile('%s/scripts/FindCapstone.cmake' % curpath, '%s/deps/tob_libraries/cmake_modules/FindCAPSTONE.cmake' % curpath)
	# Get back to the main directory
	OS.chdir(curpath)
	# Notify we successfully compiled Capstone
	UT.succ('Capstone has been successfully compiled!')
