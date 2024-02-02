
all: smrender.deb

.smrender:
	cd .. ;\
	./configure --prefix=/usr ;\
	make clean ;\
	make install DESTDIR=$$(pwd)/debian/smrender
	rm -f smrender/usr/share/smrender/rules_100000
	touch .smrender

smrender.deb: .smrender smrender/DEBIAN/control
	dpkg --build smrender

clean:
	rm -rf .smrender smrender/usr

.PHONY: clean

