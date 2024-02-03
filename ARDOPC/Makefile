#	ARDOPC Makefile

OBJS = ARDOPCommon.o LinSerial.o KISSModule.o pktARDOP.o pktSession.o BusyDetect.o i2cDisplay.o ALSASound.o \
ARDOPC.o ardopSampleArrays.o ARQ.o FFT.o FEC.o HostInterface.o Modulate.o rs.o \
berlekamp.o galois.o SoundInput.o TCPHostInterface.o SCSHostInterface.o wav.o RXO.o

# Configuration:
CFLAGS = -DLINBPQ -MMD -g 
CC = gcc

vpath %.c ../ARDOPCommonCode
vpath %.h ../ARDOPCommonCode
vpath %.o ./
			                       
all: ardopc
			
ardopc: $(OBJS)
	gcc $(OBJS) -Xlinker -Map=output.map -lrt -lm -lpthread -lasound -o ardopc


-include *.d

clean :
	rm ardopc $(OBJS)

