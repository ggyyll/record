INCLUDE_DIR= -I ${HOME}/tools/install/ffmpeg/include 

LIB_DIR= -L ${HOME}/tools/install/ffmpeg/lib  \
		 -L/usr/local/lib

LIBS= -lavdevice -lavfilter -lavformat -lavcodec -lavresample \
	-lpostproc -lswresample -lswscale -lavutil -lxcb -lxcb-shm \
	-lxcb-shape -lxcb-xfixes -lasound -lSDL2 -pthread  -lass  \
	-lvidstab -lm -lgomp -lpthread -lfreetype  -lbz2 -lz  -lvpx  \
	-llzma -lopencore-amrwb -laom -lfdk-aac -lmp3lame -lopencore-amrnb \
	-lopenjp2 -lopus -ltheoraenc -ltheoradec -logg -lvorbis -lvorbisenc \
	-lx264  -lxvidcore  -lkvazaar  -pthread  -ldl -lpthread  -lX11 \
	-static-libstdc++  -Wl,-Bstatic -lx265  -lnuma -lssl -lcrypto  

FLAG= -std=c++17   -g 

CC=clang++ 

SOURCE= main.cc record.cc


app:
	${CC} ${FLAG} ${SOURCE} ${INCLUDE_DIR} ${LIB_DIR} ${LIBS} 

