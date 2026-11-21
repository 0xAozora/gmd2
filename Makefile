CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
ZSTD_CFLAGS ?= $(shell pkg-config --cflags libzstd 2>/dev/null)
LDLIBS ?= $(shell pkg-config --libs libzstd 2>/dev/null || echo -lzstd) -lm

all: gmd2tool

HEADERS = include/gmd2_encoder.h include/gmd2_decoder.h
ATLAS_FIXTURE = tests/fixtures/jetbrains-mono

.PHONY: all headers odin odin-encoder odin-decoder check clean
headers: $(HEADERS)

odin: odin-encoder odin-decoder
odin-encoder: bindings/odin/encoder/libgmd2.a
odin-decoder: bindings/odin/decoder/libgmd2.a

bindings/odin/%/libgmd2.a: bindings/odin/%/gmd2.o
	$(AR) rcs $@ $<

bindings/odin/%/gmd2.o: bindings/%.c include/gmd2_%.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

include/gmd2_%.h: src/gmd2_codec.in Makefile
	mkdir -p include
	awk -v section="$*" 'BEGIN { part=toupper(section); guard="GMD2_" part "_H"; print "/* Generated from src/gmd2_codec.in. */"; print "#ifndef " guard; print "#define " guard } /^\/\* (COMMON|ENCODER|DECODER) \*\/$$/ { block=$$2; next } block=="COMMON" || block==part { print } END { print "#endif" }' $< > $@.tmp
	mv $@.tmp $@

CLI_SOURCES = src/gmd2.c src/gmd2_json.c src/gmd2_msgpack.c src/gmd2_cli.c
gmd2tool: $(CLI_SOURCES) src/gmd2.h $(HEADERS)
	$(CC) $(CPPFLAGS) $(ZSTD_CFLAGS) $(CFLAGS) $(CLI_SOURCES) -o $@ $(LDLIBS)

tests/test_headers: tests/test_headers.c src/gmd2_json.c src/gmd2_msgpack.c src/gmd2.h $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_headers.c src/gmd2_json.c src/gmd2_msgpack.c -o $@ -lm

check: gmd2tool tests/test_headers
	./tests/test_headers
	./gmd2tool decode $(ATLAS_FIXTURE)/atlas.gmd2 $(ATLAS_FIXTURE)/layout.json tests/roundtrip_reference.rgb
	cmp $(ATLAS_FIXTURE)/atlas.rgb tests/roundtrip_reference.rgb
	./gmd2tool encode $(ATLAS_FIXTURE)/atlas.rgb $(ATLAS_FIXTURE)/layout.json tests/reencoded.gmd2 22
	./gmd2tool decode tests/reencoded.gmd2 $(ATLAS_FIXTURE)/layout.json tests/roundtrip_reencoded.rgb
	cmp $(ATLAS_FIXTURE)/atlas.rgb tests/roundtrip_reencoded.rgb

clean:
	rm -f gmd2tool tests/test_headers tests/roundtrip_reference.rgb tests/roundtrip_reencoded.rgb tests/reencoded.gmd2
	rm -f bindings/odin/encoder/gmd2.o bindings/odin/encoder/libgmd2.a
	rm -f bindings/odin/decoder/gmd2.o bindings/odin/decoder/libgmd2.a
