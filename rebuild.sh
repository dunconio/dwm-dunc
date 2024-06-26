#!/bin/bash

function build() {

	local OLD_REVISION=0
	local REVISION=0
	if [[ -f config.h ]]; then
		diff -u0 --color config.def.h config.h
		echo
		OLD_REVISION=$(grep -E '^#define DWM_REVISION' config.h | sed -E 's/^#define DWM_REVISION[[:space:]]+"([0-9]+)".*$/\1/')
	fi

	if [[ $OLD_REVISION = 0 ]]; then

		if [[ -f REVISION ]]; then
			read OLD_REVISION <REVISION
		fi

	fi

	if [[ ! $OLD_REVISION =~ ^[0-9]+$ ]]; then
		OLD_REVISION=0
	fi
	REVISION=$((OLD_REVISION+1))

	echo "Build revision $REVISION:"

	[[ ! -f config.h ]] && cp config.def.h config.h

	sed -Ei 's/^(#define DWM_REVISION[[:space:]]+")([0-9]+)(".*)$/\1'"$REVISION"'\3/' config.h

	make
	if [[ $? != 2 ]]; then
		echo $REVISION >REVISION
		cp dwm $DWM_QUEUED
	else
		sed -Ei 's/^(#define DWM_REVISION[[:space:]]+")([0-9]+)(".*)$/\1'"$OLD_REVISION"'\3/' config.h
	fi
}

clear

build
