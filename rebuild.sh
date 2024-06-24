#!/bin/bash

function build() {

	local REVISION=0
	if [[ -f config.h ]]; then
		diff -u0 --color config.def.h config.h
		echo
		REVISION=$(grep -E '^#define DWM_REVISION' config.h | sed -E 's/^#define DWM_REVISION[[:space:]]+"([0-9]+)".*$/\1/')
	fi

	if [[ $REVISION = 0 ]]; then

		if [[ -f REVISION ]]; then
			read REVISION <REVISION
		fi

	fi

	if [[ ! $REVISION =~ ^[0-9]+$ ]]; then
		REVISION=0
	fi
	REVISION=$((REVISION+1))

	echo $REVISION >REVISION

	echo "Build revision $REVISION:"

	[[ ! -f config.h ]] && cp config.def.h config.h

	sed -Ei 's/^(#define DWM_REVISION[[:space:]]+")([0-9]+)(".*)$/\1'"$REVISION"'\3/' config.h

	make

	cp dwm $DWM_QUEUED
}

clear

build
