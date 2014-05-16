SFI=/home/jxiong/sfi
CFLAGS=-I$(SFI)/include
LDFLAGS=-L$(SFI)/lib -Xlinker -R$(SFI)/lib -lfabric -lrdmacm

TARGETS=pingpong rdma
all: $(TARGETS)

pingpong: pingpong.c Makefile
	cc pingpong.c -o pingpong $(CFLAGS) $(LDFLAGS)

rdma: rdma.c Makefile
	cc rdma.c -o rdma $(CFLAGS) $(LDFLAGS)

clean:
	rm $(TARGETS)
