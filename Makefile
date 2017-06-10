
all: 
	gcc ftpd.c -o ftpd
	gcc client.c -o client

install:
	mv ftpd   /usr/bin/
	mv client /usr/bin/

uninstall:
	rm -rf /usr/bin/ftpd
	rm -rf /usr/bin/client

clean:
	rm -rf ./Download
