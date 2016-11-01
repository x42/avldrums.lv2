avldrums.lv2 - AVLinux Drumkits
===============================

avldrums.lv2 is a simple Drum Sample Player Plugin, dedicated to the
http://www.bandshed.net/avldrumkits/


Install
-------

Compiling avldrums requires the LV2 SDK, gnu-make, a c++-compiler,
libglib, libpango, libcairo and openGL (sometimes called: glu, glx, mesa).

```bash
  git clone git://github.com/x42/avldrums.lv2.git
  cd avldrums.lv2
  make submodules
  make
  sudo make install PREFIX=/usr
```

Note to packagers: the Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CXXLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CXXFLAGS`), also
see the first 10 lines of the Makefile.
