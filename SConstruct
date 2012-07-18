
env = Environment()

ccflags = [
  '-O2',
#  '-g',
]

cpppath = [
  '#',
  '../CommandLineParser',
]

libPath = [
  '#',
  '../CommandLineParser',
]

libs = [
  'CmdLineParser',
  'LowImpactProfiler',
  'pthread',
  'rt',
]

env.Append(CPPPATH = cpppath, CCFLAGS = ccflags)
libTarget = env.StaticLibrary(target = 'LowImpactProfiler', source = 'LowImpactProfiler.cc')
env.Default(libTarget)
env.Alias('library', libTarget)

env.Append(LIBPATH = libPath, LIBS = libs)
binTarget = env.Program(target = 'simpleThreaderMain', source = 'simpleThreaderMain.cc')
env.Alias('example', binTarget)
