INCLUDE_DIR= -I ${HOME}/tools/install/ffmpeg/include
LIB_DIR= -L ${HOME}/tools/install/ffmpeg/lib -L/usr/local/lib
LIBS=-lavdevice -lavfilter -lavformat -lavcodec \
-lavresample -lpostproc -lswresample -lswscale \
-lavutil -lxcb -lxcb-shm -lxcb-shape -lxcb-xfixes \
-lasound -lSDL2 -pthread  -lass  -lvidstab -lm -lgomp \
-lpthread -lfreetype  -lbz2 -lz  -lvpx  -llzma -lopencore-amrwb \
-laom -lfdk-aac -lmp3lame -lopencore-amrnb -lopenjp2 \
-lopus -ltheoraenc -ltheoradec -logg -lvorbis -lvorbisenc \
-lx264  -lxvidcore  -lkvazaar  -pthread  -ldl -lpthread  -lX11

FLAG= -std=c++14   -g -Wl,-rpath,/usr/local/lib -Wl,--enable-new-dtags
CC=clang++ 
SOURCE= main.cc record.cc 

record:
	${CC} ${SOURCE} ${INCLUDE_DIR} ${LIB_DIR} ${LIBS} -static-libstdc++  -Wl,-Bstatic -lx265  -lnuma -lssl -lcrypto -Wl,-Bdynamic -ltcmalloc

