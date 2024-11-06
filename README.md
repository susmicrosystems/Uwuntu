
# Uwuntu



uwuntu is an operating system created for studying purpose

# Compilation

copy the config file
`cp config.mk.sample config.mk`

add the toolchain path to your env:
`PATH="$PATH:$PWD/cc/build/bin"`

build the toolchain
`make cc`

- `make` to build uwuntu.iso (suitable for USB-drive boot on real hardware)
- `make run` to build uwuntu.iso and run it under qemu

# Misc
`config.mk` can be edited to enable the compilation of external contribs

# License

uwuntu is released under GNU AGPLv3 license
