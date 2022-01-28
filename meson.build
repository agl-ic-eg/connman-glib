project('connman-glib', 'c', license : 'Apache-2.0')

systemd_dep = dependency('systemd', version : '>=222')
glib_deps = [dependency('glib-2.0'), dependency('gio-2.0'), dependency('gobject-2.0'), dependency('gio-unix-2.0')]

inc = include_directories('include')

subdir('include')
subdir('src')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries : lib,
                 version : '1.0',
                 name : 'libconnman-glib',
                 filebase : 'connman-glib',
                 description : 'GLib helper library for using ConnMan.')
