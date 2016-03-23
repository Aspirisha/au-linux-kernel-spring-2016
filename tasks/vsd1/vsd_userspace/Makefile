INCLUDE_PATH=../vsd_driver/
SHARE_PATH=../../../../vm/share/

vsd_userspace: main.cpp $(INCLUDE_PATH)/vsd_ioctl.h
	g++ -std=c++0x $< -I$(INCLUDE_PATH) -lbsd -o $@
	cp $@ $(SHARE_PATH)

.PHONY: clean

clean:
	rm -rf vsd_userspace
