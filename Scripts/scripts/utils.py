import distutils.spawn
import multiprocessing
import subprocess
import sysconfig
import platform
import colorama
import getpass
import shutil
import sys
import os

# Initialize colorama
colorama.init()

def info(_str_):
  # Informational message
  print('[+] %s%s%s' % (colorama.Fore.BLUE, _str_, colorama.Style.RESET_ALL))

def succ(_str_):
  # Success message
  print('[+] %s%s%s' % (colorama.Fore.GREEN, _str_, colorama.Style.RESET_ALL))

def warn(_str_):
  # Warning message (execution pauses)
  print('[!] %s%s%s' % (colorama.Fore.YELLOW, _str_, colorama.Style.RESET_ALL))
  raw_input()

def fail(_str_, errcode=1):
  # Failure message (execution stops)
  print('[-] %s%s%s (code = %d)' % (colorama.Fore.RED, _str_, colorama.Style.RESET_ALL, errcode))
  sys.exit(errcode)

def execute(cmd, must_succeed=True, env=None):
  # Execute the command
  rc = 0
  try:
    p = subprocess.Popen(cmd, shell=True, env=env)
    p.communicate()
    rc = p.returncode
  except subprocess.CalledProcessError as ex:
    fail('Failure while executing: %s' % cmd, ex.returncode)
  # Check if the return code is 0
  if must_succeed and rc != 0:
    fail('Failure while executing: %s' % cmd, rc)

def rmdir(name):
  # Detect the OS
  osn = os_name()
  # Force remove
  if osn.startswith('Windows'):
    # Normalize the path separators
    name = name.replace('/', '\\')
    # Execute the removal
    execute('rmdir %s /s /q 2> NUL' % name, False)
  else:
    # Normalize the path separators
    name = name.replace('\\', '/')
    # Execute the removal
    execute('rm -rf %s 2>/dev/null' % name, False)

def cmake_generator():
  # Get the OS name
  osname = os_name()
  # Determine the cmake generator
  if osname.startswith('Windows'):
    return 'Visual Studio 15 2017 Win64'
  elif osname.startswith('Linux'):
    return 'Unix Makefiles'
  elif osname.startswith('Darwin'):
    return 'Unix Makefiles'
  else:
    fail('Unsupported OS: %s' % osname)

def cpu_count():
  # Get the CPU cores count
  return multiprocessing.cpu_count()

def os_name():
  # Get the OS name
  return platform.system()

def os_user():
  # Get the username
  return getpass.getuser()

def python_path():
  # Get the Python path
  return sysconfig.get_paths()['data']

def find_and_copy(_src, _dir, _dst):
  # Find the '_src' file/folder in the '_dir' folder
  for root, dirs, files in os.walk(_dir):
    # Search the directories
    for dirn in dirs:
      if dirn == _src:
        # Full source path
        path = os.path.join(root, dirn)
        # Delete the folder if it exists
        if os.path.isdir(_dst):
          rmdir(_dst)
        # Copy it to the '_dst' folder
        shutil.copytree(path, _dst)
        # Notify we found the folder
        return True
    # Search the files
    for file in files:
      if file == _src:
        # Full source path
        path = os.path.join(root, file)
        # Delete the file if it exists
        if os.path.isfile(_dst):
          os.remove(_dst)
        # Copy it to the '_dst' folder
        shutil.copyfile(path, _dst)
        # Notify we found the file
        return True
  # Notify we didn't find the file
  return False

def is_installed(name):
  fullpath = distutils.spawn.find_executable(name)
  return fullpath != None
