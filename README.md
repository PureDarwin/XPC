# XPC and Friends! [![Build Status](https://travis-ci.org/PureDarwin/XPC.svg?branch=master)](https://travis-ci.org/PureDarwin/XPC)

An open-source reimplementation of Apple's XPC library and launchd for PureDarwin. Based on [launchd 842.91.1](https://opensource.apple.com/tarballs/launchd/launchd-842.91.1.tar.gz) and iXsystems's skeleton re-implementation of libxpc.

**NOTE: The code in this repo compiles and runs but is currently untested. In particular, the implementation of xpc is incomplete, with many functions being no-ops.**

This project differs from a 'pure' launchd built from Apple's source to align better with what can be inferred of the current (macOS 10.13) implementation:

* `liblaunch.dylib` contains no code. Instead it acts as a proxy to `libxpc.dylib` by way of indirect symbols.
* `libxpc.dylib` contains the bulk of the logic that was in `liblaunch.dylib`, along with the untested / non-existant XPC code.
* `launchd` is mostly unchanged, although audit logging is currently disabled.
* `launchctl` has had a couple of code paths commented-out so that it builds.
* A launch daemon script is included to launch bash at startup.

#### Prerequisites

You will need a version of `libSystem` which links to `libxpc`, like [this one](https://github.com/Andromeda-OS/Libsystem). You will also need a version of `libcoreservices`, which it requires. You can find one [here](https://github.com/libsystem-ethan/esdarwin).

You will also need a versions of `libedit` and `libbsm`.

#### Installation

A binary root will be made availble once the code has been shown to work to at least minimum standards.

Install the binaries into a Darwin image in the following locations:

* `libxpc.dylib` and `liblaunch.dylib` into `/usr/lib/system`.
* `launchd` into `/sbin`, but please read the discussion below first, you'll probably need to rename it to `pdlaunchd`.
* `launchctl` into `/bin`.
* Copy `org.puredarwin.console.plist` into `/System/Library/LaunchDaemons/`.
* Install a version of `libSystem` which links to `liblaunch` and `libxpc`.
* Install a version of `libsystem_coreservices` if one isn't already present.
* Install versions of `libedit` and `libbsm` if they aren't already available.

Once you have done this, check the ownership of all directories up to the `LaunchDaemons` directory and all files inside it. They need to be `root:wheel` for `launchd` to be happy about launching them.

`launchd` should run as the first user task (`pid` 1) in order to set up certain important services (such as the mach port nameserver) which child tasks inherit. When the kernel finishes booting the system it automatically runs `/sbin/launchd`. In current PureDarwin systems this file is a script which sets up various system parameters and then runs `/bin/bash`. To work with this, you should rename `launchd` to `pdlaunchd` and then replace

```
/bin/bash -i
```

in the `launchd` script with

```
exec /sbin/pdlaunchd
```

The `exec` part is important. This will allow `launchd` to replace the startup script as `pid` 1. `launchd` will then in turn launch `launchctl` to bootstrap the system. If you have installed the `org.puredarwin.console.plist` then a `bash` shell will be started.

#### TODO

* Complete implementation of xpc functions
* Get audit logging working using recent OpenBSM
