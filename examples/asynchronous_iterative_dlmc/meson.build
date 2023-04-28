config = configure_file(
  input : 'config.cfg',
  output : 'config.cfg',
  copy : true
)

asynchronous_iterative_dlmc = executable(
  'asynchronous_iterative_dlmc',
  ['asynchronous_iterative_dlmc.cpp'],
  dependencies : [skywing_core_dep]
)

conf_data = configuration_data()
conf_data.set('exe', asynchronous_iterative_dlmc.full_path())

configure_file(input: 'run.sh.in', output: 'run.sh', configuration: conf_data)