add_project_arguments('-fvisibility=hidden', language : 'c')

src = ['api.c', 'connman-agent.c', 'connman-call.c', 'call_work.c']
lib = shared_library('connman-glib',
                     sources: src,
                     version: '1.0.0',
                     soversion: '0',
                     include_directories: inc,
                     dependencies: [systemd_dep, glib_deps],
                     install: true)

if get_option('build-tester')
    lib_dep = declare_dependency(link_with: lib)
    executable('connman-glib-test',
               'test.c',
               include_directories: inc,
               dependencies: [systemd_dep, glib_deps, lib_dep])
endif
