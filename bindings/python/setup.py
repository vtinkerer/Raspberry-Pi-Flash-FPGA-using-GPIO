# python extension setup script for urjtag

from distutils.core import setup, Extension

libraries = ['urjtag']
libraries.extend( w.replace('-l', '') for w in "-lftdi1 -lusb-1.0  -lusb-1.0 ".split() if w.replace('-l', '') not in libraries )

setup(name="urjtag",
      version="2021.03",
      description="urJtag Python Bindings",
      ext_modules=[
        Extension("urjtag", ["./chain.c", "./register.c"],
                  define_macros=[('HAVE_CONFIG_H', None)],
                  include_dirs=['../..', '../../include', '../..'],
                  library_dirs=['../../src/.libs'],
                  libraries=libraries)
         ])
