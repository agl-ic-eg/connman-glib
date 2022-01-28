GLib ConnMan interface library
----------------------------
Derived from AGL network binding source at
[https://git.automotivelinux.org/apps/agl-service-network/]

Source refactoring from the binding (not necessarily exhaustive):
* network-api.c -> api.c, call_work.c
* network-connman.c -> connman-call.c
* network-common.h -> common.h, connman-call.h, call_work.h
* network-util.c -> mostly redundant, required things moved to api.c, connman-call.c

Copyright dates in files reflect their original creation dates and include
revisions made in agl-service-network before December 2021.

Building
--------
The build requirements are:
* glib 2.0 headers and libraries (from e.g. glib2-devel on Fedora or CentOS,
  libglib2.0-dev on Debian or Ubuntu).
* meson

To build:
```
meson build/
ninja -C build/
```

Usage Notes
-----------
* Users only need include `connman-glib.h` and link to the library.
* API calls generally return a gboolean, with `FALSE` indicating failure.
* `connman_init` must be called before any other API calls except
  `connman_set_log_level` or one of the callback registration functions
  (e.g. `connman_add_manager_event_callback`).
* A return code of `TRUE` from `connman_init` indicates D-Bus connection to
  **ConnMan** has succeeded.
* Callbacks may be registered after calling `connman_init`, but note that there
  is a possibility that registration calls may briefly block if they occur
  during processing of an associated event.
* It is advised that only one primary user of the library enable agent support
  to avoid conflicts.

Contributing
------------
Questions can be sent to the agl-dev-community mailing list at
<https://lists.automotivelinux.org/g/agl-dev-community>.

Bugs can be filed on the AGL JIRA instance at <https://jira.automotivelinux.org>.

Source contributions need to go through the AGL Gerrit instance, see
<https://wiki.automotivelinux.org/agl-distro/contributing>.
