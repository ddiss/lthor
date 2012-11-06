PKG_NAME := lthor
SPECFILE = $(addsuffix .spec, $(PKG_NAME))
PKG_VERSION := $(shell grep '^Version: ' $(SPECFILE)|awk '{print $$2}')

TARBALL := $(PKG_NAME)_$(PKG_VERSION).tar.gz

tarball:
	@cd .. && git archive --prefix $(PKG_NAME)-$(PKG_VERSION)/ HEAD \
		| gzip > packaging/$(TARBALL)

clean:
	@rm -f $(PKG_NAME)_*.tar.gz

all: clean tarball
