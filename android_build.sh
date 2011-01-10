export CC=agcc
export PKG_CONFIG_LIBDIR=./pc
./configure --host=arm-eabi --disable-ssl --disable-glibtest --disable-ipv6  --disable-ipv6 --disable-largefile
make -j2
