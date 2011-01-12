export CC=agcc
export PKG_CONFIG_LIBDIR=./pc
./configure --host=arm-eabi --disable-glibtest --disable-ipv6 --disable-largefile
make -j2
find -name *.a -exec cp {} ~/build/lib/ \;
cd src/fe-text
agcc -o irssi *.o -lglib-2.0 -L/home/jer/build/lib -lglib-2.0 -lcore -lfe_common_core -lfe_common_irc -lfe_irc_dcc -lfe_irc_notifylist -lirc -lirc_core -lirc_dcc -lirc_flood -lirc_notifylist -lirssi_config -lgmodule-2.0 ../../../irssi-xmpp-0.51/src/core/*.o ../../../irssi-xmpp-0.51/src/fe-common/*.o ../../../irssi-xmpp-0.51/src/fe-text/*.o ../../../irssi-xmpp-0.51/src/core/xep/*.o ../../../irssi-xmpp-0.51/src/fe-common/xep/*.o ../../../irssi-xmpp-0.51/src/fe-text/xep/*.o -lncurses ../../../loudmouth/loudmouth/*.o -lssl
