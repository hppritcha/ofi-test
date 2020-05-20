#######################################################################
# 	Ping-Pong Tests
# 	for
# 	Open Fabric Interface 1.x
#
#	Jianxin Xiong
# 	(jianxin.xiong@intel.com)
# 	2013-2017
#######################################################################

OFI_HOME = /usr/projects/hpctools/hpp/libfabric/install_not_psm2
CFLAGS    = -I$(OFI_HOME)/include -g
LDFLAGS   = -L$(OFI_HOME)/lib -Xlinker -R$(OFI_HOME)/lib -lfabric

TARGETS=pingpong pingpong-sep pingpong-sep-mt pingpong-self hello hello_tom
all: $(TARGETS)

pingpong: pingpong.c Makefile
	cc pingpong.c -o pingpong $(CFLAGS) $(LDFLAGS)

pingpong-sep: pingpong-sep.c Makefile
	cc pingpong-sep.c -o pingpong-sep $(CFLAGS) $(LDFLAGS)

pingpong-sep-mt: pingpong-sep-mt.c Makefile
	cc pingpong-sep-mt.c -o pingpong-sep-mt $(CFLAGS) $(LDFLAGS) -lpthread

pingpong-self: pingpong-self.c Makefile
	cc pingpong-self.c -o pingpong-self $(CFLAGS) $(LDFLAGS)

hello: hello.c Makefile
	cc hello.c -o hello $(CFLAGS) $(LDFLAGS)

hello_tom: hello_tom.c Makefile
	cc hello_tom.c -o hello_tom $(CFLAGS) $(LDFLAGS)

clean:
	rm $(TARGETS)
