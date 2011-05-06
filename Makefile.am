# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign

SUBDIRS = src tests

headache:
	find src tests -name "*.c" -or -name "*.h" | xargs headache -h LICENSE -c headache.conf
	echo "REMEMBER to update Ruby files by hand"
