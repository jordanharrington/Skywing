async_dlmc_example_exe = executable(
  'async_dlmc',
  ['async_dlmc.cpp'],
  dependencies : [skywing_core_dep]
)

conf_data = configuration_data()
conf_data.set('async_dlmc_exe', async_dlmc_example_exe.full_path())

configure_file(input: 'run.sh.in', output: 'run.sh', configuration: conf_data)
