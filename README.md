# V8 MonoContext shared library

Used to run javascript files within one v8 context

Basically intended to be used as a library in [V8::MonoContext][1](perl), [PyV8Mono][2](python)

# V8 dependence

We prefer to build MonoContext with strict v8 version to get stable and predictable behavior in production and not to depend from variety of changes occuring in v8 trunk.

# INSTALLATION

Currently all installation process is tested on CentOS release 6.5 (Final) x86_64 and can be made with any modern linux distribution

First of all prepare gcc, v8 library and environment

Use gcc version >=4.7.0
If you have a proper gcc version installed under prefix location:

    $ export CXX=/usr/local/gcc-4.7.3/bin/c++ 
    $ export LINK=/usr/local/gcc-4.7.3/bin/c++

Strict v8 version

    $ export V8_VERSION=3.27.0
    $ export V8_PREFIX=/usr/local/v8-$V8_VERSION
    
Clone last V8 source trunk, build dependencies and checkout 3.27.0

    $ git clone git://github.com/v8/v8.git v8 && cd v8 
    $ git checkout 3.27.0 
    $ make dependencies

Build V8 with the following recommended flags

    $ make \
      objectprint=off \
      i18nsupport=off \
      verifyheap=off \
      debuggersupport=on \
      regexp=native \
      vtunejit=off \
      extrachecks=off \
      visibility=on \
      snapshot=on \
      strictaliasing=on \
      liveobjectlist=off \
      backtrace=on \
      gdbjit=off \
      disassembler=off \
      werror=no \
      library=shared \
      soname_version=$V8_VERSION \
      x64.release

Install libs and headers in prefix location

    $ sudo -E bash -c 'mkdir -p $V8_PREFIX/{bin,include,lib64}'
    $ sudo -E bash -c 'install -D -m 755 out/x64.release/{cctest,d8,lineprocessor,mksnapshot.x64,process,shell} $V8_PREFIX/bin'
    $ sudo -E bash -c 'install -D -m 644 include/*.h $V8_PREFIX/include'
    $ sudo -E bash -c 'install -D -m 755 out/x64.release/lib.target/libv8.so.$V8_VERSION $V8_PREFIX/lib64'

Configure dynamic linker

    $ sudo -E bash -c 'echo "$V8_PREFIX/lib64" > /etc/ld.so.conf.d/v8-$V8_VERSION.conf'
    $ sudo ldconfig

Finally build and install v8monoctx.so shared library

    $ git clone https://github.com/fsitedev/v8monoctx.git v8monoctx
    $ cd v8monoctx
    $ make
    $ sudo -E bash -c 'make install'

[1]: https://github.com/fsitedev/V8-MonoContext
[2]: https://github.com/bekbulatov/PyV8Mono
